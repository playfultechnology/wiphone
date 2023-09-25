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

/* Copyright (c) 2019, ESP32 WiPhone project, by Andriy Makukha.
 *
 * Reference code:
 *    https://github.com/hughpyle/machinesalem-arduino-libs/tree/master/WM8731
 *    2013-01-14 @machinesalem,  (cc) https://creativecommons.org/licenses/by/3.0/
 * Linux:
 *    https://github.com/torvalds/linux/blob/master/sound/soc/codecs/wm8750.h
 *    https://github.com/torvalds/linux/blob/master/sound/soc/codecs/wm8750.c
 * Hardware:
 *    to configure hardware correctly, it is important to inderstand difference between
 *    muxes (multiplexers) LINSEL/RINSEL, LMIXSEL/RMIXSEL and output mixers (left, right, mono).
 *    (See block diagram in the part datasheet, page 1.)
 */

#ifndef WM8750_h
#define WM8750_h

#include "Arduino.h"
#include <Wire.h>

#define WM8750_I2C_ADDR_CSB_LOW    B0011010     // use this address if CSB pin is Low
#define WM8750_I2C_ADDR_CSB_HIGH   B0011011     // use this address if CSB pin is High

/* ----- Registers ----- */

#define WM8750_REG_LINPGA       ((unsigned char)0x00)       // Left channel PGA (programmable-gain amplifier)
#define WM8750_REG_RINPGA       ((unsigned char)0x01)       // Right channel PGA (programmable-gain amplifier)
#define WM8750_REG_LOUT1VOL     ((unsigned char)0x02)       // Left headphone
#define WM8750_REG_ROUT1VOL     ((unsigned char)0x03)       // Right headphone
#define WM8750_REG_ADCDAC       ((unsigned char)0x05)       // ADC and DAC control
#define WM8750_REG_INTERFACE    ((unsigned char)0x07)       // Digital audio interface format
#define WM8750_REG_SAMPLING     ((unsigned char)0x08)       // Sample rate
#define WM8750_REG_LDACVOL      ((unsigned char)0x0a)       // Left DAC volume
#define WM8750_REG_RDACVOL      ((unsigned char)0x0b)       // Right DAC volume
#define WM8750_REG_BASS      ((unsigned char)0x0c)       // Bass control
#define WM8750_REG_TREBLE    ((unsigned char)0x0d)       // Treble control
#define WM8750_REG_RESET     ((unsigned char)0x0f)       // Reset control
#define WM8750_REG_3D        ((unsigned char)0x10)       // 3D control
#define WM8750_REG_ALC1      ((unsigned char)0x11)       // Automatic level control 1
#define WM8750_REG_ALC2      ((unsigned char)0x12)       // Automatic level control 2
#define WM8750_REG_ALC3      ((unsigned char)0x13)       // Automatic level control 3
#define WM8750_REG_NGATE     ((unsigned char)0x14)       // Noise gate
#define WM8750_REG_LADC         ((unsigned char)0x15)       // Left ADC volume
#define WM8750_REG_RADC         ((unsigned char)0x16)       // Right ADC volume
#define WM8750_REG_ADDCTRL1     ((unsigned char)0x17)       // Additional control 1
#define WM8750_REG_ADDCTRL2     ((unsigned char)0x18)       // Additional control 2
#define WM8750_REG_POWER1       ((unsigned char)0x19)       // Power management 1
#define WM8750_REG_POWER2       ((unsigned char)0x1a)       // Power management 2
#define WM8750_REG_ADDCTRL3  ((unsigned char)0x1b)       // Additional control 3
#define WM8750_REG_ADCINMODE    ((unsigned char)0x1f)       // ADC input mode
#define WM8750_REG_LADCIN       ((unsigned char)0x20)       // Left ADC signal path
#define WM8750_REG_RADCIN       ((unsigned char)0x21)       // Right ADC signal path

// These mixers are for the output (not to be confused with LMIXSEL and RMIXSEL muxes, which can be inputs for these mixers)
#define WM8750_REG_LOUTM1       ((unsigned char)0x22)       // Left mixer (output)
#define WM8750_REG_LOUTM2       ((unsigned char)0x23)       // Left mixer control (output)
#define WM8750_REG_ROUTM1       ((unsigned char)0x24)       // Right mixer (output)
#define WM8750_REG_ROUTM2       ((unsigned char)0x25)       // Right mixer output control (output)
#define WM8750_REG_MOUTM1    ((unsigned char)0x26)       // Mono mixer control 1 (output)
#define WM8750_REG_MOUTM2    ((unsigned char)0x27)       // Mono mixer control 2 (output)

