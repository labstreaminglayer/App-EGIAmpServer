#!/usr/bin/env python3
"""
Monitor DIN events from the EGIAmpServer LSL markers stream.

Resolves the DIN stream (type "Markers", name ending in "_DIN") and prints
each event with its timestamp, raw value, and individual bit states.

Usage:
    python din_lsl_monitor.py              # auto-resolve DIN stream
    python din_lsl_monitor.py --bits       # show individual DIN bit breakdown
    python din_lsl_monitor.py --raw        # show only value and timestamp
"""

import argparse
import sys
import pylsl


DIN_BITS = 16


def format_bits(value):
    """Format a 16-bit value as individual DIN line states."""
    active = [f"DIN{i+1}" for i in range(DIN_BITS) if value & (1 << i)]
    return ", ".join(active) if active else "(none)"


def resolve_din_stream(timeout=10.0):
    """Resolve the DIN markers stream."""
    # First show all available streams for diagnostics
    print(f"Searching for LSL streams (timeout={timeout}s)...")
    all_streams = pylsl.resolve_streams(timeout)
    if all_streams:
        print(f"All available streams:")
        for s in all_streams:
            print(f"  {s.name()} (type={s.type()}, fmt={s.channel_format()}, "
                  f"ch={s.channel_count()}, rate={s.nominal_srate()}, "
                  f"src={s.source_id()})")
        print()
    else:
        print("No LSL streams found at all.\n")
        return None

    # Look for DIN stream by name
    din_streams = [s for s in all_streams if s.name().endswith("_DIN")]
    if not din_streams:
        print("No '_DIN' stream found.")
        return None
    if len(din_streams) > 1:
        print("Multiple DIN streams found:")
        for i, s in enumerate(din_streams):
            print(f"  [{i}] {s.name()} ({s.source_id()})")
        print(f"Using: {din_streams[0].name()}")
    return din_streams[0]


def main():
    parser = argparse.ArgumentParser(description="Monitor DIN events from EGIAmpServer LSL stream")
    parser.add_argument("--bits", action="store_true", help="Show individual DIN bit breakdown")
    parser.add_argument("--raw", action="store_true", help="Minimal output: value and timestamp only")
    parser.add_argument("--timeout", type=float, default=10.0, help="Stream resolve timeout (seconds)")
    args = parser.parse_args()

    info = resolve_din_stream(args.timeout)
    if info is None:
        print("No DIN stream found.", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to: {info.name()} (source: {info.source_id()})")
    print(f"  Format: {info.channel_format()}, Channels: {info.channel_count()}")
    print()

    inlet = pylsl.StreamInlet(info, processing_flags=pylsl.proc_clocksync)

    if args.raw:
        print("timestamp,value")
    else:
        print("Waiting for DIN events... (Ctrl+C to stop)\n")
        header = f"{'Timestamp':>16s}  {'Value':>7s}  {'Hex':>6s}  {'Binary':>18s}"
        if args.bits:
            header += f"  {'Active Lines'}"
        print(header)
        print("-" * len(header + "  " * 3 + "-" * 30 if args.bits else header))

    try:
        while True:
            sample, timestamp = inlet.pull_sample(timeout=1.0)
            if sample is None:
                continue

            value = int(sample[0])

            if args.raw:
                print(f"{timestamp:.6f},{value}")
            else:
                line = f"{timestamp:16.6f}  {value:7d}  0x{value:04X}  {value:016b}"
                if args.bits:
                    line += f"  {format_bits(value)}"
                print(line)

    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
