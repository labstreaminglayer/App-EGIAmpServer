#!/usr/bin/env python3
"""
Impedance Viewer - Real-time visualization of EGI electrode impedances

Connects to the EGI AmpServer impedance LSL stream and displays
impedance values on a 2D head map with color coding.

Requirements:
    pip install pylsl numpy matplotlib

Usage:
    python impedance_viewer.py [--threshold 50] [--stream-name "EGI NetAmp"]
"""

import argparse
import sys
import time
from collections import defaultdict

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Wedge
from matplotlib.collections import PatchCollection
import matplotlib.colors as mcolors

try:
    from pylsl import StreamInlet, resolve_byprop, resolve_streams
except ImportError:
    print("Error: pylsl not installed. Run: pip install pylsl")
    sys.exit(1)


def project_3d_to_2d(x, y, z):
    """
    Project 3D electrode coordinates to 2D using azimuthal equidistant projection.
    This preserves distances from the center (Cz) and is standard for EEG topomaps.

    Input: 3D coordinates on unit sphere (X=right, Y=front, Z=up)
    Output: 2D coordinates for plotting (x=right, y=front)
    """
    # Handle the vertex (Cz) case
    if abs(z - 1.0) < 1e-6:
        return 0.0, 0.0

    # Calculate the angle from vertex (theta) and azimuth (phi)
    # theta = angle from Z axis (0 at top, pi at bottom)
    theta = np.arccos(np.clip(z, -1.0, 1.0))

    # phi = azimuthal angle in XY plane
    phi = np.arctan2(x, y)  # Note: atan2(x,y) so 0 is front (nose)

    # Azimuthal equidistant projection: r proportional to theta
    # Scale so that equator (theta=pi/2) maps to r=1
    r = theta / (np.pi / 2)

    # Convert to Cartesian 2D
    x_2d = r * np.sin(phi)
    y_2d = r * np.cos(phi)

    return x_2d, y_2d


def parse_channel_info(inlet):
    """Parse channel labels and positions from LSL stream metadata."""
    info = inlet.info()
    n_channels = info.channel_count()

    labels = []
    positions = {}

    channels = info.desc().child("channels")
    if not channels.empty():
        ch = channels.child("channel")
        while not ch.empty():
            label = ch.child_value("label")
            if label:
                labels.append(label)

                # Try to get 3D location from metadata
                loc = ch.child("location")
                if not loc.empty():
                    try:
                        x = float(loc.child_value("X"))
                        y = float(loc.child_value("Y"))
                        z = float(loc.child_value("Z"))
                        # Project 3D to 2D
                        x_2d, y_2d = project_3d_to_2d(x, y, z)
                        positions[label] = (x_2d, y_2d)
                    except (ValueError, TypeError):
                        pass  # Skip if coordinates are invalid

            ch = ch.next_sibling("channel")

    # If no labels found, generate default E1, E2, ...
    if not labels:
        labels = [f"E{i+1}" for i in range(n_channels)]

    # If no positions found in metadata, generate fallback positions
    if not positions:
        print("No electrode positions found in stream metadata, using fallback layout.")
        positions = _generate_fallback_positions(n_channels, labels)
    else:
        print(f"Loaded {len(positions)} electrode positions from stream metadata.")

    return labels, positions


def _generate_fallback_positions(n_channels, labels):
    """Generate fallback positions using Fibonacci spiral when metadata unavailable."""
    positions = {}

    golden_angle = np.pi * (3 - np.sqrt(5))

    for i, label in enumerate(labels):
        # Fermat spiral for even distribution
        r = 0.9 * np.sqrt(i / max(n_channels - 1, 1))
        theta = i * golden_angle
        x = r * np.cos(theta)
        y = r * np.sin(theta)
        positions[label] = (x, y)

    # Ensure Cz is at center if present
    if "Cz" in positions:
        positions["Cz"] = (0, 0)

    return positions


def find_impedance_stream(stream_name=None, timeout=10):
    """Find and connect to an impedance LSL stream."""
    print("Looking for impedance stream...")

    if stream_name:
        # Try exact name match first
        streams = resolve_byprop("name", stream_name, timeout=timeout)
        if not streams:
            # Try partial match by looking at all streams
            all_streams = resolve_streams(wait_time=timeout)
            streams = [s for s in all_streams if stream_name.lower() in s.name().lower()]
    else:
        # Look for any Impedance type stream
        streams = resolve_byprop("type", "Impedance", timeout=timeout)

    if not streams:
        print(f"No impedance stream found after {timeout} seconds.")
        print("\nAvailable streams:")
        all_streams = resolve_streams(wait_time=2.0)
        for s in all_streams:
            print(f"  - {s.name()} (type: {s.type()}, channels: {s.channel_count()})")
        return None

    # Use the first matching stream
    stream_info = streams[0]
    print(f"Found stream: {stream_info.name()}")
    print(f"  Type: {stream_info.type()}")
    print(f"  Channels: {stream_info.channel_count()}")
    print(f"  Sample rate: {stream_info.nominal_srate()} Hz")

    inlet = StreamInlet(stream_info)
    return inlet


