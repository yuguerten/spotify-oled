#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Text bitmap zones (rendered on PC with full Arabic shaping, pushed over serial)
#define TW 54
#define TH 14
#define AW 54
#define AH 11
#define TBYTES (((TW + 7) / 8) * TH)   // 98
#define ABYTES (((AW + 7) / 8) * AH)   // 77
// Album art thumbnail (PC downloads + dithers the cover)
#define CW 32
#define CH 32
#define CBYTES (((CW + 7) / 8) * CH)   // 128

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t titleBmp[TBYTES];
uint8_t artistBmp[ABYTES];
uint8_t coverBmp[CBYTES];
bool hasT = false;
bool hasA = false;
bool hasC = false;
bool isPaused = false;
int danceStyle = 0; // 0 disco, 1 shuffle, 2 robot, 3 bounce
long trackPos = 0;  // seconds elapsed (from PC)
long trackLen = 0;  // seconds total (from PC)
unsigned long lastMsg = 0;
unsigned long danceT = 0;

// Mini club room: disco ball + rays + speaker + blinking floor + floating notes
void drawPartyBg(int step) {
  int f = step % 4;
  // Disco ball hanging above (clear of heads)
  display.drawLine(110, 12, 110, 13, SSD1306_WHITE);
  display.drawCircle(110, 16, 3, SSD1306_WHITE);
  display.drawPixel(111, 15, SSD1306_WHITE);
  // Light rays blink sideways (never through faces)
  if (f == 0 || f == 2) {
    display.drawLine(105, 16, 99, 16, SSD1306_WHITE);
    display.drawLine(115, 16, 121, 16, SSD1306_WHITE);
  } else {
    display.drawLine(105, 16, 101, 16, SSD1306_WHITE);
    display.drawLine(115, 16, 119, 16, SSD1306_WHITE);
  }
  // Speaker stack (left of the dancer, clear of text)
  display.drawRect(90, 42, 8, 8, SSD1306_WHITE);
  display.drawCircle(94, 44, 2, SSD1306_WHITE);
  display.drawCircle(94, 48, 1, SSD1306_WHITE);
  // Beat equalizer along the bottom: bars bounce with the groove
  // (decorative — no audio line wired — kicked by the dance frames)
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  int beat = (step % 4 == 1) ? 3 : 0; // kick drum on the bounce frame
  for (int x = 0, i = 0; x < 126; x += 6, i++) {
    float h = 2.0
      + 3.5 * (0.5 + 0.5 * sin(step * 0.9 + i * 1.1))
      + 2.5 * (0.5 + 0.5 * sin(step * 2.1 + i * 2.7));
    int bh = (int)(h + 0.5) + (i % 3 == 0 ? beat : 0);
    if (bh < 1) bh = 1;
    if (bh > 10) bh = 10;
    display.fillRect(x, 63 - bh, 3, bh, SSD1306_WHITE);
  }
  // Floating notes bob smoothly (no popping)
  int ph1 = (step * 2) % 32;
  int n1y = (ph1 < 16) ? (32 + ph1) : (48 - (ph1 - 16));
  int ph2 = (ph1 + 16) % 32;
  int n2y = (ph2 < 16) ? (32 + ph2) : (48 - (ph2 - 16));
  display.drawLine(124, n1y - 5, 124, n1y, SSD1306_WHITE);
  display.fillCircle(122, n1y, 2, SSD1306_WHITE);
  display.drawLine(95, n2y - 5, 95, n2y, SSD1306_WHITE);
  display.fillCircle(93, n2y, 2, SSD1306_WHITE);
}

