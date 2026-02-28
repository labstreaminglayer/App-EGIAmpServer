# Impedance Measurement

This document describes the impedance measurement implementation for EGI Net Amps amplifiers.

## Background

Electrode impedance measurement is essential for ensuring good signal quality in EEG recordings. High impedance electrodes result in noisy signals and increased susceptibility to interference. EGI amplifiers (NA400/NA410) include built-in circuitry for measuring electrode impedances without requiring external equipment.

## Measurement Principle

The impedance measurement uses a **voltage divider** technique:

1. A known calibration signal is injected through all electrodes
2. Each electrode to be measured is connected to a precision 10 kΩ reference resistor
3. The resulting signal amplitude is measured
4. Impedance is calculated from the voltage divider ratio

```
Calibration Signal → [Electrode Impedance Z] → [10K Reference] → Ground
                                             ↓
                                    Measured Amplitude
```

The impedance formula:

```
Z = (idealSignal - measuredAmplitude) / (measuredAmplitude / 10.0)
```

Where:
- `idealSignal` = expected amplitude with 0Ω electrode impedance
- `measuredAmplitude` = peak-to-peak amplitude of the measured signal
- `10.0` = reference resistor value in kΩ

## Amplifier States

The measurement process uses three amplifier states:

### Excitation State (Initial/Driving)

All electrodes actively drive the calibration signal onto the scalp:

| Setting               | Value    | Description                               |
|-----------------------|----------|-------------------------------------------|
| All 10K Resistors     | OFF      | Electrodes not connected to reference     |
| All Drive Signals     | ON       | All electrodes driving calibration signal |
| Oscillator Gate       | ON       | Enable 20 Hz sine wave generator          |
| Calibration Frequency | 20 Hz    | Low frequency for skin penetration        |
| Calibration Amplitude | 4095     | Maximum (12-bit DAC)                      |
| Wave Shape            | Sine (0) | Clean sinusoidal signal                   |
| Subject Ground        | OFF      | Not grounded during measurement           |
| Current Source        | OFF      | Voltage mode measurement                  |

### Measurement State (Per-Channel)

For the channel being measured:

| Setting              | Value  | Description                   |
|----------------------|--------|-------------------------------|
| Channel Drive Signal | OFF    | Stop driving this electrode   |
| Channel 10K Resistor | ON     | Connect to reference resistor |

All other channels remain in excitation state, creating the voltage divider.

### Reset State

After measurement, return channel to excitation state:

| Setting              | Value  | Description          |
|----------------------|--------|----------------------|
| Channel Drive Signal | ON     | Resume driving       |
| Channel 10K Resistor | OFF    | Disconnect reference |

## Measurement Algorithm

```
1. Configure amplifier to Excitation State (all channels driving)

2. For each channel to measure:
   a. Set channel to Measurement State (drive OFF, 10K ON)
   b. Wait for settling time (~30ms command + ~1s filter time)
   c. Collect samples for peak-to-peak calculation (~51 samples)
   d. Calculate amplitude = max(samples) - min(samples)
   e. Calculate impedance from amplitude
   f. Reset channel to Excitation State (drive ON, 10K OFF)

3. Optionally measure Reference (Cz) and COM electrodes

4. When done, reset amplifier to Default Acquisition State
```

## Timing Parameters

| Parameter            | Value  | Description                         |
|----------------------|--------|-------------------------------------|
| Command Time         | 30 ms  | Time for command to reach amplifier |
| Settle Time          | 0 ms   | Additional settling                 |
| Filter Time          | 1.0 s  | Duration for filter/measurement     |
| Peak-to-Peak Samples | 51     | Samples for amplitude calculation   |

Total time per channel: ~1.03 seconds

For a 256-channel net, a full scan takes approximately 4-5 minutes.

## LSL Streaming

Impedance values are streamed via LSL at 1 Hz:

