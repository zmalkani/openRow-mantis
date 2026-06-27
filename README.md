# openRow Mantis

openRow Mantis is a small rowing telemetry rig I built to get live acceleration data off the boat and onto a screen the coach can actually read while following alongside. One unit rides in the boat, reads the hull's acceleration, and sends it over a long-range radio link. A second unit on the coach's side catches those readings and hands them to a web page over Bluetooth, so you watch the stroke trace in real time on a phone or laptop.

It's deliberately stripped down. No force pads, no oarlock sensors, no app to install. Just acceleration and time, plotted live. If you've used the original openRow with the OR-1 display and oarlock, Mantis is the lighter, faster cousin that trades features for simplicity and range.

## How it works

There are two boards talking to each other:

- **TX (in the boat)** — sits in the hull with an accelerometer wired up. Every 40 ms or so it reads X/Y/Z acceleration and fires off a tiny radio packet over 915 MHz LoRa. Its little OLED shows the live numbers, the device ID, and battery level.
- **RX (with the coach)** — listens for those packets, shows them on its own OLED (acceleration, signal strength, packet count), and re-broadcasts the data over Bluetooth Low Energy.

The web dashboard (`mantis.html`) connects to the RX unit over Web Bluetooth and draws the acceleration-vs-time graph. Nothing gets installed; it runs straight from the browser.

The packets are sent as raw binary (18 bytes) instead of text to keep airtime short, which is what lets the link reach across a stretch of water without dropping strokes.

## Parts list

You need two Heltec boards — one becomes the TX, one becomes the RX — plus an accelerometer and batteries.

| Part | Qty | Notes |
| --- | --- | --- |
| Heltec WiFi LoRa 32 V3 | 2 | ESP32-S3 board with an onboard SX1262 LoRa radio and a small OLED. One flashed as TX, one as RX. |
| ADXL345 accelerometer | 1 | Lives on the TX board, in the boat. Wired to I2C (SDA on GPIO 7, SCL on GPIO 6). |
| 915 MHz antenna | 2 | One per board. Don't power the radios up without antennas attached. |
| LiPo battery (3.7 V) | 2 | One per board. The Heltec V3 has a built-in charger and battery connector. |
| Waterproof case / dry bag | 1+ | For the in-boat TX unit especially. Rowing and water go together. |
| Jumper wires | a few | For the ADXL345 to TX connections. |

A couple of things worth knowing about the hardware:

- The boards are wired for the **915 MHz** band (good for North America). If you're somewhere that uses 868 MHz or another band, change `kLoRaFrequencyMhz` in `src/TXmain.cpp` and `src/RXmain.cpp` to stay legal.
- The OLED and battery monitoring are already on the Heltec board, so you don't add anything for those.

## Building and flashing the firmware

The firmware lives in `src/` and builds with [PlatformIO](https://platformio.org/). Both boards run from the same project, just different environments:

- Flash one board with the `heltec_wifi_lora_32_V3` environment — that's the **TX**.
- Flash the other with the `heltec_wifi_lora_32_V3_rx` environment — that's the **RX**.

```sh
# transmitter (in-boat unit)
pio run -e heltec_wifi_lora_32_V3 -t upload

# receiver (coach unit)
pio run -e heltec_wifi_lora_32_V3_rx -t upload
```

Before flashing the TX, you can set its 4-digit `ID` near the top of `src/TXmain.cpp` if you're running more than one boat.

## Using the dashboard

Once the RX unit is powered on and advertising over Bluetooth, here's how to watch your data:

1. **Open the dashboard.** Go to <https://zmalkani.github.io/openRow-mantis/mantis.html> in Chrome on desktop or Android. Web Bluetooth only works in Chromium-based browsers — Safari and Firefox won't connect, and the page will tell you if that's the problem.
2. **Power on the RX unit** and make sure its OLED shows the receiver is running. The TX unit should be on too, so there's actual data flowing.
3. **Hit Connect** (top right). A browser pop-up lists nearby Bluetooth devices — pick `openRow_RX`. The status dot turns green when it's connected.
4. **Watch the numbers.** The Acceleration card shows the live resultant value, Device Uptime shows how long the TX has been running, and the graph plots acceleration over time as the boat moves.
5. **Save your data** if you want — the two export buttons up top download the session as a file once you're connected.

If the device doesn't show up in the pop-up, double-check the RX unit is powered, the antenna is on, and you're not already connected from another tab.

The two UUID fields in the config bar (Service UUID and Char UUID) are pre-filled to match the firmware. You only need to touch those if you've changed the UUIDs in `RXmain.cpp`.

## Repo layout

- `src/TXmain.cpp` — transmitter firmware (reads the accelerometer, sends LoRa packets).
- `src/RXmain.cpp` — receiver firmware (catches LoRa packets, re-broadcasts over BLE).
- `src/platformio.ini` — board and build configuration for both units.
- `mantis.html` — the live web dashboard.
- `index.html` — landing page to pick between Mantis and the older V1.
- `v1.html` — the legacy dashboard.

## A few notes

- Keep the TX and RX on the same radio settings. They're matched in the firmware already; if you change frequency, bandwidth, or spreading factor on one, change it on both or they'll go deaf to each other.
- Range depends a lot on antenna placement and how much of the boat is between the two units. Getting the antenna up and clear helps.
- This is a hobby project, not a certified product. Check your local rules before transmitting on any radio band.