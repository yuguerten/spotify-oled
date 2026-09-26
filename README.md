# spotify-oled

Shows what's playing on Spotify (desktop app) on a 128×64 SSD1306 OLED
driven by an ESP32 — with a dancing guy that gets a different dance per
track, and a sad face begging `cmon bud play music lahi7fdak` on pause.

## Hardware

- ESP32 + SSD1306 128×64 OLED over I2C (`0x3C`, SDA `21`, SCL `22`)
- Arduino libs: `Adafruit GFX`, `Adafruit SSD1306`, `Adafruit BusIO`

## Firmware

```bash
arduino-cli compile .
arduino-cli upload .
arduino-cli monitor   # uses sketch.yaml defaults (/dev/ttyUSB0, 115200)
```

## PC sender (`spotify-oled-send.py`)

Needs: `playerctl`, `python3-pyserial`, `python3-pil` (with raqm),
Noto Naskh Arabic + DejaVu Sans fonts, Spotify desktop app.

```bash
python3 spotify-oled-send.py
```

It polls Spotify (~0.5 s), renders title/artist to 1-bit bitmaps
(full Arabic shaping + bidi done on the PC), picks a dance per track
(`hash(title+artist) % 4`: disco / shuffle / robot / bounce) and pushes
everything over USB serial. Close `arduino-cli monitor` first — only one
program can hold `/dev/ttyUSB0`.