- **Stream Type**: `Impedance`
- **Sample Rate**: 1 Hz (one sample per second)
- **Data Format**: float32 (kΩ)
- **Channel Labels**: E1, E2, ..., E256, Cz

The stream publishes the **current known values** for all channels every second, even during scanning. Channels not yet measured show 1000 kΩ (maximum/invalid value).

### Stream Metadata

Each channel includes 3D electrode position in the stream description:

```xml
<channel>
  <label>E1</label>
  <type>Impedance</type>
  <unit>kohms</unit>
  <location>
    <X>0.123</X>
    <Y>0.456</Y>
    <Z>0.789</Z>
    <unit>normalized</unit>
  </location>
</channel>
```

Coordinates are on a unit sphere:
- X: positive toward right ear
- Y: positive toward nose
- Z: positive toward vertex (Cz)

## Implementation

### Key Source Files

| File                                                                     | Description                                  |
|--------------------------------------------------------------------------|----------------------------------------------|
| [ImpedanceMeasurement.h](src/core/include/egiamp/ImpedanceMeasurement.h) | Class declaration and timing structures      |
| [ImpedanceMeasurement.cpp](src/core/src/ImpedanceMeasurement.cpp)        | Measurement algorithm implementation         |
| [LSLStreamer.cpp](src/core/src/LSLStreamer.cpp)                          | LSL outlet creation with electrode positions |
| [ElectrodePositions.h](src/core/include/egiamp/ElectrodePositions.h)     | Electrode coordinate definitions             |
| [ElectrodePositions.cpp](src/core/src/ElectrodePositions.cpp)            | Geodesic position generation                 |
| [EGIAmpClient.cpp](src/core/src/EGIAmpClient.cpp)                        | Integration with streaming client            |

### Key Classes

**`ImpedanceMeasurement`** - Core measurement logic:
- `setupImpedanceState()` - Configure amplifier for impedance mode
- `startContinuousScan()` - Begin background scanning thread
- `measureChannel(int ch)` - Measure single channel (blocking)
- `feedSample(PacketFormat2&)` - Feed EEG samples for amplitude calculation
- `publishThread()` - Push values to LSL at 1 Hz

**`LSLStreamer`** - LSL stream management:
- `createImpedanceOutlet()` - Create impedance stream with metadata
- `pushSample()` - Push impedance values

### AmpServer Commands

| Command                             | Description                           |
|-------------------------------------|---------------------------------------|
| `cmd_TurnAll10KOhms`                | Set all 10K resistors (0=off, 1=on)   |
| `cmd_TurnAllDriveSignals`           | Set all drive signals (0=off, 1=on)   |
| `cmd_TurnChannel10KOhms`            | Set single channel 10K resistor       |
| `cmd_TurnChannelDriveSignals`       | Set single channel drive signal       |
| `cmd_SetOscillatorGate`             | Enable/disable calibration oscillator |
| `cmd_SetCalibrationSignalFreq`      | Set frequency (20 Hz for impedance)   |
| `cmd_SetCalibrationSignalAmplitude` | Set amplitude (0-4095)                |
| `cmd_SetWaveShape`                  | Set waveform (0=sine, 1=square)       |
| `cmd_DefaultAcquisitionState`       | Reset to normal EEG acquisition       |

## Usage

### CLI

```bash
./EGIAmpServerCLI --address 10.10.10.51 --impedance
```

### GUI

1. Link to the amplifier
2. Check "Measure Impedances" checkbox
3. Impedance stream appears on LSL network
4. Uncheck to stop measurement and return to normal EEG

### Python Visualization

```bash
cd scripts
pip install -r requirements.txt
python impedance_viewer.py --threshold 50
```

Values of 1000 kΩ indicate:
- Electrode not yet measured
- No contact with scalp
- Broken or disconnected electrode

## References

- EGI AmpServer SDK Documentation
- Net Station Acquisition source code (NSAAmpSettings.m, ImpedancesController.m)
- EGI Geodesic Sensor Net Technical Manual