#define WM8750_REG_LOUT2VOL     ((unsigned char)0x28)       // LOUT2 output
#define WM8750_REG_ROUT2VOL     ((unsigned char)0x29)       // ROUT2 output
#define WM8750_REG_MOUTVOL      ((unsigned char)0x2a)       // Mono output

/* ----- Flags ----- */

#define WM8750_INPGA_INVOL(n)       ((unsigned char)(n & 0x3f))       // Channel input volume control: (0..63) = -17.25..+30dB, step = 0.75dB, default = 0b010111 (0 dB)
#define WM8750_INPGA_ZCEN           ((unsigned char)0x40)             // Zero cross detector
#define WM8750_INPGA_MUTE           ((unsigned char)0x80)
#define WM8750_INPGA_VU             ((unsigned short)0x100)           // Volume Update

/* LOUT1/ROUT1, LOUT2/ROUT2 */
#define WM8750_OUT_VOL(n)           ((unsigned char)(n & 0x7f))       // OUT1 Volume (0..127); def.0b1111001 (def = 0db), max 6 db (0x7f), 1.09 dB steps, 0110000 = -67dB, 0000000 to 0101111 = MUTE
#define WM8750_OUT_ZCEN             ((unsigned char)0x080)            // Zero Cross Enable
#define WM8750_OUT_VU               ((unsigned short)0x100)           // Volume Update

#define WM8750_ADCDAC_ADCHPD        ((unsigned char)0x01)             // ADC high-pass filter disable
#define WM8750_ADCDAC_DEEMP(n)      ((unsigned char)(n & 3)<<1)       // De-Emphasis control: 0=disable, 1=32kHz, 2=44k1, 3=48k sample rate
#define WM8750_ADCDAC_DACMUTE       ((unsigned char)0x08)             // DAC Soft Mute (digital)
#define WM8750_ADCDAC_HPOR          ((unsigned char)0x10)             // Store dc offset when high-pass filter disabled
#define WM8750_ADCDAC_ADCPOL(n)     ((unsigned char)(n & 3)<<5)       // 00 = Polarity not inverted, 01 = L polarity invert, 10 = R polarity invert, 11 = L and R polarity invert
#define WM8750_ADCDAC_DACDIV2       ((unsigned char)0x80)             // DAC 6dB attenuate enable: 1 = -6dB enabled
#define WM8750_ADCDAC_ADCDIV2       ((unsigned short)0x100)           // ADC 6dB attenuate enable: 1 = -6dB enabled

#define WM8750_INTERFACE_FORMAT(n)  ((unsigned char)(n & 3))          // Format: 0 = reserved, 1 = MSB-first LJ, 2 = I2S format, 3 = DSP mode
#define WM8750_INTERFACE_WORDLEN(n) ((unsigned char)(n & 3)<<2)       // Word Length: 0=16 bits, 1=20 bits, 2=24 bits, 3=32 bits
#define WM8750_INTERFACE_LRP_B      ((unsigned char)0x10)             // LRC polarity (non-DSP) / DSP Mode B
#define WM8750_INTERFACE_LRSWAP     ((unsigned char)0x20)             // DAC Left Right Clock Swap
#define WM8750_INTERFACE_MASTER     ((unsigned char)0x40)             // Master/Slave Mode (1: codec is master)
#define WM8750_INTERFACE_BCLKINV    ((unsigned char)0x80)             // Bit Clock Invert

#define WM8750_SAMPLING_USBMODE     ((unsigned char)0x01)             // USB Mode Select
#define WM8750_SAMPLING_RATE(n)     ((unsigned char)(n & 0x1f)<<1)    // Sample Rate (see datasheet)
#define WM8750_SAMPLING_CLKDIV2     ((unsigned char)0x40)             // Master Clock Divide by 2 (0=MCLK, 1=MCLK/2)
#define WM8750_SAMPLING_BCM         ((unsigned short)(n & 0x03)<<7)   // BCLK Frequency: 00 = BCM (bit clock mode) function disabled; 01 = MCLK/4, 10 = MCLK/8, 11 = MCLK/16

