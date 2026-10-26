#!/usr/bin/env python3
"""
Record how an AmpServer behaves on the wire, and diff two recordings.

Used to keep the mock (mock/MockAmpServer --amp-type NA500) aligned with a real
GES 500 / Net Amps 500: record the device once, then record the mock and diff.

    # record (Net Station and every other client must be disconnected:
    # the NA500 serves one client per session)
    python ampserver_conformance.py --host NA500-256ch-0001.local --active -o na500.json
    python ampserver_conformance.py --host 127.0.0.1 --active -o mock.json
    # compare; exits 1 on any difference
    python ampserver_conformance.py --compare na500.json mock.json

Every scenario opens a fresh session and closes it at the end. On the NA500
closing the command connection ends the session, so scenarios do not leak state
into each other. Keys starting with "~" are informational (timings, clocks) and
are not compared.

--active powers the amplifier on and streams; without it only commands that do
not touch the analog front end are sent.
"""

import argparse
import json
import re
import socket
import struct
import sys
import threading
import time
from collections import Counter

CMD_PORT, NTN_PORT, DATA_PORT = 9877, 9878, 9879
HEADER = struct.Struct(">qQ")  # amp id, payload length (big-endian)
PACKET2_SIZE = 1264
# Packet format 2 fields we inspect (little-endian): din u16 @0, counter u64 @25,
# timestamp u64 @33, net code u8 @41, first reserved byte @42, EEG channel 1 @80.
PKT = struct.Struct("<H23xQQBB37xi")
RAIL = 8388000  # within a few counts of 24-bit full scale

# Reply values that legitimately differ between units, sessions or runs.
VOLATILE = re.compile(
    r"\((serial_number|time_remaining|master|slave\d|ambient_temp|lpd_temp|fpd_temp|"
    r"pl_temp|start_time|current_time|error_code|net_serial|net_recording_time) [^()]*\)")


def normalize(line):
    return VOLATILE.sub(lambda m: f"({m.group(1)} <v>)", line)


class Reader(threading.Thread):
    """Collects newline-terminated lines from a socket with arrival times."""

    def __init__(self, sock):
        super().__init__(daemon=True)
        self.sock, self.lines, self.closed = sock, [], False
        self.start()

    def run(self):
        buf = b""
        while True:
            try:
                chunk = self.sock.recv(65536)
            except OSError:
                chunk = b""
            if not chunk:
                self.closed = True
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.strip():
                    self.lines.append((time.monotonic(), line.decode(errors="replace")))


class DataReader(threading.Thread):
    """Parses data-port frames: header + payload of format-2 packets."""

    def __init__(self, sock):
        super().__init__(daemon=True)
        self.sock, self.blocks, self.packets, self.closed = sock, [], [], False
        self.start()

    def _read(self, n):
        out = b""
        while len(out) < n:
            chunk = self.sock.recv(n - len(out))
            if not chunk:
                raise EOFError
            out += chunk
        return out

    def run(self):
        try:
            while True:
                amp_id, length = HEADER.unpack(self._read(HEADER.size))
                payload = self._read(length)
                self.blocks.append((time.monotonic(), amp_id, length))
                for off in range(0, length - PACKET2_SIZE + 1, PACKET2_SIZE):
                    self.packets.append(PKT.unpack_from(payload, off))
        except (EOFError, OSError):
            self.closed = True


class Session:
    def __init__(self, host, data=False, ntn=True):
        self.host = host
        self.cmd = self._connect(CMD_PORT)
        self.cmd_reader = Reader(self.cmd)
        self.ntn = Reader(self._connect(NTN_PORT)) if ntn else None
        self.data = DataReader(self._connect(DATA_PORT)) if data else None
        # The NA500 accepts each port on its own schedule; a command sent the
        # instant the ports open can beat the notification port's accept.
        time.sleep(0.2)

    def _connect(self, port):
        sock = socket.create_connection((self.host, port), timeout=5)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(None)
        return sock

    def raw(self, payload, quiet=0.6, first=3.0):
        """Send bytes, return every reply line until `quiet` s pass with none."""
        start = len(self.cmd_reader.lines)
        t0 = time.monotonic()
        self.cmd.sendall(payload if isinstance(payload, bytes) else payload.encode())
        deadline = t0 + first
        while time.monotonic() < deadline:
            time.sleep(0.02)
            if len(self.cmd_reader.lines) > start:
                deadline = max(deadline, self.cmd_reader.lines[-1][0] + quiet)
                if time.monotonic() > self.cmd_reader.lines[-1][0] + quiet:
                    break
        got = self.cmd_reader.lines[start:]
        return [normalize(l) for _, l in got], (round(got[0][0] - t0, 2) if got else None)

    def send(self, command, channel=0, value=0, **kw):
        return self.raw(f"(sendCommand {command} 0 {channel} {value})\n", **kw)

    def notifications(self, since=0):
        return [l for _, l in self.ntn.lines[since:]] if self.ntn else []

    def close(self):
        for s in (self.cmd, self.ntn and self.ntn.sock, self.data and self.data.sock):
            if s:
                try:
                    s.close()
                except OSError:
                    pass
        time.sleep(1.0)  # let the server tear the session down before the next one


