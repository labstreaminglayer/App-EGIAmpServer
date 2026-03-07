# LSL EGI AmpServer Application

## Usage

This program should work with any amplifier that works with the AmpServer produced by EGI (http://www.egi.com/).
All communication with the amplifier happens through the Amp Server's low-level Network Protocol. Amp Server Pro SDK is [documented here](https://www.egi.com/images/stories/manuals/amp-server-pro-sdk-3-0-user-guide-rev-01.pdf).
 and the [network API here](https://www.egi.com/images/stories/manuals/amp-server-pro-sdk-3-0-network-apis-user-guide-rev-01.pdf).

  * Make sure that your AmpServer is running and can correctly record from its connected amplifier(s).

  * Start the EGIAmpServer app. You should see a window like the following.
> > ![egiampserver.png](egiampserver.png)

  * Make sure that you have the correct IP address of the AmpServer assigned. The ports correspond to the default settings of the server and should not require a change.

  * If you have multiple amplifiers connected to the AmpServer and you would like to record from a specific one, you need to set the correct amplifier ID (these should be increasing from zero). Also make sure that you are using a supported number of channels and a supported sampling rate (the defaults should work).

  * To link the application to the LSL, click the "Link" button. If all goes well you should now have a new stream on the network with name "EGI NetAmp k" (k corresponding to the index of the amplifier) and type "EEG". If you get an error you might try to manually power on the desired Amp and try to link while it is either recording or stopped.

  * For subsequent uses you can save the desired settings from the GUI via File / Save Configuration. If the app is frequently used with different settings you might make a shortcut on the desktop that points to the app and appends to the Target field of the shortcut the snippet `-c name_of_config.cfg` to denote the name of the config file that should be loaded at startup.

## Command-Line Interface

The CLI provides a lightweight alternative to the GUI:

```bash
./EGIAmpServerCLI [options]
```

### Options
- `--config <file>` - Load configuration from file
- `--address <addr>` - AmpServer IP address (default: 10.10.10.51)
- `--cmd-port <port>` - Command port (default: 9877)
- `--data-port <port>` - Data port (default: 9879)
- `--amp-id <id>` - Amplifier ID (default: 0)
- `--sample-rate <hz>` - Sample rate in Hz (default: 1000). Forces amplifier to this rate if already running at a different rate. Valid rates: 250, 500, 1000 (decimated) or 500, 1000, 2000, 4000, 8000 (native).
- `--fast-recovery` - Use native rate mode (no FPGA anti-alias filter) for lower latency. See [Sample Rate Modes](#sample-rate-modes).
- `--align-timestamps` - Adjust timestamps to compensate for anti-alias filter delay. See [Timestamp Alignment](#timestamp-alignment).
- `--impedance` - Enable impedance testing mode
- `--native-format` - Transmit raw int32 ADC counts instead of float microvolts
- `--shutdown` - Shutdown the Amp Server (terminates all connections)
- `--help` - Show help message

### Example
```bash
# Basic usage
./EGIAmpServerCLI --address 10.10.10.51

# With impedance testing
./EGIAmpServerCLI --address 10.10.10.51 --impedance

# With native format (int32 ADC counts)
./EGIAmpServerCLI --address 10.10.10.51 --native-format

# Low-latency mode (fast recovery)
./EGIAmpServerCLI --address 10.10.10.51 --sample-rate 1000 --fast-recovery

# With timestamp alignment for filter delay
./EGIAmpServerCLI --address 10.10.10.51 --sample-rate 1000 --align-timestamps
```

## Connecting to an Already-Running Amplifier

When another application (e.g., Net Station Acquisition) has already started the amplifier, the CLI's behavior depends on whether you pass any rate or mode flags.

### Default: Non-Destructive Attach

With no rate flags, the CLI detects the running amplifier, measures its current sample rate from the data stream, and attaches without disrupting the existing session:

```bash
# Attaches to whatever rate/mode the amp is already using
./EGIAmpServerCLI --address 10.10.10.51
```

This is safe to use alongside Net Station — it will not stop or reconfigure the amplifier.

### Forcing a Rate or Mode

The following flags cause the CLI to stop, reconfigure, and restart the amplifier — even if it was started by another application:

- `--sample-rate <hz>` — reinitializes if the detected rate differs from the requested rate
- `--fast-recovery` — reinitializes to ensure native (unfiltered) mode
- `--align-timestamps` — reinitializes at 500/1000 Hz to ensure decimated (filtered) mode, since the operating mode cannot be distinguished from the data stream alone

**Warning**: Reinitialization will interrupt any active Net Station recording. If you need to coexist with Net Station, omit these flags and let the CLI match the existing configuration.

### Recovery During Streaming

If the amplifier is stopped or restarted externally while the CLI is streaming:

- **Amp powered off by another app** (e.g., Net Station clicks "Off"): The CLI detects stream loss and attempts to reinitialize the amplifier with its original settings.
- **Amp restarted by another app** (e.g., Net Station reconnects): The CLI detects the new stream, adapts to whatever rate the other app configured, and recreates its LSL outlets if the rate changed.
- **Amp power-cycled or shutdown**: The CLI waits up to 120 seconds for recovery before giving up.

## Sample Rate Modes

The NA400/NA410 amplifiers support two operating modes that affect anti-aliasing and latency:

### Decimated Mode (Default)

Uses the FPGA's digital anti-aliasing filter to downsample from the ADC's native rate. This provides:
- Better frequency response (~400 Hz bandwidth at 1000 Hz sample rate)
- Higher latency due to filter delay (36-112 samples depending on rate)

Available decimated rates: 250, 500, 1000 Hz

### Native Mode (Fast Recovery)

Bypasses the FPGA filter and samples directly at the requested rate. This provides:
- Lower latency (~3 samples)
- Reduced bandwidth (~1/4 of sample rate, e.g., 250 Hz at 1000 Hz sample rate)
- Optimized for EEG-TMS and real-time BCI applications

Available native rates: 500, 1000, 2000, 4000, 8000 Hz

Use `--fast-recovery` to enable native mode for rates that support both modes (500, 1000 Hz).

### Filter Delay by Sample Rate

| Mode | Sample Rate | Filter Delay (samples) | Filter Delay (ms) |
|------|-------------|------------------------|-------------------|
| Decimated | 250 Hz | 112 | 448 ms |
| Decimated | 500 Hz | 66 | 132 ms |
| Decimated | 1000 Hz | 36 | 36 ms |
| Native | 500-8000 Hz | ~3 | ~3 ms |

## Timestamp Alignment

When using decimated mode, the FPGA anti-aliasing filter introduces a delay between when brain activity occurs and when it appears in the data stream. The `--align-timestamps` option compensates for this by adjusting LSL timestamps backward by the filter delay amount.

### When to Use

- **ERP analysis**: Enable `--align-timestamps` to align EEG data with event markers
- **Real-time BCI**: Use `--fast-recovery` instead (no filter delay to compensate)
- **Raw recording**: Disable alignment if you prefer unmodified timestamps

### Limitations

**Important**: Timestamp alignment only works correctly when this application initializes the amplifier. If Net Station or another application previously initialized the amplifier, the current operating mode (decimated vs native) cannot be queried from AmpServer. In this case:

1. The application will reinitialize the amplifier to ensure the correct mode
2. This will interrupt any existing Net Station recording
3. To avoid this, start EGIAmpServer before Net Station, or restart the amplifier

If you need to join an existing Net Station session without reinitialization, do not use `--align-timestamps` unless you are certain of the current mode.

## Impedance Testing

The application supports electrode impedance measurement using the same algorithm as Net Station Acquisition.

### Overview

When impedance mode is enabled:
- The amplifier is configured for impedance measurement (20 Hz calibration signal)
- **Two LSL streams are created**:
  1. **EEG Stream** (`type: EEG`) - Raw amplifier data containing the calibration signal
  2. **Impedance Stream** (`type: Impedance`) - Calculated impedance values in kilo-ohms
- The application cycles through each channel, measuring impedance one at a time
- A complete scan of all channels takes approximately 1-5 minutes depending on channel count

### How It Works

The impedance measurement follows Net Station's algorithm:

1. **Initial State**: All channels are "driving" the 20 Hz calibration signal
   - Drive signals ON, 10K resistors OFF for all channels
   - Oscillator enabled at 20 Hz sine wave, maximum amplitude (4095)

2. **Per-Channel Measurement**: For each channel:
   - Turn OFF the drive signal (isolate channel from calibration)
   - Turn ON the 10K reference resistor
   - Wait ~1 second for signal to settle
   - Measure peak-to-peak amplitude from collected samples
   - Calculate impedance using: `Z = (idealSignal - amplitude) / (amplitude / 10)`
   - Reset channel back to driving state

3. **Output**: After measuring all channels, the impedance values (in kOhms) are pushed to the LSL impedance stream

### Enabling Impedance Mode

#### Via CLI
```bash
./EGIAmpServerCLI --address 10.10.10.51 --impedance
```

#### Via Configuration File
Add to your `ampserver_config.cfg`:
```xml
<settings>
  <impedance>true</impedance>
  <!-- other settings -->
</settings>
```

### LSL Streams Created

#### 1. EEG Stream
- **Name**: `EGI NetAmp <amp_id>`
- **Type**: `EEG`
- **Rate**: Configured sample rate (e.g., 1000 Hz)
- **Channels**: Depends on sensor net (32-256 channels)
- **Format**: `float32` (default) or `int32` (with `--native-format`)
- **Unit**: `microvolts` (default) or `counts` (with `--native-format`)
- **Behavior**: Streams continuously with raw amplifier data (contains 20 Hz calibration signal during impedance mode)

##### Native Format Mode
When `--native-format` is enabled:
- Data is transmitted as raw int32 ADC counts instead of float microvolts
- Channel metadata includes a `conversion` field with the scaling factor
- Downstream consumers can convert to microvolts: `microvolts = counts × conversion`
- This mode is useful for applications that need maximum precision or want to handle scaling themselves (e.g., NWB export)

#### 2. Impedance Stream
- **Name**: `EGI NetAmp <amp_id> Impedance`
- **Type**: `Impedance`
- **Rate**: 1 Hz (regular rate)
- **Channels**: Same count and labels as EEG stream
- **Unit**: `kohms` (kilo-ohms)
- **Behavior**: Publishes current known impedance values every second
  - Initially, all channels show 1000 kOhms (not yet measured)
  - As each channel is measured, its value updates
  - Values persist until the next measurement of that channel

### Understanding the Data

Each sample in the impedance stream contains one value per channel:
- **Good electrodes**: Typically 5-50 kOhms
- **Acceptable electrodes**: 50-100 kOhms
- **Poor electrodes**: 100-200 kOhms
- **Bad/disconnected electrodes**: 1000 kOhms (maximum/clipped value)

### Hardware Commands Sent

#### Initial Impedance State Setup
```
cmd_TurnAll10KOhms(0)                    - 10K resistors OFF (channels driving)
cmd_TurnAllDriveSignals(1)               - Drive signals ON (all channels active)
cmd_SetSubjectGround(0)                  - Subject ground OFF
cmd_SetCurrentSource(0)                  - Current source OFF
cmd_SetCalibrationSignalFreq(20)         - 20 Hz calibration signal
cmd_SetWaveShape(0)                      - Sine wave
cmd_SetBufferedReference(0)              - Buffered reference OFF
cmd_SetOscillatorGate(1)                 - Oscillator ON
cmd_SetReference10KOhms(0)               - Reference 10K OFF
cmd_SetReferenceDriveSignal(0)           - Reference drive signal OFF
cmd_SetDrivenCommon(0)                   - Driven leg OFF
cmd_SetCalibrationSignalAmplitude(4095)  - Maximum amplitude
```

#### Per-Channel Measurement
```
# To measure channel N:
cmd_TurnChannelDriveSignals(N, 0)  - Turn OFF drive signal
cmd_TurnChannel10KOhms(N, 1)       - Turn ON 10K resistor

# After measurement:
cmd_TurnChannelDriveSignals(N, 1)  - Turn ON drive signal (reset)
cmd_TurnChannel10KOhms(N, 0)       - Turn OFF 10K resistor (reset)
```

### Limitations and Notes

- **Scan Duration**: A full 256-channel scan takes approximately 5 minutes in single-channel mode
- **No tiling sets yet**: The current implementation measures one channel at a time. Tiling set support (faster, ~1 minute for 256 channels) is planned for a future release
- **Ideal signal estimation**: The "ideal signal" (expected amplitude with 0 impedance) is estimated from the first measurement. For more accurate results, gains calibration should be performed first
- **Net Station compatibility**: Running impedance mode will interfere with Net Station Acquisition if it's connected to the same amplifier
- **Channel labels**: Both streams use identical channel labels (E1, E2, ..., En)

### Downstream Processing

Example Python code to process impedance data:
```python
import pylsl

# Resolve both streams
streams = pylsl.resolve_byprop('type', 'EEG')
impedance_streams = pylsl.resolve_byprop('type', 'Impedance')

# Create inlets
eeg_inlet = pylsl.StreamInlet(streams[0])
imp_inlet = pylsl.StreamInlet(impedance_streams[0])

# Pull impedance data (arrives at 1 Hz)
imp_sample, timestamp = imp_inlet.pull_sample(timeout=2.0)
if imp_sample:
    # Count how many channels have been measured
    measured = sum(1 for z in imp_sample if z < 1000)
    print(f"Measured: {measured}/{len(imp_sample)} channels")

    for ch, z in enumerate(imp_sample):
        if z >= 1000:
            status = "not measured"
        elif z < 50:
            status = "good"
        elif z < 100:
            status = "ok"
        elif z < 200:
            status = "poor"
        else:
            status = "bad"
        print(f"E{ch+1}: {z:.1f} kOhms ({status})")

# EEG data continues normally (contains calibration signal during impedance mode)
eeg_sample, timestamp = eeg_inlet.pull_sample()
```

## Digital Inputs (DIN)

The EEG stream includes a `DIN` channel as the last channel, containing the raw 16-bit digital input value from the amplifier's digital I/O port.

### Channel Details

- **Label**: `DIN`
- **Type**: `DIN`
- **Unit**: `uint16`
- **Position**: Last channel in the stream (after EEG and any Physio16 channels)
- **Value range**: 0-65535 (0x0000-0xFFFF)

### Accessing Individual Bits

The DIN value is transmitted as a float (or int32 in native format), but all 16-bit values are exactly preserved. To access individual bits:

```python
import pylsl

streams = pylsl.resolve_byprop('type', 'EEG', timeout=5)
inlet = pylsl.StreamInlet(streams[0])

sample, timestamp = inlet.pull_sample()
din_value = int(sample[-1])  # Last channel, convert to integer

# Extract individual bits (DIN1-DIN16)
din1 = (din_value >> 0) & 1   # Bit 0
din2 = (din_value >> 1) & 1   # Bit 1
din3 = (din_value >> 2) & 1   # Bit 2
# ... etc

# Or extract all 16 bits
bits = [(din_value >> i) & 1 for i in range(16)]
print(f"DIN1-16: {bits}")
```

### Timing Considerations

The amplifier's internal DIN ADC samples at a fixed 1 kHz rate, regardless of the EEG sample rate:

| EEG Sample Rate | DIN Behavior | Effective DIN Resolution |
|-----------------|--------------|--------------------------|
| 250 Hz          | Decimated (1 in 4 samples) | 4 ms |
| 500 Hz          | Decimated (1 in 2 samples) | 2 ms |
| 1000 Hz         | 1:1 mapping | 1 ms |
| 2000 Hz         | Duplicated (2 per DIN sample) | 1 ms |
| 4000 Hz         | Duplicated (4 per DIN sample) | 1 ms |

**Recommendation**: Use a sample rate of 1000 Hz or higher if precise DIN timing is required. At lower sample rates, fast TTL pulses (< 4ms at 250 Hz) may be missed.

### Hardware Notes

The NA400/NA410 amplifiers have a 16-bit digital I/O port. By default, all bits are configured for input. The `cmd_SetDigitalInOutDirection` command can configure specific bits for output if needed (consult EGI documentation).

# Acknowledgements
This application was written to behave near-identically to the BCI2000 AmpServer module that was originally created by EGI.

# Optional

The configuration settings can be saved to a .cfg file (see File / Save Configuration) and
subsequently loaded from such a file (via File / Load Configuration).

Importantly, the program can be started with a command-line argument of the form
`EGIAmpServer.exe myconfig.cfg`, which allows to load the config automatically at start-up.
The recommended procedure to use the app in production experiments is to make a shortcut on
the experimenter's desktop which points to a previously saved configuration customized to the
study being recorded to minimize the chance of operator error.

# Mock Server for development

To use the mock server for development:

**Terminal 1: Start mock server**

> python3 mock/mock_ampserver.py

**Terminal 2: Run CLI or GUI against localhost**
> ./cli/EGIAmpServerCLI --address 127.0.0.1
> # or update ampserver_config.cfg to use 127.0.0.1 and run GUI

The mock server generates synthetic sine waves (10-50 Hz) with noise for the EEG data, so you can also verify the LSL stream in downstream applications.

# Known Issues

## Net Station Acquisition Compatibility

See also [Connecting to an Already-Running Amplifier](#connecting-to-an-already-running-amplifier) for details on how the CLI behaves when attaching to an amp started by Net Station.

### Behavior When EGIAmpServer is Streaming First

The following table documents how EGIAmpServer behaves when it is already connected and streaming, and Net Station Acquisition (NAS) subsequently interacts with the amplifier:

| NAS Action | EGIAmpServer Behavior | Stream Status |
|------------|----------------------|---------------|
| NAS launches and scans for devices | No change detected | Continues |
| NAS clicks "On" (connect) | No change (amp already running) | Continues |
| NAS starts recording | No change detected | Continues |
| NAS stops recording | No change detected | Continues |
| NAS clicks "Off" (disconnect) | `ERROR: The stream was lost` - stream terminates | **Stopped** |
| NAS closes | No additional change | Already stopped |

**Key finding**: NAS powering off the amplifier terminates the EGIAmpServer data stream. The CLI process remains running but stops streaming data.

### Amplifier Shutdown (Severe)

NAS has an "Amplifier Shutdown" button that is more severe than simply clicking "Off". When triggered while EGIAmpServer is streaming:

| Test | Result |
|------|--------|
| EGIAmpServer stream | `ERROR: The stream was lost` - terminates |
| Ping amplifier (10.10.10.51) | **FAILED** - 100% packet loss |
| TCP connect to AmpServer | Succeeded (AmpServer software still running) |
| Query amplifier details | **FAILED** - `Failed to get amplifier details` |
| Start new stream | **FAILED** |

**Recovery**: The amplifier requires a power cycle to recover from this state. The AmpServer software remains accessible but cannot communicate with the shutdown amplifier hardware.

### Recommended Workflow

When using this application alongside Net Station Acquisition:

- **Start Net Station first**: If you plan to use Net Station Acquisition, start it and initialize the amplifier (click "On") BEFORE connecting this app. Our app will detect the running amplifier and automatically use its sample rate.

- **Do not start Net Station after**: If this app is already streaming and Net Station subsequently initializes the amplifier at a different sample rate, our app cannot detect this change. AmpServer only sends notifications to one subscriber, and Net Station consumes them when it's running.

- **Recommended workflow**:
  1. Start Net Station Acquisition
  2. Initialize the amplifier at your desired sample rate (click "On")
  3. Start EGIAmpServer (GUI: click "Link", or CLI: run with no rate flags) — it will detect the running amp and match its sample rate
  4. Both applications will now receive data at the correct rate

## Dropped Packets After Device Shutdown

After Net Station shuts down the amplifier (via "Shutdown" command), immediately starting this app may result in excessive dropped packets and eventual stream loss. This appears to be related to stale data in the connection. **Workaround**: Wait a moment and restart the app, or power cycle the amplifier.

## Sample Rate Auto-Detection

The app automatically detects the sample rate when connecting to an already-running amplifier by measuring packet timing. This detection snaps to standard rates (250, 500, or 1000 Hz). If the amplifier is idle when connecting, the app uses the sample rate configured in the UI/config file.