#define WM8750_BASS_ADAPT_BOOST     ((unsigned short)0x80)            // Bass Boost: 0 - linear (def.), 1 - adaptive
#define WM8750_BASS_HIGH_CUTOFF     ((unsigned short)0x40)            // 0 - Low Cutoff (130 Hz @ 48K; def.), 1 - High Cutoff (200 Hz @ 48K)
#define WM8750_BASS_MAX_BOOST       ((unsigned short)0x00)

#define WM8750_TREBLE_LOW_CUTOFF    ((unsigned short)0x40)            // 0 - High Cutoff (8 KHz; def.), 1 - Low Cutoff (4 KHz)
#define WM8750_TREBLE_MIN_INTENS    ((unsigned short)0x0E)

#define WM8750_POWER1_VMIDSEL(n)    ((unsigned short)(n & 0x3)<<7)    // Vmid divider enable and select: 00 – Vmid disabled (for OFF mode), 01 – 50kOhm divider enabled (for playback/record); 10 – 500kOhm divider enabled (for low-power standby); 11 – 5kOhm divider enabled (for fast start-up)
#define WM8750_POWER1_VREF          ((unsigned char)0x40)             // VREF (necessary for all other functions) power up
#define WM8750_POWER1_AINL          ((unsigned char)0x20)             // Analogue in PGA Left power up
#define WM8750_POWER1_AINR          ((unsigned char)0x10)             // Analogue in PGA Right power up
#define WM8750_POWER1_ADCL          ((unsigned char)0x08)             // ADC Left power up
#define WM8750_POWER1_ADCR          ((unsigned char)0x04)             // ADC Right power up
#define WM8750_POWER1_MICB          ((unsigned char)0x02)             // MICBIAS power up
#define WM8750_POWER1_DIGENB        ((unsigned char)0x01)             // Master clock disable     (SEE NOTE IN DATASHEET), default = 0 (enabled)

#define WM8750_POWER2_DACL          ((unsigned short)0x100)           // DAC Left enable
#define WM8750_POWER2_DACR          ((unsigned char)0x80)             // DAC Right enable
#define WM8750_POWER2_LOUT1         ((unsigned char)0x40)             // LOUT1 Enable
#define WM8750_POWER2_ROUT1         ((unsigned char)0x20)             // ROUT1 Enable
#define WM8750_POWER2_LOUT2         ((unsigned char)0x10)             // LOUT2 Enable
#define WM8750_POWER2_ROUT2         ((unsigned char)0x08)             // ROUT2 Enable
#define WM8750_POWER2_MONO          ((unsigned char)0x04)             // MONOOUT Enable
#define WM8750_POWER2_OUT3          ((unsigned char)0x02)             // OUT3 Enable
#define WM8750_POWER2_OUT2          (WM8750_POWER2_LOUT2 | WM8750_POWER2_ROUT2)
#define WM8750_POWER2_OUT1          (WM8750_POWER2_LOUT1 | WM8750_POWER2_ROUT1)
#define WM8750_POWER2_DAC           (WM8750_POWER2_DACL | WM8750_POWER2_DACR)

/* 15h, 16h */
#define WM8750_ADC_VU               ((unsigned short)0x100)           // ADC Volume Update
#define WM8750_ADC_VOL(n)           ((unsigned char)(n & 0xff))       // Default = 0 dB, 0xff = +30 dB, 0x00 = mute, 0x01 = -97 dB, 0.5 dB steps

#define WM8750_ADDCTRL1_TSDEN         ((unsigned short)0x100)           // Thermal Shutdown Enable (def. 0)
#define WM8750_ADDCTRL1_VSEL(n)       (((unsigned char)n & 0x03)<<6)    // Analogue Bias optimization: 1x (def.) - Lowest bias current, optimized for AVDD=3.3V
#define WM8750_ADDCTRL1_DMONOMIX(n)   (((unsigned char)n & 0x03)<<4)    // DAC mono mix: 00 (def.) - stereo
#define WM8750_ADDCTRL1_DATSEL(n)     (((unsigned char)n & 0x03)<<2)    // bit 1 -> left data (0 - to left ADC), bit 2 -> right data (0 - to right ADC), Def = 00 (left to left, right to right)
#define WM8750_ADDCTRL1_DACINV        ((unsigned char)0x02)             // DAC phase invert (def. 0)
#define WM8750_ADDCTRL1_TOEN          ((unsigned char)0x01)             // Timeout Enable (def. 0)

