# LSL EGI AmpServer Application

## Usage

This program should work with any amplifier that works with the AmpServer produced by EGI (http://www.egi.com/).
All communication with the amplifier happens through the Amp Server Pro SDK, [documented here](https://www.egi.com/images/stories/manuals/amp-server-pro-sdk-3-0-network-apis-user-guide-rev-01.pdf).

  * Make sure that your AmpServer is running and can correctly record from its connected amplifier(s). To connect to the Amp Server you need to purchase the Amp Server Pro SDK, see: [ftp://ftp.egi.com/pub/documentation/placards/AS_guide_8409503-50_20100421.pdf](ftp://ftp.egi.com/pub/documentation/placards/AS_guide_8409503-50_20100421.pdf), otherwise the LSL application will not work.

  * Start the EGIAmpServer app. You should see a window like the following.
> > ![egiampserver.png](egiampserver.png)

  * Make sure that you have the correct IP address of the AmpServer assigned. The ports correspond to the default settings of the server and should not require a change.

  * If you have multiple amplifiers connected to the AmpServer and you would like to record from a specific one, you need to set the correct amplifier ID (these should be increasing from zero). Also make sure that you are using a supported number of channels and a supported sampling rate (the defaults should work).

  * To link the application to the LSL, click the "Link" button. If all goes well you should now have a new stream on the network with name "EGI NetAmp k" (k corresponding to the index of the amplifier) and type "EEG". If you get an error you might try to manually power on the desired Amp and try to link while it is either recording or stopped.

  * For subsequent uses you can save the desired settings from the GUI via File / Save Configuration. If the app is frequently used with different settings you might make a shortcut on the desktop that points to the app and appends to the Target field of the shortcut the snippet `-c name_of_config.cfg` to denote the name of the config file that should be loaded at startup.

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

When using this application alongside Net Station Acquisition:

- **Start Net Station first**: If you plan to use Net Station Acquisition, start it and initialize the amplifier (click "On") BEFORE connecting this app. Our app will detect the running amplifier and automatically use its sample rate.

- **Do not start Net Station after**: If this app is already streaming and Net Station subsequently initializes the amplifier at a different sample rate, our app cannot detect this change. AmpServer only sends notifications to one subscriber, and Net Station consumes them when it's running.

- **Recommended workflow**:
  1. Start Net Station Acquisition
  2. Initialize the amplifier at your desired sample rate (click "On")
  3. Start EGIAmpServer and click "Link" - it will detect the running amp and match its sample rate
  4. Both applications will now receive data at the correct rate

## Dropped Packets After Device Shutdown

After Net Station shuts down the amplifier (via "Shutdown" command), immediately starting this app may result in excessive dropped packets and eventual stream loss. This appears to be related to stale data in the connection. **Workaround**: Wait a moment and restart the app, or power cycle the amplifier.

## Sample Rate Auto-Detection

The app automatically detects the sample rate when connecting to an already-running amplifier by measuring packet timing. This detection snaps to standard rates (250, 500, or 1000 Hz). If the amplifier is idle when connecting, the app uses the sample rate configured in the UI/config file.
