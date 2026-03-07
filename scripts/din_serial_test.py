#!/usr/bin/env python3
"""
DIN test script: toggle USB-serial output lines while monitoring the DIN LSL stream.

This uses DTR, RTS, and TX-break as 3 independent binary outputs on a standard
USB-serial adapter. Depending on how your cable maps DB-9 pins to the amplifier's
DIN connector, some or all of these may register as DIN bit changes.

DB-9 output pins:
  Pin 4: DTR    (pyserial: ser.dtr)
  Pin 7: RTS    (pyserial: ser.rts)
  Pin 3: TX     (pyserial: ser.break_condition -- held low when True)

Note: RS-232 voltage levels may not be compatible with the amp's TTL-level DIN
inputs. If no bits change, voltage level mismatch is likely.

Requirements:
  pip install pyserial pylsl
"""

import argparse
import threading
import time

import pylsl
import serial


def monitor_din(stop_event: threading.Event, stream_name: str = ""):
    """Background thread: resolve DIN marker stream and print changes."""
    inlet = None

    while not stop_event.is_set():
        if inlet is None:
            print("[DIN] Resolving DIN stream (type='Markers', name='*_DIN')...")
            streams = pylsl.resolve_byprop("type", "Markers", timeout=5)
            din_streams = [s for s in streams if s.name().endswith("_DIN")]
            if stream_name:
                din_streams = [s for s in din_streams if stream_name in s.name()]
            if not din_streams:
                names = [s.name() for s in streams]
                print(f"[DIN] Not found yet (available Markers: {names}). "
                      "Is the amp linked/streaming? Retrying...")
                continue

            info = din_streams[0]
            print(f"[DIN] Connected to '{info.name()}'")
            print("[DIN] This stream only sends samples on DIN value *changes*.")
            print("[DIN] Waiting for events...\n")
            inlet = pylsl.StreamInlet(info, max_buflen=1)

        sample, ts = inlet.pull_sample(timeout=0.5)
        if sample is None:
            continue
        din = int(sample[0])
        bits = f"{din:016b}"
        active = [i + 1 for i in range(16) if (din >> i) & 1]
        active_str = f"  active: DIN {','.join(map(str, active))}" if active else ""
        print(f"[DIN] value={din:5d}  bits={bits}  (0x{din:04X}){active_str}")


def main():
    parser = argparse.ArgumentParser(description="Test DIN inputs via USB-serial adapter")
    parser.add_argument("--port", default="/dev/cu.usbserial-210",
                        help="Serial port (default: /dev/cu.usbserial-210)")
    parser.add_argument("--stream", default="",
                        help="LSL stream name filter (default: match any DIN stream)")
    args = parser.parse_args()

    stop_event = threading.Event()

    # Start DIN monitor in background
    din_thread = threading.Thread(target=monitor_din, args=(stop_event, args.stream),
                                  daemon=True)
    din_thread.start()

    # Open serial port
    print(f"[SER] Opening {args.port}...")
    try:
        ser = serial.Serial(args.port, 9600, timeout=1)
    except serial.SerialException as e:
        print(f"[SER] Failed to open port: {e}")
        stop_event.set()
        return

    # Start with all outputs in their 'idle' state
    ser.dtr = False
    ser.rts = False
    ser.break_condition = False
    time.sleep(0.5)

    print(f"\n[SER] Port opened. Output pins:")
    print(f"  DTR (pin 4): {'HIGH' if ser.dtr else 'LOW'}")
    print(f"  RTS (pin 7): {'HIGH' if ser.rts else 'LOW'}")
    print(f"  TX  (pin 3): IDLE (high)")
    print()
    print("Commands:")
    print("  d    - Toggle DTR")
    print("  r    - Toggle RTS")
    print("  b    - Toggle TX break (holds TX low)")
    print("  1    - All outputs ON  (DTR=1, RTS=1, break=1)")
    print("  0    - All outputs OFF (DTR=0, RTS=0, break=0)")
    print("  scan - Toggle each output one at a time with pauses")
    print("  p    - Print current output states")
    print("  q    - Quit")
    print()
    print("Watch the [DIN] lines above for bit changes.")
    print()

    break_state = False

    while not stop_event.is_set():
        try:
            cmd = input("> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            break

        if cmd == "q":
            break
        elif cmd == "d":
            ser.dtr = not ser.dtr
            print(f"[SER] DTR = {ser.dtr}")
        elif cmd == "r":
            ser.rts = not ser.rts
            print(f"[SER] RTS = {ser.rts}")
        elif cmd == "b":
            break_state = not break_state
            ser.break_condition = break_state
            print(f"[SER] TX break = {break_state}")
        elif cmd == "1":
            ser.dtr = True
            ser.rts = True
            break_state = True
            ser.break_condition = True
            print("[SER] All ON: DTR=1, RTS=1, break=1")
        elif cmd == "0":
            ser.dtr = False
            ser.rts = False
            break_state = False
            ser.break_condition = False
            print("[SER] All OFF: DTR=0, RTS=0, break=0")
        elif cmd == "scan":
            print("[SER] Scanning each output (2s per toggle)...")
            for name, on, off in [
                ("DTR", lambda: setattr(ser, 'dtr', True),
                        lambda: setattr(ser, 'dtr', False)),
                ("RTS", lambda: setattr(ser, 'rts', True),
                        lambda: setattr(ser, 'rts', False)),
                ("TX break", lambda: setattr(ser, 'break_condition', True),
                             lambda: setattr(ser, 'break_condition', False)),
            ]:
                print(f"[SER]   {name} ON")
                on()
                time.sleep(2)
                print(f"[SER]   {name} OFF")
                off()
                time.sleep(2)
            break_state = False
            print("[SER] Scan complete.")
        elif cmd == "p":
            print(f"[SER] DTR={ser.dtr}, RTS={ser.rts}, break={break_state}")
        elif cmd:
            print(f"Unknown command: {cmd}")

    stop_event.set()
    ser.close()
    print("Done.")


if __name__ == "__main__":
    main()