#define WM8750_ADDCTRL2_DACOSR        ((unsigned char)0x01)             // DAC oversample rate select (Def. 0) 1 - lowest power, 0 - best SNR
#define WM8750_ADDCTRL2_ADCOSR        ((unsigned char)0x02)             // ADC oversample rate select (Def. 0) 1 - lowest power, 0 - best SNR
#define WM8750_ADDCTRL2_LRCM          ((unsigned char)0x04)             // Selects disable mode for ADCLRC and DACLRC (def. 0)
#define WM8750_ADDCTRL2_TRI           ((unsigned char)0x08)             // Tristates ADCDAT and switches ADCLRC, DACLRC and BCLK to inputs (def. 0)
#define WM8750_ADDCTRL2_ROUT2INV      ((unsigned char)0x10)             // ROUT2 Invert
#define WM8750_ADDCTRL2_HPSWPOL       ((unsigned char)0x20)             // Headphone Switch Polarity: 0 (def.) - HPDETECT high = headphone. 1 - HPDETECT high = speaker
#define WM8750_ADDCTRL2_HPSWEN        ((unsigned char)0x40)             // Headphone Switch Enable (def. 0)
#define WM8750_ADDCTRL2_OUT3SW(n)     ((unsigned short)(n & 0x03)<<7)   // OUT3 select: 00 (def) - VREF, 01 - ROUT1 (volume controlled by ROUT1VOL), 10 - MONOOUT, 11 - right mixer output (no vol. control via ROUT1VOL)

/* 22h, 24h */
// These are used "to mix the DAC output signal with analogue line-in signals from the LINPUT1/2/3,
//     RINPUT1/2/3 pins or a mono differential input (LINPUT1 – RINPUT1) or (LINPUT2 – RINPUT2)"
#define WM8750_OUTM1_MIXSEL(n)      ((unsigned char)(n & 0x07))       // Left or right: 000 = (left or right) INPUT1, 001 = INPUT2, 010 = INPUT3, 011 = ADC Input (after PGA / MICBOOST), 100 = Differential input

/* 19h */
#define WM8750_ADCINMODE_DS         ((unsigned short)0x100)           // Differential input select: 0 (def) - LINPUT1 - RINPUT1, 1 - LINPUT2 - RINPUT2
#define WM8750_ADCINMODE_MONOMIX(n) ((unsigned char)(n & 0x03)<<6)    // 00 (def) - stereo, 01: Analogue Mono Mix (using left ADC), 10: Analogue Mono Mix (using right ADC)
#define WM8750_ADCINMODE_RDCM       ((unsigned char)0x20)             // Right Channel DC Measurement
#define WM8750_ADCINMODE_LDCM       ((unsigned char)0x10)             // Left Channel DC Measurement

/* 20h, 21h */
#define WM8750_ADCIN_MICBOOST(n)    ((unsigned char)(n & 0x03)<<4)    // Microphone Gain Boost: 00 = boost off (bypassed), 01 = 13dB, 10 = 20dB, 11 = 29dB
#define WM8750_ADCIN_INSEL(n)       ((unsigned char)(n & 0x03)<<6)    // Input Select: 00 = INPUT1, 01 = INPUT2, 10 = INPUT3, 11 = L-R Differential (either LINPUT1-RINPUT1 or LINPUT2-RINPUT2, selected by DS)

// Next four are all the same in value: just the matter of explanatory name
#define WM8750_LOUTM1_LDAC            ((unsigned short)0x100)           // Left DAC to Left Mixer enable path
#define WM8750_LOUTM1_LMIXSEL         ((unsigned char)0x80)             // LMIXSEL mux signal to Left Output Mixer enable path
#define WM8750_LOUTM1_LMIXSEL_VOL(n)  ((unsigned char)(n & 0x07)<<4)    // LMIXSEL mux signal to Left Output Mixer Volume: 000 = +6 dB, (-3 dB steps), 111 = -15 dB

#define WM8750_LOUTM2_RDAC            ((unsigned short)0x100)           // Right DAC to Left Mixer enable path
#define WM8750_LOUTM2_RMIXSEL         ((unsigned char)0x80)             // RMIXSEL mux signal to Left Output Mixer enable path
#define WM8750_LOUTM2_RMIXSEL_VOL(n)  ((unsigned char)(n & 0x07)<<4)    // RMIXSEL mux signal to Left Output Mixer Volume: 000 = +6 dB, (-3 dB steps), 111 = -15 dB

