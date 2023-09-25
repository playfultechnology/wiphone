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

#ifndef SN7326_h
#define SN7326_h

#include "Arduino.h"
#include <Wire.h>

#define SN7326_I2C_ADDR_BASE        B1011000        // 7-bit I2C address

/* ----- Registers ----- */

#define SN7326_REG_CONFIG           ((unsigned char)0x08)
#define SN7326_REG_STATUS           ((unsigned char)0x10)

/* ----- Flags ----- */

#define SN7326_RESERVED             ((unsigned char)0x80)
#define SN7326_AUTO_CLEAR_5MS(n)    ((unsigned char)(n & 0x03)<<5)    // Auto clear INT after 0, 5ms or 10ms (15 ms - N/A)
#define SN7326_INPUT_FILTER_EN      ((unsigned char)0x10)             // Input port filter enable (debouncing enabled)
#define SN7326_DEBOUNCE_TIME_NORMAL ((unsigned char)0x08)             // Normal debounce time (3ms+4ms) or double (6ms+8ms)?
#define SN7326_LONGPRESS_EN         ((unsigned char)0x04)             // Long-pressed key detect enable
#define SN7326_LONGPRESS_DELAY(n)   ((unsigned char)(n & 0x03))       // Long-pressed key detect delay time (20/40/1000/2000ms)

#define SN7326_MORE                 ((unsigned char)0x80)             // More than one key to report?
#define SN7326_PRESSED              ((unsigned char)0x40)             // Pressed or released?
#define SN7326_KEYS_MASK            ((unsigned char)0x3f)             // Key mapping

typedef enum {
  SN7326_ERROR_OK             = I2C_ERROR_OK,
  SN7326_ERROR_DEV            = I2C_ERROR_DEV,
  SN7326_ERROR_ACK            = I2C_ERROR_ACK,
  SN7326_ERROR_TIMEOUT        = I2C_ERROR_TIMEOUT,
  SN7326_ERROR_BUS            = I2C_ERROR_BUS,
  SN7326_ERROR_BUSY           = I2C_ERROR_BUSY,
  SN7326_ERROR_WRITE_FAILED   = 11,
  SN7326_ERROR_REQUEST_FAILED = 12,
} sn7326_err_t;


class SN7326 {
private:
  uint8_t   _addr;
  uint8_t   _sda;
  uint8_t   _scl;

public:
  SN7326(uint8_t _addr, uint8_t _sda, uint8_t _scl) {
    this->_addr = _addr;
    this->_sda  = _sda;
    this->_scl  = _scl;
  }

  void connect() {
    Wire.begin(this->_sda, this->_scl);
    Wire.setClock(400000);
  }

  sn7326_err_t config() {
    //writeReg(SN7326_REG_CONFIG, 0xFF );   // longpress VERY VERY slow
    //return writeReg(SN7326_REG_CONFIG, SN7326_AUTO_CLEAR_5MS(0) | SN7326_INPUT_FILTER_EN | SN7326_DEBOUNCE_TIME_NORMAL | SN7326_LONGPRESS_EN | SN7326_LONGPRESS_DELAY(2));  // sometimes freezes
    //return writeReg(SN7326_REG_CONFIG, SN7326_AUTO_CLEAR_5MS(2) | SN7326_INPUT_FILTER_EN | SN7326_DEBOUNCE_TIME_NORMAL | SN7326_LONGPRESS_EN | SN7326_LONGPRESS_DELAY(2));
    return writeReg(SN7326_REG_CONFIG, SN7326_AUTO_CLEAR_5MS(2) | SN7326_INPUT_FILTER_EN | SN7326_LONGPRESS_EN | SN7326_LONGPRESS_DELAY(2));   // double debounce time
  }

  inline sn7326_err_t readKey(unsigned char &b) __attribute__((always_inline)) {
    return readReg(SN7326_REG_STATUS, b);
  }

  inline sn7326_err_t writeReg(unsigned char regAddr, unsigned char val) __attribute__((always_inline)) {
    Wire.beginTransmission(_addr);
    if (Wire.write(regAddr))
      if (Wire.write(val)) {
        return (sn7326_err_t) Wire.endTransmission();
      }
    return SN7326_ERROR_WRITE_FAILED;
  }

  inline sn7326_err_t readReg(unsigned char regAddr, unsigned char &b) __attribute__((always_inline)) {
    Wire.beginTransmission(_addr);
    if (Wire.write(regAddr)) {
      sn7326_err_t err = (sn7326_err_t) Wire.endTransmission();
      if (err != (sn7326_err_t) I2C_ERROR_OK) {
        return err;
      }

      // Restart transmission
      Wire.beginTransmission(_addr);
      if (Wire.requestFrom(_addr, (uint8_t) 1)) {
        b = Wire.read();
        return (sn7326_err_t) Wire.endTransmission();
      }
      return SN7326_ERROR_REQUEST_FAILED;
    }
    return SN7326_ERROR_WRITE_FAILED;
  }

  void reset(void) {
    //Wire.reset();
    // TODO
  }
};

#endif // SN7326_h
