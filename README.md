# SigRoam

A Wi-Fi survey (wardriving) front-end for the Flipper Zero, driving an external
ESP32 Marauder scanner over the GPIO serial port.

The Flipper does not scan anything by itself. It starts and stops the scan,
shows what the scanner reports in real time, and lets you read the raw serial
lines when something looks wrong.

## What it deliberately does not do

SigRoam is **receive-only**. It never transmits attack traffic, and it never will.
The following are permanently out of scope — not "not yet implemented", but a
product rule the project will not accept exceptions to:

- deauthentication / disassociation frames
- WPA handshake capture
- evil twin, karma, or rogue AP
- beacon spam and BLE spam
- password cracking of any kind

The app only parses what the scanner prints. If you need those features, this is
the wrong project.

## Hardware you need

- A Flipper Zero.
- An ESP32-based scanner running [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder)
  firmware, connected to the GPIO header. GPS is optional but wardriving without
  it only gives you counts, not locations.

Any Marauder-compatible board should work — the app talks the Marauder serial
protocol, not a specific product. It is developed against
[Scout Lite](https://github.com/pingequalab/scout-lite) (ESP32-C5 + L86-M33 GPS
+ microSD), which is what the pin notes below assume.

## Wiring and required setup

| Flipper pin | Use |
|---|---|
| 1 (5V) | Power for the scanner |
| 13 (TX) | Serial to the scanner |
| 14 (RX) | Serial from the scanner |

Two things will stop the app from working if you skip them:

- **`Settings → System → Log Device` must be `Off`.** Pins 13/14 are shared with
  the firmware's serial log. If the log owns them, the app cannot open the port —
  it detects this and says so on screen rather than failing silently.
- **5V on pin 1 comes from the OTG boost converter, not a permanent rail.** The
  app requests OTG and verifies the physical state before opening the port. Note
  that while USB is plugged in, the firmware defers OTG — so the scanner may have
  no power until you unplug USB and restart the app.

**Do not power the scanner from pin 9 (3.3V).** It is limited to 150 mA and a
5 GHz-capable ESP32 will exceed that on transmit peaks.

## Interface

The main menu has five entries:

- **Dashboard** — four tabs, switched with left/right:
  - `Dash` — unique BSSID estimate, AP/BLE counts, GPS fix count, bytes received,
    elapsed time. **OK starts and stops the scan.**
  - `Strm` — the most recent parsed records, scrolling.
  - `GPS` — fix status, satellites, coordinates. OK requests a GPS sample.
  - `Sess` — session and diagnostic counters.
- **Probe firmware** — sends `info` and reports what answered, so you can tell
  "nothing connected" from "connected but not Marauder".
- **Raw log** — the raw serial lines, including anything the parser did not
  recognise. This is the first place to look when the numbers look wrong.
- **Settings** — Baud (six choices, default 115200), Source, Sound, Vibro,
  Backlight, Stealth, Debug rows.
- **About** — version, compliance statement, and a QR code to this repository.

While a scan is running, Back returns to the main menu **without stopping the
scan**. Stopping is done only with OK on the Dash tab.

## Where the survey data goes

**SigRoam does not write survey data to the Flipper's SD card.** The only thing
it persists on the Flipper is your settings.

Logging is the scanner's job: Marauder writes its own CSV to the SD card on the
scanner board, and that file is what you upload to WiGLE. SigRoam gives you
control and live visibility over that session; it is not a second recorder.

The unique-BSSID number on the Dash tab is an estimate from a 4 KB Bloom filter
(32768 bits, 4 hashes), kept in RAM for the session only. It can undercount
slightly at high AP densities by design — it is a progress indicator, not the
record of what you collected.

## Building

Built with [ufbt](https://github.com/flipperdevices/flipperzero-ufbt).

```bash
ufbt              # build
ufbt launch       # build, install, and run on a connected Flipper
ufbt cli          # device log
```

Host-side unit tests cover the pure-logic layer (line assembly, parsers, model,
Bloom filter) and run without a Flipper:

```bash
make -C tools/host_test        # guards + tests
make -C tools/host_test asan   # same, with ASan/UBSan
```

## Firmware compatibility

Builds against **Official** and **Momentum** firmware from the same source; both
are checked in CI. Unleashed builds too but is not part of the regular pipeline.

The app uses only the common Flipper API — no firmware-specific headers — so it
builds against the Official SDK, which the Flipper Apps Catalog requires.

## License

GPL-3.0. See [LICENSE](LICENSE).