#define WM8750_ROUTM1_LDAC            ((unsigned short)0x100)           // Left DAC to Right Mixer enable path
#define WM8750_ROUTM1_LMIXSEL         ((unsigned char)0x80)             // LMIXSEL mux signal to Right Output Mixer enable path
#define WM8750_ROUTM1_LMIXSEL_VOL(n)  ((unsigned char)(n & 0x07)<<4)    // LMIXSEL mux signal to Right Output Mixer Volume: 000 = +6 dB, (-3 dB steps), 111 = -15 dB

#define WM8750_ROUTM2_RDAC            ((unsigned short)0x100)           // Right DAC to Right Mixer enable path
#define WM8750_ROUTM2_RMIXSEL         ((unsigned char)0x80)             // RMIXSEL mux signal to Right Output Mixer enable path
#define WM8750_ROUTM2_RMIXSEL_VOL(n)  ((unsigned char)(n & 0x07)<<4)    // RMIXSEL mux signal to Right Output Mixer Volume: 000 = +6 dB, (-3 dB steps), 111 = -15 dB

typedef enum {
  WM8750_ERROR_OK             = I2C_ERROR_OK,
  WM8750_ERROR_DEV            = I2C_ERROR_DEV,
  WM8750_ERROR_ACK            = I2C_ERROR_ACK,
  WM8750_ERROR_TIMEOUT        = I2C_ERROR_TIMEOUT,
  WM8750_ERROR_BUS            = I2C_ERROR_BUS,
  WM8750_ERROR_BUSY           = I2C_ERROR_BUSY,
  WM8750_ERROR_WRITE_FAILED   = 11,
  WM8750_ERROR_REQUEST_FAILED = 12,
} wm8750_err_t;

class WM8750 {
private:
  uint8_t   _addr;
  uint8_t   _sda;
  uint8_t   _scl;

public:
  WM8750(uint8_t _addr, uint8_t _sda, uint8_t _scl) {
    this->_addr = _addr;
    this->_sda  = _sda;
    this->_scl  = _scl;
  }

  void connect() {
    Wire.begin(this->_sda, this->_scl);
    Wire.setClock(400000);
  }

  void wakeUp() {
    // TODO
  }

  void shutDown() {
    log_v("Audio codec: shutdown");
    setReg(WM8750_REG_POWER2, 0x00);      // disable (power down) all output buffers
    setReg(WM8750_REG_POWER1, 0x00);      // disable (power down) ADC, AIN, MICB, VREF
  }

  void unmute() {
    // NOTE: this probably shouldn't be called directly; it's part of powerUp()
    log_v("Audio codec: unmute");
    //setReg(WM8750_REG_POWER1, 0x17e);
    //setReg(WM8750_REG_POWER2, 0x7e);
    setReg(WM8750_REG_ADCDAC, 0x000 | WM8750_ADCDAC_DEEMP(2));
    //setReg(WM8750_REG_LOUT1VOL, WM8750_OUT_VOL(0b1111001));
    //setReg(WM8750_REG_ROUT1VOL, WM8750_OUT_VOL(0b1111001));
  }

  void mute() {
    log_v("Audio codec: mute");
    setReg(WM8750_REG_ADCDAC, WM8750_ADCDAC_DACMUTE);
    //setReg(WM8750_REG_LOUT1VOL, WM8750_OUT_VOL(0));
    //setReg(WM8750_REG_ROUT1VOL, WM8750_OUT_VOL(0));
    //setReg(WM8750_REG_POWER2, 0x00);
    //setReg(WM8750_REG_POWER1, 0x00);
  }

  void setVolume(int8_t dBspeaker, int8_t dBheadphones) {
    uint8_t vol;

    // Headphones volume
    vol = (dBheadphones + 0x79) & 0x7f;
    log_v("Audio codec: headphones vol = %d dB,  0x%02x", dBheadphones, vol);
    setReg(WM8750_REG_LOUT1VOL, WM8750_OUT_VOL(vol));
    setReg(WM8750_REG_ROUT1VOL, WM8750_OUT_VOL(vol) | WM8750_OUT_VU);

    // Speaker volume
    vol = (dBspeaker + 0x79) & 0x7f;
    log_v("Audio codec: speaker vol = %d dB,  0x%02x", dBspeaker, vol);
    setReg(WM8750_REG_LOUT2VOL, WM8750_OUT_VOL(vol));
    setReg(WM8750_REG_ROUT2VOL, WM8750_OUT_VOL(vol) | WM8750_OUT_VU);
  }

