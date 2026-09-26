#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Text bitmap zones (rendered on PC with full Arabic shaping, pushed over serial)
#define TW 88
#define TH 14
#define AW 88
#define AH 11
#define TBYTES (TW * TH / 8)   // 154
#define ABYTES (AW * AH / 8)   // 121 (row-major, MSB first per Adafruit)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t titleBmp[TBYTES];
uint8_t artistBmp[ABYTES];
bool hasT = false;
bool hasA = false;
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
  // Dance-floor strip along the bottom (checker blinks)
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  for (int x = 0; x < 128; x += 5) {
    if (((x / 5) + f) % 2 == 0) {
      display.fillRect(x, 53, 4, 3, SSD1306_WHITE);
    } else {
      display.drawRect(x, 53, 4, 3, SSD1306_WHITE);
    }
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

// BIG sad face close-up for PAUSED — no body, all expression
void drawSadFace(int cx, int cy, int step) {
  int f = step % 4;
  int shake = (f % 2 == 0) ? 1 : -1; // trembling with upset
  int bx = cx + shake;
  // Headphone band over the top
  for (int a = 200; a <= 340; a += 6) {
    float rad = a * PI / 180.0;
    display.drawPixel(bx + 13 * cos(rad), cy - 1 + 13 * sin(rad), SSD1306_WHITE);
  }
  // Ear cups
  display.fillRect(bx - 15, cy - 4, 4, 8, SSD1306_WHITE);
  display.fillRect(bx + 11, cy - 4, 4, 8, SSD1306_WHITE);
  // Head (big, fully visible — nothing hanging over it)
  display.drawCircle(bx, cy, 11, SSD1306_WHITE);
  // Angry brows slanting hard down toward center
  display.drawLine(bx - 7, cy - 8, bx - 2, cy - 5, SSD1306_WHITE);
  display.drawLine(bx - 7, cy - 7, bx - 2, cy - 4, SSD1306_WHITE);
  display.drawLine(bx + 7, cy - 8, bx + 2, cy - 5, SSD1306_WHITE);
  display.drawLine(bx + 7, cy - 7, bx + 2, cy - 4, SSD1306_WHITE);
  // Droopy sad eyes looking down
  display.fillCircle(bx - 4, cy - 1, 2, SSD1306_WHITE);
  display.fillCircle(bx + 4, cy - 1, 2, SSD1306_WHITE);
  display.fillCircle(bx - 4, cy - 1, 1, SSD1306_BLACK);
  display.fillCircle(bx + 4, cy - 1, 1, SSD1306_BLACK);
  // Tear stream blinking under right eye
  if (f < 2) {
    display.drawLine(bx + 5, cy + 1, bx + 5, cy + 5, SSD1306_WHITE);
    display.drawPixel(bx + 5, cy + 6, SSD1306_WHITE);
  }
  // Anger veins popping on both temples
  display.drawLine(bx - 13, cy - 8, bx - 11, cy - 6, SSD1306_WHITE);
  display.drawLine(bx - 11, cy - 8, bx - 13, cy - 6, SSD1306_WHITE);
  display.drawLine(bx + 11, cy - 8, bx + 13, cy - 6, SSD1306_WHITE);
  display.drawLine(bx + 13, cy - 8, bx + 11, cy - 6, SSD1306_WHITE);
  // Mouth: deep frown <-> open wail begging for music
  if (f == 1 || f == 3) {
    display.drawCircle(bx, cy + 6, 3, SSD1306_WHITE); // wailing "play it!!"
  } else {
    display.drawLine(bx - 4, cy + 7, bx + 4, cy + 7, SSD1306_WHITE);
    display.drawLine(bx - 4, cy + 7, bx - 5, cy + 4, SSD1306_WHITE);
    display.drawLine(bx + 4, cy + 7, bx + 5, cy + 4, SSD1306_WHITE);
  }
}

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

// m:ss formatter for the header clock
void fmtTime(char *buf, long s) {
  if (s < 0) s = 0;
  sprintf(buf, "%ld:%02ld", s / 60, s % 60);
}

void drawSpotify() {
  display.clearDisplay();
  // Header
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(4, 2);
  display.println("SPOTIFY");
  // Note icon: beamed pair of eighth notes
  display.fillCircle(110, 9, 2, SSD1306_BLACK);
  display.fillCircle(119, 9, 2, SSD1306_BLACK);
  display.drawLine(112, 9, 112, 2, SSD1306_BLACK);
  display.drawLine(121, 9, 121, 2, SSD1306_BLACK);
  display.drawLine(112, 2, 121, 2, SSD1306_BLACK);
  display.drawLine(112, 3, 121, 3, SSD1306_BLACK);

  if (!hasT && !hasA && !isPaused) {
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 28);
    display.println("Waiting...");
    display.setCursor(10, 38);
    display.println("Play on Spotify");
    // Club room + full-body dancer idling while waiting
    drawPartyBg(millis() / 400);
    drawDancer(110, 38, millis() / 400);
  } else if (isPaused) {
    // PAUSED: no body — just the big upset face begging for music
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 18);
    display.println("cmon bud");
    display.setCursor(4, 28);
    display.println("play music");
    display.setCursor(4, 38);
    display.println("lahi7fdak");
    drawSadFace(106, 35, millis() / 500);
  } else {
    // PLAYING: player interface — inverted title card + artist + progress
    display.fillRoundRect(0, 14, 90, 17, 2, SSD1306_WHITE);
    if (hasT) display.drawBitmap(1, 15, titleBmp, TW, TH, SSD1306_BLACK);
    if (hasA) display.drawBitmap(0, 33, artistBmp, AW, AH, SSD1306_WHITE);
    // Progress bar
    display.drawRect(0, 46, 88, 4, SSD1306_WHITE);
    if (trackLen > 0) {
      int fw = (int)((trackPos * 84L) / trackLen);
      if (fw > 84) fw = 84;
      if (fw > 0) display.fillRect(2, 47, fw, 2, SSD1306_WHITE);
    }
    // Header clock: elapsed/total
    char t1[8], t2[8];
    fmtTime(t1, trackPos);
    fmtTime(t2, trackLen);
    display.setTextSize(1);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(52, 2);
    display.print(t1);
    display.print("/");
    display.print(t2);
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
  Serial.println("ready: send M|style|status, T|hex, A|hex over serial");
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
