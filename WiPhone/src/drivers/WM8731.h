/*
 * Used code:
 *    https://github.com/hughpyle/machinesalem-arduino-libs/tree/master/WM8731
 *    2013-01-14 @machinesalem,  (cc) https://creativecommons.org/licenses/by/3.0/
 */

#ifndef WM8731_h
#define WM8731_h

#include "Arduino.h"
#include <Wire.h>
// TODO: explore Brzo I2C library and maybe port it to ESP32

#define WM8731_I2C_ADDR_CSB_LOW    B0011010     // use this address if CSB pin is Low
#define WM8731_I2C_ADDR_CSB_HIGH   B0011011     // use this address if CSB pin is High

/* ----- Registers ----- */

#define WM8731_REG_LLINEIN          ((unsigned char)0x00)       // 00h
#define WM8731_REG_RLINEIN          ((unsigned char)0x01)       // 02h
#define WM8731_REG_LHEADOUT         ((unsigned char)0x02)       // 04h
#define WM8731_REG_RHEADOUT         ((unsigned char)0x03)       // 06h
#define WM8731_REG_ANALOG           ((unsigned char)0x04)       // 08h
#define WM8731_REG_DIGITAL          ((unsigned char)0x05)       // 0Ah
#define WM8731_REG_POWERDOWN        ((unsigned char)0x06)       // 0Ch
#define WM8731_REG_INTERFACE        ((unsigned char)0x07)       // 0Eh
#define WM8731_REG_SAMPLING         ((unsigned char)0x08)       // 10h
#define WM8731_REG_ACTIVE_CONTROL   ((unsigned char)0x09)       // 12h
#define WM8731_REG_RESET            ((unsigned char)0x0F)       // 1Eh    - writing 0 resets device

/* ----- Flags ----- */

// TODO: merge flags for left and right
#define WM8731_LLINEIN_LINVOL(n)    ((unsigned char)(n & 0x1f))       // Left channel line-input volume (0..31)
#define WM8731_LLINEIN_LINVOL_MASK  ((unsigned char)0xe0)
#define WM8731_LLINEIN_LINMUTE      ((unsigned char)0x80)             // Left line input mute to ADC
#define WM8731_LLINEIN_LRINBOTH     ((unsigned short)0x100)           // Left to Right Mic Control Join

#define WM8731_RLINEIN_RINVOL(n)    ((unsigned char)(n & 0x1f))       // Right channel line-input volume (0..31)
#define WM8731_RLINEIN_RINVOL_MASK  ((unsigned char)0xe0)
#define WM8731_RLINEIN_RINMUTE      ((unsigned char)0x80)             // Right line input mute to ADC
#define WM8731_RLINEIN_RLINBOTH     ((unsigned short)0x100)           // Right to Left Mic Control Join

#define WM8731_LHEADOUT_LHPVOL(db)  ((unsigned char)((db + 0x79) & 0x7f))       // Left Headphone Output Volume (0..127); def. 0b1111001 (def = 0db), max 6 db (0x7f), 1 db steps, 0110000 = -73dB, 0000000 to 0101111 = MUTE
#define WM8731_LHEADOUT_LHPVOL_MASK ((unsigned char)0x180)
#define WM8731_LHEADOUT_LZCEN       ((unsigned char)0x080)            // Left Channel Zero Cross Detect
#define WM8731_LHEADOUT_LRHPBOTH    ((unsigned short)0x100)           // Left to Right Headphone Control Join

#define WM8731_RHEADOUT_RHPVOL(db)  ((unsigned char)((db + 0x79) & 0x7f))       // Right Headphone Output Volume (0..127)
#define WM8731_RHEADOUT_RHPVOL_MASK ((unsigned char)0x180)
#define WM8731_RHEADOUT_RZCEN       ((unsigned char)0x080)            // Right Channel Zero Cross Detect
#define WM8731_RHEADOUT_RLHPBOTH    ((unsigned short)0x100)           // Right to Left Headphone Control Join

#define WM8731_ANALOG_MICBOOST      ((unsigned char)0x01)             // Mic Input Level Boost
#define WM8731_ANALOG_MUTEMIC       ((unsigned char)0x02)             // Mic Input Mute to ADC
#define WM8731_ANALOG_INSEL_MIC     ((unsigned char)0x04)             // Mic Input Select to ADC (zero: select Line input to ADC)
#define WM8731_ANALOG_BYPASS        ((unsigned char)0x08)             // Bypass (line inputs summed to line out)
#define WM8731_ANALOG_DACSEL        ((unsigned char)0x10)             // DAC select (only send DAC output to line out)
#define WM8731_ANALOG_SIDETONE      ((unsigned char)0x20)             // Sidetone (mic inputs summed to line out)
#define WM8731_ANALOG_SIDEATT(n)    ((unsigned char)(n & 3)<<6)       // Sidetone Attenuation 0=-6dB, 1=-9dB, 2=-12dB, 3=-15dB