def create_head_plot(positions, threshold=50):
    """Create the matplotlib figure with head outline."""
    fig, ax = plt.subplots(figsize=(10, 10))
    ax.set_aspect('equal')
    ax.set_xlim(-1.3, 1.3)
    ax.set_ylim(-1.3, 1.3)
    ax.axis('off')

    # Draw head outline
    head = Circle((0, 0), 1.0, fill=False, linewidth=2, color='black')
    ax.add_patch(head)

    # Draw nose
    nose_x = [0.1, 0, -0.1]
    nose_y = [1.0, 1.15, 1.0]
    ax.plot(nose_x, nose_y, 'k-', linewidth=2)

    # Draw ears
    ear_left = Wedge((-1.0, 0), 0.1, 90, 270, fill=False, linewidth=2, color='black')
    ear_right = Wedge((1.0, 0), 0.1, 270, 90, fill=False, linewidth=2, color='black')
    ax.add_patch(ear_left)
    ax.add_patch(ear_right)

    # Create colormap for impedance values
    # Good (green) -> Warning (yellow) -> Bad (red)
    cmap = plt.cm.RdYlGn_r  # Reversed: green=low, red=high
    norm = mcolors.Normalize(vmin=0, vmax=threshold * 2)

    # Create scatter plot for electrodes
    x_coords = []
    y_coords = []
    labels = []

    for label, (x, y) in positions.items():
        x_coords.append(x)
        y_coords.append(y)
        labels.append(label)

    # Initial plot with max impedance (will be updated)
    scatter = ax.scatter(x_coords, y_coords, c=[1000] * len(x_coords),
                        cmap=cmap, norm=norm, s=100, edgecolors='black', linewidth=0.5)

    # Add colorbar
    cbar = plt.colorbar(scatter, ax=ax, shrink=0.6, pad=0.02)
    cbar.set_label('Impedance (kΩ)', fontsize=12)

    # Add threshold line to colorbar
    cbar.ax.axhline(y=threshold, color='black', linestyle='--', linewidth=1)
    cbar.ax.text(1.5, threshold, f'{threshold} kΩ', va='center', fontsize=9)

    # Title
    title = ax.set_title('Electrode Impedances\nWaiting for data...', fontsize=14)

    # Stats text
    stats_text = ax.text(0.02, 0.02, '', transform=ax.transAxes, fontsize=10,
                        verticalalignment='bottom', family='monospace',
                        bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

    plt.tight_layout()

    return fig, ax, scatter, title, stats_text, labels


def update_plot(scatter, title, stats_text, impedances, labels, threshold):
    """Update the plot with new impedance values."""
    # Map impedances to label order
    colors = []
    for label in labels:
        if label in impedances:
            colors.append(impedances[label])
        else:
            colors.append(1000)  # Max value for missing channels

    scatter.set_array(np.array(colors))

    # Calculate statistics
    valid_impedances = [v for v in impedances.values() if v < 1000]
    if valid_impedances:
        mean_z = np.mean(valid_impedances)
        max_z = np.max(valid_impedances)
        min_z = np.min(valid_impedances)
        good_count = sum(1 for z in valid_impedances if z <= threshold)
        bad_count = len(valid_impedances) - good_count

        stats = (f"Mean: {mean_z:.1f} kΩ  |  "
                f"Range: {min_z:.1f} - {max_z:.1f} kΩ\n"
                f"Good (≤{threshold}): {good_count}  |  "
                f"Bad (>{threshold}): {bad_count}")
        stats_text.set_text(stats)

        title.set_text(f'Electrode Impedances\n{len(valid_impedances)} channels measured')
    else:
        stats_text.set_text('No valid measurements yet')
        title.set_text('Electrode Impedances\nWaiting for data...')


def main():
    parser = argparse.ArgumentParser(description='Real-time impedance visualization')
    parser.add_argument('--threshold', type=float, default=50,
                       help='Impedance threshold in kOhms (default: 50)')
    parser.add_argument('--stream-name', type=str, default=None,
                       help='LSL stream name to connect to')
    parser.add_argument('--timeout', type=float, default=10,
                       help='Stream discovery timeout in seconds (default: 10)')
    args = parser.parse_args()

    # Find and connect to stream
    inlet = find_impedance_stream(args.stream_name, args.timeout)
    if inlet is None:
        sys.exit(1)

    # Get channel info and positions from stream metadata
    labels, positions = parse_channel_info(inlet)
    n_channels = len(labels)
    print(f"Channel count: {n_channels}")
    print(f"Channel labels: {labels[:5]}...{labels[-3:] if n_channels > 8 else ''}")

    # Use positions from metadata (already filtered to stream channels)
    stream_positions = positions

    # Create plot
    fig, ax, scatter, title, stats_text, plot_labels = create_head_plot(
        stream_positions, args.threshold)

    # Enable interactive mode
    plt.ion()
    plt.show()

    print("\nReceiving impedance data... Press Ctrl+C to stop.")

    # Current impedance values
    impedances = {}

    try:
        while plt.fignum_exists(fig.number):
            # Pull sample (non-blocking with short timeout)
            sample, timestamp = inlet.pull_sample(timeout=0.1)

            if sample is not None:
                # Update impedance values
                for i, value in enumerate(sample):
                    if i < len(labels):
                        impedances[labels[i]] = value

                # Update plot
                update_plot(scatter, title, stats_text, impedances,
                           plot_labels, args.threshold)

            # Update display
            fig.canvas.draw_idle()
            fig.canvas.flush_events()

            # Small delay to prevent CPU spinning
            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\nStopped by user.")
    finally:
        plt.ioff()
        plt.close(fig)


if __name__ == "__main__":
    main()
