// ESP8266 PC Companion - 0.96" OLED (SSD1306 128x64, I2C) + touch sensor
//
// Pages (tap on a normal page = next page):
//   0 MONITOR     FPS, CPU temp, GPU temp
//   1 MUSIC       tap = play/pause | hold = next song | keep holding 2 s = leave
//   2 SCREENSAVER tap = next page  | hold = next animation (5 animations)
//   3 POWER       tap = move       | hold = select (asks YES/NO) | keep holding 2 s = leave
//   4 CLOCK       tap = next page
//
// Wiring (NodeMCU / Wemos D1 mini):
//   OLED  SDA -> D2 (GPIO4)   SCL -> D1 (GPIO5)   VCC -> 3V3   GND -> GND
//   Touch OUT -> D5 (GPIO14)  VCC -> 3V3          GND -> GND
//
// Libraries: Adafruit SSD1306, Adafruit GFX
// Data in (USB serial 115200) from pc_companion.py:
//   fps|cpuTemp|gpuTemp|HH:MM|DD Mon|playing|title|artist|posSec|durSec\n
// Commands out to the PC: CMD:PLAY CMD:NEXT CMD:SLEEP CMD:SHUTDOWN

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN    D2
#define SCL_PIN    D1
#define TOUCH_PIN  D5

#define PG_MONITOR  0
#define PG_MUSIC    1
#define PG_SAVER    2
#define PG_POWER    3
#define PG_CLOCK    4
#define NUM_PAGES   5

#define NEXT_MS    600     // hold this long = "select / next"
#define EXIT_MS    2000    // keep holding this long = leave a locked page
#define NUM_ANIM   5

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ---------- PC data ----------
int fps = 0, cpuT = 0, gpuT = 0;
bool playing = false;
String clockStr = "--:--", dateStr = "";
String title = "Nothing playing", artist = "";
int posSec = 0, durSec = 0;
unsigned long posStamp = 0;
unsigned long lastData = 0;
unsigned long ignorePlayUntil = 0;    // after a local action, ignore stale PC play state

// ---------- Power menu ----------
int powSel = 0;                       // 0 = sleep, 1 = shutdown
bool powConfirm = false;
int confSel = 0;                      // 0 = NO, 1 = YES
unsigned long powSentUntil = 0;

// ---------- UI state ----------
int page = 0;
unsigned long pageShownAt = 0;
unsigned long lastTouch = 0;
bool touching = false, consumed = false;
unsigned long pressStart = 0;
int flashIcon = 0;                    // music page: 1 = play/pause button, 2 = next button
unsigned long flashUntil = 0;
float vinylAngle = 0;
unsigned long lastFrame = 0;
int animIdx = 0;
unsigned long animNameUntil = 0;
const char* pageNames[NUM_PAGES] = {"MONITOR", "MUSIC", "SCREENSAVER", "POWER", "CLOCK"};
const char* animNames[NUM_ANIM]  = {"STARS", "MATRIX", "CUBE", "RADAR", "WAVES"};

bool lockedPage(int p)  { return p == PG_MUSIC || p == PG_POWER; }
bool releasePage(int p) { return lockedPage(p) || p == PG_SAVER; }   // actions happen on release

// ---------- Icons ----------
void iconCPU(int x, int y) {
  display.drawRect(x + 2, y + 2, 8, 8, WHITE);
  display.fillRect(x + 4, y + 4, 4, 4, WHITE);
  for (int i = 0; i < 3; i++) {
    display.drawFastVLine(x + 3 + i * 3, y, 2, WHITE);
    display.drawFastVLine(x + 3 + i * 3, y + 10, 2, WHITE);
    display.drawFastHLine(x, y + 3 + i * 3, 2, WHITE);
    display.drawFastHLine(x + 10, y + 3 + i * 3, 2, WHITE);
  }
}
void iconGPU(int x, int y) {
  display.drawRect(x, y, 14, 9, WHITE);
  display.drawCircle(x + 7, y + 4, 3, WHITE);
  display.drawPixel(x + 7, y + 4, WHITE);
  display.drawFastHLine(x + 1, y + 10, 4, WHITE);
}
void iconFPS(int x, int y) {
  display.drawRect(x, y, 14, 9, WHITE);
  display.drawFastHLine(x + 4, y + 11, 6, WHITE);
  display.drawFastVLine(x + 7, y + 9, 2, WHITE);
  display.drawLine(x + 2, y + 6, x + 5, y + 3, WHITE);
  display.drawLine(x + 5, y + 3, x + 8, y + 6, WHITE);
  display.drawLine(x + 8, y + 6, x + 11, y + 2, WHITE);
}

