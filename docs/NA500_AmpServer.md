# Net Amps 500 (GES 500) AmpServer: observed behavior

There is no public documentation for the AmpServer embedded in the GES 500. This page records how it
behaves on the wire, as measured against a real unit (firmware 0.4.9, FPGA 0.6.1.f0, PCB 4.1.0.0).
`scripts/ampserver_conformance.py` reproduces every measurement here, and its recording of that unit is
in `mock/conformance/na500_fw0.4.9.json`. `MockAmpServer --amp-type NA500` emulates this behavior.

The protocol is the AmpServer protocol used by the NA400 (commands on TCP 9877, notifications on 9878,
data on 9879). Only the differences that matter to a client are listed.

## Network

- The amplifier takes an IPv4 address by DHCP and advertises itself over mDNS as service `_na500._tcp`,
  host `NA500-256ch-<serial>.local`, with TXT records `serial` and `hostname`. Some units also hold the
  static address `169.254.3.10`.
- The AmpServer listens on IPv4 only. On a network without DHCP the unit has only an IPv6 link-local
  address and cannot be reached.
- There is no ECI port (55513) and no UDP discovery port (9880).

## Sessions

- Each of the three ports accepts **one** client per session. A second client's connection succeeds at
  the TCP level but is never served. If the data client disconnects, no new data client is served until
  the session ends.
- Closing the command connection ends the session: streaming stops, the analog front end is powered
  off, and the notification and data clients are disconnected. The next client to connect starts a new
  session. A stream left running is not resumed: the next session must send `cmd_SetPower 1` and
  `cmd_Start` again.
- `cmd_SetDinEnable` persists from one session to the next.

## Commands

- Each TCP segment is one command. A second command in the same segment is ignored, so send one
  command per write and wait for its reply. A trailing extra newline or a doubled space is harmless.
- A command name the server does not recognize, or a segment that does not parse, **re-runs the
  previous command**, with the new segment's arguments where they parse. For example, after
  `cmd_NumberOfAmps`, sending `cmd_NoSuchCommand` returns the `number_of_amps` reply again.
- Replies that differ from the NA400:

  | Command | Reply |
  |---|---|
  | `cmd_ListenToAmp`, `cmd_StopListeningToAmp` sent on the **command** port | none (the client must time out) |
  | `cmd_SetDecimatedRate` / `cmd_SetNativeRate` with an unsupported rate | none |
  | `cmd_SetPower` with a value other than 0 or 1 | none |
  | `cmd_TurnAllZeroOhms`, `cmd_TurnChannelZeroOhms`, `cmd_SetMRIPulseInfo`, `cmd_SetPhoticStimSequence` | **two** `(status complete)` lines |
  | `cmd_NumberOfAmps` | `(sendCommand_return (status complete) (number_of_amps 1))` |
  | `cmd_GetAmpDetails` | as on the NA400 plus `fpga_version`, `pcb_version`, `feature_flags`; **the final `)` is missing** |
  | `cmd_GetBatteryData` | `(battery_details (time_remaining …) (master …) (slave1 …) … (slave4 …))`; one `)` short |
  | `cmd_GetBoardStatus` | `(status_details (ambient_temp …) (lpd_temp …) (fpd_temp …) (pl_temp …))`; one `)` too many |
  | `cmd_GetCurrentTime` | `(amp_status_info (current_time N))`, N = current packet counter |
  | `cmd_SetRecordingState` | `(amp_status_info (start_time N))`, N = current packet counter |
  | `cmd_GetAmpStatus`, `cmd_NumberOfActiveAmps`, `cmd_GetPhysioConnectionStatus`, `cmd_ReceiveNotifications` | bare `(status complete)`, no data |
  | `cmd_Stop` | `(status complete)` after about 1 s |
  | `cmd_SetAmpTime <epoch µs>` | `(status complete)`; sets the clock used for packet timestamps |

- Valid rates are 250, 500, 1000, 2000, 4000, 8000 and 16000 Hz, accepted by both `cmd_SetDecimatedRate`
  and `cmd_SetNativeRate`.
- The server does not shut down on `cmd_Exit`. GTEN commands and `cmd_SetFilterAndDecimate` are not
  recognized (so they re-run the previous command).

## Front end

- After power-on the EEG channels read negative full scale (-8388608 counts) until
  `cmd_SetBufferedReference 1` is sent. `cmd_Stop` switches the buffered reference off again, so send
  it after every stop (before or after `cmd_Start`; either works).
- Front-end switch commands sent within about 20 ms of the `cmd_SetPower 1` reply are lost, although
  they are acknowledged. Wait a moment after power-on before configuring.

## Physio

- The 16 physio inputs arrive in the first 8 entries of each of the packet's two physio arrays (physio
  1-8, then 9-16); the other entries are 0. They are sampled with the EEG: no relative delay at any rate
  or mode. There is no notification or reply that reports whether a Physio16 is attached.
- `cmd_SetPIBChannelGain <channel 0-15> <gain>` sets one input's gain (1, 2, 4, 6, 8, 12 or 24). At the
  power-on default of 12 the physio scale equals the EEG's (0.0447 µV per count); at gain 1 it is 12
  times coarser. Gains **persist across sessions**, so a client should set them rather than assume them.

## Notifications

Only four are sent, each after the reply to the command that causes it:

| Notification | Sent after |
|---|---|
| `(notification ntn_AmpPowerOn 9 0 -1)` | `cmd_SetPower 1` |
| `(notification ntn_AmpPowerOff 8 0 -1)` | `cmd_SetPower 0` |
| `(notification ntn_AmpStarted 10 0 -1)` | `cmd_GetStartTime` (**not** `cmd_Start`) |
| `(notification ntn_AmpStopped 11 0 -1)` | `cmd_Stop` |

There is no `ntn_PhysioConnectionStatus` and no client-connected notification. A notification can be
lost if its command is sent the instant the notification port connects, before the server has accepted it.

## Data stream

- Data flows to the data client as soon as the amplifier is powered and started; `cmd_ListenToAmp` is
  not needed and gets no reply.
- Each block is a 16-byte big-endian header (amp id 0, payload length) followed by **8** format-2
  packets (10112 bytes), at every rate.
- The packet counter restarts at 0 on every `cmd_Start`. Before that first packet the stream carries
  **leftover packets from the previous stream** (usually 1–7, occasionally more), with their old counter
  and timestamps. They can come from a previous session. Discard packets until the counter is 0.
- Timestamps are microseconds on the amplifier's clock, advancing by exactly `1e6 / rate` per packet
  truncated to an integer (62 µs at 16 kHz). The clock is not synchronized: the unit we measured was a
  month behind until a client sent `cmd_SetAmpTime`.
- The amplifier delivers at most about 7995 packets/s, while timestamps and counters advance as if
  every sample were sent. At 16000 Hz about half the samples are lost without a trace; at 8000 Hz about
  4 per second are, enough to make the sample timeline drift by milliseconds over a minute. Rates up to
  4000 Hz are delivered in full.
- If the client stops reading for a few seconds, the amplifier drops samples once about 1 MB is
  buffered; the gap shows in the packet counter. Read the data port continuously.
- With no net attached the net code is 255. Digital inputs read 0 until `cmd_SetDinEnable`, then idle at
  `0xFFFF` (active low). The first reserved byte read 3 at idle on the unit we measured.
