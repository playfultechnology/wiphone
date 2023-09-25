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

#include "Hardware.h"
#include "driver/rmt.h"

#if GPIO_EXTENDER == 1509

// GPIO extender used from WiPhone ver.1.4 onward

rmt_item32_t rmtTxItems[] = {
  // Pull down for >500 us to turn OFF
  {{{ 1, 1, 550, 0 }}},
  // Three pulses of: 5 us ON, 5 us OFF
  {{{ 5, 1, 5, 0 }}},
  //amplifier will run in mode 1 with NCN-ON
//  {{{ 5, 1, 5, 0 }}},
//  {{{ 5, 1, 5, 0 }}},
  // End marker
  {{{ 0, 1, 0, 0 }}}
};

SX1509 gpioExtender;

void allPinMode(int16_t pin, int16_t mode) {
  if (!(pin & EXTENDER_FLAG) && pin >= 0) {
    pinMode(pin, mode);
  } else {
    gpioExtender.pinMode(pin ^ EXTENDER_FLAG, mode);
  }
}

bool allDigitalWrite(int16_t pin, int16_t val) {
  if (pin < 0) {
    return true;
  }
  if (pin & EXTENDER_FLAG) {
    gpioExtender.digitalWrite(pin ^ EXTENDER_FLAG, val);
  } else {
    digitalWrite(pin, val);
  }
  return true;
}

int allDigitalRead(uint8_t pin) {
  if (pin & EXTENDER_FLAG) {
    return gpioExtender.digitalRead(pin ^ EXTENDER_FLAG);
  } else {
    return digitalRead(pin);
  }
}

/* analogWrite values from 0 to 255 */
void allAnalogWrite(byte pin, byte val) {
  if (pin & EXTENDER_FLAG) {
    return gpioExtender.analogWrite(pin ^ EXTENDER_FLAG, val);
  } else {
    log_e("not implemented");
    // TODO: do this for ESP32
    //return analogWrite(pin, val);
  }
}

void lcdLedOnOff(bool turnOn) {
#if LCD_LED_PIN >= 0
#ifdef LCD_INVERTED_LED
  byte val = turnOn ? 255 : 0;      // It's unclear why doesn't it require inversion; TODO
#else
  byte val = turnOn ? 0 : 255;
#endif
  allAnalogWrite(LCD_LED_PIN, val);
#endif
}

void lcdLedOnOff(bool turnOn, uint8_t value) {
#if LCD_LED_PIN >= 0
#ifdef LCD_INVERTED_LED
  byte val = turnOn ? value : 0;      // It's unclear why doesn't it require inversion; TODO
#else
  byte val = turnOn ? 0 : value;
#endif
  log_d("LCD LED = %d", val);
  allAnalogWrite(LCD_LED_PIN, val);
#endif
}

#else

#if GPIO_EXTENDER == 7325

SN7325 gpioExtender(SN7325_I2C_ADDR_BASE + 1, I2C_SDA_PIN, I2C_SCK_PIN);

void allPinMode(int16_t pin, int16_t mode) {
  if (!(pin & EXTENDER_FLAG) && pin >= 0) {
    pinMode(pin, mode);
  }
  // TODO: modify extender registers here
}

bool allDigitalWrite(int16_t pin, int16_t val) {
  if (pin < 0) {
    return true;
  }
  if (pin & EXTENDER_FLAG) {
    return gpioExtender.digitalWrite(pin, val) == SN7325_ERROR_OK;
  } else {
    digitalWrite(pin, val);
    return true;
  }
}

void lcdLedOnOff(bool turnOn) {
#if LCD_LED_PIN >= 0
#ifdef LCD_INVERTED_LED
  int val = turnOn ? LOW : HIGH;
#else
  int val = turnOn ? HIGH : LOW;
#endif
  if (!allDigitalWrite(LCD_LED_PIN, val)) {
    log_e("backlight error");
  }
#endif
}

#endif // GPIO_EXTENDER == 7325
#endif // GPIO_EXTENDER == 1509

/*
 * Description:
 *     Initializes the RMT peripheral transmit channel.
 *     Based on the example:
 *         https://github.com/espressif/esp-idf/blob/6fe853a2c73437f74c0e6e79f9b15db68b231d32/examples/peripherals/rmt_tx/main/rmt_tx_main.c
 */
void rmtTxInit(int16_t rmtPin, bool idleHigh) {
  log_v("pin = %d, idle_level = %d", rmtPin, idleHigh);
  rmt_config_t config;

  // Common parameters
  config.rmt_mode       = RMT_MODE_TX;
  config.channel        = RMT_TX_CHANNEL;
  config.gpio_num       = (gpio_num_t)rmtPin;
  config.mem_block_num  = 1;
  config.clk_div        = 80;      // 80 MHz / 80 = 1 MHz (tick = 1 us)

  // TX-specific parameters
  config.tx_config.loop_en              = 0;    // disable looping
  config.tx_config.carrier_en           = 0;    // disable carrier
  config.tx_config.idle_output_en       = 1;    // enable idle output
  config.tx_config.idle_level           = idleHigh ? RMT_IDLE_LEVEL_HIGH : RMT_IDLE_LEVEL_LOW;
  config.tx_config.carrier_duty_percent = 50;
  config.tx_config.carrier_freq_hz      = 1000;
  config.tx_config.carrier_level        = RMT_CARRIER_LEVEL_HIGH;

  ESP_ERROR_CHECK(rmt_config(&config));
  ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
}

void amplifierEnable(int level) {
#if GPIO_EXTENDER == 1509
  if (level) {    // level from 1 (12 dB) to 4 (27.5 dB)
    rmt_set_idle_level(RMT_TX_CHANNEL, 1, RMT_IDLE_LEVEL_HIGH);
    rmt_write_items(RMT_TX_CHANNEL, rmtTxItems, sizeof(rmtTxItems)/sizeof(*rmtTxItems), true);
  } else {
    rmt_set_idle_level(RMT_TX_CHANNEL, 1, RMT_IDLE_LEVEL_LOW);
  }
#endif
}