def reply_info(session, command, channel=0, value=0, **kw):
    lines, latency = session.send(command, channel, value, **kw)
    return {"replies": lines, "~latency_s": latency}


# ---------------------------------------------------------------- passive

QUERIES = ["cmd_NumberOfAmps", "cmd_GetAmpDetails", "cmd_GetAmpStatus", "cmd_GetBatteryData",
           "cmd_GetBoardStatus", "cmd_NumberOfActiveAmps", "cmd_GetPhysioConnectionStatus",
           "cmd_GetCurrentTime", "cmd_GetCurrentDrift", "cmd_GetNetRecordingTime",
           "cmd_ReceiveNotifications", "cmd_StopReceivingNotifications", "cmd_None"]


def scenario_queries(host):
    s = Session(host)
    try:
        out = {c: reply_info(s, c, -1, -1) for c in QUERIES}
        out["notifications"] = s.notifications()
        return out
    finally:
        s.close()


def scenario_framing(host):
    s = Session(host)
    try:
        s.send("cmd_NumberOfAmps", -1, -1)
        two = (b"(sendCommand cmd_NumberOfAmps 0 -1 -1)\n"
               b"(sendCommand cmd_GetAmpDetails 0 -1 -1)\n")
        out = {"two_in_one_segment": s.raw(two)[0],
               "double_newline": s.raw(b"(sendCommand cmd_NumberOfAmps 0 -1 -1)\n\n")[0],
               "extra_space": s.raw(b"(sendCommand cmd_NumberOfAmps  0 -1 -1)\n")[0]}
        s.send("cmd_NumberOfAmps", -1, -1)
        out["unknown_after_NumberOfAmps"] = s.send("cmd_NoSuchCommand", -1, -1)[0]
        s.send("cmd_NumberOfAmps", -1, -1)
        s.cmd.sendall(b"(sendCommand cmd_GetAmp")
        time.sleep(0.3)
        out["split_across_segments"] = s.raw(b"Details 0 -1 -1)\n")[0]
        s.send("cmd_NumberOfAmps", -1, -1)
        out["lowercase_setPIBChannelGain"] = s.send("cmd_setPIBChannelGain", 0, 12)[0]
        return out
    finally:
        s.close()


def scenario_odd_replies(host):
    s = Session(host)
    try:
        out = {}
        for name, (cmd, ch, val) in {
            "ListenToAmp_on_cmd_port": ("cmd_ListenToAmp", 0, 0),
            "StopListeningToAmp_on_cmd_port": ("cmd_StopListeningToAmp", 0, 0),
            "SetDecimatedRate_300": ("cmd_SetDecimatedRate", 0, 300),
            "SetNativeRate_300": ("cmd_SetNativeRate", 0, 300),
            "SetPower_2": ("cmd_SetPower", 0, 2),
            "TurnAllZeroOhms_0": ("cmd_TurnAllZeroOhms", 0, 0),
            "SetMRIPulseInfo_0": ("cmd_SetMRIPulseInfo", 0, 0),
        }.items():
            out[name] = s.send(cmd, ch, val, first=2.0)[0]
            # A fresh, well-known reply shows whether the session is still usable.
            out[name + "_then_NumberOfAmps"] = s.send("cmd_NumberOfAmps", -1, -1)[0]
        return out
    finally:
        s.close()


def scenario_second_client(host):
    s = Session(host)
    try:
        s.send("cmd_NumberOfAmps", -1, -1)
        out = {}
        try:
            second = socket.create_connection((host, CMD_PORT), timeout=3)
            second.settimeout(3)
            second.sendall(b"(sendCommand cmd_NumberOfAmps 0 -1 -1)\n")
            try:
                out["second_client_reply"] = normalize(second.recv(4096).decode().strip())
            except socket.timeout:
                out["second_client_reply"] = None
            second.close()
            out["second_client_connects"] = True
        except OSError:
            out["second_client_connects"] = False
        out["first_client_still_served"] = s.send("cmd_NumberOfAmps", -1, -1)[0]
        return out
    finally:
        s.close()