// SHUFFLE: running-man energy, slides side to side, arms pumping
void drawShuffle(int cx, int cy, int step) {
  int f = step % 4;
  int dir = (f < 2) ? -1 : 1;
  int slide = (f % 2 == 1) ? 3 * dir : 0;
  int lift = (f % 2 == 1) ? -3 : 0;
  int shx = cx + slide, shy = cy - 6, hipx = cx + slide, hipy = cy + 4;
  display.drawCircle(cx + slide, cy - 12, 3, SSD1306_WHITE);
  display.drawLine(cx + slide - 1, cy - 11, cx + slide + 1, cy - 11, SSD1306_WHITE);
  display.drawLine(shx, shy, hipx, hipy, SSD1306_WHITE);
  // arms pumping opposite to legs
  display.drawLine(shx, shy, shx - 5 * dir, shy - 4, SSD1306_WHITE);
  display.drawLine(shx - 5 * dir, shy - 4, shx - 5 * dir, shy, SSD1306_WHITE);
  display.drawLine(shx, shy, shx + 5 * dir, shy + 4, SSD1306_WHITE);
  display.drawLine(shx + 5 * dir, shy + 4, shx + 2 * dir, shy + 6, SSD1306_WHITE);
  // legs scissor fast, one knee lifts
  display.drawLine(hipx, hipy, hipx - 6 * dir, hipy + 9, SSD1306_WHITE);
  display.drawLine(hipx, hipy, hipx + 4 * dir, hipy + 9 + lift, SSD1306_WHITE);
}

// ROBOT: stiff 90-degree moves, holds each pose (jerky)
void drawRobot(int cx, int cy, int step) {
  int f = (step / 2) % 2; // half speed = mechanical
  // Square head
  display.drawRect(cx - 3, cy - 15, 6, 6, SSD1306_WHITE);
  display.drawPixel(cx - 1, cy - 13, SSD1306_WHITE);
  display.drawPixel(cx + 1, cy - 13, SSD1306_WHITE);
  display.drawLine(cx - 2, cy - 11, cx + 2, cy - 11, SSD1306_WHITE);
  // Straight spine
  display.drawLine(cx, cy - 9, cx, cy + 4, SSD1306_WHITE);
  if (f == 0) {
    display.drawLine(cx, cy - 6, cx - 8, cy - 6, SSD1306_WHITE);
    display.drawLine(cx - 8, cy - 6, cx - 8, cy - 1, SSD1306_WHITE);
    display.drawLine(cx, cy - 6, cx + 8, cy - 6, SSD1306_WHITE);
    display.drawLine(cx + 8, cy - 6, cx + 8, cy - 11, SSD1306_WHITE);
    display.drawLine(cx, cy + 4, cx - 6, cy + 4, SSD1306_WHITE);
    display.drawLine(cx - 6, cy + 4, cx - 6, cy + 12, SSD1306_WHITE);
    display.drawLine(cx, cy + 4, cx, cy + 12, SSD1306_WHITE);
  } else {
    display.drawLine(cx, cy - 6, cx - 8, cy - 6, SSD1306_WHITE);
    display.drawLine(cx - 8, cy - 6, cx - 8, cy - 11, SSD1306_WHITE);
    display.drawLine(cx, cy - 6, cx + 8, cy - 6, SSD1306_WHITE);
    display.drawLine(cx + 8, cy - 6, cx + 8, cy - 1, SSD1306_WHITE);
    display.drawLine(cx, cy + 4, cx, cy + 12, SSD1306_WHITE);
    display.drawLine(cx, cy + 4, cx + 6, cy + 4, SSD1306_WHITE);
    display.drawLine(cx + 6, cy + 4, cx + 6, cy + 12, SSD1306_WHITE);
  }
}

