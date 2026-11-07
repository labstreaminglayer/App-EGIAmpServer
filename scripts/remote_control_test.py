#!/usr/bin/env python3
"""End-to-end check of the remote control service and the NA500 BNC output trigger.

Start the CLI (or GUI with <remoteport> in its config) with remote control, e.g.
    EGIAmpServerCLI --address 127.0.0.1 --remote-port 9890
against `MockAmpServer --amp-type NA500` or a real Net Amps 500, then run
    python scripts/remote_control_test.py --port 9890
The trigger check needs BNC out cabled to BNC in on a real amplifier (the mock
loops it back internally); skip it with --no-loopback.
"""

import argparse
import socket
import time

import pylsl


class Remote:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=60)
        self.file = self.sock.makefile("r")

    def __call__(self, line):
        self.sock.sendall((line + "\n").encode())
        reply = self.file.readline().strip()
        print(f"> {line}\n< {reply}")
        return reply


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=9890)
    ap.add_argument("--no-loopback", action="store_true", help="skip the BNC out -> BNC in check")
    args = ap.parse_args()
    r = Remote(args.port)

    assert r("help").startswith("OK")
    assert r("bogus").startswith("ERROR")
    if "linked=1" in r("status"):
        assert r("unlink") == "OK"
    assert r("set sample_rate 1000") == "OK"
    assert r("set data_port 0").startswith("ERROR")
    assert "sample_rate=1000" in r("status")
    assert r("trigger").startswith("ERROR")  # not linked
    assert r("link") == "OK"
    assert r("link").startswith("ERROR")
    status = r("status")
    assert "linked=1" in status
    assert r("set sample_rate 500").startswith("ERROR")  # linked

    if "Net_Amps_500" in status and not args.no_loopback:
        assert r("trigger 50").startswith("ERROR")  # out of range
        din = pylsl.resolve_bypred("type='Markers' and contains(name,'_DIN')", 1, 10.0)
        assert din, "no DIN stream"
        inlet = pylsl.StreamInlet(din[0])
        inlet.open_stream(5.0)
        time.sleep(0.5)
        inlet.pull_chunk()  # drop anything before the triggers
        sent = 0
        hits = 0
        for _ in range(5):
            assert r("trigger 500") == "OK"
            sent += 1
            deadline = time.time() + 0.2  # well inside the mock's periodic DIN pulses
            while time.time() < deadline:
                values, _ = inlet.pull_chunk(timeout=0.1)
                if any(v[0] & 0x10000 for v in values):  # BNC-in is DIN bit 16
                    hits += 1
                    break
            time.sleep(0.2)
        print(f"BNC events seen for {hits}/{sent} triggers")
        assert hits == sent

    assert r("unlink") == "OK"
    assert "linked=0" in r("status")
    print("PASS")


if __name__ == "__main__":
    main()
