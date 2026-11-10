// Orientation app (FSD.md task 4, built on task 3).
//  - Reads the BNO085's game rotation vector (a quaternion; gyro + accelerometer,
//    no magnetometer, so yaw can't jump when a magnet passes by) on I2C0
//    (SDA GP4 / SCL GP5) and turns it into Pitch (about X), Roll (about Y) and
//    Yaw (about Z) in degrees, relative to a "zero" orientation.
//  - Samples calibrated angular velocity (X, Y, Z) at 2 Hz. When all three stay
//    below 1 deg/s for at least 5 s, the current orientation becomes the new
//    zero (Pitch = Roll = Yaw = 0). Further zeroing is then suppressed until any
//    axis exceeds 1 deg/s.
//  - Shows the three angles on the WS128 twice per second.
//  - Echoes each displayed line and each zero/re-arm event to Serial1
//    (UART0 -> Debug Probe -> bench slot "probe"), since the bench can't see the screen.
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <hardware/structs/sio.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_BNO08x.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

static const int PIN_SDA = 4, PIN_SCL = 5;
static const int PIN_DIN = 22, PIN_CLK = 21, PIN_CS = 20, PIN_DC = 19, PIN_RST = 18, PIN_BL = 17;

static const uint32_t REPORT_US = 50000;     // sensor streams both reports at 20 Hz
static const uint32_t GYRO_SAMPLE_MS = 500;  // angular velocity monitored at 2 Hz
static const uint32_t DISPLAY_MS = 500;      // display refreshed at 2 Hz
static const float STILL_DPS = 1.0f;         // "still" = every axis below 1 deg/s ...
static const uint32_t STILL_MS = 5000;       // ... for at least 5 s
static const float RAD_TO_DEGF = 57.2957795f;

static Adafruit_GC9A01A tft(PIN_CS, PIN_DC, PIN_DIN, PIN_CLK, PIN_RST);
static Adafruit_BNO08x bno(-1);  // BNO085 RST pin isn't wired

// ---- Quaternions ---------------------------------------------------------------

struct Quat {
  float w, x, y, z;
};