// BOUNCE: big jump, arms in V, knees tuck at the peak
void drawBounce(int cx, int cy, int step) {
  int f = step % 4;
  int jump = (f == 0) ? 0 : (f == 1) ? -4 : (f == 2) ? -2 : 0;
  int spread = (f == 1) ? 8 : 6;
  int hx = cx, hy = cy - 12 + jump;
  display.drawCircle(hx, hy, 3, SSD1306_WHITE);
  display.drawLine(hx - 3, hy - 1, hx - 2, hy - 2, SSD1306_WHITE);
  display.drawLine(hx + 3, hy - 1, hx + 2, hy - 2, SSD1306_WHITE);
  display.drawLine(hx - 1, hy + 1, hx + 1, hy + 1, SSD1306_WHITE);
  display.drawLine(cx, cy - 6 + jump, cx, cy + 4 + jump, SSD1306_WHITE);
  // Big V arms
  display.drawLine(cx, cy - 4 + jump, cx - spread, cy - 12 + jump, SSD1306_WHITE);
  display.drawLine(cx, cy - 4 + jump, cx + spread, cy - 12 + jump, SSD1306_WHITE);
  if (f == 1) {
    // knees tucked at jump peak
    display.drawLine(cx, cy + 4 + jump, cx - 4, cy + 7 + jump, SSD1306_WHITE);
    display.drawLine(cx - 4, cy + 7 + jump, cx - 2, cy + 10 + jump, SSD1306_WHITE);
    display.drawLine(cx, cy + 4 + jump, cx + 4, cy + 7 + jump, SSD1306_WHITE);
    display.drawLine(cx + 4, cy + 7 + jump, cx + 2, cy + 10 + jump, SSD1306_WHITE);
  } else {
    display.drawLine(cx, cy + 4 + jump, cx - 5, cy + 12 + jump, SSD1306_WHITE);
    display.drawLine(cx, cy + 4 + jump, cx + 5, cy + 12 + jump, SSD1306_WHITE);
  }
}

// DISCO (style 0): lean + bent elbows/knees, finger point
void drawDancer(int cx, int cy, int step) {
  if (danceStyle == 1) { drawShuffle(cx, cy, step); return; }
  if (danceStyle == 2) { drawRobot(cx, cy, step); return; }
  if (danceStyle == 3) { drawBounce(cx, cy, step); return; }
  int f = step % 4;
  int lean = (f == 0) ? -3 : (f == 2) ? 3 : 0;
  int bounce = (f == 1) ? -2 : (f == 3) ? 1 : 0;
  int shx = cx + lean;          // shoulder
  int shy = cy - 6 + bounce;
  int hipx = cx;
  int hipy = cy + 4;
  // Head (with smile, tilted with lean)
  display.drawCircle(cx + lean, cy - 12 + bounce, 3, SSD1306_WHITE);
  display.drawPixel(cx + lean - 1, cy - 13 + bounce, SSD1306_WHITE);
  display.drawPixel(cx + lean + 1, cy - 13 + bounce, SSD1306_WHITE);
  display.drawLine(cx + lean - 1, cy - 11 + bounce, cx + lean + 1, cy - 11 + bounce, SSD1306_WHITE);
  // Body (leans with groove)
  display.drawLine(shx, shy, hipx, hipy, SSD1306_WHITE);
  if (f == 0) { // disco point left: right arm up, left hand on hip
    display.drawLine(shx, shy, shx + 4, shy - 3, SSD1306_WHITE);
    display.drawLine(shx + 4, shy - 3, shx + 8, shy - 11, SSD1306_WHITE);
    display.drawLine(shx + 8, shy - 11, shx + 10, shy - 11, SSD1306_WHITE); // finger point
    display.drawLine(shx, shy, shx - 5, shy + 2, SSD1306_WHITE);
    display.drawLine(shx - 5, shy + 2, shx - 2, shy + 5, SSD1306_WHITE);
    // Legs: left straight, right knee bent out
    display.drawLine(hipx, hipy, hipx - 4, hipy + 9, SSD1306_WHITE);
    display.drawLine(hipx, hipy, hipx + 5, hipy + 4, SSD1306_WHITE);
    display.drawLine(hipx + 5, hipy + 4, hipx + 8, hipy + 9, SSD1306_WHITE);
  } else if (f == 1) { // groove bounce: both elbows bent up
    display.drawLine(shx, shy, shx - 6, shy - 1, SSD1306_WHITE);
    display.drawLine(shx - 6, shy - 1, shx - 7, shy - 6, SSD1306_WHITE);
    display.drawLine(shx, shy, shx + 6, shy - 1, SSD1306_WHITE);
    display.drawLine(shx + 6, shy - 1, shx + 7, shy - 6, SSD1306_WHITE);
    // Legs: knees bent (dip)
    display.drawLine(hipx, hipy, hipx - 5, hipy + 4, SSD1306_WHITE);
    display.drawLine(hipx - 5, hipy + 4, hipx - 4, hipy + 9, SSD1306_WHITE);
    display.drawLine(hipx, hipy, hipx + 5, hipy + 4, SSD1306_WHITE);
    display.drawLine(hipx + 5, hipy + 4, hipx + 4, hipy + 9, SSD1306_WHITE);
  } else if (f == 2) { // disco point right (mirror)
    display.drawLine(shx, shy, shx - 4, shy - 3, SSD1306_WHITE);
    display.drawLine(shx - 4, shy - 3, shx - 8, shy - 11, SSD1306_WHITE);
    display.drawLine(shx - 8, shy - 11, shx - 10, shy - 11, SSD1306_WHITE);
    display.drawLine(shx, shy, shx + 5, shy + 2, SSD1306_WHITE);
    display.drawLine(shx + 5, shy + 2, shx + 2, shy + 5, SSD1306_WHITE);
    display.drawLine(hipx, hipy, hipx + 4, hipy + 9, SSD1306_WHITE);
    display.drawLine(hipx, hipy, hipx - 5, hipy + 4, SSD1306_WHITE);
    display.drawLine(hipx - 5, hipy + 4, hipx - 8, hipy + 9, SSD1306_WHITE);
  } else { // snap down: arms low, head nod, feet together
    display.drawLine(shx, shy, shx - 6, shy + 3, SSD1306_WHITE);
    display.drawLine(shx - 6, shy + 3, shx - 4, shy + 6, SSD1306_WHITE);
    display.drawLine(shx, shy, shx + 6, shy + 3, SSD1306_WHITE);
    display.drawLine(shx + 6, shy + 3, shx + 4, shy + 6, SSD1306_WHITE);
    display.drawLine(hipx, hipy, hipx - 3, hipy + 9, SSD1306_WHITE);
    display.drawLine(hipx, hipy, hipx + 3, hipy + 9, SSD1306_WHITE);
  }
}

