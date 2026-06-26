#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>

namespace {

//TX DEVICE ID - 4 digit identifier
int ID = 0000;

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kPacketIntervalMs = 100;

// ADXL345 IMU on separate I2C bus (Wire1)
constexpr int kImuSda = 7;
constexpr int kImuScl = 6;

constexpr int kOledRst = 21;
constexpr int kVextCtrl = 36;
constexpr int kBatteryAdcPin = 1;
constexpr int kBatteryAdcEnablePin = 37;  // ADC_CTRL: active-LOW MOSFET gates the divider
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;
constexpr float kBatteryDividerRatio = 4.9f * 1.045f;  // Meshtastic Heltec V3: 4.9 * 1.045
constexpr float kBatteryEmptyVolts = 3.30f;
constexpr float kBatteryFullVolts = 4.20f;

// Heltec WiFi LoRa 32 V3 onboard SX1262 wiring.
constexpr int kLoRaNss = 8;
constexpr int kLoRaSck = 9;
constexpr int kLoRaMosi = 10;
constexpr int kLoRaMiso = 11;
constexpr int kLoRaRst = 12;
constexpr int kLoRaBusy = 13;
constexpr int kLoRaDio1 = 14;

constexpr float kLoRaFrequencyMhz = 915.0;
constexpr float kLoRaBandwidthKhz = 125.0;
constexpr uint8_t kLoRaSpreadingFactor = 7;
constexpr uint8_t kLoRaCodingRate = 5;
constexpr uint8_t kLoRaSyncWord = 0x12;
constexpr int8_t kLoRaTxPowerDbm = 14;
constexpr uint16_t kLoRaPreambleLength = 8;
constexpr float kLoRaTcxoVoltage = 1.7;

SX1262 radio = new Module(kLoRaNss, kLoRaDio1, kLoRaRst, kLoRaBusy);
Adafruit_SSD1306 display(kOledWidth, kOledHeight, &Wire, kOledRst);

// ADXL345 IMU constants
constexpr uint8_t kAdxlAddress = 0x53;
constexpr uint8_t kAdxlDataX0 = 0x32;
constexpr uint8_t kAdxlPowerCtl = 0x2D;
bool imuReady = false;

uint32_t lastPacketAtMs = 0;
float lastAccXMps2 = 0.0f;
float lastAccYMps2 = 0.0f;
float lastAccZMps2 = 0.0f;
bool radioReady = false;
bool displayReady = false;



float readAccelerationMagnitude() {
  if (!imuReady) {
    return 0.0f;
  }
  
  // Read 6 bytes starting from DATAX0 (0x32)
  Wire1.beginTransmission(kAdxlAddress);
  Wire1.write(kAdxlDataX0);
  Wire1.endTransmission();
  
  if (Wire1.requestFrom((uint8_t)kAdxlAddress, (uint8_t)6) != 6) {
    return 0.0f;
  }
  
  // Read acceleration data (16-bit, little-endian)
  int16_t x = Wire1.read() | (Wire1.read() << 8);
  int16_t y = Wire1.read() | (Wire1.read() << 8);
  int16_t z = Wire1.read() | (Wire1.read() << 8);
  
  // Convert from ADC counts to m/s² (ADXL345 in 16G mode: 3.9mg/LSB = 0.0383 m/s²/LSB)
  lastAccXMps2 = (x * 0.0383f);
  lastAccYMps2 = (y * 0.0383f);
  lastAccZMps2 = (z * 0.0383f) - 9.81f;  // Subtract gravity from Z axis
  
  // Return primary rowing axis (X - forward/backward motion in boat)
  return lastAccXMps2;
}

void printRadioError(int16_t state) {
  Serial.print(F("LoRa error: "));
  Serial.println(state);
}

bool startRadio() {
  SPI.begin(kLoRaSck, kLoRaMiso, kLoRaMosi, kLoRaNss);

  const int16_t state = radio.begin(
      kLoRaFrequencyMhz,
      kLoRaBandwidthKhz,
      kLoRaSpreadingFactor,
      kLoRaCodingRate,
      kLoRaSyncWord,
      kLoRaTxPowerDbm,
      kLoRaPreambleLength,
      kLoRaTcxoVoltage);

  if (state != RADIOLIB_ERR_NONE) {
    printRadioError(state);
    return false;
  }

  radio.setDio2AsRfSwitch(true);
  return true;
}

bool startDisplay() {
  pinMode(kVextCtrl, OUTPUT);
  digitalWrite(kVextCtrl, LOW);
  delay(100);
  Serial.println(F("  Vext enabled"));

  // Initialize Wire on Heltec V3 default I2C pins (GPIO 17/18) for OLED
  Wire.begin(17, 18);
  Serial.println(F("  Wire begin (17, 18)"));

  if (!display.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
    Serial.println(F("  OLED init FAILED"));
    return false;
  }
  
  Serial.println(F("  OLED init success"));

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("openRow HV3 mvp ptp"));
  display.display();
  return true;
}