  void setAudioPath(bool stereo) {
    // microphone maximum boost (+29 dB)
    // send main mic into the left ADC (LADCIN)
    setReg(WM8750_REG_LADCIN, WM8750_ADCIN_MICBOOST(3) | WM8750_ADCIN_INSEL(1));
    // - feed left ADC to left data + right data; AVDD=3.3V
    setReg(WM8750_REG_ADDCTRL1, WM8750_ADDCTRL1_DATSEL(1) | WM8750_ADDCTRL1_VSEL(3));



    // main MIC is on LINPUT2, rear mic is on RINPUT2 (channel 2
    // selected by WM8750_ADCIN_INSEL (1) in both cases)

    // Note that the first ~100 production boards route the rear mic
    // into LINPUT 3 ((WM8750_ADCIN_INSEL (3)), which means it can't
    // be routed through the ADCs at the same time as the main
    // mic. we do not attempt to handle this in software as anything
    // we do still wouldn't allow simultaneous access. But if
    // someone wanted to use the rear mic instead of the front one
    // they can access it via WM8750_ADCIN_INSEL(3) on the LADCIN
    // channel
    // setReg(WM8750_REG_LADCIN, WM8750_ADCIN_MICBOOST(3) | WM8750_ADCIN_INSEL(1)); // send main mic into the left ADC (LADCIN)
    // setReg(WM8750_REG_RADCIN, WM8750_ADCIN_MICBOOST(3) | WM8750_ADCIN_INSEL(1)); // send rear mic into the right ADC (RADCIN)

    // Now changed to put the front mic on the left channel and rear mic on the right channel
    // - left ADC provides left stereo channel and right ADC provides right stereo channel; AVDD=3.3V

    // note that the WiPhone does not have a stereo mic, rather we
    // send the front mic signal on the right stereo channel and the
    // rear mic on the left stereo channel.
    //setReg(WM8750_REG_ADDCTRL1, WM8750_ADDCTRL1_DATSEL(0) | WM8750_ADDCTRL1_VSEL(3));
    //setReg(WM8750_REG_ADDCTRL1, WM8750_ADDCTRL1_DATSEL(3) | WM8750_ADDCTRL1_VSEL(3));


    // Audio path configuration (output)
    // (WM8750_REG_LOUTM1 and WM8750_REG_ROUTM1 are for selecting LMIXSEL/RMIXSEL input, so there is some asymmetry below)

    // Left side to Left Out Mixer
    setReg(WM8750_REG_LOUTM1, WM8750_LOUTM1_LDAC | WM8750_LOUTM1_LMIXSEL_VOL(2) | WM8750_OUTM1_MIXSEL(0));         // LDAC -> Left Out Mixer (LMIXSEL is ignored, but set to 0 dB volume)
    //setReg(WM8750_REG_LOUTM1, WM8750_OUTM1_MIXSEL(3)  | WM8750_LOUTM1_LMIXSEL | WM8750_LOUTM1_LMIXSEL_VOL(0));    // ADC Input (after PGA / MICBOOST) for LMIXSEL, +6 dB LMIXSEL, LMIXSEL -> Left Out Mixer

    // Right side to Left Out Mixer
    setReg(WM8750_REG_LOUTM2, 0x00);      // right to left cancelled

    if (stereo) {                   // ESP32: I2S_CHANNEL_FMT_RIGHT_LEFT

      /* Case 1: righ side -> right output */

      // Left side to Right Out Mixer
      //     RINPUT1 -> RMIXSEL (doesn't matter if RDAC is set); left to right cancelled
      setReg(WM8750_REG_ROUTM1, WM8750_OUTM1_MIXSEL(0));

      // Right -> Right Out Mixer (mixsel, max volume)
      //     RDAC -> Right Out Mixer (RMIXSEL is ignored, but set to 0 dB volume)
      setReg(WM8750_REG_ROUTM2, WM8750_ROUTM2_RDAC | WM8750_ROUTM2_RMIXSEL_VOL(2));

    } else {                        // ESP32: I2S_CHANNEL_FMT_ONLY_LEFT

      /* Case 2: left side -> left & right output */

      // Left side to Right Out Mixer
      //     LDAC -> Right Out Mixer; RINPUT1 -> RMIXSEL (doesn't matter if RDAC is set)
      setReg(WM8750_REG_ROUTM1, WM8750_ROUTM1_LDAC | WM8750_OUTM1_MIXSEL(0));
      // Right -> Right Out Mixer (mixsel, max volume)
      //     (RMIXSEL is ignored, but set to 0 dB volume)
      setReg(WM8750_REG_ROUTM2, WM8750_ROUTM2_RMIXSEL_VOL(2));

    }
  }