static Quat qMul(const Quat &a, const Quat &b) {
  return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
          a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

static Quat qConj(const Quat &q) { return {q.w, -q.x, -q.y, -q.z}; }

// Tait-Bryan angles (Z-Y-X order) in degrees: rotation about X, Y and Z.
static void toAngles(const Quat &q, float &ax, float &ay, float &az) {
  float s = 2.0f * (q.w * q.y - q.z * q.x);
  if (s > 1.0f) s = 1.0f;
  if (s < -1.0f) s = -1.0f;
  ax = atan2f(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * RAD_TO_DEGF;
  ay = asinf(s) * RAD_TO_DEGF;
  az = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * RAD_TO_DEGF;
}

// ---- Sensor ------------------------------------------------------------------

static bool sensorUp = false;

static bool enableReports() {
  return bno.enableReport(SH2_GAME_ROTATION_VECTOR, REPORT_US) &&
         bno.enableReport(SH2_GYROSCOPE_CALIBRATED, REPORT_US);
}

static bool startSensor() {
  if (!bno.begin_I2C(0x4A, &Wire) && !bno.begin_I2C(0x4B, &Wire)) return false;
  return enableReports();
}

static Quat latestQ = {1, 0, 0, 0};
static bool haveQ = false;
static float latestGyro[3];   // deg/s
static bool haveGyro = false; // a gyro report arrived since the last 2 Hz sample

static void pollSensor() {
  if (!sensorUp) return;
  if (bno.wasReset()) {
    Serial1.println("bno: sensor reset, re-enabling reports");
    enableReports();
  }
  sh2_SensorValue_t v;
  while (bno.getSensorEvent(&v)) {
    if (v.sensorId == SH2_GAME_ROTATION_VECTOR) {
      latestQ = {v.un.gameRotationVector.real, v.un.gameRotationVector.i,
                 v.un.gameRotationVector.j, v.un.gameRotationVector.k};
      haveQ = true;
    } else if (v.sensorId == SH2_GYROSCOPE_CALIBRATED) {
      latestGyro[0] = v.un.gyroscope.x * RAD_TO_DEGF;
      latestGyro[1] = v.un.gyroscope.y * RAD_TO_DEGF;
      latestGyro[2] = v.un.gyroscope.z * RAD_TO_DEGF;
      haveGyro = true;
    }
  }
}

// ---- Zeroing -------------------------------------------------------------------

static Quat zeroQ = {1, 0, 0, 0};  // orientation that reads as 0/0/0
static bool zeroArmed = true;      // a still period may (re)initialise the angles
static bool quiet = false;         // the last gyro sample was below the threshold
static uint32_t quietSince = 0;    // time of the first sample of the current quiet run
static bool statusDirty = true;    // status line needs redrawing

// One 2 Hz sample of the newest gyro report. Skipped if no report has arrived
// since the previous sample (sensor stalled or missing).
static void gyroSample() {
  if (!haveGyro) return;
  haveGyro = false;
  uint32_t now = millis();
  bool still = fabsf(latestGyro[0]) < STILL_DPS && fabsf(latestGyro[1]) < STILL_DPS &&
               fabsf(latestGyro[2]) < STILL_DPS;
  if (!still) {
    quiet = false;
    if (!zeroArmed) {
      zeroArmed = true;
      statusDirty = true;
      Serial1.println("zero: re-armed (angular velocity above 1 deg/s)");
    }
    return;
  }
  if (!quiet) {
    quiet = true;
    quietSince = now;
  }
  if (zeroArmed && haveQ && now - quietSince >= STILL_MS) {
    zeroQ = latestQ;
    zeroArmed = false;
    statusDirty = true;
    Serial1.println("zero: angles initialised to 0 (still for 5 s)");
  }
}

// ---- Display -----------------------------------------------------------------

// One row: label on the left, value on the right. Drawn off-screen and pushed in
// one go so values don't flicker.
static const int ROW_W = 200, ROW_H = 40, ROW_X = 20;
static const int ROW_Y[3] = {64, 110, 156};
static const char *LABELS[3] = {"Pitch", "Roll", "Yaw"};
static const uint16_t COLOURS[3] = {GC9A01A_ORANGE, GC9A01A_GREEN, GC9A01A_CYAN};
static GFXcanvas16 rowCanvas(ROW_W, ROW_H);

// Bit-bang one 16-bit pixel (SPI mode 0, MSB first) straight through the SIO
// registers. Adafruit's software SPI goes through digitalWrite() per bit and
// needed ~1.2 s to push the three rows. The nops keep each clock phase well
// above the GC9A01's 15 ns minimum.
#define SPI_NOP() __asm volatile("nop\n nop\n nop\n")
static inline void fastWrite16(uint16_t v) {
  const uint32_t mosi = 1u << PIN_DIN, clk = 1u << PIN_CLK;
  for (int b = 15; b >= 0; b--) {
    if (v & (1u << b)) sio_hw->gpio_set = mosi;
    else sio_hw->gpio_clr = mosi;
    SPI_NOP();
    sio_hw->gpio_set = clk;
    SPI_NOP();
    sio_hw->gpio_clr = clk;
  }
}

// Adafruit's drawRGBBitmap()/writePixels() hang with software SPI on the RP2040,
// so the window is set through the library and the pixels are sent by hand
// (CS stays low and DC stays in data mode after setAddrWindow()).
static void pushCanvas(int x, int y, GFXcanvas16 &c) {
  const uint16_t *px = c.getBuffer();
  uint32_t n = (uint32_t)c.width() * c.height();
  tft.startWrite();
  tft.setAddrWindow(x, y, c.width(), c.height());
  for (uint32_t i = 0; i < n; i++) fastWrite16(px[i]);
  tft.endWrite();
}

// printf("%f") prints nothing in this core, so format tenths by hand.
static void formatTenths(char *out, size_t size, float value) {
  long tenths = lroundf(value * 10.0f);
  const char *sign = tenths < 0 ? "-" : "";
  tenths = labs(tenths);
  snprintf(out, size, "%s%ld.%ld", sign, tenths / 10, tenths % 10);
}

static void drawRow(int i, const char *value) {
  int16_t x1, y1;
  uint16_t w, h;
  rowCanvas.fillScreen(GC9A01A_BLACK);
  rowCanvas.setFont(&FreeSans12pt7b);
  rowCanvas.setTextColor(COLOURS[i]);
  rowCanvas.setCursor(4, 29);
  rowCanvas.print(LABELS[i]);
  rowCanvas.setFont(&FreeSansBold18pt7b);
  rowCanvas.setTextColor(GC9A01A_WHITE);
  rowCanvas.getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  rowCanvas.setCursor(ROW_W - 4 - (int)w - x1, 31);
  rowCanvas.print(value);
  pushCanvas(ROW_X, ROW_Y[i], rowCanvas);
}

static void drawStatic() {
  tft.fillScreen(GC9A01A_BLACK);
  GFXcanvas16 title(120, 26);
  title.setFont(&FreeSans12pt7b);
  title.setTextColor(GC9A01A_DARKGREY);
  int16_t x1, y1;
  uint16_t w, h;
  title.getTextBounds("degrees", 0, 0, &x1, &y1, &w, &h);
  title.setCursor((120 - (int)w) / 2 - x1, 19);
  title.print("degrees");
  pushCanvas(60, 30, title);
}

static void showStatus(const char *text, uint16_t colour) {
  GFXcanvas16 c(140, 22);
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(colour);
  c.setCursor((140 - 6 * (int)strlen(text)) / 2, 7);
  c.print(text);
  pushCanvas(50, 204, c);
}

// ---- Main --------------------------------------------------------------------

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("orientation: boot (task 4: quaternion angles + auto-zero)");

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  tft.begin();
  drawStatic();
  for (int i = 0; i < 3; i++) drawRow(i, "--");

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  sensorUp = startSensor();
  Serial1.println(sensorUp ? "bno: rotation vector + gyro enabled" : "bno: NOT FOUND");
  if (!sensorUp) showStatus("BNO085 not found", GC9A01A_RED);
  Serial1.println("orientation: ready");
}

// Drain the sensor and take a gyro sample whenever one is due. Called from loop()
// and between display rows, so a redraw can never skip a sample.
static uint32_t nextGyro = 0;
static void service() {
  pollSensor();
  if ((int32_t)(millis() - nextGyro) >= 0) {
    nextGyro += GYRO_SAMPLE_MS;
    gyroSample();
  }
}

void loop() {
  static uint32_t nextShow = millis() + DISPLAY_MS + GYRO_SAMPLE_MS / 2;  // between samples
  static uint32_t nextRetry = 0;
  if (nextGyro == 0) nextGyro = millis() + GYRO_SAMPLE_MS;

  service();

  uint32_t now = millis();
  if (!sensorUp && (int32_t)(now - nextRetry) >= 0) {
    nextRetry = now + 5000;
    sensorUp = startSensor();
    if (sensorUp) {
      Serial1.println("bno: found on retry");
      statusDirty = true;
    }
  }

  if ((int32_t)(now - nextShow) < 0) return;
  nextShow += DISPLAY_MS;

  char text[3][12];
  if (haveQ) {
    float a[3];
    toAngles(qMul(qConj(zeroQ), latestQ), a[0], a[1], a[2]);
    for (int i = 0; i < 3; i++) formatTenths(text[i], sizeof text[i], a[i]);
  } else {
    for (int i = 0; i < 3; i++) strcpy(text[i], "--");
  }
  uint32_t t0 = millis();
  for (int i = 0; i < 3; i++) {
    drawRow(i, text[i]);
    service();
  }
  if (statusDirty && sensorUp) {
    statusDirty = false;
    if (zeroArmed) showStatus("", GC9A01A_BLACK);
    else showStatus("zeroed", GC9A01A_DARKGREEN);
  }
  Serial1.printf("show: Pitch=%s Roll=%s Yaw=%s deg (%s, draw %lu ms)\n", text[0], text[1],
                 text[2], zeroArmed ? "armed" : "zeroed", (unsigned long)(millis() - t0));
}