# Names to check for recognition. An unrecognized name re-runs the previous
# command on the NA500, so each is sent right after cmd_NumberOfAmps. All are
# sent with value 0, which leaves the amplifier in its idle state.
NAMES = ["cmd_Reset", "cmd_DefaultAcquisitionState", "cmd_DefaultSignalGeneration", "cmd_Exit",
         "cmd_TurnAll10KOhms", "cmd_TurnChannel10KOhms", "cmd_SetCOM10KOhms",
         "cmd_TurnAllDriveSignals", "cmd_TurnChannelDriveSignals", "cmd_SetSubjectGround",
         "cmd_SetCurrentSource", "cmd_SetCalibrationSignalFreq", "cmd_SetBufferedReference",
         "cmd_SetOscillatorGate", "cmd_SetReference10KOhms", "cmd_SetReferenceDriveSignal",
         "cmd_SetWaveShape", "cmd_SetDrivenCommon", "cmd_SetCalibrationSignalAmplitude",
         "cmd_SetAnalogOutput", "cmd_SetDigitalOutputData", "cmd_SetDigitalInOutDirection",
         "cmd_GetStartTime", "cmd_SetPIBChannelGain", "cmd_TurnChannelZeroOhms",
         "cmd_SetPhoticStimSequence", "cmd_SetSpo2Enable", "cmd_SetDinEnable", "cmd_SetTestMode",
         "cmd_SetOpticalClockMode", "cmd_SetRecordingState", "cmd_SetBncInMode",
         "cmd_SetBncOutMode", "cmd_SetBncOutPulseWidth", "cmd_SetBlankingPulseWidth",
         "cmd_SetTriggerBncManualOut", "cmd_SetQuietMode", "cmd_SetNetRecordingTime",
         "cmd_SetFilterAndDecimate", "cmd_GTENGetStatus"]


def scenario_recognized(host):
    s = Session(host)
    try:
        baseline = s.send("cmd_NumberOfAmps", -1, -1)[0]
        out = {}
        for name in NAMES:
            s.send("cmd_NumberOfAmps", -1, -1)
            out[name] = s.send(name, 0, 0)[0] != baseline
        return out
    finally:
        s.close()


# ---------------------------------------------------------------- active

