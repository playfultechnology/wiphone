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

#include <dummy.h>

#include <HTTPClient.h>
#include <HTTPUpdate.h>


// TODO:
// - check WiFi before calling => checked so issue#69 is fixed
// - check if using strncpy correctly (does not terminate with a nul)

#include "esp32-hal.h"
#include <stdio.h>
#include "GUI.h"
#include "tinySIP.h"
#include "config.h"
#include "clock.h"
#include "Audio.h"
#include "lwip/api.h"
#include <WiFi.h>
#include "Networks.h"
#include "esp_log.h"
#include <Update.h>
#include "ota.h"
#include "lora.h"
#include "esp_ota_ops.h"
#include "Test.h"

static bool been_in_verify = false;

#ifndef WIPHONE_PRODUCTION
#include "Test.h"
#endif
//#define UDP_SIP   no need this here
extern "C" bool verifyOta() {
  log_d("In verify ota");
  been_in_verify = true;
  return true;
}

static Ota ota("");

GUI gui;
uint32_t chipId = 0;

#ifdef LORA_MESSAGING
static Lora lora;
#endif

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  PERIPHERALS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

SN7326 keypad(SN7326_I2C_ADDR_BASE, I2C_SDA_PIN, I2C_SCK_PIN);
CW2015 gauge(CW2015_I2C_ADDR, I2C_SDA_PIN, I2C_SCK_PIN);

#if defined(MOTOR_DRIVER) && MOTOR_DRIVER == 8833
DRV8833 motorDriver = DRV8833();
#endif

#ifdef USER_SERIAL
HardwareSerial userSerial(2);
int userSerialLastSize = 0;
#endif

#ifdef USE_VIRTUAL_KEYBOARD
WiFiUDP *udpKeypad = NULL;
#endif

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  I2S AUDIO  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

Audio* audio;

void audio_test() {
  log_e("AUDIO TEST");
  if (audio->start()) {
    log_d("audio: started");
    audio->playRingtone(&SPIFFS);
    //audio->playFile(&SPIFFS, "/ringtone.mp3");
  } else {
    log_e("audio: failed");
  }
}

/* Description:
 *     Setup ringtone playback and vibration motor.
 *     For the ringtone to work, SPIFFS should have two files:
 *         ringtone.mp3
 *             - VERY preferably 16 KHz (or less) audio file
 *         ringtone.ini
 *             - INI file (compatible with NanoINI) which allows three configurations:
 *               - "vibro_on" - time the vibration motor is ON at a time, milliseconds
 *               - "vibro_off" - time the vibration motor is OFF at a time, milliseconds
 *               - "delay" - delay before the vibration motor turn ON for the first time, milliseconds
 *     TODO: generate both files if they are absent
 */
void startRingtone() {

  // Start audio
  audio->start();

  // Start playing ringtone
  if (!audio->playRingtone(&SPIFFS)) {
    log_d("ERROR: could not play file in SPIFFS");
  }

  // Initialize vibrating
  gui.state.vibroOn = false;
  gui.state.vibroToggledMs = millis();

  // Default configuration
  gui.state.vibroOnPeriodMs   = 500;
  gui.state.vibroOffPeriodMs  = 2500;
  gui.state.vibroDelayMs      = gui.state.vibroOnPeriodMs + gui.state.vibroOffPeriodMs;
  gui.state.vibroNextDelayMs  = gui.state.vibroDelayMs;

  // Initialize the keyboard LED and motor: both OFF
  allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
  allDigitalWrite(KEYBOARD_LED, HIGH);

  // Load vibromotor configuration
  IniFile ini("/ringtone.ini");
  if (ini.load() && !ini.isEmpty()) {
    gui.state.vibroOnPeriodMs  = ini[0].getIntValueSafe("vibro_on", gui.state.vibroOnPeriodMs);
    gui.state.vibroOffPeriodMs = ini[0].getIntValueSafe("vibro_off", gui.state.vibroOffPeriodMs);
    gui.state.vibroDelayMs     = ini[0].getIntValueSafe("delay", gui.state.vibroOnPeriodMs + gui.state.vibroOffPeriodMs);
    gui.state.vibroNextDelayMs = gui.state.vibroDelayMs;

    log_d("vibro on = %d", gui.state.vibroOnPeriodMs);
    log_d("vibro off = %d", gui.state.vibroOffPeriodMs);
    log_d("vibro delay = %d", gui.state.vibroDelayMs);
  }

  // Kickstart ringing
  gui.state.ringing = true;
}

/* Description:
 *     ringtone stops ringing in two cases:
 *       1) user reacted: accepted or declined an incoming call
 *       2) incoming call got cancelled before user reacted
 */
