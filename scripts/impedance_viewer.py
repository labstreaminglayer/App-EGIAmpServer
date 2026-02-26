#!/usr/bin/env python3
"""
Impedance Viewer - Real-time visualization of EGI electrode impedances

Connects to the EGI AmpServer impedance LSL stream and displays
impedance values on a 2D head map with color coding.

Requirements:
    pip install pylsl numpy matplotlib

Usage:
    python impedance_viewer.py [--stream-name "EGINetAmp_51"]
"""

import argparse
import sys
import time

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Wedge
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

    Input: 3D coordinates in mm (X=right, Y=front, Z=up)
    Output: 2D coordinates for plotting (x=right, y=front), normalized to head circle
    """
    # Normalize to unit sphere
    r3d = np.sqrt(x*x + y*y + z*z)
    if r3d < 1e-6:
        return 0.0, 0.0
    xn, yn, zn = x / r3d, y / r3d, z / r3d

    # Handle the vertex (Cz) case
    if abs(zn - 1.0) < 1e-6:
        return 0.0, 0.0

    # Calculate the angle from vertex (theta) and azimuth (phi)
    # theta = angle from Z axis (0 at top, pi at bottom)
    theta = np.arccos(np.clip(zn, -1.0, 1.0))

    # phi = azimuthal angle in XY plane
    phi = np.arctan2(xn, yn)  # Note: atan2(x,y) so 0 is front (nose)

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

                # Try to get 3D location from metadata (expected in mm)
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


def create_egi_colormap():
    """Create EGI-style impedance colormap with 4 discrete bands.

    Cyan:   0 - 50 kOhm   (good)
    Green:  50 - 100 kOhm  (acceptable)
    Yellow: 100 - 1000 kOhm (high)
    Red:    1000+ kOhm      (bad / no signal)
    """
    cmap = mcolors.ListedColormap(['cyan', 'green', 'gold', 'red'])
    boundaries = [0, 50, 100, 1000, 5000]
    norm = mcolors.BoundaryNorm(boundaries, cmap.N)
    return cmap, norm, boundaries


def create_head_plot(positions):
    """Create the matplotlib figure with head outline."""
    fig, ax = plt.subplots(figsize=(10, 10))
    ax.set_aspect('equal')

    # Compute bounds from electrode positions with padding
    if positions:
        all_x = [p[0] for p in positions.values()]
        all_y = [p[1] for p in positions.values()]
        max_r = max(np.sqrt(x**2 + y**2) for x, y in positions.values())
        lim = max(max_r, 1.0) + 0.2
    else:
        lim = 1.3
    ax.set_xlim(-lim, lim)
    ax.set_ylim(-lim, lim)
    ax.axis('off')

    # Draw head outline (equator circle at r=1)
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

    # EGI-style colormap
    cmap, norm, boundaries = create_egi_colormap()

    # Create scatter plot for electrodes
    x_coords = []
    y_coords = []
    labels = []

    for label, (x, y) in positions.items():
        x_coords.append(x)
        y_coords.append(y)
        labels.append(label)

    # Initial plot with max impedance (will be updated)
    scatter = ax.scatter(x_coords, y_coords, c=[5000] * len(x_coords),
                        cmap=cmap, norm=norm, s=100, edgecolors='black', linewidth=0.5,
                        zorder=5)

    # Add colorbar with band labels
    cbar = plt.colorbar(scatter, ax=ax, shrink=0.6, pad=0.02,
                        ticks=[25, 75, 550, 3000])
    cbar.set_label('Impedance (kΩ)', fontsize=12)
    cbar.ax.set_yticklabels(['0–50\n(Good)', '50–100', '100–1000', '1000+\n(Bad)'])

    # Title
    title = ax.set_title('Electrode Impedances\nWaiting for data...', fontsize=14)

    # Stats text
    stats_text = ax.text(0.02, 0.02, '', transform=ax.transAxes, fontsize=10,
                        verticalalignment='bottom', family='monospace',
                        bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

    # Hover annotation (initially hidden)
    annot = ax.annotate("", xy=(0, 0), xytext=(10, 10),
                        textcoords="offset points",
                        bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="black", alpha=0.9),
                        fontsize=10, zorder=10)
    annot.set_visible(False)

    plt.tight_layout()

    return fig, ax, scatter, title, stats_text, labels, annot


def update_plot(scatter, title, stats_text, impedances, labels):
    """Update the plot with new impedance values."""
    # Map impedances to label order
    colors = []
    for label in labels:
        if label in impedances:
            colors.append(impedances[label])
        else:
            colors.append(5000)  # Max value for missing channels

    scatter.set_array(np.array(colors))

    # Calculate statistics
    valid_impedances = [v for v in impedances.values() if v < 1000]
    if valid_impedances:
        mean_z = np.mean(valid_impedances)
        max_z = np.max(valid_impedances)
        min_z = np.min(valid_impedances)
        good_count = sum(1 for z in valid_impedances if z <= 50)
        ok_count = sum(1 for z in valid_impedances if 50 < z <= 100)
        bad_count = len(valid_impedances) - good_count - ok_count

        stats = (f"Mean: {mean_z:.1f} kΩ  |  "
                f"Range: {min_z:.1f} - {max_z:.1f} kΩ\n"
                f"Good (≤50): {good_count}  |  "
                f"OK (50–100): {ok_count}  |  "
                f"High (>100): {bad_count}")
        stats_text.set_text(stats)

        title.set_text(f'Electrode Impedances\n{len(valid_impedances)} channels measured')
    else:
        stats_text.set_text('No valid measurements yet')
        title.set_text('Electrode Impedances\nWaiting for data...')


def main():
    parser = argparse.ArgumentParser(description='Real-time impedance visualization')
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

    # Create plot
    fig, ax, scatter, title, stats_text, plot_labels, annot = create_head_plot(positions)

    # Current impedance values (shared with hover handler)
    impedances = {}

    # Hover handler for mouseover tooltips
    def on_hover(event):
        if event.inaxes != ax:
            if annot.get_visible():
                annot.set_visible(False)
                fig.canvas.draw_idle()
            return

        cont, ind = scatter.contains(event)
        if cont:
            # Get the closest point
            idx = ind["ind"][0]
            label = plot_labels[idx]
            pos = scatter.get_offsets()[idx]
            annot.xy = pos

            z_val = impedances.get(label)
            if z_val is not None:
                text = f"{label}: {z_val:.1f} kΩ"
            else:
                text = f"{label}: --"
            annot.set_text(text)
            annot.set_visible(True)
            fig.canvas.draw_idle()
        else:
            if annot.get_visible():
                annot.set_visible(False)
                fig.canvas.draw_idle()

    fig.canvas.mpl_connect("motion_notify_event", on_hover)

    # Enable interactive mode
    plt.ion()
    plt.show()

    print("\nReceiving impedance data... Press Ctrl+C to stop.")

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
                update_plot(scatter, title, stats_text, impedances, plot_labels)

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