def summarize_stream(d, seconds_host):
    pk = d.packets
    if not pk:
        return {"packets": 0, "blocks": len(d.blocks)}
    # A new stream may open with leftovers of the previous one (old counter and
    # clock) before its own counter starts at 0; measure the stream after them.
    reset = next((i for i, p in enumerate(pk) if p[1] == 0), 0)
    counters = [p[1] for p in pk[reset:]]
    stamps = [p[2] for p in pk[reset:]]
    diffs = Counter(b - a for a, b in zip(stamps, stamps[1:]))
    return {
        "header_amp_ids": sorted({b[1] for b in d.blocks}),
        "packets_per_block": sorted({b[2] // PACKET2_SIZE for b in d.blocks}),
        "payload_multiple_of_1264": all(b[2] % PACKET2_SIZE == 0 for b in d.blocks),
        "counter_starts_at_0": counters[0] == 0,
        "counter_step_set": sorted({b - a for a, b in zip(counters, counters[1:])}),
        "timestamp_step_us_top": [k for k, _ in diffs.most_common(2)],
        "net_codes": sorted({p[3] for p in pk}),
        "din_most_common": hex(Counter(p[0] for p in pk[reset:]).most_common(1)[0][0]),
        "reserved0_values": sorted({p[4] for p in pk}),
        "eeg_railed": round(sum(abs(p[5]) >= RAIL for p in pk[reset:]) / max(1, len(pk) - reset), 1),
        "~stale_packets_before_reset": reset,
        "~packets": len(pk),
        "~rate_by_host_clock": round(len(pk) / seconds_host),
        "~rate_by_timestamps": round((len(stamps) - 1) * 1e6 / max(1, stamps[-1] - stamps[0])),
        "~amp_clock_minus_host_s": round(stamps[-1] / 1e6 - time.time(), 1),
    }


def stream_for(s, seconds):
    """Collect everything the data port delivers for `seconds` from now."""
    t0 = time.monotonic()
    s.data.packets.clear()
    s.data.blocks.clear()
    time.sleep(seconds)
    return time.monotonic() - t0


def scenario_power(host):
    s = Session(host)
    try:
        out = {}
        for v in (1, 0):
            n = len(s.ntn.lines)
            out[f"SetPower_{v}"] = reply_info(s, "cmd_SetPower", 0, v, quiet=1.0)
            time.sleep(1.0)
            out[f"SetPower_{v}_notifications"] = s.notifications(n)
        return out
    finally:
        s.close()


def scenario_stream(host, rate, mode="Decimated"):
    s = Session(host, data=True)
    try:
        out = {}
        s.send("cmd_SetPower", 0, 1)
        out[f"Set{mode}Rate"] = s.send(f"cmd_Set{mode}Rate", 0, rate)[0]
        n = len(s.ntn.lines)
        s.data.packets.clear()
        s.data.blocks.clear()
        t0 = time.monotonic()
        out["Start"] = reply_info(s, "cmd_Start", -1, -1)
        out["Start_notifications"] = s.notifications(n)
        # No cmd_ListenToAmp on the data port: does data flow anyway?
        time.sleep(2.0)
        out["stream_without_ListenToAmp"] = summarize_stream(s.data, time.monotonic() - t0)
        n = len(s.ntn.lines)
        out["GetStartTime"] = reply_info(s, "cmd_GetStartTime", -1, -1)
        out["GetCurrentTime"] = reply_info(s, "cmd_GetCurrentTime", -1, -1)
        out["SetRecordingState_0"] = reply_info(s, "cmd_SetRecordingState", -1, 0)
        out["GetStartTime_notifications"] = s.notifications(n)
        n = len(s.ntn.lines)
        out["Stop"] = reply_info(s, "cmd_Stop", -1, -1, quiet=1.0, first=5.0)
        time.sleep(0.5)
        out["Stop_notifications"] = s.notifications(n)
        # A second Start in the same session.
        s.data.packets.clear()
        s.data.blocks.clear()
        t0 = time.monotonic()
        s.send("cmd_Start", -1, -1)
        time.sleep(1.0)
        restart = summarize_stream(s.data, time.monotonic() - t0)
        out["restart_counter_starts_at_0"] = restart.get("counter_starts_at_0")
        out["~restart_stale_packets"] = restart.get("~stale_packets_before_reset")
        s.send("cmd_Stop", -1, -1, first=5.0)
        s.send("cmd_SetPower", 0, 0)
        return out
    finally:
        s.close()


def scenario_data_reconnect(host):
    s = Session(host, data=True)
    try:
        out = {}
        s.send("cmd_SetPower", 0, 1)
        s.send("cmd_SetDecimatedRate", 0, 1000)
        s.send("cmd_Start", -1, -1)
        time.sleep(1.0)
        out["packets_before_drop"] = len(s.data.packets) > 0
        s.data.sock.close()
        time.sleep(0.5)
        s.data = DataReader(s._connect(DATA_PORT))
        time.sleep(2.0)
        out["packets_after_reconnect"] = len(s.data.packets) > 0
        out["cmd_still_served"] = s.send("cmd_NumberOfAmps", -1, -1)[0]
        s.send("cmd_Stop", -1, -1, first=5.0)
        return out
    finally:
        s.close()


def scenario_stalled_reader(host):
    """Start streaming but leave the data port unread for 5 s, then read."""
    s = Session(host)
    data = s._connect(DATA_PORT)
    try:
        s.send("cmd_SetPower", 0, 1)
        s.send("cmd_SetDecimatedRate", 0, 1000)
        s.send("cmd_Start", -1, -1)
        time.sleep(5.0)
        s.data = DataReader(data)
        time.sleep(2.0)
        pk = s.data.packets
        reset = next((i for i, p in enumerate(pk) if p[1] == 0), 0)
        steps = [b[1] - a[1] for a, b in zip(pk[reset:], pk[reset + 1:])]
        s.send("cmd_Stop", -1, -1, first=5.0)
        s.send("cmd_SetPower", 0, 0)
        return {"samples_lost": any(step > 1 for step in steps),
                "~largest_counter_jump": max(steps, default=0)}
    finally:
        s.close()


def scenario_front_end(host):
    """Is the EEG usable before and after cmd_SetBufferedReference, and after a Stop?"""
    s = Session(host, data=True)
    try:
        def railed():
            s.data.packets.clear()
            time.sleep(1.5)
            pk = s.data.packets[-500:]
            return round(sum(abs(p[5]) >= RAIL for p in pk) / max(1, len(pk)), 1)
        # A switch command sent the moment cmd_SetPower 1 is answered.
        n = len(s.cmd_reader.lines)
        s.cmd.sendall(b"(sendCommand cmd_SetPower 0 0 1)\n")
        while len(s.cmd_reader.lines) == n:
            time.sleep(0.0005)
        s.send("cmd_SetBufferedReference", 0, 1, quiet=0.05)
        s.send("cmd_SetDecimatedRate", 0, 1000)
        s.send("cmd_Start", -1, -1)
        out = {"railed_when_set_right_after_power_on": railed()}
        s.send("cmd_Stop", -1, -1, first=5.0)
        s.send("cmd_Start", -1, -1)
        out["railed_after_Start"] = railed()
        s.send("cmd_SetBufferedReference", 0, 1)
        out["railed_after_SetBufferedReference_1"] = railed()
        s.send("cmd_Stop", -1, -1, first=5.0)
        s.send("cmd_Start", -1, -1)
        out["railed_after_Stop_Start"] = railed()
        s.send("cmd_Stop", -1, -1, first=5.0)
        s.send("cmd_SetPower", 0, 0)
        return out
    finally:
        s.close()


def scenario_after_session_end(host):
    """Leave the amp powered and streaming, drop the session, then Start only."""
    s = Session(host, data=True)
    s.send("cmd_SetPower", 0, 1)
    s.send("cmd_SetDecimatedRate", 0, 1000)
    s.send("cmd_Start", -1, -1)
    time.sleep(1.0)
    s.close()
    s = Session(host, data=True)
    try:
        time.sleep(1.0)
        out = {"data_flows_in_new_session_before_Start": len(s.data.packets) > 0}
        s.send("cmd_Start", -1, -1)
        out["stream_after_Start_without_SetPower"] = summarize_stream(s.data, stream_for(s, 1.5))
        s.send("cmd_Stop", -1, -1, first=5.0)
        s.send("cmd_SetPower", 0, 0)
        return out
    finally:
        s.close()


def record(host, active):
    scenarios = {
        "queries": scenario_queries,
        "framing": scenario_framing,
        "odd_replies": scenario_odd_replies,
        "second_client": scenario_second_client,
        "recognized": scenario_recognized,
    }
    if active:
        scenarios.update({
            "power": scenario_power,
            "stream_decimated_250": lambda h: scenario_stream(h, 250),
            "stream_decimated_1000": lambda h: scenario_stream(h, 1000),
            "stream_native_16000": lambda h: scenario_stream(h, 16000, "Native"),
            "data_reconnect": scenario_data_reconnect,
            "stalled_reader": scenario_stalled_reader,
            "front_end": scenario_front_end,
            "after_session_end": scenario_after_session_end,
        })
    results = {}
    for name, fn in scenarios.items():
        print(f"[{name}] ...", file=sys.stderr, flush=True)
        try:
            results[name] = fn(host)
        except Exception as exc:  # record and keep going: one wedge must not hide the rest
            results[name] = {"error": f"{type(exc).__name__}: {exc}"}
    return results


def flatten(d, prefix=""):
    for k, v in d.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            yield from flatten(v, key + ".")
        else:
            yield key, v


def compare(path_a, path_b):
    a, b = (dict(flatten(json.load(open(p)))) for p in (path_a, path_b))
    keys = sorted(k for k in a.keys() | b.keys() if not k.rsplit(".", 1)[-1].startswith("~"))
    diffs = [k for k in keys if a.get(k) != b.get(k)]
    for k in diffs:
        print(f"{k}\n  {path_a}: {a.get(k)!r}\n  {path_b}: {b.get(k)!r}")
    print(f"{len(diffs)} difference(s) in {len(keys)} compared keys")
    return 1 if diffs else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host")
    ap.add_argument("--active", action="store_true", help="power on and stream")
    ap.add_argument("-o", "--output", help="write the recording here (default: stdout)")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"))
    args = ap.parse_args()
    if args.compare:
        sys.exit(compare(*args.compare))
    if not args.host:
        ap.error("--host is required unless --compare is given")
    text = json.dumps(record(args.host, args.active), indent=1)
    if args.output:
        open(args.output, "w").write(text + "\n")
    else:
        print(text)


if __name__ == "__main__":
    main()