bool startIMU() {
  // Initialize Wire1 on GPIO 6/7 for ADXL345
  Wire1.begin(kImuSda, kImuScl);
  Serial.println(F("  Wire1 begin (7, 6)"));
  
  // Check if ADXL345 is present by reading WHO_AM_I register (0x00, should be 0xE5)
  Wire1.beginTransmission(kAdxlAddress);
  Wire1.write(0x00);
  Wire1.endTransmission();
  
  Wire1.requestFrom((uint8_t)kAdxlAddress, (uint8_t)1);
  if (Wire1.available()) {
    uint8_t whoAmI = Wire1.read();
    Serial.print(F("  WHO_AM_I: 0x"));
    Serial.println(whoAmI, HEX);
    if (whoAmI != 0xE5) {
      Serial.println(F("  ADXL345 WHO_AM_I mismatch"));
      return false;
    }
  } else {
    Serial.println(F("  No I2C response from ADXL345"));
    return false;
  }
  
  // Enable power (POWER_CTL = 0x2D, set bit 3 for measurement mode)
  Wire1.beginTransmission(kAdxlAddress);
  Wire1.write(kAdxlPowerCtl);
  Wire1.write(0x08);  // Measurement mode
  Wire1.endTransmission();
  
  delay(50);
  
  // Set range to 16G (DATA_FORMAT = 0x31, value = 0x0B)
  Wire1.beginTransmission(kAdxlAddress);
  Wire1.write(0x31);
  Wire1.write(0x0B);
  Wire1.endTransmission();
  
  Serial.println(F("  ADXL345 configured"));
  return true;
}

void updateDisplay(uint32_t timestampMs, float accMps2) {
  if (!displayReady) {
    return;
  }

  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("openRow tx"));

  display.setCursor(88, 0);
  display.print(radioReady ? F("LIVE") : F("RFERR"));
  display.drawFastHLine(0, 10, kOledWidth, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print(F("ID "));
  char idBuffer[5];
  snprintf(idBuffer, sizeof(idBuffer), "%04d", ID);
  display.print(idBuffer);

  display.setCursor(64, 14);
  display.print(F("t "));
  display.print(timestampMs);
  display.drawFastHLine(0, 25, kOledWidth, SSD1306_WHITE);

  display.setCursor(0, 29);
  display.print(F("X:"));
  display.print(lastAccXMps2, 2);
  display.print(F(" Y:"));
  display.print(lastAccYMps2, 2);

  display.setCursor(0, 40);
  display.print(F("Z:"));
  display.print(lastAccZMps2, 2);

  display.setTextSize(1);
  display.drawFastVLine(86, 26, 38, SSD1306_WHITE);
  display.setCursor(92, 34);
  // Heltec V3 enable sequence from Meshtastic Power.cpp:
  // read current state, invert it, write inverted — ensures MOSFET is toggled on
  pinMode(kBatteryAdcEnablePin, INPUT);
  uint8_t adcEnableVal = !(digitalRead(kBatteryAdcEnablePin));
  pinMode(kBatteryAdcEnablePin, OUTPUT);
  digitalWrite(kBatteryAdcEnablePin, adcEnableVal);
  delay(10);
  uint32_t rawMv = analogReadMilliVolts(kBatteryAdcPin);
  uint32_t raw = analogRead(kBatteryAdcPin);
  // Disable: set back to ANALOG mode (Meshtastic Heltec V3 disable sequence)
  pinMode(kBatteryAdcEnablePin, ANALOG);
  Serial.printf("BATT raw=%lu mV=%lu\n", raw, rawMv);
  float batteryVolts = (rawMv / 1000.0f) * kBatteryDividerRatio;
  float batteryPct = (batteryVolts - kBatteryEmptyVolts) / (kBatteryFullVolts - kBatteryEmptyVolts) * 100.0f;
  batteryPct = constrain(batteryPct, 0.0f, 100.0f);

  display.print(batteryPct);
  display.print(F("%"));
  display.setCursor(92, 48);
  display.print(batteryVolts, 2);
  display.print(F("V"));

  display.display();
}

void sendTelemetryPacket(uint32_t timestampMs, float accMps2, int ID) {
  char packet[64];
  snprintf(packet, sizeof(packet), "%lu,%.3f,%.3f,%.3f,%d",
           static_cast<unsigned long>(timestampMs), 
           lastAccXMps2, lastAccYMps2, lastAccZMps2, ID);

  Serial.print(F("TX "));
  Serial.println(packet);

  const int16_t state = radio.transmit(packet);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioError(state);
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(500);
  Serial.println(F("\n\n=== openRow startup ==="));

  // Meshtastic Heltec V3 variant: ADC_ATTEN_DB_2_5 for high-resistance divider (~0.7-0.9V range)
  analogSetPinAttenuation(kBatteryAdcPin, ADC_2_5db);
  // ADC_CTRL GPIO37: leave floating (ANALOG mode) until a read is needed
  pinMode(kBatteryAdcEnablePin, ANALOG);

  Serial.println(F("Starting display..."));
  displayReady = startDisplay();
  Serial.print(F("Display ready: "));
  Serial.println(displayReady ? "YES" : "FAIL");

  Serial.println(F("Starting IMU..."));
  imuReady = startIMU();
  Serial.print(F("IMU ready: "));
  Serial.println(imuReady ? "YES" : "FAIL");

  Serial.println(F("Starting radio..."));
  radioReady = startRadio();
  if (!radioReady) {
    Serial.println(F("Radio init failed. Check board type, antenna, and LoRa pin map."));
  } else {
    Serial.println(F("LoRa radio ready"));
  }

  Serial.println(F("Setup complete. Entering loop."));
  updateDisplay(millis(), 0);
}

void loop() {
  const uint32_t nowMs = millis();
  if (nowMs - lastPacketAtMs >= kPacketIntervalMs) {
    lastPacketAtMs = nowMs;
    readAccelerationMagnitude();
    sendTelemetryPacket(nowMs, 0, ID);
    updateDisplay(nowMs, 0);
  }
}
