# Changelog

All notable changes to SigRoam are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1] — unreleased

First public release. Wi-Fi survey front-end for the Flipper Zero driving an
external ESP32 Marauder scanner over the GPIO serial port.

### Added

- **Dashboard** with four tabs (left/right to switch): live counters, parsed
  record stream, GPS status, and session diagnostics. OK on the Dash tab starts
  and stops the scan; the scan keeps running when you leave the page with Back.
- **Firmware probe** — sends `info` and reports what answered, distinguishing
  "nothing connected" from "connected but not Marauder".
- **Raw log** — unparsed serial lines, including anything the parser did not
  recognise, so a malformed feed is inspectable rather than invisible.
- **Settings** — baud rate (six choices, default 115200), source codec, sound,
  vibro, backlight, stealth, and debug rows. Persisted across restarts.
- **Unique-BSSID estimate** from a 4 KB Bloom filter (32768 bits, 4 hashes),
  session-scoped and RAM-only.
- **GPIO serial handling** with the OTG power gate, physical-state verification
  before opening the port, and detection of the Log Device conflict on pins
  13/14 — each reported on screen instead of failing silently.
- **QR code on the About page** linking to this repository.

### Notes

- Receive-only by design. No deauthentication, handshake capture, evil twin,
  karma, beacon/BLE spam, or password cracking — permanently out of scope.
- Survey data is not written to the Flipper's SD card. Logging is the scanner's
  job; the Flipper provides control and live visibility.
- Builds against Official and Momentum firmware from the same source using only
  the common Flipper API.