// ---------- Helpers ----------
void drawCentered(const String& s, int cx, int y, int size) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(cx - w / 2, y);
  display.print(s);
}

String fmtTime(int s) {
  if (s < 0) s = 0;
  char b[16];
  snprintf(b, sizeof(b), "%d:%02d", s / 60, s % 60);
  return String(b);
}

int curPos() {
  int p = posSec;
  if (playing && posStamp) p += (millis() - posStamp) / 1000;
  if (durSec > 0 && p > durSec) p = durSec;
  return p;
}

// Finger-down time on a locked page (0 when not holding)
unsigned long heldNow(unsigned long now) {
  return (touching && lockedPage(page) && !consumed) ? now - pressStart : 0;
}

void drawHoldOverlay(unsigned long held, const char* afterNext) {
  display.fillRect(0, 51, 128, 13, BLACK);
  drawCentered(held < NEXT_MS ? "HOLD..." : afterNext, 64, 51, 1);
  display.drawRect(0, 60, 128, 4, WHITE);
  unsigned long hh = held > EXIT_MS ? EXIT_MS : held;
  display.fillRect(1, 61, (int)(126UL * hh / EXIT_MS), 2, WHITE);
  display.drawFastVLine(1 + (int)(126UL * NEXT_MS / EXIT_MS), 57, 3, WHITE);   // "select" mark
}

void drawHeader(const char* text) {
  display.setTextSize(1);
  display.setCursor(0, 1);
  display.print(text);
  display.drawFastHLine(0, 11, 128, WHITE);
}

// Size-1 text inside the region starting at x0 (width w). Scrolls right-to-left if too long.
void drawMarqueeAt(const String& s, int x0, int y, int w, unsigned long t) {
  int16_t x1, y1; uint16_t tw, th;
  display.setTextSize(1);
  display.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
  if ((int)tw <= w) {
    display.setCursor(x0 + (w - (int)tw) / 2, y);
    display.print(s);
    return;
  }
  int gap = 24;
  int offset = (t / 45) % (tw + gap);
  display.setCursor(x0 - offset, y);
  display.print(s);
  display.setCursor(x0 - offset + tw + gap, y);
  display.print(s);
  display.fillRect(0, y, x0, 8, BLACK);   // hide text that slid under the vinyl
}

