#!/usr/bin/env python3
"""
Raw TCP timestamp test: compare DIN and Physio16 timing using FPGA timestamps.

Connects directly to the AmpServer data stream (bypassing LSL entirely) and
reads PacketFormat2 binary packets.  For each unique packet, records the FPGA
timestamp, DIN state, and Physio16 ch1 value.  After recording, finds DIN
transitions and Physio16 transitions and reports whether they appear in the
same packet or in different packets (and by how many samples/microseconds).

The amp must already be streaming (started by Net Station, the GUI, etc.).

Usage:
    python raw_timestamp_test.py                          # default 30s
    python raw_timestamp_test.py --duration 60            # longer
    python raw_timestamp_test.py --host 10.10.10.51       # non-default IP
    python raw_timestamp_test.py --save data.npz          # save raw data
"""

import argparse
import socket
import struct
import sys
import time

import numpy as np


# PacketFormat2 layout (packed, 1264 bytes total)
# All fields little-endian except the outer AmpDataPacketHeader which is big-endian.
PF2_SIZE = 1264
HEADER_SIZE = 16  # AmpDataPacketHeader: int64 ampID + uint64 length (big-endian)

# Offsets within PacketFormat2 (all little-endian)
OFF_DIN = 0             # uint16_t digitalInputs
OFF_COUNTER = 25        # uint64_t packetCounter  (after din(2) + tr(1) + 2*pib_aux(11))
OFF_TIMESTAMP = 33      # uint64_t timeStamp (microseconds, FPGA clock)
OFF_PIB1_DATA = 1136    # int32_t pib1_Data[16]  (64 bytes)


def recv_exact(sock, n):
    """Read exactly n bytes from socket."""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Connection closed")
        buf.extend(chunk)
    return bytes(buf)


def parse_header(data):
    """Parse AmpDataPacketHeader (big-endian)."""
    amp_id, length = struct.unpack('>qQ', data)
    return amp_id, length


def parse_sample(data):
    """Extract fields we care about from a PacketFormat2 packet."""
    din = struct.unpack_from('<H', data, OFF_DIN)[0]
    counter = struct.unpack_from('<Q', data, OFF_COUNTER)[0]
    timestamp = struct.unpack_from('<Q', data, OFF_TIMESTAMP)[0]
    pib1_ch1 = struct.unpack_from('<i', data, OFF_PIB1_DATA)[0]  # int32
    return din, counter, timestamp, pib1_ch1


def find_edges_by_counter(values, counters, timestamps, debounce_samples=50):
    """Find transitions in a signal using hysteresis + debounce.

    Returns list of (counter, timestamp_us) at each transition.
    """
    if len(values) < 10:
        return []

    vmin, vmax = float(np.min(values)), float(np.max(values))
    vrange = vmax - vmin
    if vrange < abs(vmax) * 0.01:
        return []

    lo = vmin + vrange * 0.10
    hi = vmax - vrange * 0.10

    state = None
    edges = []
    last_edge_idx = -debounce_samples * 2

    for i in range(len(values)):
        v = float(values[i])
        if v <= lo:
            if state == 'high' and (i - last_edge_idx) >= debounce_samples:
                edges.append((counters[i], timestamps[i]))
                last_edge_idx = i
            state = 'low'
        elif v >= hi:
            if state == 'low' and (i - last_edge_idx) >= debounce_samples:
                edges.append((counters[i], timestamps[i]))
                last_edge_idx = i
            state = 'high'

    return edges


