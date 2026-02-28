#!/usr/bin/env python3
"""
Analyze Physio16 data from LSL stream to verify scaling factors.

Expected input: 10 Hz sine wave, 50 uV amplitude on PIB1 channels 8 and 16.
"""

import numpy as np
from pylsl import StreamInlet, resolve_byprop
import time

def main():
    print("Looking for EGI stream...")
    streams = resolve_byprop('type', 'EEG')

    if not streams:
        print("No EEG streams found!")
        return

    inlet = StreamInlet(streams[0])
    info = inlet.info()

    n_channels = info.channel_count()
    srate = info.nominal_srate()

    print(f"Found stream: {info.name()}")
    print(f"  Channels: {n_channels}")
    print(f"  Sample rate: {srate} Hz")

    # Get channel labels
    ch = info.desc().child("channels").child("channel")
    labels = []
    for i in range(n_channels):
        labels.append(ch.child_value("label"))
        ch = ch.next_sibling()

    # Find PIB channel indices
    pib_indices = [i for i, label in enumerate(labels) if label.startswith("PIB")]
    print(f"  PIB channels: {len(pib_indices)} (indices {pib_indices[0]}-{pib_indices[-1]})")

    # PIB8 and PIB16 indices (within the PIB channels)
    pib8_idx = pib_indices[7]   # PIB8 (0-indexed: 7)
    pib16_idx = pib_indices[15]  # PIB16 (0-indexed: 15)

    print(f"  PIB8 at index {pib8_idx} (label: {labels[pib8_idx]})")
    print(f"  PIB16 at index {pib16_idx} (label: {labels[pib16_idx]})")

    # Collect 3 seconds of data
    print("\nCollecting 3 seconds of data...")
    duration = 3.0
    samples = []
    start_time = time.time()

    while time.time() - start_time < duration:
        sample, timestamp = inlet.pull_sample(timeout=1.0)
        if sample:
            samples.append(sample)

    if not samples:
        print("No samples received!")
        return

    data = np.array(samples)
    print(f"Collected {len(samples)} samples ({len(samples)/srate:.2f} seconds)")

    # Extract PIB8 and PIB16
    pib8_data = data[:, pib8_idx]
    pib16_data = data[:, pib16_idx]

    print(f"\n=== PIB8 (channel 8, should use scale -0.00111758708 per PDF) ===")
    print(f"  Mean: {np.mean(pib8_data):.4f} uV")
    print(f"  Std:  {np.std(pib8_data):.4f} uV")
    print(f"  Min:  {np.min(pib8_data):.4f} uV")
    print(f"  Max:  {np.max(pib8_data):.4f} uV")
    print(f"  Peak-to-peak: {np.max(pib8_data) - np.min(pib8_data):.4f} uV")
    print(f"  Estimated amplitude: {(np.max(pib8_data) - np.min(pib8_data))/2:.4f} uV")

    print(f"\n=== PIB16 (channel 16, should use scale +0.00111758708 per PDF) ===")
    print(f"  Mean: {np.mean(pib16_data):.4f} uV")
    print(f"  Std:  {np.std(pib16_data):.4f} uV")
    print(f"  Min:  {np.min(pib16_data):.4f} uV")
    print(f"  Max:  {np.max(pib16_data):.4f} uV")
    print(f"  Peak-to-peak: {np.max(pib16_data) - np.min(pib16_data):.4f} uV")
    print(f"  Estimated amplitude: {(np.max(pib16_data) - np.min(pib16_data))/2:.4f} uV")

    # Check correlation to see if they're inverted
    correlation = np.corrcoef(pib8_data, pib16_data)[0, 1]
    print(f"\n=== Correlation between PIB8 and PIB16 ===")
    print(f"  Correlation: {correlation:.4f}")
    if correlation > 0.9:
        print("  -> Signals are IN PHASE (same polarity)")
    elif correlation < -0.9:
        print("  -> Signals are INVERTED (opposite polarity)")
    else:
        print("  -> Signals have mixed/unclear relationship")

    # Calculate what scaling factor would give 50 uV amplitude
    # Current implementation uses EEG scaling factor
    # If actual amplitude != 50, we can back-calculate the correct factor
    expected_amplitude = 1000.0  # uV (1 mV) - adjust to match your signal generator

    measured_amp_pib8 = (np.max(pib8_data) - np.min(pib8_data)) / 2
    measured_amp_pib16 = (np.max(pib16_data) - np.min(pib16_data)) / 2

    print(f"\n=== Scaling Factor Analysis ===")
    print(f"  Expected amplitude: {expected_amplitude} uV")
    print(f"  Measured PIB8 amplitude: {measured_amp_pib8:.4f} uV")
    print(f"  Measured PIB16 amplitude: {measured_amp_pib16:.4f} uV")

    if measured_amp_pib8 > 0:
        ratio8 = expected_amplitude / measured_amp_pib8
        print(f"  PIB8 correction ratio: {ratio8:.4f}x")
    if measured_amp_pib16 > 0:
        ratio16 = expected_amplitude / measured_amp_pib16
        print(f"  PIB16 correction ratio: {ratio16:.4f}x")

    # Show raw ADC values if we can back-calculate
    # The current code does: value_uV = raw_adc * scaling_factor
    # So: raw_adc = value_uV / scaling_factor
    # Known scaling factors:
    print(f"\n=== Reference Scaling Factors ===")
    print(f"  NA400 EEG:     0.00015522042")
    print(f"  PDF PIB 1-8:  -0.00111758708")
    print(f"  PDF PIB 9-16:  0.00111758708")
    print(f"  JSON Physio:   0.28610229492188")

    # Ratio between PDF physio and NA400 EEG scaling
    pdf_to_eeg_ratio = 0.00111758708 / 0.00015522042
    json_to_eeg_ratio = 0.28610229492188 / 0.00015522042
    print(f"\n  PDF physio / NA400 EEG ratio: {pdf_to_eeg_ratio:.4f}")
    print(f"  JSON physio / NA400 EEG ratio: {json_to_eeg_ratio:.4f}")

if __name__ == "__main__":
    main()
