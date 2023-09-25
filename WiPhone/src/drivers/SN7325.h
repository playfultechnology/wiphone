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

#ifndef SN7325_h
#define SN7325_h

#include "Arduino.h"
#include <Wire.h>

#define SN7325_I2C_ADDR_BASE        B1011000        // 7-bit I2C address (AD0 = LOW, AD1 = LOW)

/* ----- Registers ----- */

// Port A = Open-drain (OD) port
// Port B = Push-pull (PP) port

#define SN7325_INPUT_PORT_A         ((unsigned char)0x00)
#define SN7325_INPUT_PORT_B         ((unsigned char)0x01)
#define SN7325_OUTPUT_PORT_A        ((unsigned char)0x02)
#define SN7325_OUTPUT_PORT_B        ((unsigned char)0x03)
#define SN7325_CONFIG_PORT_A        ((unsigned char)0x04)       // 1 - input, 0 - output
#define SN7325_CONFIG_PORT_B        ((unsigned char)0x05)
#define SN7325_INTERRUPT_PORT_A     ((unsigned char)0x06)       // 1 - interrupts OFF, 0 - interrupts ON
#define SN7325_INTERRUPT_PORT_B     ((unsigned char)0x07)

/* ----- Pins ----- */
#define EXTENDER_FLAG               0x80
/*     Port A: open-drain (OD)     */
#define EXTENDER_PIN_A0             (EXTENDER_FLAG |  0)
#define EXTENDER_PIN_A1             (EXTENDER_FLAG |  1)
#define EXTENDER_PIN_A2             (EXTENDER_FLAG |  2)
#define EXTENDER_PIN_A3             (EXTENDER_FLAG |  3)
#define EXTENDER_PIN_A4             (EXTENDER_FLAG |  4)
#define EXTENDER_PIN_A5             (EXTENDER_FLAG |  5)
#define EXTENDER_PIN_A6             (EXTENDER_FLAG |  6)
#define EXTENDER_PIN_A7             (EXTENDER_FLAG |  7)
/*     Port B: push-pull (PP)     */
#define EXTENDER_PIN_B0             (EXTENDER_FLAG |  8)
#define EXTENDER_PIN_B1             (EXTENDER_FLAG |  9)
#define EXTENDER_PIN_B2             (EXTENDER_FLAG | 10)
#define EXTENDER_PIN_B3             (EXTENDER_FLAG | 11)
#define EXTENDER_PIN_B4             (EXTENDER_FLAG | 12)
#define EXTENDER_PIN_B5             (EXTENDER_FLAG | 13)
#define EXTENDER_PIN_B6             (EXTENDER_FLAG | 14)
#define EXTENDER_PIN_B7             (EXTENDER_FLAG | 15)

#define EXTENDER_PIN_FLAG_A0        0x0001
#define EXTENDER_PIN_FLAG_A1        0x0002
#define EXTENDER_PIN_FLAG_A2        0x0004
#define EXTENDER_PIN_FLAG_A3        0x0008
#define EXTENDER_PIN_FLAG_A4        0x0010
#define EXTENDER_PIN_FLAG_A5        0x0020
#define EXTENDER_PIN_FLAG_A6        0x0040
#define EXTENDER_PIN_FLAG_A7        0x0080
#define EXTENDER_PIN_FLAG_B0        0x0100
#define EXTENDER_PIN_FLAG_B1        0x0200
#define EXTENDER_PIN_FLAG_B2        0x0400
#define EXTENDER_PIN_FLAG_B3        0x0800
#define EXTENDER_PIN_FLAG_B4        0x1000
#define EXTENDER_PIN_FLAG_B5        0x2000
#define EXTENDER_PIN_FLAG_B6        0x4000
#define EXTENDER_PIN_FLAG_B7        0x8000

typedef enum {
  SN7325_ERROR_OK             = I2C_ERROR_OK,
  SN7325_ERROR_DEV            = I2C_ERROR_DEV,
  SN7325_ERROR_ACK            = I2C_ERROR_ACK,
  SN7325_ERROR_TIMEOUT        = I2C_ERROR_TIMEOUT,
  SN7325_ERROR_BUS            = I2C_ERROR_BUS,
  SN7325_ERROR_BUSY           = I2C_ERROR_BUSY,
  SN7325_ERROR_WRITE_FAILED   = 11,
  SN7325_ERROR_REQUEST_FAILED = 12,
} sn7325_err_t;


class SN7325 {
protected:
  uint8_t   _addr;
  uint8_t   _sda;
  uint8_t   _scl;

  // Bitmask for input pins
  uint8_t   _portAInput;
  uint8_t   _portBInput;

  // Bitmask for output pins (1 - HIGH, 0 - LOW)
  uint8_t   _portAOutput;
  uint8_t   _portBOutput;

public:
  SN7325(uint8_t _addr, uint8_t _sda, uint8_t _scl) {
    this->_addr = _addr;
    this->_sda  = _sda;
    this->_scl  = _scl;

    // All output pins
    _portAInput = 0;
    _portAInput = 0;

    // All pins low
    _portAOutput = 0x0;
    _portAOutput = 0x0;
  }

