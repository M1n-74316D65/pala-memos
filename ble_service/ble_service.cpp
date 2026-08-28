#include "Arduino.h"
#include "ble_service.h"
#include "config_store.h"
#include "battery.h"
#include "shtc3.h"
#include "notes.h"
#include "../../globals.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define STATUS_CHAR_UUID    "19b10001-e8f2-537e-4f6c-d104768a1214"
#define CONFIG_CHAR_UUID    "19b10002-e8f2-537e-4f6c-d104768a1214"

static BLEServer*         pServer        = nullptr;
static BLECharacteristic* pStatusChar    = nullptr;
static BLECharacteristic* pConfigChar    = nullptr;
static bool               s_deviceConnected = false;
static bool               s_bleActive       = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      s_deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      s_deviceConnected = false;
      if (s_bleActive) {
        BLEDevice::startAdvertising();
      }
    }
};

class ConfigCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        // Format: [PIN\n] ssid\npass\nurl\ntoken\nvis\ntz\nnew_pin
        // PIN is required as line 0 when a portal PIN is already stored.
        String lines[8];
        int lineCount = 0;
        int start = 0;
        for (int i = 0; i <= (int)rxValue.length(); i++) {
          if (i == (int)rxValue.length() || rxValue[i] == '\n') {
            if (lineCount < 8) {
              lines[lineCount++] = rxValue.substring(start, i);
              lines[lineCount - 1].trim();
            }
            start = i + 1;
          }
        }

        String savedPin = configGetPortalPin();
        int base = 0;
        if (savedPin.length() > 0) {
          if (lineCount < 1 || lines[0] != savedPin) {
            Serial.println("[BLE] Config write rejected: PIN mismatch");
            return;
          }
          base = 1;
        }

        if (lineCount - base >= 2) {
          String ssid = lines[base];
          String pass = lines[base + 1];
          String mUrl = (lineCount > base + 2) ? lines[base + 2] : configGetMemosUrl();
          String mTok = (lineCount > base + 3) ? lines[base + 3] : configGetMemosToken();
          String mVis = (lineCount > base + 4) ? lines[base + 4] : configGetMemosVisibility();
          int tz      = (lineCount > base + 5 && lines[base + 5].length()) ? lines[base + 5].toInt() : configGetTimezoneOffsetMin();
          String pin  = (lineCount > base + 6) ? lines[base + 6] : savedPin;

          configSet(ssid, pass, mUrl, mTok, mVis, tz, pin);
          bleNotifyStatus();
        }
      }
    }
};

static MyServerCallbacks s_serverCallbacks;
static ConfigCallbacks   s_configCallbacks;

void bleServiceInit() {
  if (s_bleActive) return;
  BLEDevice::init("Pala-Note");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(&s_serverCallbacks);

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pStatusChar = pService->createCharacteristic(
                      STATUS_CHAR_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pStatusChar->addDescriptor(new BLE2902());

  pConfigChar = pService->createCharacteristic(
                      CONFIG_CHAR_UUID,
                      BLECharacteristic::PROPERTY_WRITE
                    );
  pConfigChar->setCallbacks(&s_configCallbacks);

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  s_bleActive = true;
}

void bleServiceStart() {
  if (!s_bleActive) bleServiceInit();
  BLEDevice::startAdvertising();
  bleNotifyStatus();
}

void bleServiceStop() {
  if (!s_bleActive) return;
  BLEDevice::stopAdvertising();
  BLEDevice::deinit(true);
  pServer = nullptr;
  pStatusChar = nullptr;
  pConfigChar = nullptr;
  s_deviceConnected = false;
  s_bleActive = false;
}

bool bleIsActive()    { return s_bleActive; }
bool bleIsConnected() { return s_deviceConnected; }

void bleNotifyStatus() {
  if (!pStatusChar) return;
  float t = 0.0f, h = 0.0f;
  shtc3Read(t, h);
  int batt = readBatteryPercent();

  char json[192];
  snprintf(json, sizeof(json),
           "{\"dev\":\"pala\",\"notes\":%d,\"batt\":%d,\"temp\":%.1f,\"hum\":%d,\"sync\":\"memos\",\"ssid\":\"%s\"}",
           (int)noteIndex.size(), batt, t, (int)roundf(h), configGetWifiSsid().c_str());

  pStatusChar->setValue((uint8_t*)json, strlen(json));
  if (s_deviceConnected) {
    pStatusChar->notify();
  }
}
