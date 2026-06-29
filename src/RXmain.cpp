#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEAdvertising.h>

#include "logo.h"

namespace {


constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kDisplayRefreshMs = 250;
constexpr uint32_t kPacketStaleMs = 3000;
constexpr bool kVerbosePacketLogs = false;

constexpr int kOledSda = 17;
constexpr int kOledScl = 18;
constexpr int kOledRst = 21;
constexpr int kVextCtrl = 36;
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;

// Heltec WiFi LoRa 32 V3 onboard SX1262 wiring.
constexpr int kLoRaNss = 8;
constexpr int kLoRaSck = 9;
constexpr int kLoRaMosi = 10;
constexpr int kLoRaMiso = 11;
constexpr int kLoRaRst = 12;
constexpr int kLoRaBusy = 13;
constexpr int kLoRaDio1 = 14;

constexpr float kLoRaFrequencyMhz = 915.0;
constexpr float kLoRaBandwidthKhz = 500.0;  // must match TX
constexpr uint8_t kLoRaSpreadingFactor = 7;
constexpr uint8_t kLoRaCodingRate = 5;
constexpr uint8_t kLoRaSyncWord = 0x12;
constexpr int8_t kLoRaTxPowerDbm = 14;
constexpr uint16_t kLoRaPreambleLength = 8;
constexpr float kLoRaTcxoVoltage = 1.7;

SX1262 radio = new Module(kLoRaNss, kLoRaDio1, kLoRaRst, kLoRaBusy);
Adafruit_SSD1306 display(kOledWidth, kOledHeight, &Wire, kOledRst);

// Binary telemetry packet — must match TXmain.cpp exactly
struct __attribute__((packed)) TelemetryPacket {
  uint32_t timestampMs;
  float accX;
  uint16_t deviceId;
};

// BLE components
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTelemetryCharacteristic = nullptr;
bool bleReady = false;
const char* TELEMETRY_SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0";
const char* TELEMETRY_CHARACTERISTIC_UUID = "abcdef01-1234-5678-1234-56789abcdef0";

volatile bool receivedFlag = false;
bool radioReady = false;
bool displayReady = false;

uint32_t lastDisplayAtMs = 0;
uint32_t lastPacketAtMs = 0;
uint32_t lastTimestampMs = 0;
uint16_t lastDeviceId = 0;
float lastAccXMps2 = 0.0f;
float lastRssiDbm = 0.0f;
float lastSnrDb = 0.0f;
uint32_t packetCount = 0;

void setReceivedFlag() {
  receivedFlag = true;
}

void printRadioError(int16_t state) {
  Serial.print(F("LoRa error: "));
  Serial.println(state);
}

bool startBLE() {
  try {
    // Initialize BLE device
    NimBLEDevice::init("openRow_RX");
    
    // Create BLE server
    pServer = NimBLEDevice::createServer();
    
    // Create service
    NimBLEService* pService = pServer->createService(TELEMETRY_SERVICE_UUID);
    
    // Create telemetry characteristic with notify property
    pTelemetryCharacteristic = pService->createCharacteristic(
        TELEMETRY_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    
    pTelemetryCharacteristic->setCallbacks(new NimBLECharacteristicCallbacks());
    
    // Set initial value
    pTelemetryCharacteristic->setValue("0,0");
    
    // Start the server (services start automatically)
    pServer->start();
    
    // Start advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(TELEMETRY_SERVICE_UUID);
    pAdvertising->start();
    
    Serial.println(F("BLE initialized and advertising"));
    return true;
  } catch (const std::exception& e) {
    Serial.print(F("BLE init error: "));
    Serial.println(e.what());
    return false;
  }
}

bool startDisplay() {
  pinMode(kVextCtrl, OUTPUT);
  digitalWrite(kVextCtrl, LOW);
  delay(100);

  Wire.begin(kOledSda, kOledScl);

  if (!display.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
    Serial.println(F("OLED init failed. Check Vext power, OLED pins, and I2C address."));
    return false;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("openRow receiver"));
  display.println(F("waiting for LoRa"));
  display.display();
  return true;
}

void showSplash() {
  if (!displayReady) {
    return;
  }
  showOpenRowSplash(display);
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
  radio.setDio1Action(setReceivedFlag);

  const int16_t rxState = radio.startReceive();
  if (rxState != RADIOLIB_ERR_NONE) {
    printRadioError(rxState);
    return false;
  }

  return true;
}

void updateDisplay() {
  if (!displayReady) {
    return;
  }

  const uint32_t nowMs = millis();
  const bool hasPacket = packetCount > 0;
  const bool packetFresh = hasPacket && (nowMs - lastPacketAtMs <= kPacketStaleMs);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print(F("openRow rx"));

  display.setCursor(88, 0);
  if (!radioReady) {
    display.print(F("RF ERR"));
  } else {
    display.print(packetFresh ? F("LIVE") : F("WAIT"));
  }
  display.drawFastHLine(0, 10, kOledWidth, SSD1306_WHITE);

  display.setCursor(0, 13);
  display.print(F("id "));
  if (hasPacket) {
    char idBuffer[5];
    snprintf(idBuffer, sizeof(idBuffer), "%04u", lastDeviceId);
    display.print(idBuffer);
  } else {
    display.print(F("----"));
  }
  display.print(F(" pkts "));
  display.print(packetCount);
  display.drawFastHLine(0, 24, kOledWidth, SSD1306_WHITE);

  display.setCursor(0, 28);
  display.print(F("ts "));
  if (hasPacket) {
    display.print(lastTimestampMs);
    display.print(F(" ms"));
  } else {
    display.print(F("--"));
  }
  display.drawFastHLine(0, 38, kOledWidth, SSD1306_WHITE);

  display.setCursor(0, 42);
  if (hasPacket) {
    display.print(F("X:"));
    display.print(lastAccXMps2, 2);
  } else {
    display.print(F("acc --"));
  }

  display.setCursor(0, 53);
  if (hasPacket) {
    display.print(F("m/s2"));
  }

  display.drawFastVLine(86, 39, 25, SSD1306_WHITE);
  display.setCursor(92, 42);
  if (hasPacket) {
    display.print(lastRssiDbm, 0);
    display.print(F("dB"));
    display.setCursor(92, 53);
    display.print(F("S "));
    display.print(lastSnrDb, 1);
  } else {
    display.print(F("no pkt"));
  }

  display.display();
}

void handleReceivedPacket() {
  receivedFlag = false;

  TelemetryPacket pkt;
  const int16_t state = radio.readData(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
  if (state == RADIOLIB_ERR_NONE) {
    if (kVerbosePacketLogs) {
      Serial.printf("RX t=%lu x=%.3f id=%u\n",
                    pkt.timestampMs, pkt.accX, pkt.deviceId);
    }

    lastTimestampMs = pkt.timestampMs;
    lastDeviceId    = pkt.deviceId;
    lastAccXMps2    = pkt.accX;
    lastRssiDbm     = radio.getRSSI();
    lastSnrDb       = radio.getSNR();
    lastPacketAtMs  = millis();
    packetCount++;

    // Broadcast over BLE (primary X axis for rowing)
    if (bleReady && pTelemetryCharacteristic) {
      String blePayload = String(lastTimestampMs) + "," + String(lastAccXMps2, 3);
      pTelemetryCharacteristic->setValue(blePayload.c_str());
      pTelemetryCharacteristic->notify();
    }
  } else {
    printRadioError(state);
  }

  const int16_t rxState = radio.startReceive();
  if (rxState != RADIOLIB_ERR_NONE) {
    printRadioError(rxState);
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(500);

  Serial.println();
  Serial.println(F("openRow LoRa receiver MVP"));
  Serial.println(F("expected packet: timestamp_ms,accX,accY,accZ,device_id"));

  displayReady = startDisplay();
  showSplash();

  bleReady = startBLE();
  if (!bleReady) {
    Serial.println(F("BLE init failed."));
  } else {
    Serial.println(F("BLE receiver ready"));
    Serial.print(F("BLE characteristic pointer: "));
    Serial.println((uint32_t)pTelemetryCharacteristic, HEX);
  }

  radioReady = startRadio();
  if (!radioReady) {
    Serial.println(F("Radio init failed. Check board type, antenna, and LoRa pin map."));
  } else {
    Serial.println(F("LoRa receiver ready"));
  }

  updateDisplay();
}

void loop() {
  if (receivedFlag) {
    handleReceivedPacket();
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastDisplayAtMs >= kDisplayRefreshMs) {
    lastDisplayAtMs = nowMs;
    updateDisplay();
  }
}