  void connect() {
    Wire.begin(this->_sda, this->_scl);
    Wire.setClock(400000);
  }
  sn7325_err_t config(uint16_t portInputBA=0, uint16_t portOutputBA=0) {
    // Set initial state
    // - GPIO directions
    _portAInput = portInputBA & 0xFF;
    _portBInput = (portInputBA >> 8) & 0xFF;
    // - output state
    _portAOutput = portOutputBA & 0xFF;
    _portBOutput = (portOutputBA >> 8) & 0xFF;

    // Write it to registers
    sn7325_err_t err = writeReg(SN7325_CONFIG_PORT_A, _portAInput);
    if (err == SN7325_ERROR_OK) {
      err = writeReg(SN7325_CONFIG_PORT_B, _portBInput);
      if (err == SN7325_ERROR_OK) {
        err = writeReg(SN7325_OUTPUT_PORT_A, _portAOutput);
        if (err == SN7325_ERROR_OK) {
          err = writeReg(SN7325_OUTPUT_PORT_B, _portBOutput);
        }
      }
    }
    return err;
  }

  // Set bit -> interrupt enabled
  sn7325_err_t setInterrupts(uint16_t interruptsBA=0) {
    // Negate: set bit -> interrupt disabled
    uint8_t intA = (~interruptsBA) & 0xFF;
    uint8_t intB = ((~interruptsBA) >> 8) & 0xFF;

    // Write it to registers
    sn7325_err_t err = writeReg(SN7325_INTERRUPT_PORT_A, intA);
    if (err == SN7325_ERROR_OK) {
      err = writeReg(SN7325_INTERRUPT_PORT_B, intB);
    }
    return err;
  }

  void showState() {
    unsigned char res;
    sn7325_err_t err;
    log_d("State:");
    err = readReg(SN7325_INTERRUPT_PORT_A, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- interr A = 0x%c %d", res, HEX);
    }
    err = readReg(SN7325_INTERRUPT_PORT_B, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- interr B = 0x%x %d", res, HEX);
    }
    err = readReg(SN7325_CONFIG_PORT_A, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- config A = 0x%c %d", res, HEX);
    }
    err = readReg(SN7325_CONFIG_PORT_B, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- config B = 0x%c %d", res, HEX);
    }
    err = readReg(SN7325_INPUT_PORT_A, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- input A = 0x%c %d", res, HEX);
    }
    err = readReg(SN7325_INPUT_PORT_B, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- input B = 0x%c %d", res, HEX);
    }
    err = readReg(SN7325_OUTPUT_PORT_A, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- output A = 0x%c %d", res, HEX);
    }
    err = readReg(SN7325_OUTPUT_PORT_B, res);
    if (err == SN7325_ERROR_OK) {
      log_d("- output B = 0x%c %d", res, HEX);
    }
  }

  sn7325_err_t pinMode(uint8_t pin, uint8_t mode) {
    if (pin >= EXTENDER_PIN_B0) {
      setMode(_portBInput, pin-EXTENDER_PIN_B0, mode);
      return writeReg(SN7325_CONFIG_PORT_B, _portBInput);
    } else {
      setMode(_portAInput, pin-EXTENDER_PIN_A0, mode);
      return writeReg(SN7325_CONFIG_PORT_A, _portAInput);
    }
  }

  sn7325_err_t digitalWrite(uint8_t pin, uint8_t state) {
    if (pin >= EXTENDER_PIN_B0) {
      setOutput(_portBOutput, pin-EXTENDER_PIN_B0, state);
      return writeReg(SN7325_OUTPUT_PORT_B, _portBOutput);
    } else {
      setOutput(_portAOutput, pin-EXTENDER_PIN_A0, state);
      return writeReg(SN7325_OUTPUT_PORT_A, _portAOutput);
    }
  }

  uint8_t digitalRead(uint8_t pin) {
    // TODO: check for errors here
    if (pin >= EXTENDER_PIN_B0) {
      uint8_t reg;
      //sn7325_err_t err =
      readReg(SN7325_INPUT_PORT_B, reg);
      return (reg>>(pin-EXTENDER_PIN_B0)) & 1  ? HIGH : LOW;
    } else {
      uint8_t reg;
      //sn7325_err_t err =
      readReg(SN7325_INPUT_PORT_A, reg);
      return (reg>>(pin-EXTENDER_PIN_A0)) & 1  ? HIGH : LOW;
    }
  }

protected:
  void setMode(uint8_t &reg, uint8_t portPin, uint8_t mode) {
    if (mode == INPUT) {
      // Set bit for input
      reg |= (1 << portPin);
    } else {
      // Clear bit for output
      reg &= ~(1 << portPin);
    }
  }

  void setOutput(uint8_t &reg, uint8_t portPin, uint8_t state) {
    if (state == HIGH) {
      // Set bit for HIGH
      reg |= (1 << portPin);
    } else {
      // Clear bit for LOW
      reg &= ~(1 << portPin);
    }
  }

  sn7325_err_t writeReg(unsigned char regAddr, unsigned char val) {
    Wire.beginTransmission(_addr);
    if (Wire.write(regAddr))
      if (Wire.write(val)) {
        return (sn7325_err_t) Wire.endTransmission();
      }
    return SN7325_ERROR_WRITE_FAILED;
  }

public:
  sn7325_err_t readReg(unsigned char regAddr, unsigned char &b) {
    Wire.beginTransmission(_addr);
    if (Wire.write(regAddr)) {
      sn7325_err_t err = (sn7325_err_t) Wire.endTransmission();
      if (err != (sn7325_err_t) I2C_ERROR_OK) {
        return err;
      }

      // Restart transmission
      Wire.beginTransmission(_addr);
      if (Wire.requestFrom(_addr, (uint8_t) 1)) {
        b = Wire.read();
        return (sn7325_err_t) Wire.endTransmission();
      }
      return SN7325_ERROR_REQUEST_FAILED;
    }
    return SN7325_ERROR_WRITE_FAILED;
  }
};

#endif // SN7325_h