#define WM8731_DIGITAL_ADCHPD       ((unsigned char)0x01)             // ADC High Pass Filter Disable
#define WM8731_DIGITAL_DEEMP(n)     ((unsigned char)(n & 3)<<1)       // De-Emph: 0=disable, 1=32kHz, 2=44k1, 3=48k
#define WM8731_DIGITAL_DACMU        ((unsigned char)0x08)             // DAC Soft Mute (digital)
#define WM8731_DIGITAL_HPOR         ((unsigned char)0x10)             // Store DC offset when High Pass filter disabled

#define WM8731_POWERDOWN_LINEINPD   ((unsigned char)0x01)             // Line Input Power Down
#define WM8731_POWERDOWN_MICPD      ((unsigned char)0x02)             // Mic Input Power Down
#define WM8731_POWERDOWN_ADCPD      ((unsigned char)0x04)             // ADC Power Down
#define WM8731_POWERDOWN_DACPD      ((unsigned char)0x08)             // DAC Power Down
#define WM8731_POWERDOWN_OUTPD      ((unsigned char)0x10)             // Line Output Power Down
#define WM8731_POWERDOWN_OSCPD      ((unsigned char)0x20)             // Oscillator Power Down
#define WM8731_POWERDOWN_CLKOUTPD   ((unsigned char)0x40)             // CLKOUT Power Down
#define WM8731_POWERDOWN_POWEROFF   ((unsigned char)0x80)             // Power Off Device

#define WM8731_INTERFACE_FORMAT(n)  ((unsigned char)(n & 3))          // Format: 0=MSB-first RJ, 1=MSB-first LJ, 2=I2S, 3=DSP
#define WM8731_INTERFACE_WORDLEN(n) ((unsigned char)(n & 3)<<2)       // Word Length: 0=16 1=20 2=24 3=32
#define WM8731_INTERFACE_LRP        ((unsigned char)0x10)             // DACLRC phase control
#define WM8731_INTERFACE_LRSWAP     ((unsigned char)0x20)             // DAC Left Right Clock Swap
#define WM8731_INTERFACE_MASTER     ((unsigned char)0x40)             // Master/Slave Mode (1: codec is master)
#define WM8731_INTERFACE_BCLKINV    ((unsigned char)0x80)             // Bit Clock Invert

#define WM8731_SAMPLING_USBMODE     ((unsigned char)0x01)             // USB Mode Select
#define WM8731_SAMPLING_BOSR        ((unsigned char)0x02)             // Base OverSampling Rate
#define WM8731_SAMPLING_RATE(n)     ((unsigned char)(n & 0x0f)<<2)    // Sample Rate
#define WM8731_SAMPLING_CLKIDIV2    ((unsigned char)0x40)             // Core Clock Divider Select (0=MCLK, 1=MCLK/2)
#define WM8731_SAMPLING_CLKODIV2    ((unsigned char)0x80)             // CLKOUT Divider Select (0=MCLK, 1=MCLK/2)

#define WM8731_ACTIVE               ((unsigned char)1)

class WM8731 {
private:
  uint8_t   _addr;
  uint8_t   _sda;
  uint8_t   _scl;

public:
  WM8731(uint8_t _addr, uint8_t _sda, uint8_t _scl) {
    this->_addr = _addr;
    this->_sda  = _sda;
    this->_scl  = _scl;
  }

  void connect() {
    Wire.begin(this->_sda, this->_scl);
    Wire.setClock(400000);    // 100 kHz    TODO: explore datasheets and maybe go faster
  }

  void wakeUp() {
    setReg(WM8731_REG_POWERDOWN, 0x00);
  }

  void shutDown() {
    setReg(WM8731_REG_POWERDOWN, 0xFF);
  }

  void setVolume(int8_t db) {
    if (db < -73) {
      setReg9bit(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LRHPBOTH | WM8731_LHEADOUT_LHPVOL(-74));
    } else if (db > 6) {
      setReg9bit(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LRHPBOTH | WM8731_LHEADOUT_LHPVOL(6));
    } else {
      setReg9bit(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LRHPBOTH | WM8731_LHEADOUT_LHPVOL(db));
    }
  }

