#!/usr/bin/env python3
"""Analyze the raw capture from bnc_blanking_capture.py; retain event epochs."""

import argparse
import json
import os
import pathlib

os.environ.setdefault('MPLCONFIGDIR', '/private/tmp/egi-bnc-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

PACKET = np.dtype(dict(names=['counter', 'timestamp', 'flags', 'eeg'],
                      formats=['<u8', '<u8', 'u1', ('<i4', (256,))],
                      offsets=[25, 33, 42, 80], itemsize=1264))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=pathlib.Path)
    args = ap.parse_args()
    folder = args.capture
    meta = json.loads((folder / 'capture.json').read_text())
    packets = np.memmap(folder / 'packets.bin', dtype=PACKET, mode='r')
    high = (packets['flags'] & 4) != 0
    edges = np.flatnonzero(high & ~np.r_[False, high[:-1]])
    pre, post, rate = 80, 120, meta['rate']
    offsets = np.arange(-pre, post + 1)
    time_ms = offsets * 1000 / rate
    epochs, modes, blocks, positions = [], [], [], []
    rows = []
    for block in meta['blocks']:
        if 'end_sample' not in block:
            continue
        start, end = block['start_sample'], block['end_sample']
        found = edges[(edges >= start + pre) & (edges < end - post)]
        rows.append(dict(block=block['number'], mode=block['mode'],
                         sent=len(block['pulses']), events=len(found), samples=end-start))
        for edge in found:
            idx = edge + offsets
            # Exclude incomplete epochs rather than interpreting gaps as gating.
            if not np.all(np.diff(packets['counter'][idx].astype(np.int64)) == 1):
                continue
            epochs.append(packets['eeg'][idx].copy())
            modes.append(block['mode'])
            blocks.append(block['number'])
            positions.append(int(edge))
    if not epochs:
        raise RuntimeError(f'No usable BNC event epochs: {rows}')
    epochs = np.asarray(epochs)
    modes, blocks = np.asarray(modes), np.asarray(blocks)
    baseline = (time_ms >= -15) & (time_ms <= -3)
    rail = np.abs(epochs.astype(np.float64)) >= 8388000
    spread = np.median(np.std(epochs[:, baseline, :], axis=1), axis=0)
    rail_fraction = rail.mean(axis=(0, 1))
    useful = (spread > 1) & (rail_fraction < .1)
    selected = np.flatnonzero(useful)
    if not selected.size:
        selected = np.arange(256)
    channel = int(selected[np.argmax(spread[selected])])
    x = epochs[:, :, selected].astype(np.float64)
    dx = np.diff(x, axis=1)
    dt = time_ms[1:]
    baseline_d = (dt >= -15) & (dt <= -3)
    gate_d = (dt >= .5) & (dt <= 2)
    baseline_level = np.median(np.abs(dx[:, baseline_d, :]), axis=(1, 2))
    baseline_level = np.maximum(baseline_level, 1)
    normalized = np.median(np.abs(dx), axis=2) / baseline_level[:, None]
    zero = np.mean(dx == 0, axis=2)
    summary = dict(amplifier_details=meta.get('amplifier_details'),
                   complete=meta.get('complete', False), capture_errors=meta['errors'],
                   sample_count=len(packets), bytes=(folder/'packets.bin').stat().st_size,
                   requested_rate_hz=rate, blocks=rows, usable_epochs=len(epochs),
                   useful_channels=int(useful.sum()), plotted_channel=channel+1,
                   overall_epoch_rail_fraction=float(rail.mean()), modes={})
    for mode in (1, 2):
        keep = modes == mode
        gate_per_event = np.mean(normalized[keep][:, gate_d], axis=1)
        info = dict(events=int(keep.sum()),
                    median_baseline_abs_increment_counts=float(np.median(baseline_level[keep])),
                    gate_0_5_to_2ms_normalized_increment_median=float(np.median(gate_per_event)),
                    gate_0_5_to_2ms_normalized_increment_q25_q75=np.percentile(gate_per_event,[25,75]).tolist(),
                    gate_exact_repeat_fraction=float(zero[keep][:,gate_d].mean()),
                    baseline_exact_repeat_fraction=float(zero[keep][:,baseline_d].mean()))
        summary['modes'][str(mode)] = info
    for row in rows:
        keep = blocks == row['block']
        row['usable_epochs'] = int(keep.sum())
        if keep.any():
            row['gate_normalized_increment'] = float(np.median(np.mean(normalized[keep][:,gate_d],axis=1)))
        start = next(b['start_sample'] for b in meta['blocks'] if b['number']==row['block'])
        end = start + row['samples']
        counter_diff = np.diff(packets['counter'][start:end].astype(np.int64))
        timestamp_diff = np.diff(packets['timestamp'][start:end].astype(np.int64))
        row['counter_discontinuities'] = int((counter_diff != 1).sum())
        row['median_timestamp_step_us'] = float(np.median(timestamp_diff))
        row['timestamp_discontinuities'] = int((timestamp_diff != 1000000 // rate).sum())
    np.savez_compressed(folder/'event_epochs.npz', eeg_counts=epochs, mode=modes,
                        block=blocks, sample_index=positions, time_ms=time_ms,
                        useful_channels=useful)
    (folder/'summary.json').write_text(json.dumps(summary, indent=2))
    fig, axes = plt.subplots(4, 1, figsize=(11, 12), constrained_layout=True)
    scale = 750000 / 2**24
    colors = {1:'#3274a1',2:'#d65035'}
    for mode in (1, 2):
        keep = modes == mode
        label=f'Mode {mode} (n={keep.sum()})'
        centered = epochs[keep,:,channel].astype(float)
        centered -= centered[:,baseline].mean(axis=1)[:,None]
        axes[0].plot(time_ms, centered.mean(axis=0)*scale, label=label, color=colors[mode])
        axes[1].plot(dt,np.median(normalized[keep],axis=0),label=label,color=colors[mode])
        axes[2].plot(dt,zero[keep].mean(axis=0),label=label,color=colors[mode])
        axes[3].plot(time_ms,centered[0]*scale,label=f'Mode {mode}: first event',color=colors[mode])
    axes[0].set_ylabel(f'Ch {channel+1} mean EEG (µV)\nbaseline subtracted')
    axes[1].set_ylabel('Median |sample increment|\nrelative to pre-pulse baseline')
    axes[2].set_ylabel('Fraction of channels\nwith exact repeated samples')
    axes[3].set_ylabel(f'Ch {channel+1} single event (µV)')
    axes[3].set_xlabel('Time from BNC input event (ms)')
    axes[0].set_title(f'NA500 BNC loopback: open-input EEG, {rate} Hz native, '
                      f'{meta["blanking_width_us"]} µs blanking\n'
                      'Mode sequence 1 → 2 → 2 → 1; nominal gate shown in grey')
    for ax in axes:
        ax.axvspan(0,meta['blanking_width_us']/1000,color='grey',alpha=.15)
        ax.axvline(0,color='black',lw=.6)
        ax.set_xlim(-8,15)
        ax.grid(alpha=.2)
        ax.legend(loc='upper right')
    fig.savefig(folder/'comparison.png',dpi=160)
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    main()
