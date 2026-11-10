// Connection check (FSD.md task 2).
//  - BNO085 on I2C0: SDA GP4 (pin 6), SCL GP5 (pin 7). Scans the bus, then
//    starts the sensor and prints its product IDs.
//  - WS128 (GC9A01, software SPI): draws colour bars and a result line. The
//    bench can't see the screen, so the user confirms it by eye.
// Results go to Serial1 (UART0 -> Debug Probe -> bench slot "probe").
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_BNO08x.h>

static const int PIN_SDA = 4, PIN_SCL = 5;
static const int PIN_DIN = 22, PIN_CLK = 21, PIN_CS = 20, PIN_DC = 19, PIN_RST = 18, PIN_BL = 17;

static Adafruit_GC9A01A tft(PIN_CS, PIN_DC, PIN_DIN, PIN_CLK, PIN_RST);
static Adafruit_BNO08x bno(-1);  // RST not wired

static int scanI2C() {
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial1.printf("i2c: device at 0x%02X\n", a);
      found++;
    }
  }
  Serial1.printf("i2c: %d device(s)\n", found);
  return found;
}

static bool checkBno() {
  bool ok = bno.begin_I2C(0x4A, &Wire) || bno.begin_I2C(0x4B, &Wire);
  if (!ok) {
    Serial1.println("bno: begin_I2C failed");
    return false;
  }
  for (int n = 0; n < bno.prodIds.numEntries; n++) {
    Serial1.printf("bno: part %lu ver %d.%d.%d\n", (unsigned long)bno.prodIds.entry[n].swPartNumber,
                   bno.prodIds.entry[n].swVersionMajor, bno.prodIds.entry[n].swVersionMinor,
                   bno.prodIds.entry[n].swVersionPatch);
  }
  // Prove data flows: enable the gyro and wait for one report.
  if (!bno.enableReport(SH2_GYROSCOPE_CALIBRATED, 50000)) {
    Serial1.println("bno: enableReport failed");
    return false;
  }
  sh2_SensorValue_t v;
  uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    if (bno.getSensorEvent(&v) && v.sensorId == SH2_GYROSCOPE_CALIBRATED) {
      Serial1.println("bno: gyro report received");
      return true;
    }
    delay(5);
  }
  Serial1.println("bno: no gyro report within 2 s");
  return false;
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("conncheck: boot");

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  tft.begin();
  const uint16_t bars[] = {GC9A01A_RED, GC9A01A_GREEN, GC9A01A_BLUE, GC9A01A_WHITE};
  for (int i = 0; i < 4; i++) tft.fillRect(i * 60, 0, 60, 240, bars[i]);
  Serial1.println("lcd: colour bars drawn (red, green, blue, white)");

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  int devices = scanI2C();
  bool bnoOk = devices > 0 && checkBno();
  Serial1.printf("check: BNO085 %s\n", bnoOk ? "PASS" : "FAIL");

  tft.fillRect(20, 95, 200, 50, GC9A01A_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(bnoOk ? GC9A01A_GREEN : GC9A01A_RED);
  tft.setCursor(30, 108);
  tft.print(bnoOk ? "BNO OK" : "BNO FAIL");
  Serial1.println("check: done");
}

void loop() { delay(1000); }