  void powerUp() {
    // According to "Power Up Sequence" section of the datasheet (p. 57)
    // TODO: do not power unneeded stuff
    setReg(WM8731_REG_POWERDOWN, 1<<4);   // power up everything except OUTPD

    // Set required values (TODO)
    // Sparkfun: 0x03  = DSP mode (SPI), 16-bit
    //setReg(WM8731_REG_INTERFACE, 0x03);   // Sparkfun
    setReg(WM8731_REG_INTERFACE, WM8731_INTERFACE_WORDLEN(0) | WM8731_INTERFACE_FORMAT(2));   // 16-bit word, I2S, Slave
    //setReg(WM8731_REG_INTERFACE, WM8731_INTERFACE_WORDLEN(0) | WM8731_INTERFACE_FORMAT(2) | WM8731_INTERFACE_MASTER);   // 16-bit word, I2S, Master

    // Set IN and OUT volumes
    // (Bacically same as Sparkfun here)
    setReg9bit(WM8731_REG_LLINEIN, 279);   // 0 dB volume control for both lines in (default) - WORKS
    //setReg9bit(WM8731_REG_LLINEIN, 0B100011011);   //  +6 dB volume control for both lines in   - WORKS
    //setReg9bit(WM8731_REG_LLINEIN, 0B100011111);   // +12 dB volume control for both lines in   - WORKS
    //setReg(WM8731_REG_LLINEIN, WM8731_LLINEIN_LINMUTE);   // 0 dB volume control for left  line in (default)
    //setReg(WM8731_REG_RLINEIN, 23);   // 0 dB volume control for right line in (default)
    //setReg(WM8731_REG_LLINEIN, WM8731_LLINEIN_LINMUTE);
    //setReg(WM8731_REG_RLINEIN, WM8731_RLINEIN_RINMUTE);
    //setReg9bit(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LRHPBOTH | WM8731_LHEADOUT_LHPVOL(0));    // 0 dB volume control for both headphones
    setReg9bit(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LRHPBOTH | WM8731_LHEADOUT_LHPVOL(-6));   // -6 dB volume control for both headphones (defautl) - WORKS
    //setReg9bit(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LRHPBOTH | WM8731_LHEADOUT_LHPVOL(-12));  // -12 dB volume control for both headphones
    //setReg(WM8731_REG_LHEADOUT, WM8731_LHEADOUT_LHPVOL(0)); // 0 dB volume control for left  headphone
    //setReg(WM8731_REG_RHEADOUT, WM8731_LHEADOUT_LHPVOL(0)); // 0 dB volume control for right headphone

    // Digital audio path configuration
    // Sparkfun: 0
    setReg(WM8731_REG_DIGITAL, 0);    // Enable ADC High Pass Filter  (1 - disable)

    // Analog audio path configuration
    // Sparkfun: WM8731_ANALOG_MUTEMIC | WM8731_ANALOG_DACSEL
    //setReg(WM8731_REG_ANALOG, WM8731_ANALOG_MUTEMIC | WM8731_ANALOG_DACSEL);    // Sparkfun - WORKS
    //setReg(WM8731_REG_ANALOG, WM8731_ANALOG_MICBOOST | WM8731_ANALOG_INSEL_MIC | WM8731_ANALOG_DACSEL | WM8731_ANALOG_SIDETONE); // mic_input + DAC + sidetone - WORKS
    //setReg(WM8731_REG_ANALOG, WM8731_ANALOG_MICBOOST | WM8731_ANALOG_INSEL_MIC | WM8731_ANALOG_SIDETONE);  // mic input + sidetone - WORKS
    setReg(WM8731_REG_ANALOG, WM8731_ANALOG_MICBOOST | WM8731_ANALOG_INSEL_MIC | WM8731_ANALOG_DACSEL); // mic_input + DAC    - WORKS

    // Sampling control
    // Sparkfun:        0xA0 = B10100000: 44 kHz, CLKODIV2 = 1, Normal mode
    // (Sparkfun 8 kHz: 0xAC = B10101100: CLKIDIV2 = 1, CLKODIV2 = 1  (8.018 kHz)
    //setReg(WM8731_REG_SAMPLING, WM8731_SAMPLING_RATE(6));   // Normal mode, 32/32 kHz
    //setReg(WM8731_REG_SAMPLING, WM8731_SAMPLING_USBMODE);    // USB mode, 48/48 kHz
    //setReg(WM8731_REG_SAMPLING, 0xA0);   // Sparkfun 44 kHz
    //setReg(WM8731_REG_SAMPLING, WM8731_SAMPLING_RATE(3) | WM8731_SAMPLING_USBMODE);   // USB mode, 8/8 kHz  - WORKS
    setReg(WM8731_REG_SAMPLING, 0xAC);   // Sparkfun 8 kHz    - WORKS

    // Activate + power output
    setReg(WM8731_REG_ACTIVE_CONTROL, WM8731_ACTIVE);
    setReg(WM8731_REG_POWERDOWN, 0);   // power up everything
  }

  inline void setReg(unsigned char regAddr, unsigned char val) __attribute__((always_inline)) {
    Wire.beginTransmission(_addr);
    Wire.write(regAddr<<1);   // 7-bit address
    Wire.write(val);
    Wire.endTransmission();
  }

  inline void setReg9bit(unsigned char regAddr, unsigned short val) __attribute__((always_inline)) {   // machinesalem-arduino-libs
    Wire.beginTransmission(_addr);
    Wire.write( (unsigned char)((regAddr<<1) | ((val>>8) & 0x1)) );
    Wire.write( (unsigned char)(val & 0xFF) );
    Wire.endTransmission();
  }
private:
};

#endif // WM8731_h