void stopRingtone() {
  audio->shutdown();
  gui.state.ringing = false;
  gui.state.vibroOn = false;
  allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
  allDigitalWrite(KEYBOARD_LED, HIGH);
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  HEADPHONE INTERRUPT  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

volatile bool headphoneEvent = false;

void IRAM_ATTR headphoneInterrupt() {
  headphoneEvent = true;
}

// Function that is called after interrupt occurs (not within interrupt)
void headphoneServiceInterrupt() {
#ifdef HEADPHONE_DETECT_PIN
  bool headphones = allDigitalRead(HEADPHONE_DETECT_PIN);
#else
  bool headphones = false;      // TODO: maybe make headphone detect for version 1.3
#endif // HEADPHONE_DETECT_PIN
  log_d("Headphones event = %d", headphones);
  audio->setHeadphones(headphones);
  headphoneEvent = false;
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  KEYPAD INTERRUPT & PROCESSING  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

#define KEYBOARD_BUFFER_LENGTH 13         // up to 12 keypresses can be remembered

volatile uint8_t keypadToRead = 0;
uint32_t keypadState = 0;        // 32-bit mask for current state of buttons
RingBuffer<char> keypadBuff(KEYBOARD_BUFFER_LENGTH);        // TODO: maybe used cbuf.h from ESP32 stack?

void IRAM_ATTR keyboardInterrupt() {
  // This function is intentionally minimal
  // (for example, adding a Serial output here produces ISR crashes)
  keypadToRead = 1;
}

// Function that is called after interrupt occurs (not within interrupt)
// Connect to keypad scanners via I2C and decode the keyboard events into buffer keypadBuff
void keyboardRead() {
  uint8_t key;
  uint32_t mask;
  uint32_t newState = 0;
  char c;
  do {
    key = 0;
    sn7326_err_t err = keypad.readKey(key);
    if (err) {
      log_d("keypad err code=%d", err);
    }
    if (err == SN7326_ERROR_BUSY) {
      log_d("i2c reset");
      keypad.reset();         // this seems to resolve rare event of complete hanging
    }

    // Decode lower 6 bits for character
    switch (key & B111111) {
#ifndef WIPHONE_KEYBOARD
    // 16-key stock keyboard
    case B001011:
      mask = WIPHONE_KEY_MASK_0;
      break;
    case B000000:
      mask = WIPHONE_KEY_MASK_1;
      break;
    case B001000:
      mask = WIPHONE_KEY_MASK_2;
      break;
    case B010000:
      mask = WIPHONE_KEY_MASK_3;
      break;
    case B000001:
      mask = WIPHONE_KEY_MASK_4;
      break;
    case B001001:
      mask = WIPHONE_KEY_MASK_5;
      break;
    case B010001:
      mask = WIPHONE_KEY_MASK_6;
      break;
    case B000010:
      mask = WIPHONE_KEY_MASK_7;
      break;
    case B001010:
      mask = WIPHONE_KEY_MASK_8;
      break;
    case B010010:
      mask = WIPHONE_KEY_MASK_9;
      break;
    case B000011:
      mask = WIPHONE_KEY_MASK_ASTERISK;
      break;
    case B010011:
      mask = WIPHONE_KEY_MASK_HASH;
      break;
    case B011000:
      mask = WIPHONE_KEY_MASK_UP;
      break;
    case B011001:
      mask = WIPHONE_KEY_MASK_BACK;
      break;
    case B011010:
      mask = WIPHONE_KEY_MASK_OK;
      break;
    case B011011:
      mask = WIPHONE_KEY_MASK_DOWN;
      break;
#else
    // 25-key WiPhone keyboard layout
    case B100001:
      mask = WIPHONE_KEY_MASK_0;
      break;
    case B1000:
      mask = WIPHONE_KEY_MASK_1;
      break;
    case B1001:
      mask = WIPHONE_KEY_MASK_2;
      break;
    case B1010:
      mask = WIPHONE_KEY_MASK_3;
      break;
    case B10000:
      mask = WIPHONE_KEY_MASK_4;
      break;
    case B10001:
      mask = WIPHONE_KEY_MASK_5;
      break;
    case B10010:
      mask = WIPHONE_KEY_MASK_6;
      break;
    case B11000:
      mask = WIPHONE_KEY_MASK_7;
      break;
    case B11001:
      mask = WIPHONE_KEY_MASK_8;
      break;
    case B11010:
      mask = WIPHONE_KEY_MASK_9;
      break;
    case B100000:
      mask = WIPHONE_KEY_MASK_ASTERISK;
      break;
    case B100010:
      mask = WIPHONE_KEY_MASK_HASH;
      break;
    case B10:
      mask = WIPHONE_KEY_MASK_UP;
      break;
    case B100100:
      mask = WIPHONE_KEY_MASK_BACK;
      break;
    case B10100:
      mask = WIPHONE_KEY_MASK_OK;
      break;
    case B1:
      mask = WIPHONE_KEY_MASK_DOWN;
      break;

    case B1100:
      mask = WIPHONE_KEY_MASK_LEFT;
      break;
    case B11100:
      mask = WIPHONE_KEY_MASK_RIFHT;
      break;
    case B100:
      mask = WIPHONE_KEY_MASK_SELECT;
      break;
    case B0:
      mask = WIPHONE_KEY_MASK_CALL;
      break;
    case B11:
      mask = WIPHONE_KEY_MASK_END;
      break;
    case B1011:
      mask = WIPHONE_KEY_MASK_F1;
      break;
    case B10011:
      mask = WIPHONE_KEY_MASK_F2;
      break;
    case B11011:
      mask = WIPHONE_KEY_MASK_F3;
      break;
    case B100011:
      mask = WIPHONE_KEY_MASK_F4;
      break;
#endif
    default:
      mask = 0;   // unknown button detected
    }

    // Decode "pressed/released" bit
    if (key & SN7326_PRESSED) {
      if (!(keypadState & mask)) {
        keypadState |= mask;
        newState |= mask;
        //Serial.print(c); Serial.println(" pressed");

        // Process key if there is still space left in the key buffer
        if (!keypadBuff.full()) {
          switch (mask) {
          case WIPHONE_KEY_MASK_0:
            c = '0';
            break;
          case WIPHONE_KEY_MASK_1:
            c = '1';
            break;
          case WIPHONE_KEY_MASK_2:
            c = '2';
            break;
          case WIPHONE_KEY_MASK_3:
            c = '3';
            break;
          case WIPHONE_KEY_MASK_4:
            c = '4';
            break;
          case WIPHONE_KEY_MASK_5:
            c = '5';
            break;
          case WIPHONE_KEY_MASK_6:
            c = '6';
            break;
          case WIPHONE_KEY_MASK_7:
            c = '7';
            break;
          case WIPHONE_KEY_MASK_8:
            c = '8';
            break;
          case WIPHONE_KEY_MASK_9:
            c = '9';
            break;
          case WIPHONE_KEY_MASK_ASTERISK:
            c = '*';
            break;
          case WIPHONE_KEY_MASK_HASH:
            c = '#';
            break;
          case WIPHONE_KEY_MASK_UP:
            c = WIPHONE_KEY_UP;
            break;
          case WIPHONE_KEY_MASK_BACK:
            c = WIPHONE_KEY_BACK;
            break;
          case WIPHONE_KEY_MASK_OK:
            c = WIPHONE_KEY_OK;
            break;
          case WIPHONE_KEY_MASK_DOWN:
            c = WIPHONE_KEY_DOWN;
            break;

          case WIPHONE_KEY_MASK_LEFT:
            c = WIPHONE_KEY_LEFT;
            break;
          case WIPHONE_KEY_MASK_RIFHT:
            c = WIPHONE_KEY_RIGHT;
            break;
          case WIPHONE_KEY_MASK_SELECT:
            c = WIPHONE_KEY_SELECT;
            break;
          case WIPHONE_KEY_MASK_CALL:
            c = WIPHONE_KEY_CALL;
            break;
          case WIPHONE_KEY_MASK_END:
            c = WIPHONE_KEY_END;
            break;
          case WIPHONE_KEY_MASK_F1:
            c = WIPHONE_KEY_F1;
            break;
          case WIPHONE_KEY_MASK_F2:
            c = WIPHONE_KEY_F2;
            break;
          case WIPHONE_KEY_MASK_F3:
            c = WIPHONE_KEY_F3;
            break;
          case WIPHONE_KEY_MASK_F4:
            c = WIPHONE_KEY_F4;
            break;

          default:
            c = 0;
          }
          if (c) {
            keypadBuff.put(c);
          }
        }
      }
    } else {
      if (keypadState & mask) {
        keypadState &= ~mask;
        //Serial.print(c); Serial.println(" released");
      }
    }
  } while (key & SN7326_MORE);       // decode "more" bit
  keypadToRead = 0;

  // Some buttons were "released" silently
  if (newState < keypadState) {
    keypadState = newState;
  }
}

#ifdef USE_VIRTUAL_KEYBOARD
void keyboardUdpRead() {
  if (udpKeypad && udpKeypad->parsePacket() > 0) {
    char buff[1000];
    int cb = udpKeypad->read(buff, sizeof(buff) - 1);
    buff[cb] = 0;
    //log_d("Keypad received: %s", buff);
    for (int i = 0; i < cb && !keypadBuff.full(); i++) {
      if (buff[i] == 10 || buff[i] == 13 || !buff[i]) {
        continue;
      }
      keypadBuff.put(buff[i]);
    }
  }
}
#endif

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  POWER/END BUTTON INTERRUPT  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

// Interrup routines to check for the power button presses, which is not connected to keypad scanner, but via a dedicated GPIO extender pin

bool powerButtonPressed = false;
bool poweringOff = false;
volatile bool gpioExtenderEvent = false;

void IRAM_ATTR gpioExtenderInterrupt() {
  // This function is intentionally minimal
  gpioExtenderEvent = true;
}

// Function that is called after interrupt occurs (not within interrupt)
bool gpioExtenderServiceInterrupt() {
  gpioExtenderEvent = false;
  bool powerButton = gpioExtender.digitalRead(POWER_CHECK & ~EXTENDER_FLAG) == LOW;
  //log_d("powerButton = %d", powerButton);
  if (powerButton != powerButtonPressed) {
    powerButtonPressed = powerButton;
    if (powerButton) {
      keypadBuff.put(WIPHONE_KEY_END);
    }
    return true;
  }
  return false;
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  SETUP  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void setup() {
  // Initialize serial
  const uart_config_t uart_config = {
    .baud_rate = SERIAL_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  int RX_BUF_SIZE =  1024;

  uart_param_config(UART_NUM_0, &uart_config);
  uart_driver_install(UART_NUM_0, RX_BUF_SIZE * 2, 0, 0, NULL, 0);

  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }

  log_i("\r\nChip id: %X %d %d", chipId, ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));
  log_i("Firmware version: %s", FIRMWARE_VERSION);

  // Initialize I2C and wake up battery gauge first
  gauge.connect();
#if defined(WIPHONE_BOARD) || defined(WIPHONE_INTEGRATED)
  gui.state.gaugeInited = gauge.configure();
  log_d("\r\nBattery gauge: %s\n", gui.state.gaugeInited ? "OK" : "FAILED");
  delay(10);
#endif // WIPHONE_BOARD || WIPHONE_INTEGRATED

  log_v("Free memory after gauge: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Initialize GPIO extender
#ifdef WIPHONE_INTEGRATED_1_4
  if (gpioExtender.begin()) {
    log_v("extender succ");
    gui.state.extenderInited = true;
    // Input
    allPinMode(POWER_CHECK, INPUT);
    allPinMode(TF_CARD_DETECT_PIN, INPUT);
    allPinMode(BATTERY_CHARGING_STATUS_PIN, INPUT);
    // Output
    allPinMode(KEYBOARD_LED, OUTPUT);
    allPinMode(VIBRO_MOTOR_CONTROL, OUTPUT);
    allPinMode(POWER_CONTROL, OUTPUT);
    // Default state
    allDigitalWrite(POWER_CONTROL, LOW);
    allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
    allDigitalWrite(KEYBOARD_LED, HIGH);
  } else {
    log_e("extender failed");
    gui.state.extenderInited = false;
  }
#else // not WIPHONE_INTEGRATED_1_4
#ifdef WIPHONE_INTEGRATED_1_3
  {
    auto err = gpioExtender.config(
                 // Input pins
                 EXTENDER_PIN_FLAG_A2 | EXTENDER_PIN_FLAG_B1,
                 // Output HIGH
                 EXTENDER_PIN_FLAG_A0 | EXTENDER_PIN_FLAG_A2 | EXTENDER_PIN_FLAG_B0 | EXTENDER_PIN_FLAG_B1 | EXTENDER_PIN_FLAG_B7
               );
    if (err != SN7325_ERROR_OK) {
      log_d("GPIO extender error = %d", err);
    }
    gpioExtender.showState();
  }
#else // not WIPHONE_INTEGRATED_1_3
#ifdef WIPHONE_INTEGRATED_1
  {
    auto err = gpioExtender.config(
                 // Input pins
                 EXTENDER_PIN_FLAG_A2 | EXTENDER_PIN_FLAG_B1,
                 // Output HIGH
                 EXTENDER_PIN_FLAG_A0 | EXTENDER_PIN_FLAG_A1 | EXTENDER_PIN_FLAG_B0 | EXTENDER_PIN_FLAG_B1
               );
    if (err != SN7325_ERROR_OK) {
      log_d("GPIO extender error = %d", err);
    }
    gui.state.extenderInited = (err == SN7325_ERROR_OK);
    err = gpioExtender.setInterrupts(EXTENDER_PIN_FLAG_A2);     // POWER_OFF interrupt
    if (err != SN7325_ERROR_OK) {
      log_d("GPIO extender error = %d", err);
    }
    gpioExtender.showState();
  }
#endif // WIPHONE_INTEGRATED_1
#endif // WIPHONE_INTEGRATED_1_3
#endif // WIPHONE_INTEGRATED_1_4

  log_v("Free memory after integrated: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Fast test of PSRAM presence
  void* p = heap_caps_malloc(100000, MALLOC_CAP_SPIRAM);
  if (p != NULL) {
    gui.state.psramInited = true;
    freeNull((void **) &p);
  }

  log_v("Free memory after psram: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Initialize power
#if defined(POWER_CONTROL) && POWER_CONTROL >= 0
  allPinMode(POWER_CONTROL, OUTPUT);
  allDigitalWrite(POWER_CONTROL, LOW);
#endif

  log_v("Free memory after power: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

#if defined(POWER_CHECK) && POWER_CHECK >= 0
#ifdef WIPHONE_INTEGRATED_1_4
  log_d("enabling interrupt (rev1.4)");
  gpioExtender.enableInterrupt(2, FALLING);
#else // not WIPHONE_INTEGRATED_1_4
#ifdef WIPHONE_INTEGRATED_1_3
  log_d("enabling interrupt input (rev1.3)");
  allPinMode(POWER_CHECK, INPUT_PULLUP);
#endif // WIPHONE_INTEGRATED_1_3
#endif // WIPHONE_INTEGRATED_1_4
#endif // defined(POWER_CHECK) && POWER_CHECK >= 0

  Random.feed(micros());      // TODO: maybe feed microphone data to Random.feed()

  // Mounter internal filesystem
  if (SPIFFS.begin()) {
    log_d("SPI filesystem mounted");
  } else {
    log_d("SPI filesystem mount FAILED");
  }

  log_v("Free memory after internal fs: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Initialize GUI
  log_d("Initializing screen");
  gui.init(lcdLedOnOff);
  gui.redrawScreen(false, false, true);   // only screen

  log_v("Free memory after gui: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

#if LCD_LED_PIN >= 0
  // Turn on backlight
  log_d("LCD_LED_PIN = %d", LCD_LED_PIN);
  allPinMode(LCD_LED_PIN, OUTPUT);
#if GPIO_EXTENDER == 1509
  gpioExtender.ledDriverInit(LCD_LED_PIN ^ EXTENDER_FLAG);
#else
  allPinMode(LCD_LED_PIN, OUTPUT);
#endif // GPIO_EXTENDER == 1509
  gui.toggleScreen();
#endif

  log_v("Free memory after lcd led: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Mount SD card and SPIFFS (AFTER the screen & SPI initialization)
#if defined(WIPHONE_BOARD) || defined(WIPHONE_INTEGRATED)

  // Init SD card detection pin
  allPinMode(TF_CARD_DETECT_PIN, INPUT);

  // Initilize hardware serial:
  gui.state.battVoltage = gauge.readVoltage();
  gui.state.battSoc = gauge.readSocPrecise();
  log_d("Voltage = %.2f", gui.state.battVoltage);
  log_d("SOC = %.1f", gui.state.battSoc);
#endif // WIPHONE_BOARD || WIPHONE_INTEGRATED

  log_v("Free memory after sd spiffs: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // If voltage is extremely low, power off immediately
  if (gui.state.battVoltage < 3.1) {
    powerOff();
    gui.processEvent(millis(), POWER_OFF_EVENT);
  }

  // INITIALIZE OTHER I2C DEVICES

  // Battery gauge
#if defined(WIPHONE_BOARD) || defined(WIPHONE_INTEGRATED)
  gauge.showVersion();
#endif // WIPHONE_BOARD

  // Mount SD card

  if (SD.begin(SD_CARD_CS_PIN, SPI, SD_CARD_FREQUENCY)) {
    log_d("Card mounted");
  } /*else {
    log_d("Card mount FAILED");
  }*/


  // Initialize keypad
  {
    //keypad.connect();     // sets I2C speed to 400 kHz (as per datasheet)
    sn7326_err_t err = keypad.config();
    if (err != SN7326_ERROR_OK) {
      log_d("keypad error = %d", err);
    }
    gui.state.scannerInited = (err == SN7326_ERROR_OK);
  }

  log_v("Free memory after keypad: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Initialize RMT peripheral to enable the amplifier
#ifdef WIPHONE_INTEGRATED_1_4
  rmtTxInit(AMPLIFIER_SHUTDOWN, false);
#else
#ifdef WIPHONE_INTEGRATED_1_3
  // ?
#endif // WIPHONE_INTEGRATED_1_3
#endif // WIPHONE_INTEGRATED_1_4

  // Initialize audio systems (hardware codec and I2S peripheral)
#ifdef I2S_MCLK_GPIO0
  {
    // Use GPIO_0 for providing MCLK to the audio codec IC
    //SET_PERI_REG_BITS(PIN_CTRL, CLK_OUT1, 0, CLK_OUT1_S);       // esp-adf
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1); // esp-adf
    REG_SET_FIELD(PIN_CTRL, CLK_OUT1, 0);                         // Source: https://esp32.com/viewtopic.php?t=1521
  }
#endif // I2S_MCLK_GPIO0


  log_v("Free memory after config files: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // GPIOs

#ifdef WIPHONE_INTEGRATED
  pinMode(BATTERY_CHARGING_STATUS_PIN, INPUT);
  pinMode(BATTERY_PPR_PIN, INPUT);
#endif // WIPHONE_INTEGRATED
#ifdef WIPHONE_BOARD
  pinMode(BATTERY_CHARGING_STATUS_PIN, INPUT);
  //pinMode(USB_POWER_DETECT_PIN, INPUT);
#endif // WIPHONE_BOARD

#if defined(KEYBOARD_RESET_PIN) && KEYBOARD_RESET_PIN > 0
  pinMode(KEYBOARD_RESET_PIN, OUTPUT);         // keypad RST
  digitalWrite(KEYBOARD_RESET_PIN, HIGH);      // no RST
  delay(1);                    // 1 ms
  digitalWrite(KEYBOARD_RESET_PIN, LOW);       // RST
  delay(1);                    // 1 ms
  digitalWrite(KEYBOARD_RESET_PIN, HIGH);      // no RST
#endif

  pinMode(KEYBOARD_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(KEYBOARD_INTERRUPT_PIN), keyboardInterrupt, FALLING);
  pinMode(GPIO_EXTENDER_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_EXTENDER_INTERRUPT_PIN), gpioExtenderInterrupt, FALLING);


#if defined(MOTOR_DRIVER) && MOTOR_DRIVER == 8833
  motorDriver.attachMotorA(AIN1, AIN2);
  motorDriver.attachMotorB(BIN1, BIN2);
  pinMode(MotorEN , OUTPUT);
  digitalWrite(MotorEN , LOW);
#endif

  //esp_pm_config_esp32_t conf = { RTC_CPU_FREQ_240M, 240, RTC_CPU_FREQ_80M, 10, true };
//  esp_pm_config_esp32_t conf = { RTC_CPU_FREQ_240M, RTC_CPU_FREQ_80M, true };
//  esp_err_t err;
//  err = esp_pm_configure((const void*) &conf);
//  log_d("Power management: %d", (int) err);

#ifdef USER_SERIAL
  userSerial.begin(USER_SERIAL_BAUD, USER_SERIAL_CONFIG, USER_SERIAL_RX, USER_SERIAL_TX);
  //allDigitalWrite(EXTENDER_PIN_B0, HIGH);     // TODO: why do we do this?
#endif


  printf("\r\nBooting...\r\n");

  uint8_t mac[6];
  wifiState.getMac(mac);

  nvs_stats_t nvs_stats;
  nvs_get_stats(NULL, &nvs_stats);
  log_d("NVS stats: UsedEntries = %d, FreeEntries = %d, AllEntries = %d\n",
        nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

  gui.loadSettings();
  gui.reloadMessages();

  log_v("Free memory after reload messages: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  if (gui.state.dimming || gui.state.sleeping) {
    uint32_t now = millis();
    if (gui.state.doDimming()) {
      gui.state.scheduleEvent(SCREEN_DIM_EVENT, now + gui.state.dimAfterMs*2);
    }
    if (gui.state.doSleeping()) {
      gui.state.scheduleEvent(SCREEN_SLEEP_EVENT, now + gui.state.sleepAfterMs*2);
    }
  }

  log_v("Free memory after gui sleeping: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // Set thread priorities (NOTE: in FreeRTOS, tasks with higher priorities will always be exectuted first, unless they have nothing to do)
  uint32_t mainThreadPrio = ESP_TASK_TCPIP_PRIO >> 1;
  if (tskIDLE_PRIORITY >= mainThreadPrio) {
    mainThreadPrio = tskIDLE_PRIORITY + 1;
  }
  log_i("lwIP thread priority: %d", ESP_TASK_TCPIP_PRIO);
  log_i("Main loop thread priority: %d", mainThreadPrio);
  log_i("Idle task priority: %d", tskIDLE_PRIORITY);
  log_i("Tick period: %d ms", portTICK_PERIOD_MS);
  vTaskPrioritySet(NULL, mainThreadPrio);

  log_v("Free memory after vTaskPrioritySet: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));


  wifiState.init();

  log_v("Free memory after wifi init: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  if (!wifiState.hasPreferredSsid()) {
    wifiState.disable();  // if we don't have a saved SSID to connect to, turn off WiFi to save power
  }

  int counter = 0;
  wifiState.loadPreferred();

  if (!wifiState.userDisabled()) {
    wifiState.connectToPreferred();
    while (!wifiState.isConnected() && ++counter < 10) {
      log_v("Waiting for wifi: %d %d", counter, ESP.getFreeHeap());
      wifiState.connectToPreferred();
      delay(500);
    }
  }

  if (!ota.hasJustUpdated() && ota.userRequestedUpdate()) {
    gui.drawOtaUpdate();
    ota.doUpdate();
  } else if (!ota.hasJustUpdated() && ota.updateExists() && (ota.autoUpdateEnabled() || ota.userRequestedUpdate())) {
    gui.drawOtaUpdate();
    ota.doUpdate();
  }

  ota.setUserRequestedUpdate(false);

  static Audio audio_local(true, I2S_BCK_PIN, I2S_WS_PIN, I2S_MOSI_PIN, I2S_MISO_PIN);
  audio = &audio_local;
  gui.state.codecInited = !audio->error();

  // Load phone configs
  {
    CriticalFile ini(Storage::ConfigsFile);
    if ((ini.load() || ini.restore()) && !ini.isEmpty()) {
      if (ini[0].hasKey("v") && !strcmp(ini[0]["v"], "1")) {    // check version of the file format

        // Load audio volume
        if (ini.hasSection("audio")) {
          int8_t speakerVol, headphonesVol, loudspeakerVol;
          audio->getVolumes(speakerVol, headphonesVol, loudspeakerVol);      // default values
          speakerVol = ini["audio"].getIntValueSafe("speaker_vol", speakerVol);
          headphonesVol = ini["audio"].getIntValueSafe("headphones_vol", headphonesVol);
          loudspeakerVol = ini["audio"].getIntValueSafe("loudspeaker_vol", loudspeakerVol);
          audio->setVolumes(speakerVol, headphonesVol, loudspeakerVol);
          log_d("loaded volume: earpiece = %d dB, headphones = %d dB, loudspeaker = %d dB", speakerVol, headphonesVol, loudspeakerVol);
          log_i("loaded volume: earpiece = %d dB, headphones = %d dB, loudspeaker = %d dB", speakerVol, headphonesVol, loudspeakerVol);
        }

        // Load timezone config
        if (ini.hasSection("time")) {
          float tz = ini["time"].getFloatValueSafe("zone", 0);
          ntpClock.setTimeZone(tz);
        }

        // Load screen dimming & sleeping config
        if (ini.hasSection("screen")) {
          gui.state.brightLevel = ini["screen"].getIntValueSafe("bright_level", 100);
          gui.state.dimming = ini["screen"].getIntValueSafe("dimming", 0) > 0;
          gui.state.dimLevel = ini["screen"].getIntValueSafe("dim_level", 15);
          gui.state.dimAfterMs = ini["screen"].getIntValueSafe("dim_after_s", 20)*1000;
          gui.state.sleeping = ini["screen"].getIntValueSafe("sleeping", 0) > 0;
          gui.state.sleepAfterMs = ini["screen"].getIntValueSafe("sleep_after_s", 30)*1000;
          gui.state.screenBrightness = gui.state.brightLevel-1; // Forces the new brightness setting to be applied
          gui.processEvent(0, 0); // Need to call gui event loop so brightness settings are applied
        } else {
          gui.state.brightLevel = 100;
          gui.state.dimming = true;
          gui.state.dimLevel = 15;
          gui.state.dimAfterMs = 20000;
          gui.state.sleeping = true;
          gui.state.sleepAfterMs = 30000;
        }

        // Load keypad locking config
        gui.state.locking = ini.hasSection("lock") ? ini["lock"].getIntValueSafe("lock_keyboard", 0) : 1;
      }
    }
    gui.setAudio(audio);
  }

#ifdef HEADPHONE_DETECT_PIN
  pinMode(HEADPHONE_DETECT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HEADPHONE_DETECT_PIN), headphoneInterrupt, CHANGE);
  bool headphones = allDigitalRead(HEADPHONE_DETECT_PIN);
  audio->setHeadphones(headphones);
#endif // HEADPHONE_DETECT_PIN

  log_v("Free memory after audio: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  ntpClock.startUpdates();

  // Setup for LoRa messaging
#ifdef LORA_MESSAGING
  lora.setup();
#endif

  log_d("WiPhone, firmware date = " __DATE__);

#ifdef CONFIG_APP_ROLLBACK_ENABLE
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    log_i("Got partition state: %d %d %d", ota_state, ESP_OTA_IMG_PENDING_VERIFY, been_in_verify);
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      log_i("Committing update");
      ota.commitUpdate();
    }
  }

#endif

  printf("\r\nBooted\r\n");

  gui.state.booted = true;

  log_d("# # # # # # # # # # # # # # # # # # # # # # # # # # # #  END OF SETUP  # # # # # # # # # # # # # # # # # # # # # # # # # # # # ");
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  MAIN LOOP  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

TinySIP sip;
char lastKeys[7];         // TODO: use RingBuffer?
uint32_t msLastKeyPress = 0;        // for any button being pressed
uint32_t msLastKeyInput = 0;        // for the keyboard timeouts during alphanumeric intputs
uint32_t msPowerOffStarted = 0;
uint32_t msHangingUp = 0;
uint32_t msHungUp = 0;
uint32_t msLastRtpPacket = 0;
uint32_t msLastBatt = 0;
uint32_t msLastUsbCheck = 0;
uint32_t msLastMinute = 0;
uint32_t msLastWifiRetry = -WIFI_RETRY_PERIOD_MS;
uint32_t msLastWiFiRssi = -WIFI_CHECK_PERIOD_MS;
uint8_t  rtpSilentCnt = 0x0;  // for the other praty rtp stream silent detection
bool keypadLedsOn = false;
bool updateMessageTimes = false;
bool waitingForClockUpdate = true;
uint8_t wifiTerminateSip = 0x0;

int8_t restoreSpeakerVol, restoreHeadphonesVol, restoreLoudspeakerVol;

uint32_t usbConnected = 0;        // DEBUG
uint32_t usbConnectedChecks = 0;  // DEBUG

bool lastTurnOff = true;

//uint32_t msProfileStart = 0;
//LinearArray<uint32_t, false> msProfile;

uint32_t last_lora_send = 0;

void loop() {
  while (1) {
    uint32_t now = millis();
    // DEBUG
    //uint32_t loopTime = micros();
    //if (!msProfileStart) msProfileStart = loopTime;

    appEventResult redrawWhat = DO_NOTHING;

    // Power button check
    if (gpioExtenderEvent) {
      if (gpioExtenderServiceInterrupt()) {
        if (powerButtonPressed) {
          msPowerOffStarted = now;
        } else if (poweringOff) {
          redrawWhat |= gui.processEvent(now, POWER_NOT_OFF_EVENT);
          poweringOff = false;
        }
      }
    }

    // Headphone check
    if (headphoneEvent) {
      headphoneServiceInterrupt();
    }

    // KEYPAD + TICK

    // Check if interrupt occured
    uint8_t toRead = keypadToRead;

    // Read all the keys to buffer
    if (toRead) {
      keyboardRead();
    }
#ifdef USE_VIRTUAL_KEYBOARD
    keyboardUdpRead();
#endif

    // Process keys buffer
    EventType keyPressed;
    bool anyPressed = false;
    while (!keypadBuff.empty()) {
      // Retrieve button from buffer safely
      keyPressed = keypadBuff.get();


      // Process key
      Random.feed(rotate5(now) ^ keyPressed);

      // Shift old characters and remember current character
      for (uint8_t k = sizeof(lastKeys) - 1; k > 0; k--) {
        lastKeys[k] = lastKeys[k - 1];
      }
      lastKeys[0] = keyPressed;

      if (!anyPressed && gui.state.inputType == InputType::AlphaNum) {
        msLastKeyInput = now;
      }
      anyPressed = true;

      // Process key
#ifndef STEAL_THE_USER_BUTTONS
      if (keyPressed == WIPHONE_KEY_F1) {
      }

      if (keyPressed == WIPHONE_KEY_F2) {
      }

      if (keyPressed == WIPHONE_KEY_F3) {
      }

      if (keyPressed == WIPHONE_KEY_F4) {
      }

      if (keyPressed == WIPHONE_KEY_END) {
        gui.state.setSipState(CallState::HangUp);
      }




#else
      if (keyPressed == WIPHONE_KEY_F1) {
        gui.toggleScreen();
        //allDigitalWrite(ENABLE_DAUGHTER_33V, HIGH);
        //gui.longBatteryAnimation();
        allDigitalWrite(VIBRO_MOTOR_CONTROL, HIGH);
        allDigitalWrite(KEYBOARD_LED, LOW);
        delay(500);
        allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
        allDigitalWrite(KEYBOARD_LED, HIGH);
        gui.toggleScreen();
      }

      if (keyPressed == WIPHONE_KEY_F2) {
        audio_test();
      }

      if (keyPressed == WIPHONE_KEY_F3) {
        bool isLoud = !audio->isLoudspeaker();
        log_d("Loudspeaker: %d", isLoud);
        audio->chooseSpeaker(isLoud);
        log_d("WiFi Mode: %d", WiFi.getMode());
        log_d("WiFi Status: %d", WiFi.status());
      }

      if (keyPressed == WIPHONE_KEY_F4) {
        //esp_sleep_enable_ext0_wakeup((gpio_num_t)KEYBOARD_INTERRUPT_PIN,1);
        //esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)KEYBOARD_INTERRUPT_PIN,0); //1 = Low to High, 0 = High to Low. Pin pulled HIGH
        //esp_sleep_enable_timer_wakeup(5000000);
        log_d("begin delay");
        delay(500); // debounce
        log_d("begin light sleep");
        delay(10); // let print finish
        //esp_deep_sleep_start();
        esp_light_sleep_start();
        log_d("awake!!!");

        //LOG_MEM_STATUS;

        //gui.setDumpRegion();
        //gui.frameToSerial();    // "screenshot"
      }
#endif

      // Process event in GUI
      redrawWhat |= gui.processEvent(now, keyPressed);
      //log_d("redrawWhat = 0x%x", redrawWhat);

      // Check for "Easter eggs"
      // Easter eggs are currently broken, probably because we now use numeric key entry to dial numbers by default, and can't type '*'
      // (this is not the only thing preventing it from working, but after checking the lack of '*' I stopped)
      if (!memcmp(lastKeys, "##", 2)) {
        // check for Easter eggs
        if (!memcmp(lastKeys + 2, "101**", 5)) {    // **101##   +
          log_d("Easter egg = 101: starting an SIP client");
          gui.state.setSipState(CallState::InvitingCallee);
        } else if (!memcmp(lastKeys + 2, "301**", 5)) {    // **103##
          log_d("Easter egg = 103: send register request");
          sip.registration();
        } else if (!memcmp(lastKeys + 2, "601**", 5)) {    // **106##
          //log_d("Easter egg = 106: send message test");
          log_d("Easter egg = 106: send message test. Add a sip account in WiPhone.ino to use this test");
          sip.sendMessage("sip:user@host.com", "Hello from WiPhone");
        } else if (!memcmp(lastKeys + 2, "701", 5)) {    // **107##
          log_d("Easter egg = 107: test motor and blink LED");
          allDigitalWrite(VIBRO_MOTOR_CONTROL, HIGH);
          allDigitalWrite(KEYBOARD_LED, LOW);
          delay(2500);
          allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
          allDigitalWrite(KEYBOARD_LED, HIGH);
        } else if (!memcmp(lastKeys + 2, "801**", 5)) {    // **108## - soft off
          log_d("Easter egg = 108: soft off");
          allDigitalWrite(POWER_CONTROL, HIGH);

          // AUDIO

        } else if (!memcmp(lastKeys + 2, "002**", 5)) {    // **200##
          log_d("Easter egg = 200: audio test on");
          audio_test();
        } else if (!memcmp(lastKeys + 2, "102**", 5)) {    // **201##
          log_d("Easter egg = 201: audio shutdown (audio test off)");
          audio->shutdown();
        } else if (!memcmp(lastKeys + 2, "202**", 5)) {    // **202##
          log_d("Easter egg = 202: sending RTP stream from microphone");
          audio->openRtpConnection(5000);
          audio->sendRtpStreamFromMic(Audio::G722_RTP_PAYLOAD, IPAddress(192, 168, 1, 15), 5000);
        } else if (!memcmp(lastKeys + 2, "302**", 5)) {    // **203##   - (receiving from distant server)
          log_d("Easter egg = 203: play incoming RTP stream");
          audio->openRtpConnection(5000);
          audio->playRtpStream(Audio::G722_RTP_PAYLOAD);
        } else if (!memcmp(lastKeys + 2, "402**", 5)) {    // **204##
          log_d("Easter egg = 204: recording mic audio");
          audio->setBitsPerSample(16);
          audio->setSampleRate(16000);
          audio->setMonoOutput(true);
          audio->recordFromMic();
        } else if (!memcmp(lastKeys + 2, "502**", 5)) {    // **205##
          log_d("Easter egg = 205: stop recording WAV");
          char filename[100];
          sprintf(filename, "/audio_%02d%02d%02d%02d%02d%02d.pcm", ntpClock.getYear()-2000, ntpClock.getMonth(), ntpClock.getDay(), ntpClock.getHour(), ntpClock.getMinute(), ntpClock.getSecond());
          log_d("creating file %s", filename);
          audio->saveWavRecord(&SD, filename);
          audio->shutdown();

          // RINGTONE

        } else if (!memcmp(lastKeys + 2, "103**", 5)) {    // **301## - ringtone on
          log_d("Easter egg = 301: ringtone on");
          startRingtone();
        } else if (!memcmp(lastKeys + 2, "203**", 5)) {    // **302## - ringtone off
          log_d("Easter egg = 302: ringtone off");
          stopRingtone();
        }
#ifdef _WIPHONE_TEST_H_
        else {
          anyPressed = easteregg_tests(lastKeys, anyPressed);
        }
#endif
      }
    }

    // Turn on/off the keypad LEDs for 5s
    if (anyPressed) {
      msLastKeyPress = now;       // this is also used to idle keyboard event (when a symbol gets chosen via waiting)
      if (!keypadLedsOn) {
        // Turn on LEDs
        allDigitalWrite(KEYBOARD_LED, LOW);
        keypadLedsOn = true;
      }
    } else if (keypadLedsOn && elapsedMillis(now, msLastKeyPress, KEYPAD_LEDS_ON_MS)) {
      // Turn off LEDs
      allDigitalWrite(KEYBOARD_LED, HIGH);
      keypadLedsOn = false;
    }

    // Connect to WiFi
    if (wifiState.doReconnect() && !wifiState.isConnected() && elapsedMillis(now, msLastWifiRetry, WIFI_RETRY_PERIOD_MS) && !wifiState.userDisabled()) {
      if (wifiState.connectToPreferred()) {
        log_d("Connecting to WiFi");
      } else {
        log_d("Not connecting to WiFi");
      }
      msLastWifiRetry = now;        // TODO: encapsulate into WiFiState
    }

#ifdef USE_VIRTUAL_KEYBOARD
    if (udpKeypad == NULL && wifiState.isConnected()) {
      log_d("Setting a connection");
      udpKeypad = new WiFiUDP();
      udpKeypad->begin(VIRTUAL_KEYBOARD_PORT); // need to check this for memory leak... does it get called repeatedly?
      IPAddress ipAddr = WiFi.localIP();
      log_d("Send UDP packets to:\n%d.%d.%d.%d:%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3], VIRTUAL_KEYBOARD_PORT);
    }
#endif

    // Trigger periodic event: APP_TIMER_EVENT
    if (gui.state.msAppTimerEventPeriod > 0) {
      if (elapsedMillis(now, gui.state.msAppTimerEventLast, gui.state.msAppTimerEventPeriod)) {
        redrawWhat |= gui.processEvent(now, APP_TIMER_EVENT);
        gui.state.msAppTimerEventLast = now;
      }
    }

    // Trigger scheduled events
    EventType evnt;
    while (evnt = gui.state.popEvent(now)) {
      redrawWhat |= gui.processEvent(now, evnt);
    }

    // Update message times
    if (updateMessageTimes) {
      gui.reloadMessages();
      updateMessageTimes = false;
    }

    /*if (wifiState.isConnected()) {
      const char* host = "phonetester";
      MDNSResponder mdnsResponder; // = new MDNSResponder();
      if (mdnsResponder.begin("WiPhone")) {
        IPAddress addr = mdnsResponder.queryHost(host, 2000);
        log_d("Address: %d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
      }
      // const char* host = "phonetester";
      // resolveMdns(host);
    }*/

    // Clock update
    if (ntpClock.isUpdated()) {
      if (waitingForClockUpdate) {
        updateMessageTimes = true;
        waitingForClockUpdate = false;
      }
      msLastMinute = now;
      redrawWhat |= gui.processEvent(now, TIME_UPDATE_EVENT);
    } else if (elapsedMillis(now, msLastMinute, TIME_UPDATE_MINUTE_MS) || (ntpClock.getSecond() > 0 && elapsedMillis(now, msLastMinute, TIME_UPDATE_MINUTE_MS - ntpClock.getSecond() * 1000))) {
      // Tick at the beginning of next minute (or tick if not ticked for more than a minute)
      ntpClock.minuteTick(now);
      msLastMinute = now;
      redrawWhat |= gui.processEvent(now, TIME_UPDATE_EVENT);
    }

//    // Turn OFF?
//    {
//      bool turnOff = gpioExtender.digitalRead(EXTENDER_PIN_B1) == HIGH ? true : false;
//      if (turnOff != lastTurnOff) {
//        lastTurnOff = turnOff;
//        log_d("TURN %s", turnOff ? "YES" : "NO");
//        if (! turnOff) {
//          log_d("BYE");
//          delay(2000);
//          allDigitalWrite(EXTENDER_PIN_B0, LOW);
//        }
//      }
//    }

    // Battery update
#ifdef WIPHONE_BOARD

    // Check battery state
    if (elapsedMillis(now, msLastBatt, BATTERY_CHECK_PERIOD_MS)) {
      msLastBatt = now;
      float v = gauge.readVoltage();
      if (v > 0.0) {
        gui.state.battVoltage = v;
      }
      float soc = gauge.readSocPrecise();
      if (soc > 0.0) {
        gui.state.battSoc = soc;
      }
      gui.state.battUpdated = true;
#if TF_CARD_DETECT_PIN >= 0
      gui.state.cardPresent = digitalRead(TF_CARD_DETECT_PIN) == LOW ? true : false;
#else
      gui.state.cardPresent = false;
#endif
      gui.state.battCharged = digitalRead(BATTERY_CHARGING_STATUS_PIN) == HIGH ? true : false;

      log_d("Voltage/SOC = %.2f/%d%%", v, (int) round(soc));
      log_d("SD card = %d", gui.state.cardPresent);
      log_d("Charged = %d", gui.state.battCharged);
#if defined(POWER_CHECK) && POWER_CHECK >= 0
      log_d("Power button = %d", gpioExtender.digitalRead(POWER_CHECK) == LOW ? 1 : 0);
#endif
      redrawWhat |= gui.processEvent(now, BATTERY_UPDATE_EVENT);

      // Power off at low battery to avoid unexpected behaviours
      if (v <= 3.3 && now >= 30000) {     // allow phone to work on low battery for 30 seconds
        powerOff();
        redrawWhat |= gui.processEvent(now, POWER_OFF_EVENT);
      }
    }

    // Check for changes in WiFi strength
    if (elapsedMillis(now, msLastWiFiRssi, WIFI_CHECK_PERIOD_MS)) {
      msLastWiFiRssi = now;
      int rssi = WiFi.RSSI();
      if (GUI::wifiSignalStrength(gui.state.wifiRssi) != GUI::wifiSignalStrength(rssi)) {
        gui.state.wifiRssi = rssi;
        redrawWhat |= gui.processEvent(now, WIFI_ICON_UPDATE_EVENT);
      } else {
        gui.state.wifiRssi = rssi;
      }
    }

    // Check if USB is connected / charging the battery
    if (elapsedMillis(now, msLastUsbCheck, USB_CHECK_PERIOD_MS)) {
      msLastUsbCheck = now;
      //bool usbHere = gpioExtender.digitalRead(USB_POWER_DETECT_PIN)==LOW ? true : false;
      bool usbHere = digitalRead(BATTERY_PPR_PIN) == LOW ? true : false;
      usbConnected += usbHere ? 1 : 0;
      usbConnectedChecks += 1;
      if (usbHere != gui.state.usbConnected) {
        // USB cable status changed
        gui.state.usbConnected = usbHere;
        // If USB is connected -> enable blinking
        // otherwise -> disable blinking
        gui.state.battBlinkOn = usbHere;
        redrawWhat |= gui.processEvent(now, USB_UPDATE_EVENT);
      } else if (usbHere && !gui.state.battCharged && gui.state.battSoc < 100) {
#ifndef BATTERY_BLINKING_OFF
        // If blinking enabled -> trigger BATTERY_BLINK_EVENT every second
        gui.state.battBlinkOn = !gui.state.battBlinkOn;
        redrawWhat |= gui.processEvent(now, BATTERY_BLINK_EVENT);
        log_v("blinked");
#endif
      }
      if (!(usbConnectedChecks & 15)) {     // Dump check result to the logs occasionally
        log_d("USB = %d (%.1f%%), checks=%d", gui.state.usbConnected, 100.0 * usbConnected / usbConnectedChecks, usbConnectedChecks);
      }
    }

    // Power OFF
#ifdef WIPHONE_INTEGRATED_1_4
    if (powerButtonPressed && !poweringOff && elapsedMillis(now, msPowerOffStarted, 2500)) {
      powerOff();
      redrawWhat |= gui.processEvent(now, POWER_OFF_EVENT);
    }
#endif // WIPHONE_INTEGRATED_1_4
#endif // WIPHONE_BOARD

    if (gui.state.inputCurKey && msLastKeyInput && elapsedMillis(now, msLastKeyInput, KEYPAD_IDLE_MS)) {
      log_i("keypad idle");
      redrawWhat |= gui.processEvent(now, KEYBOARD_TIMEOUT_EVENT);
      msLastKeyInput = 0;
    }

    // User serial processing
#ifdef USER_SERIAL
    // Read what has arrived to user UART
    while (userSerial.available() > 0) {
      char ch = userSerial.read();
      log_d("USER SERIAL: %c", ch);
      gui.state.userSerialBuffer.put(ch);
    }
    if (gui.state.userSerialBuffer.size() > 0 && userSerialLastSize != gui.state.userSerialBuffer.size()) {
      // Trigger UART event
      log_d("User serial: %s", gui.state.userSerialBuffer.getCopy());
      redrawWhat |= gui.processEvent(now, USER_SERIAL_EVENT);
      userSerialLastSize = gui.state.userSerialBuffer.size();
    }
#endif // USER_SERIAL

    // GUI update
    if (redrawWhat & REDRAW_ALL) {
      gui.redrawScreen(redrawWhat & REDRAW_HEADER, redrawWhat & REDRAW_FOOTER, redrawWhat & REDRAW_SCREEN, redrawWhat & LOCK_UNLOCK);
    }

    // SIP CLIENT:

    //Added: check if disconnected from wif during call
    if (gui.state.hasSipAccount() && !wifiState.isConnected()) {
      if (sip.isBusy()) {
        log_d("Device disconnected from WIFI");
        log_d("Call will be terminated");

        // terminate audio session
        audio->showAudioStats();
        audio->shutdown();
        audio->setVolumes(restoreSpeakerVol, restoreHeadphonesVol, restoreLoudspeakerVol);

        sip.wifiTerminateCall();  // wifi is disconnected but need to destroy this dialogue
        gui.exitCall();
        
        gui.state.setSipState(CallState::HungUp);
        appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
        gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);

        wifiTerminateSip = TERMINATE_OK;

        //gui.state.sipAccountChanged = true;
      } else {
        /*
         * in order to do ping and update staled state.
        */
        //sip.checkCall(now);
      }
    }

    /*check if remote party is disconnected during call*/
    if (rtpSilentPeriod == RTP_SILENT_ON) {

      rtpSilentPeriod = RTP_SILENT_OFF;

      if (rtpSilentCnt == 0x01) {
        rtpSilentCnt = 0x0;

        // Stop media session
        audio->showAudioStats();
        audio->shutdown();
        audio->setVolumes(restoreSpeakerVol, restoreHeadphonesVol, restoreLoudspeakerVol);

        if (sip.isBusy()) {
          log_d("No RTP Packets From Remote Part");  // Send logs msgs with wifi disconnection

          sip.rtpSilent();
          gui.exitCall();

          gui.state.setSipState(CallState::HungUp);
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);

          wifiTerminateSip = TERMINATE_OK;
        }
      }
      rtpSilentCnt++;
    }
    //    This is the "user-agent core", something that binds together SIP library, GUI, audio interfaces and message storage.
    //    It implements transitions between different call states.
    if (gui.state.hasSipAccount() && wifiState.isConnected()) {
      if( wifiTerminateSip == TERMINATE_OK ) {
        gui.state.sipState == CallState::NotInited;
        wifiTerminateSip = 0x0;
      }
      if (gui.state.sipState == CallState::NotInited  ||  gui.state.sipAccountChanged) {
        log_d("SIP is going to init");
        // Connect to SIP proxy
        uint8_t mac[6];
        wifiState.getMac(mac);
        if (sip.init( gui.state.fromNameDyn,
                      gui.state.fromUriDyn,
                      gui.state.proxyPassDyn,
                      mac )) {
          sip.triedToMakeCallCounter = 0;
          log_d("Connected to SIP");
          gui.state.setSipState(CallState::Idle);
          log_d("caller free (0) = %s", sip.isBusy() ? "NO" : "YES");
        } else {
          // Failed to connect to proxy
          log_e("failed to connect to SIP");
          gui.state.setSipState(CallState::Error);      // permanent error state  TODO
        }
        Random.feed(now);
        gui.state.sipEnabled = true;
        gui.state.sipAccountChanged = false;

      } else if (gui.state.sipState == CallState::Idle) {
        sip.triedToMakeCallCounter = 0;
        bool anySip = false;      // anything received?
        TinySIP::StateFlags_t res;
        do {
          res = sip.checkCall(now);     // TODO: all of this logic could be reorganized to call checkCall in one place and then process results according to the current state
          if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            anySip = true;
          }
          if (res & TinySIP::EVENT_INCOMING_CALL) {
            gui.state.setRemoteNameUri(sip.getRemoteName(), sip.getRemoteUri());
            gui.becomeCallee();
            gui.state.setSipState(CallState::BeingInvited);
            startRingtone();
          } else if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            log_d("UNPROCESSED CALL STATE (Idle): 0x%x", res);
            gui.state.setSipState(CallState::Idle);
          }
        } while (res & TinySIP::EVENT_MORE_BUFFER);
        bool isRegistered = sip.registrationValid(now);
        if (anySip || isRegistered != gui.state.sipRegistered) {
          log_d("setting reason @ CallState::Idle");
          gui.state.setSipReason(sip.getReason());
          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          // Force GUI to update screen if registration status has changed
          // NOTE: SIP registration can happen only in Idle state
          if (isRegistered != gui.state.sipRegistered) {
            // Ingicate successful registration
            gui.state.sipRegistered = isRegistered;
            // Allow GUI display it
            res |= gui.processEvent(now, REGISTRATION_UPDATE_EVENT);       // the app should decide whether to react to registration update
            log_d("SIP EVENT_REGISTERED = %d", gui.state.sipRegistered);
          }
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);

        } else if (gui.state.outgoingMessages.size()) {

          // Send queued messages
          MessageData* msg = gui.state.outgoingMessages[0];
          if (msg) {
            auto err = sip.sendMessage(msg->getOtherUri(), msg->getMessageText());
            if (err == TINY_SIP_OK) {
              gui.flash.messages.setSent(*msg);
              delete msg;
              gui.state.outgoingMessages.remove(0);
            } else {
              log_e("message sending FAILED");
            }
          }

        }

      } else if (gui.state.sipState == CallState::BeingInvited) {

        bool anySip = false;      // anything received
        TinySIP::StateFlags_t res;
        do {
          res = sip.checkCall(now);
          if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            anySip = true;
          }
          if (res & TinySIP::EVENT_CALL_TERMINATED) {
            if (gui.state.sipState != CallState::HungUp) {
              stopRingtone();
              log_d("call terminated @ BeingInvited");
              gui.state.setSipState(CallState::HungUp);
              msHungUp = now;
            }
          } else if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            log_d("UNPROCESSED CALL STATE (BeingInvited): 0x%x", res);
          }
        } while (res & TinySIP::EVENT_MORE_BUFFER);
        if (anySip) {
          log_d("setting reason @ CallState::BeingInvited");
          gui.state.setSipReason(sip.getReason());
          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }

      } else if (gui.state.sipState == CallState::Accept) {

        log_v("Accepting call");

        stopRingtone();
        int res = sip.acceptCall();
        if (res == TINY_SIP_OK) {
          gui.state.setSipState(CallState::InvitedCallee);
        } else {
          log_e("could not accept call, err = %d", res);
          gui.state.setSipState(CallState::HungUp);
          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }

      } else if (gui.state.sipState == CallState::Decline) {

        log_d("Declining call");

        stopRingtone();

        int res = sip.declineCall();
        if (res == TINY_SIP_OK) {
          gui.state.setSipState(CallState::HangingUp);
        } else {
          log_d("could not decline = %d", res);
          gui.state.setSipState(CallState::HungUp);
          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }

      } else if (gui.state.sipState == CallState::InvitingCallee and gui.state.sipRegistered) {

        // Initialize / start call

        log_d("Calling: %s", gui.state.calleeUriDyn);
        if (strchr(gui.state.calleeUriDyn, '@') != NULL and  strlen(gui.state.calleeUriDyn)>0 and gui.state.sipRegistered) {
          sip.startCall(gui.state.calleeUriDyn, now);
          // Proceed to next state
          gui.state.setSipState(CallState::InvitedCallee);
          gui.redrawScreen(true, true, true, true);         // TODO: one of two special cases of redrawAll
        } else {
          log_e("sip callee unavailable");
          gui.state.setSipState(CallState::Idle);
        }

      } else if (gui.state.sipState == CallState::InvitedCallee) {
        // Audio session configs
        IPAddress rtpRemoteIP((uint32_t) 0);
        int rtpRemotePort = 0;
        uint16_t rtpLocalPort = 0;
        uint8_t audioFormat = TinySIP::NULL_RTP_PAYLOAD;

        bool callEstablished = false;
        bool anySip = false;      // anything received
        TinySIP::StateFlags_t res;
        do {
          res = sip.checkCall(now);
          if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            anySip = true;
          }
          if (res & TinySIP::EVENT_CALL_CONFIRMED) {
            if (gui.state.sipState != CallState::Call) {  // change state only once
              log_d("call established");
              callEstablished = true;

              // Copy audio session configs
              if (!rtpRemoteIP.fromString(sip.getRemoteAudioAddr())) {
                rtpRemoteIP = resolveDomain(sip.getRemoteAudioAddr());
                if (!(uint32_t) rtpRemoteIP) {
                  log_e("couldn't parse IP address from \"%s\"", sip.getRemoteAudioAddr());
                }
              }
              rtpRemotePort = sip.getRemoteAudioPort();
              audioFormat = sip.getAudioFormat();
              log_d("  RTP rmt addr: %8X", (uint32_t) rtpRemoteIP);
              log_d("  RTP rmt port: %d", rtpRemotePort);
              log_d("  Audio format:  %d", audioFormat);

              if ((uint32_t)rtpRemoteIP && rtpRemotePort && audioFormat != TinySIP::NULL_RTP_PAYLOAD) {
                rtpLocalPort = sip.getLocalAudioPort();
                log_d("  RTP loc port: %d", rtpLocalPort);
              }
              gui.state.setSipState(CallState::Call);
            }
          } else if (res & TinySIP::EVENT_CALL_TERMINATED) {
            if (gui.state.sipState != CallState::HungUp) {
              gui.state.setSipState(CallState::Decline);
              log_d("call terminated @ InvitedCallee = %d", now);
              //gui.state.setSipState(CallState::HungUp);
              msHungUp = now;
            } else {
              gui.state.setSipState(CallState::Decline);
              log_d("call @ InvitedCallee is Declined");
            }
          } else if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            log_d("UNPROCESSED CALL STATE: 0x%x", res);
          }
        } while (res & TinySIP::EVENT_MORE_BUFFER);

        // Update screen to show that a call was started
        //work by techtesh
        if (anySip) {
          log_d("setting reason @ CallState::InvitedCallee");
          gui.state.setSipReason(sip.getReason());
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }

        // Start audio only after the screen is updated
        if (callEstablished) {
          // If audio configs are OK -> turn on audio (speaker & microphone) & start listening to audio port
          if ((uint32_t)rtpRemoteIP && rtpRemotePort && audioFormat != TinySIP::NULL_RTP_PAYLOAD) {
            audio->openRtpConnection(rtpLocalPort);
            // This works the opposite of how you might expect. The ear speaker is what ends up getting muted. Probably need to remove after checking with Andriy.
            //audio->getVolumes(restoreSpeakerVol, restoreHeadphonesVol, restoreLoudspeakerVol);
            //audio->setVolume(-70, 6);                                   // max. volume for headphones, min. volume for speaker
            //audio->setVolumes(restoreSpeakerVol, restoreHeadphonesVol, Audio::MuteVolume);    // mute loudspeaker for calls
            audio->sendRtpStreamFromMic(audioFormat, rtpRemoteIP, rtpRemotePort);
            audio->playRtpStream(audioFormat, rtpRemotePort);
          } else {
            log_e("audio session failure");
            gui.state.setSipReason("audio failed");
            // Force GUI to update screen
            appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
            gui.redrawScreen(false, false, true);
          }
        }

      } else if (gui.state.sipState == CallState::Call) {

        // Process any SIP requests quickly when call is established

        bool anySip = false;      // anything received
        TinySIP::StateFlags_t res;
        do {
          res = sip.checkCall(now);
          if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            anySip = true;
          }
          if (res & TinySIP::EVENT_CALL_TERMINATED) {
            if (gui.state.sipState != CallState::HungUp) {
              audio->showAudioStats();
              audio->shutdown();
              log_d("call terminated by remote @ Call");
              audio->setVolumes(restoreSpeakerVol, restoreHeadphonesVol, restoreLoudspeakerVol);
              gui.state.setSipState(CallState::HungUp);
              msHungUp = now;
            } else if(gui.state.sipState == CallState::HungUp) {
              log_d("Hang up call before remote party answers it");
              sip.declineCall();
            }
          } else if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            log_d("UNPROCESSED CALL STATE (2): 0x%x", res);
          }
        } while (res & TinySIP::EVENT_MORE_BUFFER);
        if (anySip) {
          log_d("setting reason @ CallState::Call");
          gui.state.setSipReason(sip.getReason());
          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }

      } else if (gui.state.sipState == CallState::HangingUp) {

        bool anySip = false;      // anything received
        TinySIP::StateFlags_t res;
        do {
          res = sip.checkCall(now);
          if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            anySip = true;
          }
          if (res & TinySIP::EVENT_CALL_TERMINATED) {
            if (gui.state.sipState != CallState::HungUp) {
              log_d("call terminated @ HangingUp");
              gui.state.setSipState(CallState::HungUp);
              msHungUp = now;
            }
          } else if (res != TinySIP::EVENT_NONE && res != TinySIP::EVENT_RESPONSE_PARSED && res != TinySIP::EVENT_REQUEST_PARSED) {
            log_d("UNPROCESSED CALL STATE (3): 0x%x", res);
          }
        } while (res & TinySIP::EVENT_MORE_BUFFER);

        // (Timeout &) Update GUI
        if (anySip || elapsedMillis(now, msHangingUp, HANGUP_TIMEOUT_MS)) {
          if (anySip) {
            log_d("setting reason @ CallState::HangingUp");
            gui.state.setSipReason(sip.getReason());
          } else {
            log_d("hang up timeout");
            gui.state.setSipState(CallState::Idle);     // go straight to Idle on timeout
            log_d("caller free (1) = %s", sip.isBusy() ? "NO" : "YES");
          }

          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }

      } else if (gui.state.sipState == CallState::HangUp) {

        // User request to hangup call -> send BYE / CANCEL request
        log_d("Terminating call");
        stopRingtone();
        // Stop media session
        audio->showAudioStats();
        audio->shutdown();
        audio->setVolumes(restoreSpeakerVol, restoreHeadphonesVol, restoreLoudspeakerVol);

        int res = sip.terminateCall(now);
        if (res == TINY_SIP_OK) {
          msHangingUp = now;

          // Proceed to next state
          gui.state.setSipState(CallState::HangingUp);
        } else {
          log_d("terminating error = %d", res);
          gui.state.setSipState(CallState::HungUp);
          // Force GUI to update screen
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
        }


        // Go back to normal

        /*if (elapsedMillis(now, msHungUp, GUI::HUNGUP_TO_NORMAL_MS)) {

          log_d("hungup timeout: now = %d, msHungUp = %d", now, msHungUp);
          gui.state.setSipState(CallState::Idle);
          log_d("caller free (2) = %s", sip.isBusy() ? "NO" : "YES");
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN, true);       // TODO: one of two special cases of redrawAll
        }*/

      } else if (gui.state.sipState == CallState::HungUp) {

        // Go back to normal

        if (elapsedMillis(now, msHungUp, GUI::HUNGUP_TO_NORMAL_MS)) {

          log_d("hungup timeout: now = %d, msHungUp = %d", now, msHungUp);
          gui.state.setSipState(CallState::Idle);
          log_d("caller free (2) = %s", sip.isBusy() ? "NO" : "YES");
          appEventResult res = gui.processEvent(now, CALL_UPDATE_EVENT);
          gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN, true);       // TODO: one of two special cases of redrawAll
        }

      }

      // Check for incoming messages
      TextMessage* msg = NULL;
      if (msg = sip.checkMessage(now, ntpClock.getExactUtcTime(), ntpClock.isTimeKnown())) {
        log_v("message received");
        // Save message from external RAM into a file (part of message database)
        gui.flash.messages.saveMessage(msg->message, msg->from, msg->to, true, msg->useTime ? msg->utcTime : 0);    // time == 0 for unknown real time
        delete msg;
        // Pass event to GUI
        appEventResult res = gui.processEvent(now, NEW_MESSAGE_EVENT);
        gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
      }
    } else {
      gui.state.sipRegistered = false;
    }

#ifdef LORA_MESSAGING
    if (lora.loop()) {
      log_d("Received LoRa message");
      appEventResult res = gui.processEvent(now, NEW_MESSAGE_EVENT);
      gui.redrawScreen(res & REDRAW_HEADER, res & REDRAW_FOOTER, res & REDRAW_SCREEN);
    }

    if (gui.state.outgoingLoraMessages.size() > 0) {
      log_d("Sending LoRa message");
      // Send queued messages
      MessageData* msg = gui.state.outgoingLoraMessages[0];
      if (msg) {
        lora.send_message(msg->getOtherUri(), msg->getMessageText());
        gui.flash.messages.setSent(*msg);
        delete msg;
        gui.state.outgoingLoraMessages.remove(0);
      }
    }
#endif

//    if (gui.state.ledPleaseTurnOn) {
//      allDigitalWrite(EXTENDER_PIN_B2, HIGH);
//      gui.state.ledPleaseTurnOn = false;
//    }
//    if (gui.state.ledPleaseTurnOff) {
//      allDigitalWrite(EXTENDER_PIN_B2, LOW);
//      gui.state.ledPleaseTurnOff = false;
//    }

    // RINGTONE

    if (gui.state.ringing && gui.state.sipState != CallState::Call) {

      // Check if end of ringtone was reached

      if (audio->isEof()) {

        // Rewind the ringtone file
        audio->rewind();

        // Restart vibro motor logic
        gui.state.vibroOn = false;
        gui.state.vibroToggledMs = now;
        gui.state.vibroNextDelayMs = gui.state.vibroDelayMs;
        allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
        allDigitalWrite(KEYBOARD_LED, HIGH);

      }

      // Control vibro motor

      if (elapsedMillis(now, gui.state.vibroToggledMs, gui.state.vibroNextDelayMs)) {

        gui.state.vibroToggledMs = now;
        gui.state.vibroOn = !gui.state.vibroOn;
        gui.state.vibroNextDelayMs = gui.state.vibroOn ? gui.state.vibroOnPeriodMs : gui.state.vibroOffPeriodMs;
        allDigitalWrite(VIBRO_MOTOR_CONTROL, gui.state.vibroOn ? HIGH : LOW);
        allDigitalWrite(KEYBOARD_LED, gui.state.vibroOn ? LOW : HIGH);

      }
    }

    // Audio
    //msProfile.add(micros()-loopTime);
//    uint32_t loopTime = micros();
//    if (!msProfileStart) msProfileStart = loopTime;
    audio->loop();

//    // Profiler
//    msProfile.add(micros()-loopTime);
//    if (loopTime - msProfileStart > 2500000) {
//      uint32_t sum = 0, mx = 0, cnt = 0;

//      log_d("Profile:");
//      for (auto it = msProfile.iterator(); it.valid(); ++it) {
//        uint32_t c1 = *it;
//        DEBUG_PRINTF("%d ", c1);      // Before audio
//        ++it;
//        log_d("%d", *it - c1);        // After audio
//        sum += *it;
//        mx = (mx < *it) ? *it : mx;
//        cnt++;
//      }
//      log_d("Loop: avg = %.1f us, max = %d", (float)sum/cnt, mx);
//      msProfile.purge();
//      msProfileStart = 0;

//      log_d("Audio profile:");
//      for (auto it = audio->profile.iterator(); it.valid(); it++) {
//        uint32_t tot = (*it).time[6] - (*it).time[0];
//        sum += tot;
//        mx = (mx < tot) ? tot : mx;
//        cnt++;
//        it->show();
//      }
//      log_d("Audio loop: sum = %d, avg = %.1f us, max = %d, cnt = %d", sum, (float)sum/cnt, mx, cnt);
//      audio->profile.purge();

//      #ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
//      showRunTimeStats();
//      #endif
//    }

    // Theoretically, gives time for modem sleep? Allows to consume less power?
    //delay(1);   // sleep for 1 millisecond
    //vTaskDelay(1);    // sleep for a single tick: allows context switch
    taskYIELD();      // force context switch

    //esp_sleep_enable_timer_wakeup(1000000); // 0.001 s
    //int ret = esp_light_sleep_start();
    //if (ret != ESP_OK) printf("light sleep error: %d\r\n", ret);
  }
}

void powerOff() {
  log_i("POWER OFF");
  allDigitalWrite(POWER_CONTROL, HIGH);       // produces a power down from software
  msPowerOffStarted = millis();
  poweringOff = true;
}
