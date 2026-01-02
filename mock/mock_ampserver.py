#!/usr/bin/env python3
"""
Mock EGI AmpServer for development testing.

Simulates an NA400 amplifier with 256 channels.
Listens on command (9877), notification (9878), and data (9879) ports.
"""

import argparse
import socket
import struct
import threading
import time
import math
import random

# Amplifier configuration (matches captured real device)
AMP_CONFIG = {
    "serial_number": "A15530499",
    "amp_type": "NA400",
    "legacy_board": "false",
    "packet_format": 2,
    "system_version": "2.0.17",
    "number_of_channels": 256,
}

class MockAmpServer:
    def __init__(self, host="0.0.0.0", cmd_port=9877, notify_port=9878, data_port=9879,
                 impedance_mode=False):
        self.host = host
        self.cmd_port = cmd_port
        self.notify_port = notify_port
        self.data_port = data_port
        self.impedance_mode = impedance_mode

        self.running = False
        self.streaming = False
        self.client_id = 0
        self.notification_id = 0
        self.notification_clients = []

    def start(self):
        self.running = True

        # Start listener threads
        threads = [
            threading.Thread(target=self._command_listener, daemon=True),
            threading.Thread(target=self._notification_listener, daemon=True),
            threading.Thread(target=self._data_listener, daemon=True),
        ]

        for t in threads:
            t.start()

        print(f"Mock AmpServer started")
        print(f"  Command port:      {self.cmd_port}")
        print(f"  Notification port: {self.notify_port}")
        print(f"  Data port:         {self.data_port}")
        mode = "impedance" if self.impedance_mode else "eeg"
        print(f"Mode: {mode}")
        print(f"Press Ctrl+C to stop.\n")

        try:
            while self.running:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\nShutting down...")
            self.running = False

    def _command_listener(self):
        """Handle command port connections."""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.cmd_port))
        server.listen(5)
        server.settimeout(1.0)

        while self.running:
            try:
                client, addr = server.accept()
                print(f"[CMD] Client connected from {addr}")
                threading.Thread(
                    target=self._handle_command_client,
                    args=(client,),
                    daemon=True
                ).start()
            except socket.timeout:
                continue

    def _handle_command_client(self, client):
        """Process commands from a client."""
        client.settimeout(1.0)
        buffer = ""

        while self.running:
            try:
                data = client.recv(4096)
                if not data:
                    break

                buffer += data.decode('utf-8', errors='ignore')

                # Process complete commands (ended with newline or closing paren)
                while '\n' in buffer or (buffer.count('(') > 0 and buffer.count('(') == buffer.count(')')):
                    if '\n' in buffer:
                        cmd, buffer = buffer.split('\n', 1)
                    else:
                        cmd = buffer
                        buffer = ""

                    cmd = cmd.strip()
                    if cmd:
                        response = self._process_command(cmd)
                        if response:
                            client.send((response + '\n').encode())

            except socket.timeout:
                continue
            except Exception as e:
                print(f"[CMD] Error: {e}")
                break

        print("[CMD] Client disconnected")
        client.close()

    def _process_command(self, cmd):
        """Parse and respond to a command."""
        print(f"[CMD] Received: {cmd}")

        if "cmd_GetAmpDetails" in cmd:
            response = (
                f"(sendCommand_return (status complete) "
                f"(amp_details "
                f"(serial_number {AMP_CONFIG['serial_number']}) "
                f"(amp_type {AMP_CONFIG['amp_type']}) "
                f"(legacy_board {AMP_CONFIG['legacy_board']}) "
                f"(packet_format {AMP_CONFIG['packet_format']}) "
                f"(system_version {AMP_CONFIG['system_version']}) "
                f"(number_of_channels {AMP_CONFIG['number_of_channels']})))"
            )
        elif "cmd_Stop" in cmd:
            self.streaming = False
            self._send_notification("ntn_AmpStopped")
            response = "(sendCommand_return (status complete))"
        elif "cmd_SetPower" in cmd:
            if "1)" in cmd or "1\n" in cmd or cmd.endswith(" 1"):
                self._send_notification("ntn_AmpPowerOn")
            else:
                self._send_notification("ntn_AmpPowerOff")
            response = "(sendCommand_return (status complete))"
        elif "cmd_Start" in cmd:
            self._send_notification("ntn_AmpStarted")
            response = "(sendCommand_return (status complete))"
        elif "cmd_SetDecimatedRate" in cmd:
            response = "(sendCommand_return (status complete))"
        elif "cmd_DefaultAcquisitionState" in cmd:
            response = "(sendCommand_return (status complete))"
        else:
            response = "(sendCommand_return (status complete))"

        print(f"[CMD] Response: {response[:80]}...")
        return response

    def _notification_listener(self):
        """Handle notification port connections."""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.notify_port))
        server.listen(5)
        server.settimeout(1.0)

        while self.running:
            try:
                client, addr = server.accept()
                print(f"[NOTIFY] Client connected from {addr}")
                self.client_id += 1
                self.notification_clients.append(client)

                # Send initial connection notification
                msg = f"(sendCommand_return (status complete) (amp_server_status_info (client_connected (client_id {self.client_id}))))\n"
                client.send(msg.encode())

            except socket.timeout:
                continue

    def _send_notification(self, notification_type):
        """Send a notification to all connected clients."""
        self.notification_id += 1
        msg = f"(notification {notification_type} {self.notification_id} 0 -1)\n"

        for client in self.notification_clients[:]:
            try:
                client.send(msg.encode())
            except:
                self.notification_clients.remove(client)

    def _data_listener(self):
        """Handle data port connections."""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.data_port))
        server.listen(5)
        server.settimeout(1.0)

        while self.running:
            try:
                client, addr = server.accept()
                print(f"[DATA] Client connected from {addr}")
                threading.Thread(
                    target=self._handle_data_client,
                    args=(client,),
                    daemon=True
                ).start()
            except socket.timeout:
                continue

    def _handle_data_client(self, client):
        """Handle data streaming to a client."""
        client.settimeout(1.0)

        # Wait for cmd_ListenToAmp command
        try:
            data = client.recv(4096)
            cmd = data.decode('utf-8', errors='ignore')
            print(f"[DATA] Received: {cmd.strip()}")

            if "cmd_ListenToAmp" in cmd:
                self.streaming = True
                self._stream_data(client)
        except Exception as e:
            print(f"[DATA] Error: {e}")

        print("[DATA] Client disconnected")
        client.close()

    def _stream_data(self, client):
        """Stream synthetic EEG data in PacketFormat2."""
        packet_counter = 0
        sample_rate = 1000
        n_channels = AMP_CONFIG["number_of_channels"]

        # PacketFormat2 structure size
        # 2 (digitalInputs) + 1 (tr) + 11 (pib1_aux) + 11 (pib2_aux) +
        # 8 (packetCounter) + 8 (timeStamp) + 1 (netCode) + 38 (reserved) +
        # 256*4 (eegData) + 3*4 (auxData) + 5*4 (monitors) + 16*4 (pib1) + 16*4 (pib2)
        # = 80 + 1024 + 12 + 20 + 64 + 64 = 1264 bytes per sample

        print(f"[DATA] Starting stream: {n_channels} channels @ {sample_rate} Hz")

        start_time = time.time()

        while self.running and self.streaming:
            try:
                # Generate one sample
                t = packet_counter / sample_rate

                # Create PacketFormat2 packet
                packet = self._create_packet_format2(packet_counter, t, n_channels)

                # Create header: ampID (int64, big-endian) + length (uint64, big-endian)
                header = struct.pack(">qQ", 0, len(packet))

                client.send(header + packet)
                packet_counter += 1

                # Pace to ~1000 Hz
                expected_time = start_time + (packet_counter / sample_rate)
                sleep_time = expected_time - time.time()
                if sleep_time > 0:
                    time.sleep(sleep_time)

            except Exception as e:
                print(f"[DATA] Stream error: {e}")
                break

        print(f"[DATA] Stream stopped after {packet_counter} samples")

    def _create_packet_format2(self, packet_counter, t, n_channels):
        """Create a PacketFormat2 binary packet with synthetic data."""

        ref_monitor = 0

        if self.impedance_mode:
            # Deterministic counts so compliance math can be verified.
            base_count = 10000
            step = 250
            ref_monitor = base_count
            eeg_data = [base_count + (ch % 8) * step for ch in range(256)]
            tr_value = 0xFB  # Injecting current flag (bit 2 cleared)
        else:
            # Generate synthetic EEG: sine waves at different frequencies per channel
            eeg_data = []
            for ch in range(256):
                freq = 10 + (ch % 40)  # 10-50 Hz sine waves
                amplitude = 100  # microvolts (will be scaled by client)
                # Add some noise
                value = int(amplitude * math.sin(2 * math.pi * freq * t) + random.gauss(0, 10))
                eeg_data.append(value)
            tr_value = 0

        # Determine net code based on channel count
        if n_channels == 256:
            net_code = 2  # GSN256_2_0
        elif n_channels == 128:
            net_code = 1  # GSN128_2_0
        elif n_channels == 64:
            net_code = 0  # GSN64_2_0
        else:
            net_code = 15  # NoNet

        # Build packet
        packet = b''

        # digitalInputs (uint16)
        packet += struct.pack("<H", 0)

        # tr (uint8)
        packet += struct.pack("<B", tr_value)

        # pib1_aux (11 bytes)
        packet += b'\x00' * 11

        # pib2_aux (11 bytes)
        packet += b'\x00' * 11

        # packetCounter (uint64)
        packet += struct.pack("<Q", packet_counter)

        # timeStamp (uint64) - microseconds since start
        timestamp = int(t * 1_000_000)
        packet += struct.pack("<Q", timestamp)

        # netCode (uint8)
        packet += struct.pack("<B", net_code)

        # reserved (38 bytes)
        packet += b'\x00' * 38

        # eegData (256 x int32)
        for val in eeg_data:
            packet += struct.pack("<i", val)

        # auxData (3 x int32)
        packet += struct.pack("<3i", 0, 0, 0)

        # refMonitor, comMonitor, driveMonitor, diagnosticsChannel, currentSense (5 x int32)
        packet += struct.pack("<5i", ref_monitor, 0, 0, 0, 0)

        # pib1_Data, pib2_Data (32 x int32)
        packet += struct.pack("<32i", *([0] * 32))

        return packet


def main():
    parser = argparse.ArgumentParser(description="Mock EGI AmpServer")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address")
    parser.add_argument("--cmd-port", type=int, default=9877, help="Command port")
    parser.add_argument("--notify-port", type=int, default=9878, help="Notification port")
    parser.add_argument("--data-port", type=int, default=9879, help="Data port")
    parser.add_argument("--impedance", action="store_true",
                        help="Emit deterministic impedance packets")
    args = parser.parse_args()

    server = MockAmpServer(
        host=args.host,
        cmd_port=args.cmd_port,
        notify_port=args.notify_port,
        data_port=args.data_port,
        impedance_mode=args.impedance,
    )
    server.start()


if __name__ == "__main__":
    main()