// Polite-cat PLEASE meme for the PAUSED screen (40x36 mono, left side)
static const uint8_t PAUSED_IMG[] = {
  0x00, 0x20, 0x00, 0x00, 0x01, 0x00, 0x58, 0x00, 0x04, 0x55, 0x00, 0x48,
  0x92, 0x41, 0x0A, 0x01, 0x36, 0x00, 0x10, 0x2A, 0x04, 0x54, 0x21, 0x04,
  0x55, 0x00, 0xAA, 0x84, 0x41, 0x2A, 0x00, 0x2D, 0x50, 0x14, 0x5A, 0x00,
  0xB7, 0xFF, 0xC0, 0x55, 0x08, 0x9A, 0xBF, 0x80, 0xAB, 0x00, 0x6D, 0xFF,
  0xE0, 0x55, 0x00, 0xB5, 0xBF, 0xF4, 0x2E, 0x00, 0xAB, 0xFF, 0xF8, 0xAA,
  0x08, 0xB5, 0x7F, 0xDA, 0x14, 0x00, 0xE8, 0x7F, 0xFC, 0x16, 0x01, 0xD0,
  0x37, 0x60, 0x2A, 0x03, 0x70, 0x7F, 0xC0, 0x8C, 0x13, 0xF8, 0x3E, 0xC0,
  0x09, 0x03, 0x7A, 0xB7, 0x81, 0x05, 0x05, 0xBF, 0xFA, 0xD0, 0x82, 0x03,
  0x6F, 0xAF, 0xFF, 0x43, 0x05, 0xBA, 0xD5, 0x5F, 0xA1, 0x12, 0xDF, 0xAC,
  0xF7, 0xD1, 0x03, 0x6A, 0xD4, 0xBD, 0x73, 0x02, 0xAB, 0x65, 0xEF, 0x79,
  0x23, 0x55, 0x56, 0xB5, 0xAF, 0x05, 0x50, 0x91, 0x55, 0x7B, 0x02, 0xAA,
  0x45, 0x4A, 0xAF, 0x02, 0xA5, 0x28, 0x25, 0x7F, 0x02, 0xB5, 0x55, 0x92,
  0xB7, 0x12, 0xAA, 0xAA, 0x4A, 0xDF, 0x02, 0xAA, 0xAA, 0xAA, 0xBF, 0x03,
  0x55, 0x49, 0x25, 0x6F, 0x05, 0x5A, 0xA4, 0x95, 0xBF, 0x02, 0xA5, 0x2A,
  0x56, 0xEF, 0x05, 0xAE, 0xD6, 0xB5, 0xBF, 0x02, 0xAF, 0x5E, 0x9B, 0xFF,
};

