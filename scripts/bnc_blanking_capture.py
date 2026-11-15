#!/usr/bin/env python3
"""Capture a real NA500 mode-1/mode-2 BNC loopback comparison.

BNC OUT must be connected to BNC IN. Other AmpServer clients must be unlinked.
Uses one session, 4000 Hz native, 1000 us default blanking/output widths, and ABBA
blocks. Saves every 1264-byte PacketFormat2 (little endian, no frame headers),
command replies, notifications, and sample-index boundaries. No LSL required.
Leaves the BNC input in mode 2, then stops and powers down acquisition.
"""

import argparse
import datetime
import json
import pathlib
import socket
import struct
import threading
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host', default='10.10.10.62')
    ap.add_argument('--out', type=pathlib.Path, required=True)
    ap.add_argument('--pulses', type=int, default=20)
    ap.add_argument('--interval', type=float, default=0.5)
    ap.add_argument('--width', type=int, default=1000)
    args = ap.parse_args()
    if not 100 <= args.width <= 1600 or args.pulses < 1 or args.interval < .1:
        ap.error('width must be 100-1600 us; pulses >= 1; interval >= .1 s')
    args.out.mkdir(parents=True, exist_ok=False)
    meta = dict(host=args.host, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                rate=4000, native=True, blanking_width_us=args.width,
                output_width_us=1000, sequence=[1, 2, 2, 1],
                input_description='Open EEG inputs; BNC OUT cabled to BNC IN',
                packet_size=1264, samples=0, commands=[], notifications=[], blocks=[], errors=[])
    stop = threading.Event()
    power_on = threading.Event()
    sockets, workers = [], []
    command_file = (args.out / 'commands.jsonl').open('w')

    def save():
        (args.out / 'capture.json').write_text(json.dumps(meta, indent=2))

    def connect(port):
        sock = socket.create_connection((args.host, port), timeout=5)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sockets.append(sock)
        return sock

    def exact(sock, count):
        result = bytearray()
        while len(result) < count:
            if stop.is_set():
                raise EOFError
            try:
                chunk = sock.recv(count - len(result))
            except socket.timeout:
                continue
            if not chunk:
                raise EOFError
            result.extend(chunk)
        return result

    def data_reader(sock):
        try:
            with (args.out / 'packets.bin').open('wb') as output:
                while not stop.is_set():
                    amp, length = struct.unpack('>qQ', exact(sock, 16))
                    if amp != 0 or length % 1264 or not 0 < length <= 1264 * 65536:
                        raise ValueError(f'Invalid frame header: {amp}, {length}')
                    payload = exact(sock, length)
                    output.write(payload)
                    meta['samples'] += length // 1264
        except (EOFError, OSError, ValueError) as exc:
            if not stop.is_set():
                meta['errors'].append(f'data reader: {exc!r}')

    def notifications(sock):
        buffer = b''
        try:
            while not stop.is_set():
                try:
                    chunk = sock.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                buffer += chunk
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    text = line.decode(errors='replace')
                    meta['notifications'].append(dict(monotonic=time.monotonic(), text=text))
                    if 'ntn_AmpPowerOn' in text:
                        power_on.set()
        except OSError:
            pass

    cmd = None
    identified = False
    try:
        cmd = connect(9877)
        cmd.settimeout(12)
        cmd_file = cmd.makefile('rb')
        notify, data = connect(9878), connect(9879)
        for sock, target in ((notify, notifications), (data, data_reader)):
            sock.settimeout(1)
            worker = threading.Thread(target=target, args=(sock,), daemon=True)
            workers.append(worker)
            worker.start()
        time.sleep(.2)

        def send(name, value=0, channel=0):
            entry = dict(command=name, value=value, channel=channel,
                         monotonic_before=time.monotonic(), sample_before=meta['samples'])
            cmd.sendall(f'(sendCommand {name} 0 {channel} {value})\n'.encode())
            reply = cmd_file.readline().decode(errors='replace').strip()
            entry.update(reply=reply, monotonic_after=time.monotonic(), sample_after=meta['samples'])
            meta['commands'].append(entry)
            command_file.write(json.dumps(entry) + '\n')
            command_file.flush()
            if '(status complete)' not in reply:
                raise RuntimeError(f'{name}: {reply!r}')
            return reply

        details = send('cmd_GetAmpDetails')
        meta['amplifier_details'] = details
        print(details, flush=True)
        if '(amp_type NA500)' not in details or '(packet_format 2)' not in details:
            raise RuntimeError('This capture requires an NA500 with packet format 2')
        identified = True
        send('cmd_Stop')
        send('cmd_SetPower', 0)
        send('cmd_ReceiveNotifications')
        send('cmd_SetAmpTime', time.time_ns() // 1000)
        send('cmd_SetPower', 1)
        print('Waiting for power-on notification...', flush=True)
        if not power_on.wait(35):
            raise RuntimeError('No amplifier power-on confirmation')
        time.sleep(.3)
        send('cmd_SetNativeRate', 4000)
        send('cmd_DefaultAcquisitionState')
        send('cmd_SetBufferedReference', 1)
        send('cmd_SetDinEnable', 1)
        send('cmd_SetBlankingPulseWidth', args.width)
        send('cmd_SetBncInMode', 1)
        send('cmd_SetBncOutMode', 1)
        send('cmd_SetBncOutPulseWidth', 1000)
        send('cmd_Start')
        time.sleep(2)
        if meta['samples'] < 1000:
            raise RuntimeError('No usable stream after start')
        for number, mode in enumerate(meta['sequence'], 1):
            send('cmd_SetBlankingPulseWidth', args.width)
            send('cmd_SetBncInMode', mode)
            time.sleep(.75)
            block = dict(number=number, mode=mode, start_sample=meta['samples'], pulses=[])
            meta['blocks'].append(block)
            print(f'Block {number}/4: mode {mode}, {args.pulses} pulses', flush=True)
            time.sleep(.5)
            for pulse in range(args.pulses):
                start = time.monotonic()
                send('cmd_SetTriggerBncManualOut')
                block['pulses'].append(len(meta['commands']) - 1)
                time.sleep(max(0, args.interval - (time.monotonic() - start)))
                if meta['errors']:
                    raise RuntimeError(meta['errors'])
            time.sleep(.5)
            block['end_sample'] = meta['samples']
            save()
        meta['complete'] = True
    except BaseException as exc:
        meta['errors'].append(repr(exc))
        raise
    finally:
        if cmd is not None and identified:
            for name, value in (('cmd_SetBlankingPulseWidth', args.width), ('cmd_SetBncInMode', 2),
                                ('cmd_Stop', 0), ('cmd_SetPower', 0)):
                try:
                    send(name, value)
                except Exception as exc:
                    meta['errors'].append(f'cleanup {name}: {exc!r}')
        stop.set()
        for sock in sockets:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        for worker in workers:
            worker.join(3)
        save()
        command_file.close()
        print(f'Saved {meta["samples"]} raw packets to {args.out}', flush=True)


if __name__ == '__main__':
    main()
