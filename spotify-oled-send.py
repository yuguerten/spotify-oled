#!/usr/bin/env python3
"""Spotify -> ESP32 OLED bridge.

Polls the Spotify desktop app via playerctl, renders the track title/artist
(with full Arabic shaping + bidi, Pillow + raqm) into 1-bit bitmaps, and
pushes them over USB serial to the ESP32 sketch in this folder
(esp32-i2c-scanner.ino).

Protocol (115200 baud, one command per line):
  M|<dance_style>|<Playing|Paused>   track state (style = hash(title+artist) % 4)
  T|<hex>                            title bitmap, 54x14 px, Adafruit packing
  A|<hex>                            artist bitmap, 54x11 px, Adafruit packing
  C|<hex>                            cover art, 32x32 px dithered (Spotify/iTunes)
  P|<pos_sec>|<len_sec>              progress clock for the bar + times
  W|HH:MM|temp|code                  Strasbourg clock + weather (idle screen)

Requirements: playerctl, python3-pyserial, python3-pil (with raqm),
Noto Naskh Arabic + DejaVu Sans fonts, Spotify desktop app playing.

Run:  python3 spotify-oled-send.py   (close `arduino-cli monitor` first,
only one program can hold /dev/ttyUSB0 at a time)
"""
import serial, subprocess, time
from PIL import Image, ImageDraw, ImageFont

AR_FONT = '/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf'
LT_FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
TW, TH, T_SIZE = 54, 14, 12
AW, AH, A_SIZE = 54, 11, 9
WIN = 54
GAP = 12
CW, CH = 32, 32

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


def render_line(text, size, h, bold=False):
    """Full-width 1-bit image of the shaped line (visual order).

    bold=True double-strikes each run for a heavier title weight.
    """
    rs = split_runs(text)
    rtl = first_strong(text) == 'ar'
    fonts = {'ar': font(AR_FONT, size), 'lt': font(LT_FONT, size)}
    meas = ImageDraw.Draw(Image.new('1', (8, 8), 0))
    widths = [int(meas.textlength(s, font=fonts[k],
                                  direction='rtl' if k == 'ar' else 'ltr'))
              for k, s in rs]
    img = Image.new('1', (sum(widths) + 4 + (1 if bold else 0), h), 0)
    d = ImageDraw.Draw(img)

    def strike(xe, k, s):
        d.text((xe, 1), s, font=fonts[k], fill=1,
               direction='rtl' if k == 'ar' else 'ltr')
        if bold:
            d.text((xe + 1, 1), s, font=fonts[k], fill=1,
                   direction='rtl' if k == 'ar' else 'ltr')

    if not rtl:
        x = 2
        for (k, s), w in zip(rs, widths):
            strike(x, k, s)
            x += w
    else:
        x = img.width - 2
        for (k, s), w in zip(rs, widths):
            strike(x - w, k, s)
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
             '--format',
             '{{title}}|{{artist}}|{{status}}|{{mpris:length}}|{{mpris:artUrl}}'],
            text=True, timeout=5,
             stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ''


def position():
    try:
        return int(float(subprocess.check_output(
            ['playerctl', '-p', 'spotify', 'position'],
            text=True, timeout=5,
             stderr=subprocess.DEVNULL).strip()))
    except Exception:
        return None


WX = {'t': 0, 'temp': None, 'code': -1}


def weather():
    # Open-Meteo, free, no key — Strasbourg 48.57N 7.75E, cached 10 min
    if time.time() - WX['t'] > 600 or WX['temp'] is None:
        try:
            import json
            import urllib.request
            with urllib.request.urlopen(
                    'https://api.open-meteo.com/v1/forecast?latitude=48.57'
                    '&longitude=7.75&current=temperature_2m,weather_code'
                    '&timezone=Europe%2FParis', timeout=8) as r:
                d = json.load(r)['current']
            WX.update(t=time.time(), temp=int(round(d['temperature_2m'])),
                      code=int(d['weather_code']))
        except Exception:
            pass
    return WX['temp'], WX['code']


def pick_dance(s):
    h = 5381
    for c in s:
        h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h % 4


ART_CACHE = {}


def download_img(url):
    import urllib.request
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'spotify-oled/1.0'})
        return Image.open(urllib.request.urlopen(req, timeout=8)).convert('L')
    except Exception:
        return None


def itunes_art(title, artist):
    import json
    import urllib.parse
    import urllib.request
    try:
        q = urllib.parse.quote(f'{artist} {title}')
        with urllib.request.urlopen(
                f'https://itunes.apple.com/search?term={q}&media=music&limit=1',
                timeout=8) as r:
            d = json.load(r)
        if d.get('resultCount'):
            u = d['results'][0]['artworkUrl100'].replace('100x100bb', '600x600bb')
            return download_img(u)
    except Exception:
        pass
    return None