// Parse "M|style|status", "T|hex", "A|hex" lines from the PC sender
bool parseHex(const String &hex, uint8_t *buf, int n) {
  if ((int)hex.length() < n * 2) return false;
  for (int i = 0; i < n; i++) {
    char a = hex.charAt(i * 2), b = hex.charAt(i * 2 + 1);
    int va = (a >= '0' && a <= '9') ? a - '0' : (a >= 'a' && a <= 'f') ? a - 'a' + 10 : (a >= 'A' && a <= 'F') ? a - 'A' + 10 : 0;
    int vb = (b >= '0' && b <= '9') ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : (b >= 'A' && b <= 'F') ? b - 'A' + 10 : 0;
    buf[i] = (va << 4) | vb;
  }
  return true;
}

// m:ss formatter for the time readout
void fmtTime(char *buf, long s) {
  if (s < 0) s = 0;
  sprintf(buf, "%ld:%02ld", s / 60, s % 60);
}

// 3x5 micro digits for the elapsed/remaining readout
static const uint8_t MICRO[12][5] = {
  {0x7,0x5,0x5,0x5,0x7}, // 0
  {0x2,0x6,0x2,0x2,0x7}, // 1
  {0x7,0x1,0x7,0x4,0x7}, // 2
  {0x7,0x1,0x7,0x1,0x7}, // 3
  {0x5,0x5,0x7,0x1,0x1}, // 4
  {0x7,0x4,0x7,0x1,0x7}, // 5
  {0x7,0x4,0x7,0x5,0x7}, // 6
  {0x7,0x1,0x1,0x2,0x2}, // 7
  {0x7,0x5,0x7,0x5,0x7}, // 8
  {0x7,0x5,0x7,0x1,0x7}, // 9
  {0x0,0x2,0x0,0x2,0x0}, // :
  {0x0,0x0,0x7,0x0,0x0}, // -
};
void drawMicroChar(int x, int y, char c, uint16_t color) {
  int idx = -1;
  if (c >= '0' && c <= '9') idx = c - '0';
  else if (c == ':') idx = 10;
  else if (c == '-') idx = 11;
  if (idx < 0) return;
  for (int r = 0; r < 5; r++)
    for (int col = 0; col < 3; col++)
      if (MICRO[idx][r] & (0x4 >> col)) display.drawPixel(x + col, y + r, color);
}
void drawMicroText(int x, int y, const char *s, uint16_t color) {
  while (*s) { drawMicroChar(x, y, *s, color); x += 4; s++; }
}
int microWidth(const char *s) { return strlen(s) * 4 - 1; }

