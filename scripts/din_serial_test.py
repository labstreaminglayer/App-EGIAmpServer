#!/usr/bin/env python3
"""
Test script to probe DIN inputs via USB-serial connection.
Run this while monitoring DIN output from EGIAmpServerCLI.
"""

import serial
import time
import sys

PORT = "/dev/cu.usbserial-210"
BAUD = 9600

def main():
    print(f"Opening {PORT} at {BAUD} baud...")
    ser = serial.Serial(PORT, BAUD, timeout=1)

    print("\nSerial port opened. Current control line states:")
    print(f"  DTR: {ser.dtr}")
    print(f"  RTS: {ser.rts}")
    print(f"  CTS: {ser.cts}")
    print(f"  DSR: {ser.dsr}")
    print(f"  CD:  {ser.cd}")
    print(f"  RI:  {ser.ri}")

    print("\n--- Interactive Mode ---")
    print("Commands:")
    print("  d - Toggle DTR")
    print("  r - Toggle RTS")
    print("  b - Toggle BREAK")
    print("  0-9 - Send that digit as a byte")
    print("  s - Send 0x00")
    print("  f - Send 0xFF")
    print("  a - Send all bytes 0x00-0xFF rapidly")
    print("  t - Send test pattern (0x55, 0xAA)")
    print("  p - Print control line states")
    print("  q - Quit")
    print()

    break_state = False

    while True:
        try:
            cmd = input("> ").strip().lower()
        except EOFError:
            break

        if cmd == 'q':
            break
        elif cmd == 'd':
            ser.dtr = not ser.dtr
            print(f"DTR = {ser.dtr}")
        elif cmd == 'r':
            ser.rts = not ser.rts
            print(f"RTS = {ser.rts}")
        elif cmd == 'b':
            break_state = not break_state
            ser.break_condition = break_state
            print(f"BREAK = {break_state}")
        elif cmd == 'p':
            print(f"  DTR: {ser.dtr}, RTS: {ser.rts}")
            print(f"  CTS: {ser.cts}, DSR: {ser.dsr}, CD: {ser.cd}, RI: {ser.ri}")
        elif cmd == 's':
            ser.write(b'\x00')
            print("Sent 0x00")
        elif cmd == 'f':
            ser.write(b'\xff')
            print("Sent 0xFF")
        elif cmd == 'a':
            print("Sending 0x00-0xFF...")
            for i in range(256):
                ser.write(bytes([i]))
                time.sleep(0.01)
            print("Done")
        elif cmd == 't':
            ser.write(b'\x55\xaa')
            print("Sent 0x55, 0xAA")
        elif cmd.isdigit():
            ser.write(cmd.encode())
            print(f"Sent '{cmd}' (0x{ord(cmd):02x})")
        elif cmd:
            # Send arbitrary string
            ser.write(cmd.encode())
            print(f"Sent: {cmd!r}")

    ser.close()
    print("Closed.")

if __name__ == "__main__":
    main()