// ---------- Page 0: Hardware monitor ----------
void drawMonitor() {
  bool online = (millis() - lastData) < 3000;

  display.drawRoundRect(0, 0, 128, 32, 4, WHITE);
  iconFPS(6, 6);
  display.setTextSize(1);
  display.setCursor(8, 20);
  display.print("FPS");
  if (online) {
    String f = String(fps);
    int16_t x1, y1; uint16_t w, h;
    display.setTextSize(3);
    display.getTextBounds(f, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(30 + (92 - w) / 2, 5);
    display.print(f);
  } else {
    drawCentered("NO PC", 76, 9, 2);
  }

  display.drawRoundRect(0, 35, 62, 29, 4, WHITE);
  display.drawRoundRect(66, 35, 62, 29, 4, WHITE);

  iconCPU(5, 39);
  display.setTextSize(1);
  display.setCursor(21, 41);
  display.print("CPU");
  display.setTextSize(2);
  display.setCursor(21, 48);
  display.print(online ? String(cpuT) : "--");
  display.setTextSize(1);
  display.print("C");

  iconGPU(70, 39);
  display.setTextSize(1);
  display.setCursor(88, 41);
  display.print("GPU");
  display.setTextSize(2);
  display.setCursor(88, 48);
  display.print(online ? String(gpuT) : "--");
  display.setTextSize(1);
  display.print("C");
}

// ---------- Page 1: Music ----------
void drawVinyl(int cx, int cy, float a) {
  display.drawCircle(cx, cy, 14, WHITE);
  display.drawCircle(cx, cy, 10, WHITE);
  for (int k = 0; k < 2; k++) {                  // two shine arcs that rotate
    float aa = a + k * 3.14159f;
    for (int i = 0; i < 6; i++) {
      float b = aa + i * 0.12f;
      display.drawPixel(cx + (int)(12.0f * cosf(b)), cy + (int)(12.0f * sinf(b)), WHITE);
    }
  }
  display.fillCircle(cx, cy, 4, WHITE);          // label
  display.drawPixel(cx + (int)(2.5f * cosf(a)), cy + (int)(2.5f * sinf(a)), BLACK);  // spin marker
  display.drawPixel(cx, cy, BLACK);              // spindle hole
}

void drawNoteIcon(int x, int y) {
  display.fillCircle(x + 2, y + 6, 2, WHITE);
  display.drawFastVLine(x + 4, y, 7, WHITE);
  display.drawLine(x + 4, y, x + 7, y + 2, WHITE);
}

void drawPlayPauseGlyph(int cx, int cy, bool showPause, uint16_t c) {
  if (showPause) {
    display.fillRect(cx - 3, cy - 3, 2, 7, c);
    display.fillRect(cx + 1, cy - 3, 2, 7, c);
  } else {
    display.fillTriangle(cx - 2, cy - 4, cx - 2, cy + 4, cx + 4, cy, c);
  }
}

void drawNextGlyph(int cx, int cy, uint16_t c) {
  display.fillTriangle(cx - 4, cy - 4, cx - 4, cy + 4, cx + 1, cy, c);
  display.fillRect(cx + 2, cy - 4, 2, 9, c);
}

void drawButton(int x, int w, bool flash) {
  if (flash) display.fillRoundRect(x, 53, w, 11, 3, WHITE);
  else       display.drawRoundRect(x, 53, w, 11, 3, WHITE);
}

void drawControls(unsigned long now) {
  bool fPlay = (flashIcon == 1 && now < flashUntil);
  bool fNext = (flashIcon == 2 && now < flashUntil);

  drawButton(2, 58, fPlay);                        // tap: play / pause
  drawPlayPauseGlyph(13, 58, playing, fPlay ? BLACK : WHITE);
  display.setTextSize(1);
  display.setTextColor(fPlay ? BLACK : WHITE);
  display.setCursor(24, 55);
  display.print("TAP");

  drawButton(66, 60, fNext);                       // hold: next
  drawNextGlyph(77, 58, fNext ? BLACK : WHITE);
  display.setTextColor(fNext ? BLACK : WHITE);
  display.setCursor(88, 55);
  display.print("HOLD");
  display.setTextColor(WHITE);
}

void drawMusic() {
  unsigned long now = millis();

  if (lastFrame == 0) lastFrame = now;
  if (playing) vinylAngle += (now - lastFrame) * 0.003f;
  lastFrame = now;

  drawNoteIcon(0, 1);
  display.setTextSize(1);
  display.setCursor(11, 1);
  display.print(playing ? "NOW PLAYING" : "PAUSED");
  for (int i = 0; i < 6; i++) {
    int h = playing ? 2 + (int)(7 * (0.5 + 0.5 * sinf(now / 110.0f + i * 1.5f))) : 2;
    display.fillRect(92 + i * 6, 10 - h, 4, h, WHITE);
  }
  display.drawFastHLine(0, 11, 128, WHITE);

  drawMarqueeAt(title, 38, 14, 90, now);
  drawMarqueeAt(artist, 38, 25, 90, now);

  drawVinyl(17, 28, vinylAngle);

  int pos = curPos();
  display.drawRect(38, 36, 90, 5, WHITE);
  if (durSec > 0) {
    int fw = (int)(88L * pos / durSec);
    if (fw > 0) display.fillRect(39, 37, fw, 3, WHITE);
  }
  display.setCursor(38, 43);
  display.print(fmtTime(pos));
  String total = durSec > 0 ? fmtTime(durSec) : String("--:--");
  display.setCursor(128 - total.length() * 6, 43);
  display.print(total);

  unsigned long held = heldNow(now);
  if (held > 150) drawHoldOverlay(held, "REL=NEXT  KEEP=EXIT");
  else drawControls(now);
}

// ---------- Page 3: Power ----------
void drawMoon(int cx, int cy, uint16_t fg, uint16_t bg) {
  display.fillCircle(cx, cy, 9, fg);
  display.fillCircle(cx + 5, cy - 3, 8, bg);
}
void drawPowerIcon(int cx, int cy, uint16_t fg, uint16_t bg) {
  display.drawCircle(cx, cy, 9, fg);
  display.drawCircle(cx, cy, 8, fg);
  display.fillRect(cx - 3, cy - 11, 7, 8, bg);     // gap at the top
  display.fillRect(cx - 1, cy - 10, 3, 10, fg);    // the bar
}
void drawPowerCell(int x, bool sel, const char* label, int kind) {
  uint16_t fg = sel ? BLACK : WHITE, bg = sel ? WHITE : BLACK;
  if (sel) display.fillRoundRect(x, 13, 60, 36, 5, WHITE);
  else     display.drawRoundRect(x, 13, 60, 36, 5, WHITE);
  if (kind == 0) drawMoon(x + 30, 28, fg, bg);
  else           drawPowerIcon(x + 30, 28, fg, bg);
  display.setTextColor(fg);
  drawCentered(label, x + 30, 39, 1);
  display.setTextColor(WHITE);
}
void drawYesNo(int x, bool sel, const char* label) {
  uint16_t fg = sel ? BLACK : WHITE;
  if (sel) display.fillRoundRect(x, 28, 50, 18, 4, WHITE);
  else     display.drawRoundRect(x, 28, 50, 18, 4, WHITE);
  display.setTextColor(fg);
  drawCentered(label, x + 25, 33, 1);
  display.setTextColor(WHITE);
}

void drawPower() {
  unsigned long now = millis();
  drawHeader("POWER");

  if (now < powSentUntil) {
    drawCentered("COMMAND SENT", 64, 28, 1);
  } else if (powConfirm) {
    drawCentered(powSel == 0 ? "PUT PC TO SLEEP?" : "SHUT DOWN PC?", 64, 15, 1);
    drawYesNo(8, confSel == 0, "NO");
    drawYesNo(70, confSel == 1, "YES");
  } else {
    drawPowerCell(2, powSel == 0, "SLEEP", 0);
    drawPowerCell(66, powSel == 1, "SHUTDOWN", 1);
  }

  unsigned long held = heldNow(now);
  if (held > 150) drawHoldOverlay(held, "REL=OK  KEEP=EXIT");
  else drawCentered(powConfirm ? "TAP=SWITCH  HOLD=OK" : "TAP=MOVE  HOLD=OK", 64, 53, 1);
}

// ---------- Page 2: Screensaver animations ----------
// 0 STARS: stars flying toward you
#define NSTARS 40
float sx[NSTARS], sy[NSTARS], sz[NSTARS];
bool starsInit = false;
void resetStar(int i) {
  sx[i] = random(-640, 640) / 10.0;
  sy[i] = random(-320, 320) / 10.0;
  sz[i] = random(10, 100) / 10.0;
}
void animStars() {
  if (!starsInit) { for (int i = 0; i < NSTARS; i++) resetStar(i); starsInit = true; }
  for (int i = 0; i < NSTARS; i++) {
    sz[i] -= 0.15;
    if (sz[i] <= 0.2) resetStar(i);
    int px = 64 + (int)(sx[i] / sz[i] * 4);
    int py = 32 + (int)(sy[i] / sz[i] * 4);
    if (px < 0 || px > 127 || py < 0 || py > 63) { resetStar(i); continue; }
    if (sz[i] < 3) display.fillRect(px, py, 2, 2, WHITE);
    else display.drawPixel(px, py, WHITE);
  }
}

// 1 MATRIX: falling character columns
#define MCOLS 21
float mHead[MCOLS], mSpd[MCOLS];
bool mInit = false;
void animMatrix() {
  if (!mInit) {
    for (int c = 0; c < MCOLS; c++) { mHead[c] = random(-64, 64); mSpd[c] = random(5, 16) / 10.0; }
    mInit = true;
  }
  int tick = millis() / 300;
  display.setTextSize(1);
  for (int c = 0; c < MCOLS; c++) {
    mHead[c] += mSpd[c];
    if (mHead[c] - 40 > 64) { mHead[c] = -random(0, 40); mSpd[c] = random(5, 16) / 10.0; }
    for (int k = 0; k < 5; k++) {
      int y = ((int)mHead[c] - k * 8);
      int r = y >> 3;
      y = r << 3;
      if (y <= -8 || y >= 64) continue;
      char ch = 33 + ((c * 31 + r * 17 + tick) % 60);
      if (k == 0) display.drawChar(c * 6, y, ch, BLACK, WHITE, 1);   // bright head
      else        display.drawChar(c * 6, y, ch, WHITE, BLACK, 1);
    }
  }
}

// 2 CUBE: two counter-rotating wireframe cubes
const int8_t cubeV[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
const uint8_t cubeE[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
void drawCube(float size, float ax, float ay, float az) {
  int px[8], py[8];
  float sxr = sinf(ax), cxr = cosf(ax), syr = sinf(ay), cyr = cosf(ay), szr = sinf(az), czr = cosf(az);
  for (int i = 0; i < 8; i++) {
    float x = cubeV[i][0] * size, y = cubeV[i][1] * size, z = cubeV[i][2] * size;
    float y1 = y * cxr - z * sxr, z1 = y * sxr + z * cxr; y = y1; z = z1;
    float x1 = x * cyr + z * syr, z2 = -x * syr + z * cyr; x = x1; z = z2;
    float x2 = x * czr - y * szr, y2 = x * szr + y * czr; x = x2; y = y2;
    float k = 90.0f / (90.0f + z);
    px[i] = 64 + (int)(x * k);
    py[i] = 32 + (int)(y * k);
  }
  for (int e = 0; e < 12; e++)
    display.drawLine(px[cubeE[e][0]], py[cubeE[e][0]], px[cubeE[e][1]], py[cubeE[e][1]], WHITE);
}
void animCube() {
  float t = millis() / 1000.0f;
  drawCube(13, t * 0.7f, t * 1.1f, t * 0.3f);
  drawCube(6, -t * 1.3f, -t * 0.9f, t * 0.5f);
}

// 3 RADAR: sweep with fading trail and blips that light up when passed
const float blipAng[5] = {0.7f, 2.1f, 3.4f, 4.4f, 5.5f};
const float blipR[5]   = {18, 26, 12, 22, 28};
void animRadar() {
  const int cx = 64, cy = 32;
  float ang = fmodf(millis() / 1000.0f * 1.8f, 6.28318f);
  display.drawCircle(cx, cy, 30, WHITE);
  display.drawCircle(cx, cy, 20, WHITE);
  display.drawCircle(cx, cy, 10, WHITE);
  display.drawFastHLine(cx - 30, cy, 61, WHITE);
  display.drawFastVLine(cx, cy - 30, 61, WHITE);

  for (int i = 0; i < 5; i++) {
    float diff = ang - blipAng[i];
    if (diff < 0) diff += 6.28318f;
    int bx = cx + (int)(blipR[i] * cosf(blipAng[i]));
    int by = cy + (int)(blipR[i] * sinf(blipAng[i]));
    if (diff < 0.6f)      display.fillCircle(bx, by, 2, WHITE);
    else if (diff < 1.5f) display.drawCircle(bx, by, 2, WHITE);
    else if (diff < 2.6f) display.drawPixel(bx, by, WHITE);
  }

  for (int k = 0; k < 6; k++) {                      // sweep line + dotted fading trail
    float a = ang - k * 0.07f;
    float ca = cosf(a), sa = sinf(a);
    int step = (k == 0) ? 1 : k + 1;
    for (int r = 0; r <= 30; r += step)
      display.drawPixel(cx + (int)(r * ca), cy + (int)(r * sa), WHITE);
  }
}

// 4 WAVES: three layered sine waves
void animWaves() {
  float t = millis() / 1000.0f;
  float breathe = 1.0f + 0.25f * sinf(t * 0.6f);
  for (int w = 0; w < 3; w++) {
    float A = (14 - w * 3) * breathe;
    float k = 0.06f + 0.045f * w;
    float sp = ((w % 2) ? -1.0f : 1.0f) * (1.6f + 0.5f * w);
    int prevY = 32;
    for (int x = 0; x < 128; x += 2) {
      int y = 32 + (int)(A * sinf(k * x + sp * t + w * 1.3f));
      if (x > 0) {
        display.drawLine(x - 2, prevY, x, y, WHITE);
        if (w == 0) display.drawLine(x - 2, prevY + 1, x, y + 1, WHITE);
      }
      prevY = y;
    }
  }
}

void drawScreensaver() {
  switch (animIdx) {
    case 0: animStars();  break;
    case 1: animMatrix(); break;
    case 2: animCube();   break;
    case 3: animRadar();  break;
    case 4: animWaves();  break;
  }
  if (millis() < animNameUntil) {                    // small label right after switching
    String lbl = String(animIdx + 1) + "/" + String(NUM_ANIM) + " " + animNames[animIdx];
    int w = lbl.length() * 6 + 8;
    display.fillRoundRect(64 - w / 2, 53, w, 11, 3, BLACK);
    display.drawRoundRect(64 - w / 2, 53, w, 11, 3, WHITE);
    display.setTextSize(1);
    display.setCursor(64 - w / 2 + 4, 55);
    display.print(lbl);
  }
}

// ---------- Page 4: Clock ----------
void drawClock() {
  drawCentered(clockStr, 64, 14, 3);
  drawCentered(dateStr, 64, 46, 1);
  display.drawRoundRect(0, 0, 128, 64, 6, WHITE);
}

// ---------- Serial parsing ----------
String getField(const String& s, int idx) {
  int start = 0;
  for (int i = 0; i < idx; i++) {
    start = s.indexOf('|', start);
    if (start < 0) return "";
    start++;
  }
  int end = s.indexOf('|', start);
  if (end < 0) end = s.length();
  return s.substring(start, end);
}

String lineBuf = "";
void handleLine() {
  lineBuf.trim();
  if (lineBuf.length() > 5) {                        // normal data line
    unsigned long now = millis();
    fps = getField(lineBuf, 0).toInt();
    cpuT = getField(lineBuf, 1).toInt();
    gpuT = getField(lineBuf, 2).toInt();
    clockStr = getField(lineBuf, 3);
    dateStr = getField(lineBuf, 4);
    if (now > ignorePlayUntil) playing = getField(lineBuf, 5) == "1";
    title = getField(lineBuf, 6);
    artist = getField(lineBuf, 7);
    posSec = getField(lineBuf, 8).toInt();
    durSec = getField(lineBuf, 9).toInt();
    posStamp = now;
    if (title.length() == 0) title = "Nothing playing";
    lastData = now;
  }
  lineBuf = "";
}

void readSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') handleLine();
    else if (lineBuf.length() < 200) lineBuf += ch;
  }
}

// ---------- Touch ----------
void onPageEnter(int p) {
  if (p == PG_POWER) { powConfirm = false; powSel = 0; }
}

void gotoPage(int p, unsigned long now) {
  page = p;
  pageShownAt = now;
  lastTouch = now;
  onPageEnter(p);
}

void onTap(unsigned long now) {
  switch (page) {
    case PG_MUSIC:
      playing = !playing;
      ignorePlayUntil = now + 2000;
      Serial.println("CMD:PLAY");
      flashIcon = 1;
      flashUntil = now + 350;
      break;
    case PG_POWER:
      if (powConfirm) confSel ^= 1;
      else powSel ^= 1;
      break;
    case PG_SAVER:
      gotoPage((page + 1) % NUM_PAGES, now);
      break;
  }
}

void onHold(unsigned long now) {                     // released after holding >= NEXT_MS
  switch (page) {
    case PG_MUSIC:
      Serial.println("CMD:NEXT");
      flashIcon = 2;
      flashUntil = now + 350;
      break;
    case PG_POWER:
      if (!powConfirm) {
        powConfirm = true;
        confSel = 0;                                 // default answer is NO
      } else if (confSel == 1) {
        Serial.println(powSel == 0 ? "CMD:SLEEP" : "CMD:SHUTDOWN");
        powConfirm = false;
        powSentUntil = now + 2000;
      } else {
        powConfirm = false;
      }
      break;
  }
}

void handleTouch() {
  unsigned long now = millis();
  bool s = digitalRead(TOUCH_PIN) == HIGH;

  if (s && !touching) {                              // finger down
    touching = true;
    pressStart = now;
    consumed = false;
    if (!releasePage(page)) {                        // monitor / clock: tap = next page
      if (now - lastTouch > 300) gotoPage((page + 1) % NUM_PAGES, now);
      consumed = true;                               // this press must not act on the next page
    }
  } else if (s && touching && !consumed) {           // finger still down
    unsigned long held = now - pressStart;
    if (lockedPage(page) && held >= EXIT_MS) {       // long hold: leave the locked page
      consumed = true;
      gotoPage((page + 1) % NUM_PAGES, now);
    } else if (page == PG_SAVER && held >= NEXT_MS) {// screensaver: hold = next animation
      consumed = true;
      animIdx = (animIdx + 1) % NUM_ANIM;
      animNameUntil = now + 1200;
    }
  } else if (!s && touching) {                       // finger up
    touching = false;
    unsigned long held = now - pressStart;
    if (releasePage(page) && !consumed && held >= 40) {
      if (held < NEXT_MS) onTap(now);
      else onHold(now);
    }
    consumed = false;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(TOUCH_PIN, INPUT);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);                             // faster I2C = smoother animation
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);
  display.setTextWrap(false);
  display.clearDisplay();
  drawCentered("PC COMPANION", 64, 28, 1);
  display.display();
  delay(800);
  pageShownAt = millis();
}

void loop() {
  readSerial();
  handleTouch();

  display.clearDisplay();
  switch (page) {
    case PG_MONITOR:  drawMonitor();     break;
    case PG_MUSIC:    drawMusic();       break;
    case PG_SAVER:    drawScreensaver(); break;
    case PG_POWER:    drawPower();       break;
    case PG_CLOCK:    drawClock();       break;
  }

  if (millis() - pageShownAt < 700) {                // page name banner
    display.fillRect(0, 24, 128, 16, BLACK);
    display.drawRect(0, 24, 128, 16, WHITE);
    drawCentered(pageNames[page], 64, 28, 1);
  }

  display.display();
  delay(20);
}
