#!/usr/bin/env python3
"""Spotify -> ESP32 OLED bridge.

Polls the Spotify desktop app via playerctl, renders the track title/artist
(with full Arabic shaping + bidi, Pillow + raqm) into 1-bit bitmaps, and
pushes them over USB serial to the ESP32 sketch in this folder
(esp32-i2c-scanner.ino).

Protocol (115200 baud, one command per line):
  M|<dance_style>|<Playing|Paused>   track state (style = hash(title+artist) % 4)
  T|<hex>                            title bitmap, 88x14 px, Adafruit packing
  A|<hex>                            artist bitmap, 88x11 px, Adafruit packing

Requirements: playerctl, python3-pyserial, python3-pil (with raqm),
Noto Naskh Arabic + DejaVu Sans fonts, Spotify desktop app playing.

Run:  python3 spotify-oled-send.py   (close `arduino-cli monitor` first,
only one program can hold /dev/ttyUSB0 at a time)
"""
import serial, subprocess, time
from PIL import Image, ImageDraw, ImageFont

AR_FONT = '/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf'
LT_FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
TW, TH, T_SIZE = 88, 14, 12
AW, AH, A_SIZE = 88, 11, 9
WIN = 88
GAP = 12

FONTS = {}


def font(path, size):
    if (path, size) not in FONTS:
        FONTS[(path, size)] = ImageFont.truetype(path, size)
    return FONTS[(path, size)]


def is_ar(c):
    return ('\u0600' <= c <= '\u06FF' or '\u0750' <= c <= '\u077F' or
            '\u08A0' <= c <= '\u08FF' or '\uFB50' <= c <= '\uFDFF' or
            '\uFE70' <= c <= '\uFEFF')


def split_runs(text):
    out, cur, cls = [], '', None
    for c in text:
        k = 'ar' if (is_ar(c) or c == ' ') else 'lt'
        if k != cls and cur:
            out.append((cls, cur))
            cur = ''
        cls = k
        cur += c
    if cur:
        out.append((cls, cur))
    merged = []
    for k, s in out:
        if s.strip() == '' and merged:
            pk, ps = merged.pop()
            merged.append((pk, ps + s))
        else:
            merged.append((k, s))
    return merged


def first_strong(text):
    for c in text:
        if is_ar(c):
            return 'ar'
        if c.isalpha():
            return 'lt'
    return 'lt'


def render_line(text, size, h):
    """Full-width 1-bit image of the shaped line (visual order)."""
    rs = split_runs(text)
    rtl = first_strong(text) == 'ar'
    fonts = {'ar': font(AR_FONT, size), 'lt': font(LT_FONT, size)}
    meas = ImageDraw.Draw(Image.new('1', (8, 8), 0))
    widths = [int(meas.textlength(s, font=fonts[k],
                                  direction='rtl' if k == 'ar' else 'ltr'))
              for k, s in rs]
    img = Image.new('1', (sum(widths) + 4, h), 0)
    d = ImageDraw.Draw(img)
    if not rtl:
        x = 2
        for (k, s), w in zip(rs, widths):
            d.text((x, 1), s, font=fonts[k], fill=1,
                   direction='rtl' if k == 'ar' else 'ltr')
            x += w
    else:
        x = img.width - 2
        for (k, s), w in zip(rs, widths):
            d.text((x - w, 1), s, font=fonts[k], fill=1,
                   direction='rtl' if k == 'ar' else 'ltr')
            x -= w
    return img


def pack_row_major(img):
    """Adafruit drawBitmap packing: row-major, MSB = leftmost pixel."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for y in range(h):
        for xb in range((w + 7) // 8):
            b = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x < w and px[x, y]:
                    b |= (0x80 >> bit)
            out.append(b)
    return bytes(out)


def window_hex(full, off):
    win = full.crop((off, 0, off + WIN, full.height))
    return pack_row_major(win).hex()


def snapshot():
    try:
        return subprocess.check_output(
            ['playerctl', '-p', 'spotify', 'metadata',
             '--format', '{{title}}|{{artist}}|{{status}}'],
            text=True, timeout=5).strip()
    except Exception:
        return ''


def pick_dance(s):
    h = 5381
    for c in s:
        h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h % 4


s = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
try:
    s.dtr = False
    s.rts = False
except Exception:
    pass
time.sleep(2.5)  # let ESP32 boot after open-reset
try:
    s.reset_input_buffer()
except Exception:
    pass


def send(line):
    s.write((line + '\n').encode())
    s.flush()


last = ''
fullT = fullA = None
offT = offA = 0
last_push = 0


def push_windows():
    global offT, offA
    if fullT is not None:
        if fullT.width > WIN:
            strip = Image.new('1', (fullT.width + GAP, TH), 0)
            strip.paste(fullT, (0, 0))
            offT = (offT + 3) % (strip.width - WIN + 1)
            send('T|' + window_hex(strip, offT))
        else:
            send('T|' + window_hex(fullT, 0))
    if fullA is not None:
        if fullA.width > WIN:
            strip = Image.new('1', (fullA.width + GAP, AH), 0)
            strip.paste(fullA, (0, 0))
            offA = (offA + 3) % (strip.width - WIN + 1)
            send('A|' + window_hex(strip, offA))
        else:
            send('A|' + window_hex(fullA, 0))


while True:
    msg = snapshot()
    if msg:
        parts = msg.split('|')
        if len(parts) >= 2 and parts[0].strip():
            title = parts[0].replace('|', ' ').strip()
            artist = parts[1].replace('|', ' ').strip() if len(parts) > 1 else ''
            status = parts[2].strip() if len(parts) > 2 and parts[2].strip() else 'Playing'
            if msg != last:
                last = msg
                style = pick_dance(title + artist)
                fullT = render_line(title, T_SIZE, TH)
                fullA = render_line(artist, A_SIZE, AH)
                offT = offA = 0
                send(f'M|{style}|{status}')
                time.sleep(0.05)
                push_windows()
                last_push = time.time()
                print(f'sent: {title}|{artist}|{status} style={style}', flush=True)
    # re-push windows regularly: scrolls long lines, retries static ones
    if fullT is not None and time.time() - last_push > 0.4:
        scrolling = (fullT.width > WIN) or (fullA is not None and fullA.width > WIN)
        if scrolling or time.time() - last_push > 2.0:
            push_windows()
            last_push = time.time()
    time.sleep(0.1)