def cover_hex(title, artist, art_url):
    """32x32 dithered cover packed for drawBitmap, cached per track."""
    from PIL import ImageOps
    key = (title, artist)
    if key not in ART_CACHE:
        img = download_img(art_url) if art_url else None
        if img is None:
            img = itunes_art(title, artist)
        ART_CACHE[key] = img
        # keep the cache small
        while len(ART_CACHE) > 20:
            ART_CACHE.pop(next(iter(ART_CACHE)))
    img = ART_CACHE[key]
    if img is None:
        return None
    img = ImageOps.autocontrast(img, cutoff=2).resize((CW, CH), Image.LANCZOS)
    bw = img.convert('1', dither=Image.FLOYDSTEINBERG)
    return pack_row_major(bw).hex()


ser = None
need_full = True  # force a full M/T/A/C resync (fresh boot or reconnect)


def ensure_serial():
    """Open the port if needed. Returns True when usable."""
    global ser, need_full
    if ser is not None:
        return True
    try:
        ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
        try:
            ser.dtr = False
            ser.rts = False
        except Exception:
            pass
        time.sleep(2.5)  # let ESP32 boot after open-reset
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        need_full = True  # ESP32 rebooted blank — resend everything
        print('serial: connected /dev/ttyUSB0', flush=True)
        return True
    except Exception as e:
        ser = None
        return False


def drop_serial(reason):
    global ser
    print(f'serial: dropped ({reason}), retrying...', flush=True)
    try:
        if ser is not None:
            ser.close()
    except Exception:
        pass
    ser = None


def send(line):
    """False when the port died (caller: drop + retry later)."""
    try:
        ser.write((line + '\n').encode())
        ser.flush()
        return True
    except Exception as e:
        drop_serial(e)
        return False


last = ''
fullT = fullA = None
cover_now = None
offT = offA = 0
last_push = 0
track_len = 0
lastP = (-1, -1)
last_pos_q = 0
idle_on = False
last_idle_min = ''


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
    if not ensure_serial():
        time.sleep(2)  # cable unplugged / port busy — wait and retry
        continue
    msg = snapshot()
    if msg:
        parts = msg.split('|')
        if len(parts) >= 2 and parts[0].strip():
            title = parts[0].replace('|', ' ').strip()
            artist = parts[1].replace('|', ' ').strip() if len(parts) > 1 else ''
            status = parts[2].strip() if len(parts) > 2 and parts[2].strip() else 'Playing'
            if len(parts) > 3:
                try:
                    track_len = int(int(parts[3]) // 1000000)
                except ValueError:
                    pass
            art_url = parts[4].strip() if len(parts) > 4 else ''
            if msg != last or need_full:
                last = msg
                style = pick_dance(title + artist)
                fullT = render_line(title, T_SIZE, TH, bold=True)
                fullA = render_line(artist, A_SIZE, AH)
                cover_now = cover_hex(title, artist, art_url)
                offT = offA = 0
                if not send(f'M|{style}|{status}'):
                    continue
                time.sleep(0.05)
                push_windows()
                if cover_now:
                    time.sleep(0.05)
                    send('C|' + cover_now)
                    print(f'cover: {len(cover_now) // 2} bytes', flush=True)
                last_push = time.time()
                need_full = False
                print(f'sent: {title}|{artist}|{status} style={style}', flush=True)
    # re-push windows regularly: scrolls long lines, retries static ones + cover
    if fullT is not None and time.time() - last_push > 0.4:
        scrolling = (fullT.width > WIN) or (fullA is not None and fullA.width > WIN)
        if scrolling or time.time() - last_push > 2.0:
            push_windows()
            if cover_now and time.time() - last_push > 2.0:
                send('C|' + cover_now)
            last_push = time.time()
    # progress clock (~0.5s, sent only when the second changes)
    if fullT is not None and time.time() - last_pos_q > 0.5:
        last_pos_q = time.time()
        pos = position()
        if pos is not None and (pos, track_len) != lastP:
            lastP = (pos, track_len)
            send(f'P|{pos}|{track_len}')
    # idle: Spotify closed/nothing — Strasbourg clock + weather screen
    parts0 = msg.split('|') if msg else []
    if not msg or not parts0[0].strip():
        cur_min = time.strftime('%H:%M')
        if not idle_on or cur_min != last_idle_min or need_full:
            idle_on = True
            last_idle_min = cur_min
            need_full = False
            temp, code = weather()
            send('M|0|Idle')
            time.sleep(0.05)
            send(f'W|{cur_min}|{temp if temp is not None else 99}|{code}')
    else:
        idle_on = False
    time.sleep(0.1)
