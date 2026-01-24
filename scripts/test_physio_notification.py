#!/usr/bin/env python3
"""
Test Physio16 connection status notifications.

Connects to AmpServer and monitors for physio connection status changes.
"""

import socket
import threading
import time

AMP_ADDRESS = '10.10.10.51'
CMD_PORT = 9877
NOTIF_PORT = 9878

def notification_listener(notif_sock, stop_event):
    """Listen for notifications and print them."""
    notif_sock.settimeout(1.0)
    while not stop_event.is_set():
        try:
            data = notif_sock.recv(4096)
            if data:
                msg = data.decode().strip()
                if msg:
                    print(f"\n[NOTIFICATION] {msg}")
                    if "Physio" in msg or "physio" in msg:
                        print("  ^^^ PHYSIO STATUS ^^^")
        except socket.timeout:
            continue
        except Exception as e:
            if not stop_event.is_set():
                print(f"[ERROR] Notification listener: {e}")
            break

def send_command(cmd_sock, command):
    """Send a command and return the response."""
    cmd_sock.send(f"{command}\n".encode())
    time.sleep(0.1)
    try:
        response = cmd_sock.recv(4096).decode().strip()
        return response
    except:
        return None

def main():
    print(f"Connecting to AmpServer at {AMP_ADDRESS}...")

    # Connect to notification port
    notif_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    notif_sock.connect((AMP_ADDRESS, NOTIF_PORT))
    print(f"Connected to notification port {NOTIF_PORT}")

    # Connect to command port
    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cmd_sock.settimeout(5.0)
    cmd_sock.connect((AMP_ADDRESS, CMD_PORT))
    print(f"Connected to command port {CMD_PORT}")

    # Start notification listener thread
    stop_event = threading.Event()
    listener_thread = threading.Thread(target=notification_listener, args=(notif_sock, stop_event))
    listener_thread.start()

    print("\n" + "="*60)
    print("Physio16 Connection Status Test")
    print("="*60)
    print("\nCommands:")
    print("  q - Query physio status (cmd_GetPhysioConnectionStatus)")
    print("  s - Subscribe to notifications (cmd_ReceiveNotifications)")
    print("  x - Exit")
    print("\nChange the Physio16 plug and watch for notifications.")
    print("="*60 + "\n")

    # Subscribe to notifications first
    print("Subscribing to notifications...")
    resp = send_command(cmd_sock, "(sendCommand cmd_ReceiveNotifications 0 0 (value 0))")
    print(f"[RESPONSE] {resp}")

    try:
        while True:
            cmd = input("\nEnter command (q/s/x): ").strip().lower()

            if cmd == 'x':
                break
            elif cmd == 'q':
                print("\nSending cmd_GetPhysioConnectionStatus...")
                resp = send_command(cmd_sock, "(sendCommand cmd_GetPhysioConnectionStatus 0 0 (value 0))")
                print(f"[RESPONSE] {resp}")
            elif cmd == 's':
                print("\nSending cmd_ReceiveNotifications...")
                resp = send_command(cmd_sock, "(sendCommand cmd_ReceiveNotifications 0 0 (value 0))")
                print(f"[RESPONSE] {resp}")
            else:
                print("Unknown command. Use q, s, or x.")

    except KeyboardInterrupt:
        print("\nInterrupted.")

    # Cleanup
    stop_event.set()
    listener_thread.join(timeout=2)
    cmd_sock.close()
    notif_sock.close()
    print("Disconnected.")

if __name__ == "__main__":
    main()
