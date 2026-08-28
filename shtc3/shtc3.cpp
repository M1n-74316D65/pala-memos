#include "Arduino.h"
#include "shtc3.h"

extern "C" {
#include "../i2c_bsp/i2c_bsp.h"
}

static uint8_t crc8(const uint8_t* data, int len) {
  uint8_t crc = 0xFF;
  for (int j = 0; j < len; j++) {
    crc ^= data[j];
    for (int i = 0; i < 8; i++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x31;
      else            crc = (crc << 1);
    }
  }
  return crc;
}

bool shtc3Init() {
  return (shtc3_handle != nullptr);
}

bool shtc3Read(float& tempC, float& humidityPercent) {
  if (!shtc3_handle) return false;

  // 1. Wakeup command (0x3517)
  uint8_t wakeCmd[2] = {0x35, 0x17};
  if (i2c_write_buff(shtc3_handle, -1, wakeCmd, 2) != 0) return false;
  delayMicroseconds(300);

  // 2. Measure command: Normal power, clock stretching disabled, T first (0x7866)
  uint8_t measCmd[2] = {0x78, 0x66};
  if (i2c_write_buff(shtc3_handle, -1, measCmd, 2) != 0) {
    // Put back to sleep before returning
    uint8_t sleepCmd[2] = {0xB0, 0x98};
    i2c_write_buff(shtc3_handle, -1, sleepCmd, 2);
    return false;
  }
  delay(15);

  // 3. Read 6 bytes: [T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC]
  uint8_t data[6] = {0};
  if (i2c_read_buff(shtc3_handle, -1, data, 6) != 0) {
    uint8_t sleepCmd[2] = {0xB0, 0x98};
    i2c_write_buff(shtc3_handle, -1, sleepCmd, 2);
    return false;
  }

  // 4. Sleep command (0xB098)
  uint8_t sleepCmd[2] = {0xB0, 0x98};
  i2c_write_buff(shtc3_handle, -1, sleepCmd, 2);

  // Verify CRCs
  if (crc8(&data[0], 2) != data[2] || crc8(&data[3], 2) != data[5]) {
    return false;
  }

  uint16_t rawT  = ((uint16_t)data[0] << 8) | data[1];
  uint16_t rawRH = ((uint16_t)data[3] << 8) | data[4];

  tempC           = -45.0f + 175.0f * ((float)rawT / 65535.0f);
  humidityPercent = 100.0f * ((float)rawRH / 65535.0f);

  if (humidityPercent > 100.0f) humidityPercent = 100.0f;
  if (humidityPercent < 0.0f)   humidityPercent = 0.0f;

  return true;
}

String shtc3Label() {
  float t = 0.0f, h = 0.0f;
  if (!shtc3Read(t, h)) return "";
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f C  %d%%", t, (int)roundf(h));
  return String(buf);
}