def main():
    parser = argparse.ArgumentParser(
        description="Raw TCP timestamp test: DIN vs Physio16 using FPGA timestamps.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--host", default="10.10.10.51",
                        help="AmpServer IP (default: 10.10.10.51)")
    parser.add_argument("--port", type=int, default=9879,
                        help="Data stream port (default: 9879)")
    parser.add_argument("--duration", type=int, default=30,
                        help="Recording duration in seconds (default: 30)")
    parser.add_argument("--save", metavar="FILE",
                        help="Save raw data to .npz file")
    args = parser.parse_args()

    # ── Connect ──────────────────────────────────────────────────────────
    print(f"Connecting to {args.host}:{args.port}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    try:
        sock.connect((args.host, args.port))
    except (socket.timeout, ConnectionRefusedError) as e:
        print(f"Connection failed: {e}", file=sys.stderr)
        sys.exit(1)

    # Send listen command
    cmd = "(sendCommand cmd_ListenToAmp 0 0 0)\n"
    sock.sendall(cmd.encode())
    print("Listening for data...\n")

    # ── Record ───────────────────────────────────────────────────────────
    all_din = []
    all_counter = []
    all_timestamp = []
    all_pib1 = []

    last_counter = None
    din_event_count = 0
    last_din = None

    start = time.time()
    try:
        while time.time() - start < args.duration:
            # Read packet header (big-endian)
            hdr_data = recv_exact(sock, HEADER_SIZE)
            amp_id, length = parse_header(hdr_data)
            n_samples = length // PF2_SIZE

            # Read all samples in this batch
            batch_data = recv_exact(sock, length)

            for s in range(n_samples):
                offset = s * PF2_SIZE
                sample_data = batch_data[offset:offset + PF2_SIZE]
                din, counter, timestamp, pib1_ch1 = parse_sample(sample_data)

                # Skip duplicates (same packetCounter = decimated duplicate)
                if counter == last_counter:
                    continue
                last_counter = counter

                # Active-low inversion (match what LSL app does)
                din_inverted = (~din) & 0xFFFF

                all_din.append(din_inverted)
                all_counter.append(counter)
                all_timestamp.append(timestamp)
                all_pib1.append(pib1_ch1)

                # Print DIN transitions live
                if last_din is not None and (din_inverted & 1) != (last_din & 1):
                    din_event_count += 1
                    bit0 = "ON" if din_inverted & 1 else "OFF"
                    elapsed = time.time() - start
                    print(f"  [{elapsed:6.1f}s] DIN-1 {bit0}  "
                          f"counter={counter}  fpga_ts={timestamp} us")
                last_din = din_inverted

    except KeyboardInterrupt:
        print("\nStopped early.")
    finally:
        # Stop listening
        try:
            stop_cmd = "(sendCommand cmd_StopListeningToAmp 0 0 0)\n"
            sock.sendall(stop_cmd.encode())
        except Exception:
            pass
        sock.close()

    # ── Convert to arrays ────────────────────────────────────────────────
    din_arr = np.array(all_din, dtype=np.uint16)
    counter_arr = np.array(all_counter, dtype=np.uint64)
    ts_arr = np.array(all_timestamp, dtype=np.uint64)
    pib1_arr = np.array(all_pib1, dtype=np.int32)

    n = len(din_arr)
    if n == 0:
        print("No data received.", file=sys.stderr)
        sys.exit(1)

    # Estimate sample rate from FPGA timestamps
    if n > 1:
        duration_us = float(ts_arr[-1] - ts_arr[0])
        if duration_us > 0:
            measured_rate = (n - 1) / (duration_us / 1e6)
            print(f"\nRecorded {n} unique samples over "
                  f"{duration_us/1e6:.2f}s ({measured_rate:.0f} Hz)")
        else:
            print(f"\nRecorded {n} unique samples")

    if args.save:
        np.savez(args.save, din=din_arr, counter=counter_arr,
                 timestamp=ts_arr, pib1=pib1_arr)
        print(f"Saved to {args.save}")

    # ── Find DIN-1 transitions ───────────────────────────────────────────
    din_bit0 = din_arr & 1
    din_edges = []
    for i in range(1, n):
        if din_bit0[i] != din_bit0[i - 1]:
            din_edges.append((counter_arr[i], ts_arr[i]))

    # ── Find Physio16 ch1 transitions ────────────────────────────────────
    sample_rate = measured_rate if n > 1 and duration_us > 0 else 1000
    debounce = max(int(sample_rate * 0.05), 10)  # 50ms debounce in samples
    physio_edges = find_edges_by_counter(pib1_arr, counter_arr, ts_arr,
                                         debounce_samples=debounce)

    # ── Match and report ─────────────────────────────────────────────────
    print(f"\nDIN-1 transitions:     {len(din_edges)}")
    print(f"Physio16 transitions:  {len(physio_edges)}")

    if not din_edges or not physio_edges:
        print("\nNot enough transitions to analyze.")
        sys.exit(0)

    # Convert to arrays for matching
    din_ts_us = np.array([e[1] for e in din_edges], dtype=np.float64)
    din_ctr = np.array([e[0] for e in din_edges], dtype=np.uint64)
    phy_ts_us = np.array([e[1] for e in physio_edges], dtype=np.float64)
    phy_ctr = np.array([e[0] for e in physio_edges], dtype=np.uint64)

    # Match each DIN edge to nearest Physio edge
    max_gap_us = 500_000  # 500ms
    used = set()
    pairs = []

    for i in range(len(din_edges)):
        diffs = np.abs(phy_ts_us - din_ts_us[i])
        order = np.argsort(diffs)
        for idx in order:
            if diffs[idx] >= max_gap_us:
                break
            if idx not in used:
                sample_delta = int(phy_ctr[idx]) - int(din_ctr[i])
                time_delta_us = float(phy_ts_us[idx]) - float(din_ts_us[i])
                pairs.append((din_ctr[i], phy_ctr[idx], sample_delta,
                              time_delta_us))
                used.add(idx)
                break

    # ── Report ───────────────────────────────────────────────────────────
    print(f"Matched pairs:         {len(pairs)}\n")

    if not pairs:
        print("No matched transitions.")
        sys.exit(0)

    sample_deltas = np.array([p[2] for p in pairs])
    time_deltas_us = np.array([p[3] for p in pairs])
    time_deltas_ms = time_deltas_us / 1000.0

    print(f"{'#':>3}  {'DIN counter':>14}  {'Physio counter':>14}  "
          f"{'Samples':>8}  {'FPGA delta':>12}")
    print("-" * 60)
    for i, (dc, pc, sd, td) in enumerate(pairs):
        print(f"{i+1:3d}  {dc:14d}  {pc:14d}  {sd:+8d}  {td/1000:+12.2f} ms")

    print(f"\n--- FPGA timestamp delta (Physio - DIN) ---")
    print(f"  Mean:   {np.mean(time_deltas_ms):+.2f} ms")
    print(f"  Median: {np.median(time_deltas_ms):+.2f} ms")
    print(f"  Std:    {np.std(time_deltas_ms):.2f} ms")
    print(f"  Range:  [{np.min(time_deltas_ms):+.2f}, "
          f"{np.max(time_deltas_ms):+.2f}] ms")

    print(f"\n--- Sample count delta (Physio - DIN) ---")
    print(f"  Mean:   {np.mean(sample_deltas):+.1f} samples")
    print(f"  Median: {np.median(sample_deltas):+.1f} samples")
    print(f"  Range:  [{np.min(sample_deltas):+d}, "
          f"{np.max(sample_deltas):+d}] samples")

    zero_count = np.sum(sample_deltas == 0)
    print(f"\n  Same packet (delta=0): {zero_count}/{len(pairs)}")

    if zero_count == len(pairs):
        print("\n  >> ALL transitions appear in the SAME packet.")
        print("     No delay between DIN and Physio16 at the hardware level.")
    else:
        print(f"\n  >> Physio16 transitions appear in DIFFERENT packets than DIN.")
        print(f"     Hardware-level delay: {np.median(time_deltas_ms):+.2f} ms "
              f"({np.median(sample_deltas):+.0f} samples)")


if __name__ == "__main__":
    main()