void drawSpotify() {
  display.clearDisplay();
  // Header
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(4, 2);
  display.println("Spotify");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  if (!hasT && !hasA && !isPaused) {
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 28);
    display.println("Waiting...");
    display.setCursor(10, 38);
    display.println("Play on Spotify");
    // Play triangle in header (idle — press play)
    display.fillTriangle(104, 2, 104, 9, 111, 5, SSD1306_BLACK);
    // Club room + full-body dancer idling while waiting
    drawPartyBg(millis() / 400);
    drawDancer(110, 38, millis() / 400);
  } else if (isPaused) {
    // PAUSED: 3afak kid + begging text (drawn emoji retired)
    // Play triangle in header (hit play!)
    display.fillTriangle(104, 2, 104, 9, 111, 5, SSD1306_BLACK);
    display.drawBitmap(0, 14, PAUSED_IMG, 40, 36, SSD1306_WHITE);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(42, 14);
    display.println("PLEASE?");
    display.setTextSize(1);
    display.setCursor(44, 29);
    display.println("cmon bud");
    display.setCursor(44, 36);
    display.println("play music");
    display.setCursor(44, 43);
    display.println("lahi7fdak");
    // EQ flatlines while paused — no bounce without music
    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    for (int x = 0; x < 126; x += 6) display.fillRect(x, 61, 3, 2, SSD1306_WHITE);
  } else {
    // PLAYING: classic now-playing — art frame, bold title, artist, times, round bar
    // Pause icon in header (music is playing)
    display.fillRect(104, 3, 3, 6, SSD1306_BLACK);
    display.fillRect(109, 3, 3, 6, SSD1306_BLACK);
    // Album art thumbnail (left), note placeholder until the cover arrives
    display.drawRect(0, 13, 34, 34, SSD1306_WHITE);
    if (hasC) {
      display.drawBitmap(1, 14, coverBmp, CW, CH, SSD1306_WHITE);
    } else {
      display.fillCircle(12, 33, 2, SSD1306_WHITE);
      display.fillCircle(22, 33, 2, SSD1306_WHITE);
      display.drawLine(14, 33, 14, 25, SSD1306_WHITE);
      display.drawLine(24, 33, 24, 25, SSD1306_WHITE);
      display.drawLine(14, 25, 24, 25, SSD1306_WHITE);
    }
    if (hasT) display.drawBitmap(36, 14, titleBmp, TW, TH, SSD1306_WHITE);
    if (hasA) display.drawBitmap(36, 29, artistBmp, AW, AH, SSD1306_WHITE);
    // Times row (micro digits): elapsed left, remaining right
    char t1[8], t2[8];
    fmtTime(t1, trackPos);
    fmtTime(t2, trackLen > trackPos ? trackLen - trackPos : 0);
    drawMicroText(36, 42, t1, SSD1306_WHITE);
    char rem[10];
    rem[0] = '-';
    strcpy(rem + 1, t2);
    drawMicroText(90 - microWidth(rem), 42, rem, SSD1306_WHITE);
    // Rounded progress bar
    display.drawRoundRect(0, 47, 90, 4, 2, SSD1306_WHITE);
    if (trackLen > 0) {
      int fw = (int)((trackPos * 86L) / trackLen);
      if (fw > 86) fw = 86;
      if (fw > 0) display.fillRect(2, 48, fw, 2, SSD1306_WHITE);
    }
    // Club room + full-body dancer freestanding on the right
    drawPartyBg(millis() / 250);
    drawDancer(110, 38, millis() / 250);
  }
  display.display();
}

void setup() {
  Serial.setRxBufferSize(1024); // bitmap hex lines are ~300 chars
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 failed");
    for (;;);
  }
  drawSpotify();
  Serial.println("ready: send M|.. T|hex A|hex C|hex P|.. over serial");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 2 && line.charAt(1) == '|') {
      char kind = line.charAt(0);
      String payload = line.substring(2);
      if (kind == 'M') {
        // M|style|status
        int sep = payload.indexOf('|');
        danceStyle = payload.substring(0, sep > 0 ? sep : payload.length()).toInt() % 4;
        if (sep > 0) {
          String st = payload.substring(sep + 1);
          st.trim();
          isPaused = st.equalsIgnoreCase("Paused") || st.equalsIgnoreCase("Pause");
        }
        drawSpotify();
      } else if (kind == 'T') {
        if (parseHex(payload, titleBmp, TBYTES)) { hasT = true; drawSpotify(); }
      } else if (kind == 'A') {
        if (parseHex(payload, artistBmp, ABYTES)) { hasA = true; drawSpotify(); }
      } else if (kind == 'C') {
        if (parseHex(payload, coverBmp, CBYTES)) { hasC = true; drawSpotify(); }
      } else if (kind == 'P') {
        // P|posSec|lenSec — progress clock, no redraw reset
        int sep = payload.indexOf('|');
        trackPos = payload.substring(0, sep > 0 ? sep : payload.length()).toInt();
        if (sep > 0) trackLen = payload.substring(sep + 1).toInt();
      }
    }
    lastMsg = millis();
  }
  // dance tick: redraw ~4fps so the room keeps moving
  if (millis() - danceT > 250) {
    danceT = millis();
    drawSpotify();
  }
  delay(20);
}