  wm8750_err_t powerUp(bool stereo, int samplingRate, uint16_t powerMask=0, uint16_t crystal_KHz=0) {
    log_v("Audio codec: power up");

    // TODO: power saving: WM8750_POWER1_VMISEL(1) + ADCOSR + DACOSR
    //setReg(WM8750_REG_POWER1, WM8750_POWER1_VMIDSEL(2) | WM8750_POWER1_VREF);   // fast startup
    wm8750_err_t err = setReg(WM8750_REG_POWER1, WM8750_POWER1_VMIDSEL(2) | WM8750_POWER1_VREF | WM8750_POWER1_AINL | WM8750_POWER1_ADCL | WM8750_POWER1_MICB);   // fast startup + microphone + Left ADC
    if (err != WM8750_ERROR_OK) {
      return err;
    }

    // Power up DAC + output
    if (!powerMask)
      // Power everything by default: DACs + speaker + headphone + earpiece outputs
    {
      powerMask = WM8750_POWER2_OUT1 | WM8750_POWER2_OUT2 | WM8750_POWER2_DAC | WM8750_POWER2_OUT3;
    }
    //setReg(WM8750_REG_POWER2, WM8750_POWER2_OUT1 | WM8750_POWER2_DAC);     // DAC + headphones
    //setReg(WM8750_REG_POWER2, WM8750_POWER2_OUT2 | WM8750_POWER2_DAC);     // DAC + speaker
    //setReg(WM8750_REG_POWER2, WM8750_POWER2_OUT1 | WM8750_POWER2_OUT2 | WM8750_POWER2_DACR);     // DAC + speaker + headphones
    //setReg(WM8750_REG_POWER2, WM8750_POWER2_OUT1 | WM8750_POWER2_OUT2 | WM8750_POWER2_DAC | WM8750_POWER2_OUT3);     // DAC + speaker + headphones + earpiece
    setReg(WM8750_REG_POWER2, powerMask);

    // Interface
    setReg(WM8750_REG_INTERFACE, WM8750_INTERFACE_WORDLEN(0) | WM8750_INTERFACE_FORMAT(2));   // 16-bit word, I2S, Slave
    //setReg(WM8750_REG_INTERFACE, WM8750_INTERFACE_WORDLEN(0) | WM8750_INTERFACE_FORMAT(2) | WM8750_INTERFACE_LRSWAP);   // 16-bit word, I2S, Slave, Right/Left
    //setReg(WM8750_REG_INTERFACE, WM8750_INTERFACE_WORDLEN(0) | WM8750_INTERFACE_FORMAT(2) | WM8750_INTERFACE_LRP_B);   // 16-bit word, I2S, Slave, LR inverse
    //setReg(WM8750_REG_INTERFACE, WM8750_INTERFACE_WORDLEN(0) | WM8750_INTERFACE_FORMAT(3) | WM8750_INTERFACE_LRP_B);   // 16-bit word, DSP, Slave, mode B

    // Input volume = 0 dB
    setReg(WM8750_REG_LINPGA, WM8750_INPGA_INVOL(0b010111));
    setReg(WM8750_REG_RINPGA, WM8750_INPGA_INVOL(0b010111) | WM8750_INPGA_VU);

    // LADC max volume (+30 dB)
    setReg(WM8750_REG_LADC, WM8750_ADC_VOL((0xff-60)) | WM8750_ADC_VU);

    // Headphone volume = -24 dB (to avoid sudden and loud pop)
    setReg(WM8750_REG_LOUT1VOL, WM8750_OUT_VOL(0b1100001));
    setReg(WM8750_REG_ROUT1VOL, WM8750_OUT_VOL(0b1100001) | WM8750_OUT_VU);

    // Speaker volume = -24 dB (to avoid sudden and loud pop)
    setReg(WM8750_REG_LOUT2VOL, WM8750_OUT_VOL(0b1100001));
    setReg(WM8750_REG_ROUT2VOL, WM8750_OUT_VOL(0b1100001) | WM8750_OUT_VU);

    // Enable correct outputs for the speakers + headphone detection
    uint16_t val = 0;
    // Detecting headphone typically switches between OUT1 (headphones) and OUT2 (speaker)
    // (headphones are typically connected to LOUT1 / ROUT1,
    //  speakers are connected to LOUT2 / ROUT2, OUT3 / LOUT1)
    //val |= WM8750_ADDCTRL2_HPSWEN;      // Detect headphones and switch between OUT1 / OUT2 automatically
    if (powerMask & WM8750_POWER2_OUT2) {
      val |= WM8750_ADDCTRL2_ROUT2INV;  // Invert ROUT2 for the speaker
    }
    if (powerMask & WM8750_POWER2_OUT3) {
      val |= WM8750_ADDCTRL2_OUT3SW(1);  // OUT3 = ROUT1 (otherwise VREF)
    }
    setReg(WM8750_REG_ADDCTRL2, val);

    this->setAudioPath(stereo);

    log_v("SR = %d, MCLK = %d", samplingRate, crystal_KHz);
    // Sampling control
    if (crystal_KHz == 12000) {
      // USB mode (12.00 MHz crystal)
      if (samplingRate == 8000) {
        setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_USBMODE | WM8750_SAMPLING_RATE(0b00110));   // 8/8 kHz
      } else if (samplingRate == 16000) {
        setReg(WM8750_REG_SAMPLING,  WM8750_SAMPLING_USBMODE | WM8750_SAMPLING_RATE(0b01010));  // 16/16 kHz
      } else if (samplingRate == 32000) {
        setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_USBMODE | WM8750_SAMPLING_RATE(0b01100));   // 32/32 kHz
      } else if (samplingRate == 44100) {
        setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_USBMODE | WM8750_SAMPLING_RATE(0b10001));   // 44.1/44.1 kHz
      } else {
        log_e("sampling rate %d not implemented", samplingRate);
      }
    } else if (crystal_KHz == 12288) {
      // Normal clock mode (12.288 MHz crystal)
      if (samplingRate == 8000) {
        setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_RATE(0b00110));   // 8/8 kHz
      } else if (samplingRate == 16000) {
        setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_RATE(0b01010));   // 16/16 kHz
      } else if (samplingRate == 32000) {
        setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_RATE(0b01100));   // 32/32 kHz
      } else {
        log_e("sampling rate %d not implemented", samplingRate);
      }
    } else if (crystal_KHz == 0) {
      // MCLK is fed from ESP32 I2S clock and is proportionate to the sampling rate
      // Sampling rate provided by user is ignored
      setReg(WM8750_REG_SAMPLING, WM8750_SAMPLING_RATE(0b01110));   // MCLK/128 (same config as for 32000 Hz sampling rate)
    } else {
      log_e("crystal freq. %d not implemented", crystal_KHz);
    }

    // Graphic equalizer
    //if (samplingRate >= 32000) {
    // Boost bass for the music
    setReg(WM8750_REG_BASS, WM8750_BASS_HIGH_CUTOFF | WM8750_BASS_MAX_BOOST);        // high cutoff (200 Hz) / +9 dB / linear bass control
    setReg(WM8750_REG_TREBLE, WM8750_TREBLE_MIN_INTENS | WM8750_TREBLE_LOW_CUTOFF);      // low cutoff (4 KHz) / -6 dB
    //} else {
    //    // Default configuration
    //    setReg(WM8750_REG_BASS, 0x0F);        // bypass (OFF)
    //    setReg(WM8750_REG_TREBLE, 0x0F);      // disable
    //}

    // Unmute DAC
    err = setReg(WM8750_REG_ADCDAC, 0x000 | WM8750_ADCDAC_DEEMP(2));        // deemphasis for 44.1 kHz
    if (err != WM8750_ERROR_OK) {
      return err;
    }

    return WM8750_ERROR_OK;
  }

protected:

  wm8750_err_t setReg(unsigned char regAddr, unsigned short val) {   // machinesalem-arduino-libs
    Wire.beginTransmission(_addr);
    if (Wire.write( (unsigned char)((regAddr<<1) | ((val>>8) & 0x1)) ))
      if (Wire.write( (unsigned char)(val & 0xFF) )) {
        return (wm8750_err_t) Wire.endTransmission();
      }
    return WM8750_ERROR_WRITE_FAILED;
  }
};

#endif // WM8750_h
