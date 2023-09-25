/*
Copyright © 2019, 2020, 2021, 2022 HackEDA, Inc.
Licensed under the WiPhone Public License v.1.0 (the "License"); you
may not use this file except in compliance with the License. You may
obtain a copy of the License at
https://wiphone.io/WiPhone_Public_License_v1.0.txt.

Unless required by applicable law or agreed to in writing, software,
hardware or documentation distributed under the License is distributed
on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#ifndef _CW2015_h_
#define _CW2015_h_

#include "Arduino.h"
#include <Wire.h>
#include "config.h"

#ifdef WIPHONE_PRODUCTION
#define GAUGE_DEBUG0(s, ...)
#define GAUGE_DEBUG1(s, ...)
#define GAUGE_DEBUG2(s1, s2, ...)
#else
#define GAUGE_DEBUG0(s, ...) Serial.print(s, ##__VA_ARGS__)
#define GAUGE_DEBUG1(s, ...) Serial.println(s, ##__VA_ARGS__)
#define GAUGE_DEBUG2(s1, s2, ...) do { Serial.print(s1); Serial.println(s2, ##__VA_ARGS__); } while(0)
#endif // WIPHONE_PRODUCTION

/* 7-bit address (last bit is R/W bit, set in i2cWrite automatically) */
#define CW2015_I2C_ADDR         ((unsigned char) 0x62)        // 0x62 = 0xC5 >> 1

typedef enum {
  CW2015_ERROR_OK             = I2C_ERROR_OK,
  CW2015_ERROR_DEV            = I2C_ERROR_DEV,
  CW2015_ERROR_ACK            = I2C_ERROR_ACK,
  CW2015_ERROR_TIMEOUT        = I2C_ERROR_TIMEOUT,
  CW2015_ERROR_BUS            = I2C_ERROR_BUS,
  CW2015_ERROR_BUSY           = I2C_ERROR_BUSY,
  CW2015_ERROR_READ_FAILED,
  CW2015_ERROR_WRITE_FAILED,
  CW2015_ERROR_REQUEST_FAILED
} cw2015_err_t;

// Registers
#define CW2015_REG_VERSION          ((unsigned char)0x00)     // R
#define CW2015_REG_VCELL1           ((unsigned char)0x02)     // R
#define CW2015_REG_VCELL2           ((unsigned char)0x03)     // R
#define CW2015_REG_SOC1             ((unsigned char)0x04)     // R
#define CW2015_REG_SOC2             ((unsigned char)0x05)     // R
#define CW2015_REG_RRT_ALRT1        ((unsigned char)0x06)     // W/R
#define CW2015_REG_RRT_ALRT2        ((unsigned char)0x07)     // W/R
#define CW2015_REG_CONFIG           ((unsigned char)0x08)     // W/R
#define CW2015_REG_MODE             ((unsigned char)0x0A)     // W/R


class CW2015 {
private:
  uint8_t   _addr;
  uint8_t   _sda;
  uint8_t   _scl;

public:
  CW2015(uint8_t _addr, uint8_t _sda, uint8_t _scl) {
    this->_addr = _addr;
    this->_sda  = _sda;
    this->_scl  = _scl;
  }

  void connect() {
    Wire.begin(this->_sda, this->_scl, 400000);     // can be 400000
  }

  bool configure() {
    unsigned char val;
    unsigned int cnt = 0;
    const unsigned int tries = 25000;
    do {
      // Setting this register correctly is hugely important for this chip
      setReg(CW2015_REG_MODE, 0x00);
      if (readReg(CW2015_REG_MODE, val) != CW2015_ERROR_OK) {
        val = 0xFF;
      }
    } while (val!=0 && ++cnt < tries);
    return cnt < tries ? true : false;
  }

  float readVoltage() {
    uint8_t v1, v2;
    if (readReg(CW2015_REG_VCELL1, v1) == (cw2015_err_t) CW2015_ERROR_OK) {
      if (readReg(CW2015_REG_VCELL2, v2) == (cw2015_err_t) CW2015_ERROR_OK) {
        return (((unsigned short)v1<<8) + v2) * 0.000305;
      }
    }
    return 0.0;
  }

  float readSocPrecise() {
    uint8_t s1, s2;
    if (readReg(CW2015_REG_SOC1, s1) == (cw2015_err_t) CW2015_ERROR_OK) {
      if (readReg(CW2015_REG_SOC2, s2) == (cw2015_err_t) CW2015_ERROR_OK) {
        return s1 + s2 * 0.00390625;
      }
    }
    return 0.0;
  }

  void showError(cw2015_err_t err) {
    switch(err) {
    case CW2015_ERROR_OK:
      GAUGE_DEBUG1("CW2015_ERROR_OK");
      break;
    case CW2015_ERROR_DEV:
      GAUGE_DEBUG1("CW2015_ERROR_DEV");
      break;
    case CW2015_ERROR_ACK:
      GAUGE_DEBUG1("CW2015_ERROR_ACK");
      break;
    case CW2015_ERROR_TIMEOUT:
      GAUGE_DEBUG1("CW2015_ERROR_TIMEOUT");
      break;
    case CW2015_ERROR_BUS:
      GAUGE_DEBUG1("CW2015_ERROR_BUS");
      break;
    case CW2015_ERROR_BUSY:
      GAUGE_DEBUG1("CW2015_ERROR_BUSY");
      break;
    case CW2015_ERROR_READ_FAILED:
      GAUGE_DEBUG1("CW2015_ERROR_READ_FAILED");
      break;
    case CW2015_ERROR_WRITE_FAILED:
      GAUGE_DEBUG1("CW2015_ERROR_WRITE_FAILED");
      break;
    case CW2015_ERROR_REQUEST_FAILED:
      GAUGE_DEBUG1("CW2015_ERROR_REQUEST_FAILED");
      break;
    }
  }

  void showVersion() {
    uint8_t res;
    cw2015_err_t err = readReg(CW2015_REG_VERSION, res);
    if (err==CW2015_ERROR_OK) {
      GAUGE_DEBUG2("VERSION = ", res, HEX);
    } else {
      GAUGE_DEBUG0("VERSION = ERR: ");
      showError(err);
    }
  }

  cw2015_err_t setReg(unsigned char regAddr, unsigned char val) {
    Wire.beginTransmission(_addr);
    if (Wire.write(regAddr))
      if (Wire.write(val)) {
        return (cw2015_err_t) Wire.endTransmission();
      }
    return CW2015_ERROR_WRITE_FAILED;
  }

  cw2015_err_t readReg(unsigned char regAddr, unsigned char &b) {
    Wire.beginTransmission(_addr);
    if (Wire.write(regAddr)) {
      cw2015_err_t err = (cw2015_err_t) Wire.endTransmission();
      if (err != CW2015_ERROR_OK) {
        return err;
      }

      // Restart transmission
      Wire.beginTransmission(_addr);
      if (Wire.requestFrom(_addr, (uint8_t) 1)) {
        b = Wire.read();
        return (cw2015_err_t) Wire.endTransmission();
      }
      return CW2015_ERROR_REQUEST_FAILED;
    }
    return CW2015_ERROR_READ_FAILED;
  }
};

#endif // _CW2015_h_
