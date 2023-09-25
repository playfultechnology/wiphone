/***************************************************
  Arduino TFT graphics library targeted at ESP8266
  and ESP32 based boards.

  This is a standalone library that contains the
  hardware driver, the graphics functions and the
  proportional fonts.

  The larger fonts are Run Length Encoded to reduce
  their FLASH footprint.

 ****************************************************/

// Stop fonts etc being loaded multiple times
#ifndef _TFT_eSPIH_
#define _TFT_eSPIH_

//#define ESP32 //Just used to test ESP32 options

// Include header file that defines the fonts loaded, the TFT drivers
// available and the pins to be used
#include "User_Setup_Select.h"

#ifndef TAB_COLOUR
  #define TAB_COLOUR 0
#endif

// If the frequency is not defined, set a default
#ifndef SPI_FREQUENCY
  #define SPI_FREQUENCY  20000000
#endif

// If the frequency is not defined, set a default
#ifndef SPI_READ_FREQUENCY
  #define SPI_READ_FREQUENCY SPI_FREQUENCY
#endif

#ifdef ST7789_DRIVER
  #define TFT_SPI_MODE SPI_MODE3
#else
  #define TFT_SPI_MODE SPI_MODE0
#endif

// If the frequency is not defined, set a default
#ifndef SPI_TOUCH_FREQUENCY
  #define SPI_TOUCH_FREQUENCY  2500000
#endif

// Use GLCD font in error case where user requests a smooth font file
// that does not exist (this is a temporary fix to stop ESP32 reboot)
#ifdef SMOOTH_FONT
  #ifndef LOAD_GLCD
    #define LOAD_GLCD
  #endif
  #ifndef SMOOTH_FONT_SPIFFS
    #define SMOOTH_FONT_SPIFFS
  #endif
#endif

// Only load the fonts defined in User_Setup.h (to save space)
// Set flag so RLE rendering code is optionally compiled
#ifdef LOAD_GLCD
  #include "Fonts/glcdfont.c"
#endif

#ifdef LOAD_FONT2
  #include "Fonts/Font16.h"
#endif

#ifdef LOAD_FONT4
  #include "Fonts/Font32rle.h"
  #define LOAD_RLE
#endif

#ifdef LOAD_FONT6
  #include "Fonts/Font64rle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#endif

#ifdef LOAD_FONT7
  #include "Fonts/Font7srle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#endif

#ifdef LOAD_FONT8
  #include "Fonts/Font72rle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#elif defined LOAD_FONT8N
  #define LOAD_FONT8
  #include "Fonts/Font72x53rle.h"
  #ifndef LOAD_RLE
    #define LOAD_RLE
  #endif
#endif

#include <Arduino.h>
#include <Print.h>
#include <stdint.h>

#include <pgmspace.h>

#include <SPI.h>

#ifdef SMOOTH_FONT_SPIFFS
  // Call up the SPIFFS FLASH filing system for the anti-aliased fonts
  #define FS_NO_GLOBALS
  #include <FS.h>

  #ifdef ESP32
    #include "SPIFFS.h"
  #endif
#endif

#ifdef ESP32
  #include "rom/tjpgd.h"
#endif // ESP32

#ifndef TFT_DC
  #define DC_C // No macro allocated so it generates no code
  #define DC_D // No macro allocated so it generates no code
#else
  #if defined (ESP8266) && defined (D0_USED_FOR_DC)
    #define DC_C digitalWrite(TFT_DC, LOW)
    #define DC_D digitalWrite(TFT_DC, HIGH)
  #elif defined (ESP32)
    #if defined (ESP32_PARALLEL)
      #define DC_C GPIO.out_w1tc = (1 << TFT_DC)
      #define DC_D GPIO.out_w1ts = (1 << TFT_DC)

    #else
      #if TFT_DC >= 32
        #define DC_C GPIO.out1_w1ts.val = (1 << (TFT_DC - 32)); \
                     GPIO.out1_w1tc.val = (1 << (TFT_DC - 32))
        #define DC_D GPIO.out1_w1tc.val = (1 << (TFT_DC - 32)); \
                     GPIO.out1_w1ts.val = (1 << (TFT_DC - 32))
      #else
        #if TFT_DC >= 0
          #define DC_C GPIO.out_w1ts = (1 << TFT_DC); \
                       GPIO.out_w1tc = (1 << TFT_DC)
          #define DC_D GPIO.out_w1tc = (1 << TFT_DC); \
                       GPIO.out_w1ts = (1 << TFT_DC)
        #else
          #define DC_C
          #define DC_D
        #endif
      #endif
    #endif
  #else
    #define DC_C GPOC=dcpinmask
    #define DC_D GPOS=dcpinmask
  #endif
#endif

#if defined (TFT_SPI_OVERLAP)
  #undef TFT_CS
#endif

#ifndef TFT_CS
  #define CS_L // No macro allocated so it generates no code
  #define CS_H // No macro allocated so it generates no code
#else
  #if defined (ESP8266) && defined (D0_USED_FOR_CS)
    #define CS_L digitalWrite(TFT_CS, LOW)
    #define CS_H digitalWrite(TFT_CS, HIGH)
  #elif defined (ESP32)
    #if defined (ESP32_PARALLEL)
      #define CS_L // The TFT CS is set permanently low during init()
      #define CS_H
    #else
      #if TFT_CS >= 32
        #define CS_L GPIO.out1_w1ts.val = (1 << (TFT_CS - 32)); \
                     GPIO.out1_w1tc.val = (1 << (TFT_CS - 32))
        #define CS_H GPIO.out1_w1tc.val = (1 << (TFT_CS - 32)); \
                     GPIO.out1_w1ts.val = (1 << (TFT_CS - 32))
      #else
        #if TFT_CS >= 0
          #define CS_L GPIO.out_w1ts = (1 << TFT_CS);GPIO.out_w1tc = (1 << TFT_CS)
          #define CS_H GPIO.out_w1tc = (1 << TFT_CS);GPIO.out_w1ts = (1 << TFT_CS)
        #else
          #define CS_L
          #define CS_H
        #endif
      #endif
    #endif
  #else
    #define CS_L GPOC=cspinmask
    #define CS_H GPOS=cspinmask
  #endif
#endif


//#define TFT_CS_INVERSE			// CS signal is reversed in WiPhone to allow using a single GPIO for display CS and SD card CS

#ifdef TFT_CS_INVERSE
  #define TFT_CS_SEL	HIGH
  #define TFT_CS_DESEL	LOW
  #define TFT_SELECT	CS_H
  #define TFT_DESELECT
#else
  #define TFT_CS_SEL	LOW
  #define TFT_CS_DESEL	HIGH
  #define TFT_SELECT	CS_L
  #define TFT_DESELECT	CS_H
#endif

// chip select signal for touchscreen
#ifndef TOUCH_CS
  #define T_CS_L // No macro allocated so it generates no code
  #define T_CS_H // No macro allocated so it generates no code
#else
  #define T_CS_L digitalWrite(TOUCH_CS, LOW)
  #define T_CS_H digitalWrite(TOUCH_CS, HIGH)
#endif


#ifdef TFT_WR
  #if defined (ESP32)
    #define WR_L GPIO.out_w1tc = (1 << TFT_WR)
    #define WR_H GPIO.out_w1ts = (1 << TFT_WR)
  #else
    #define WR_L GPOC=wrpinmask
    #define WR_H GPOS=wrpinmask
  #endif
#endif


#if defined (ESP32) && defined (ESP32_PARALLEL)
  // Mask for the 8 data bits to set pin directions
  #define dir_mask ((1 << TFT_D0) | (1 << TFT_D1) | (1 << TFT_D2) | (1 << TFT_D3) | (1 << TFT_D4) | (1 << TFT_D5) | (1 << TFT_D6) | (1 << TFT_D7))

  // Data bits and the write line are cleared to 0 in one step
  #define clr_mask (dir_mask | (1 << TFT_WR))

  // A lookup table is used to set the different bit patterns, this uses 1kByte of RAM
  #define set_mask(C) xset_mask[C] // 63fps Sprite rendering test 33% faster, graphicstest only 1.8% faster than shifting in real time

  // Real-time shifting alternative to above to save 1KByte RAM, 47 fps Sprite rendering test
  /*#define set_mask(C) ((C&0x80)>>7)<<TFT_D7 | ((C&0x40)>>6)<<TFT_D6 | ((C&0x20)>>5)<<TFT_D5 | ((C&0x10)>>4)<<TFT_D4 | \
                        ((C&0x08)>>3)<<TFT_D3 | ((C&0x04)>>2)<<TFT_D2 | ((C&0x02)>>1)<<TFT_D1 | ((C&0x01)>>0)<<TFT_D0
  //*/

  // Write 8 bits to TFT
  #define tft_Write_8(C)  GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t)C); WR_H

  // Write 16 bits to TFT
  #ifdef PSEUDO_8_BIT
    #define tft_Write_16(C) WR_L;GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t)(C >> 0)); WR_H
  #else
    #define tft_Write_16(C) GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t)(C >> 8)); WR_H; \
                          GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t)(C >> 0)); WR_H
  #endif

  // 16 bit write with swapped bytes
  #define tft_Write_16S(C) GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t) (C >>  0)); WR_H; \
                           GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t) (C >>  8)); WR_H

  // Write 32 bits to TFT
  #define tft_Write_32(C) GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t) (C >> 24)); WR_H; \
                          GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t) (C >> 16)); WR_H; \
                          GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t) (C >>  8)); WR_H; \
                          GPIO.out_w1tc = clr_mask; GPIO.out_w1ts = set_mask((uint8_t) (C >>  0)); WR_H

  #ifdef TFT_RD
    #define RD_L GPIO.out_w1tc = (1 << TFT_RD)
    //#define RD_L digitalWrite(TFT_WR, LOW)
    #define RD_H GPIO.out_w1ts = (1 << TFT_RD)
    //#define RD_H digitalWrite(TFT_WR, HIGH)
  #endif

#elif  defined (ILI9488_DRIVER) // 16 bit colour converted to 3 bytes for 18 bit RGB

  // Write 8 bits to TFT
  #define tft_Write_8(C)  SPI.transfer(C)

  // Convert 16 bit colour to 18 bit and write in 3 bytes
  #define tft_Write_16(C) SPI.transfer((C & 0xF800)>>8); \
                          SPI.transfer((C & 0x07E0)>>3); \
                          SPI.transfer((C & 0x001F)<<3)

  // Convert swapped byte 16 bit colour to 18 bit and write in 3 bytes
  #define tft_Write_16S(C) SPI.transfer(C & 0xF8); \
                           SPI.transfer((C & 0xE0)>>11 | (C & 0x07)<<5); \
                           SPI.transfer((C & 0x1F00)>>5)
  // Write 32 bits to TFT
  #define tft_Write_32(C) SPI.write32(C)

#elif  defined (RPI_ILI9486_DRIVER)

  #define tft_Write_8(C)  SPI.transfer(0); SPI.transfer(C)
  #define tft_Write_16(C) SPI.write16(C)
  #define tft_Write_32(C) SPI.write32(C)

#else

  #define tft_Write_8(C)  SPI.transfer(C)
  #define tft_Write_16(C) SPI.write16(C)
  #define tft_Write_32(C) SPI.write32(C)

#endif

#ifdef LOAD_GFXFF
  // We can include all the free fonts and they will only be built into
  // the sketch if they are used

  #include "Fonts/GFXFF/gfxfont.h"

  // Call up any user custom fonts
  #include "User_Setups/User_Custom_Fonts.h"

  // Original Adafruit_GFX "Free Fonts"
  #include "Fonts/GFXFF/TomThumb.h"  // TT1

  #include "Fonts/GFXFF/FreeMono9pt7b.h"  // FF1 or FM9
  #include "Fonts/GFXFF/FreeMono12pt7b.h" // FF2 or FM12
  #include "Fonts/GFXFF/FreeMono18pt7b.h" // FF3 or FM18
  #include "Fonts/GFXFF/FreeMono24pt7b.h" // FF4 or FM24

  #include "Fonts/GFXFF/FreeMonoOblique9pt7b.h"  // FF5 or FMO9
  #include "Fonts/GFXFF/FreeMonoOblique12pt7b.h" // FF6 or FMO12
  #include "Fonts/GFXFF/FreeMonoOblique18pt7b.h" // FF7 or FMO18
  #include "Fonts/GFXFF/FreeMonoOblique24pt7b.h" // FF8 or FMO24
  
  #include "Fonts/GFXFF/FreeMonoBold9pt7b.h"  // FF9  or FMB9
  #include "Fonts/GFXFF/FreeMonoBold12pt7b.h" // FF10 or FMB12
  #include "Fonts/GFXFF/FreeMonoBold18pt7b.h" // FF11 or FMB18
  #include "Fonts/GFXFF/FreeMonoBold24pt7b.h" // FF12 or FMB24
  
  #include "Fonts/GFXFF/FreeMonoBoldOblique9pt7b.h"  // FF13 or FMBO9
  #include "Fonts/GFXFF/FreeMonoBoldOblique12pt7b.h" // FF14 or FMBO12
  #include "Fonts/GFXFF/FreeMonoBoldOblique18pt7b.h" // FF15 or FMBO18
  #include "Fonts/GFXFF/FreeMonoBoldOblique24pt7b.h" // FF16 or FMBO24
  
  // Sans serif fonts
  #include "Fonts/GFXFF/FreeSans9pt7b.h"  // FF17 or FSS9
  #include "Fonts/GFXFF/FreeSans12pt7b.h" // FF18 or FSS12
  #include "Fonts/GFXFF/FreeSans18pt7b.h" // FF19 or FSS18
  #include "Fonts/GFXFF/FreeSans24pt7b.h" // FF20 or FSS24
  
  #include "Fonts/GFXFF/FreeSansOblique9pt7b.h"  // FF21 or FSSO9
  #include "Fonts/GFXFF/FreeSansOblique12pt7b.h" // FF22 or FSSO12
  #include "Fonts/GFXFF/FreeSansOblique18pt7b.h" // FF23 or FSSO18
  #include "Fonts/GFXFF/FreeSansOblique24pt7b.h" // FF24 or FSSO24
  
  #include "Fonts/GFXFF/FreeSansBold9pt7b.h"  // FF25 or FSSB9
  #include "Fonts/GFXFF/FreeSansBold12pt7b.h" // FF26 or FSSB12
  #include "Fonts/GFXFF/FreeSansBold18pt7b.h" // FF27 or FSSB18
  #include "Fonts/GFXFF/FreeSansBold24pt7b.h" // FF28 or FSSB24
  
  #include "Fonts/GFXFF/FreeSansBoldOblique9pt7b.h"  // FF29 or FSSBO9
  #include "Fonts/GFXFF/FreeSansBoldOblique12pt7b.h" // FF30 or FSSBO12
  #include "Fonts/GFXFF/FreeSansBoldOblique18pt7b.h" // FF31 or FSSBO18
  #include "Fonts/GFXFF/FreeSansBoldOblique24pt7b.h" // FF32 or FSSBO24
  
  // Serif fonts
  #include "Fonts/GFXFF/FreeSerif9pt7b.h"  // FF33 or FS9
  #include "Fonts/GFXFF/FreeSerif12pt7b.h" // FF34 or FS12
  #include "Fonts/GFXFF/FreeSerif18pt7b.h" // FF35 or FS18
  #include "Fonts/GFXFF/FreeSerif24pt7b.h" // FF36 or FS24
  
  #include "Fonts/GFXFF/FreeSerifItalic9pt7b.h"  // FF37 or FSI9
  #include "Fonts/GFXFF/FreeSerifItalic12pt7b.h" // FF38 or FSI12
  #include "Fonts/GFXFF/FreeSerifItalic18pt7b.h" // FF39 or FSI18
  #include "Fonts/GFXFF/FreeSerifItalic24pt7b.h" // FF40 or FSI24
  
  #include "Fonts/GFXFF/FreeSerifBold9pt7b.h"  // FF41 or FSB9
  #include "Fonts/GFXFF/FreeSerifBold12pt7b.h" // FF42 or FSB12
  #include "Fonts/GFXFF/FreeSerifBold18pt7b.h" // FF43 or FSB18
  #include "Fonts/GFXFF/FreeSerifBold24pt7b.h" // FF44 or FSB24
  
  #include "Fonts/GFXFF/FreeSerifBoldItalic9pt7b.h"  // FF45 or FSBI9
  #include "Fonts/GFXFF/FreeSerifBoldItalic12pt7b.h" // FF46 or FSBI12
  #include "Fonts/GFXFF/FreeSerifBoldItalic18pt7b.h" // FF47 or FSBI18
  #include "Fonts/GFXFF/FreeSerifBoldItalic24pt7b.h" // FF48 or FSBI24
  
#endif // #ifdef LOAD_GFXFF

//These enumerate the text plotting alignment (reference datum point)
#define TOP_DATUM_FLAG			0x01
#define MIDDLE_DATUM_FLAG		0x02
#define BASELINE_DATUM_FLAG		0x04
#define BOTTOM_DATUM_FLAG		0x08

#define LEFT_DATUM_FLAG			0x10
#define CENTER_DATUM_FLAG		0x20
#define RIGHT_DATUM_FLAG		0x40

#define TL_DATUM	TOP_DATUM_FLAG | LEFT_DATUM_FLAG		// Top left (default)
#define TC_DATUM	TOP_DATUM_FLAG | CENTER_DATUM_FLAG		// Top centre
#define TR_DATUM	TOP_DATUM_FLAG | RIGHT_DATUM_FLAG		// Top right
#define ML_DATUM	MIDDLE_DATUM_FLAG | LEFT_DATUM_FLAG		// Middle left
#define CL_DATUM	ML_DATUM	// Centre left, same as above
#define MC_DATUM	MIDDLE_DATUM_FLAG | CENTER_DATUM_FLAG	// Middle centre
#define CC_DATUM	MC_DATUM	// Centre centre, same as above
#define MR_DATUM	MIDDLE_DATUM_FLAG | RIGHT_DATUM_FLAG	// Middle right
#define CR_DATUM	MR_DATUM	// Centre right, same as above
#define BL_DATUM	BOTTOM_DATUM_FLAG | LEFT_DATUM_FLAG		// Bottom left
#define BC_DATUM	BOTTOM_DATUM_FLAG | CENTER_DATUM_FLAG	// Bottom centre
#define BR_DATUM	BOTTOM_DATUM_FLAG | RIGHT_DATUM_FLAG	// Bottom right
#define L_BASELINE	BASELINE_DATUM_FLAG | LEFT_DATUM_FLAG	// Left character baseline (Line the 'A' character would sit on)
#define C_BASELINE	BASELINE_DATUM_FLAG | CENTER_DATUM_FLAG	// Centre character baseline
#define R_BASELINE	BASELINE_DATUM_FLAG | RIGHT_DATUM_FLAG	// Right character baseline

// New color definitions use for all my libraries
#define TFT_BLACK       0x0000      /*   0,   0,   0 */
#define TFT_NAVY        0x000F      /*   0,   0, 128 */
#define TFT_DARKGREEN   0x03E0      /*   0, 128,   0 */
#define TFT_DARKCYAN    0x03EF      /*   0, 128, 128 */
#define TFT_MAROON      0x7800      /* 128,   0,   0 */
#define TFT_PURPLE      0x780F      /* 128,   0, 128 */
#define TFT_OLIVE       0x7BE0      /* 128, 128,   0 */
#define TFT_LIGHTGREY   0xC618      /* 192, 192, 192 */
#define TFT_DARKGREY    0x7BEF      /* 128, 128, 128 */
#define TFT_BLUE        0x001F      /*   0,   0, 255 */
#define TFT_GREEN       0x07E0      /*   0, 255,   0 */
#define TFT_CYAN        0x07FF      /*   0, 255, 255 */
#define TFT_RED         0xF800      /* 255,   0,   0 */
#define TFT_MAGENTA     0xF81F      /* 255,   0, 255 */
#define TFT_YELLOW      0xFFE0      /* 255, 255,   0 */
#define TFT_WHITE       0xFFFF      /* 255, 255, 255 */
#define TFT_ORANGE      0xFDA0      /* 255, 180,   0 */
#define TFT_GREENYELLOW 0xB7E0      /* 180, 255,   0 */
#define TFT_PINK        0xFC9F

// Next is a special 16 bit colour value that encodes to 8 bits
// and will then decode back to the same 16 bit value.
// Convenient for 8 bit and 16 bit transparent sprites.
#define TFT_TRANSPARENT 0x0120

// Swap any type
template <typename T> static inline void
swap_coord(T& a, T& b) { T t = a; a = b; b = t; }

// This structure allows sketches to retrieve the user setup parameters at runtime
// by calling getSetup(), zero impact on code size unless used, mainly for diagnostics
typedef struct
{
int16_t esp;
uint8_t trans;
uint8_t serial;
uint8_t overlap;

uint16_t tft_driver; // Hexadecimal code
uint16_t tft_width;  // Rotation 0 width and height
uint16_t tft_height;

uint8_t r0_x_offset; // Offsets, not all used yet
uint8_t r0_y_offset;
uint8_t r1_x_offset;
uint8_t r1_y_offset;
uint8_t r2_x_offset;
uint8_t r2_y_offset;
uint8_t r3_x_offset;
uint8_t r3_y_offset;

int8_t pin_tft_mosi;
int8_t pin_tft_miso;
int8_t pin_tft_clk;
int8_t pin_tft_cs;

int8_t pin_tft_dc;
int8_t pin_tft_rd;
int8_t pin_tft_wr;
int8_t pin_tft_rst;

int8_t pin_tft_d0;
int8_t pin_tft_d1;
int8_t pin_tft_d2;
int8_t pin_tft_d3;
int8_t pin_tft_d4;
int8_t pin_tft_d5;
int8_t pin_tft_d6;
int8_t pin_tft_d7;

int8_t pin_tch_cs;

int16_t tft_spi_freq;
int16_t tch_spi_freq;
} setup_t;

// This is a structure to conveniently hold information on the default fonts
// Stores pointer to font character image address table, width table and height

// Create a null set in case some fonts not used (to prevent crash)
const  uint8_t widtbl_null[1] = {0};
PROGMEM const uint8_t chr_null[1] = {0};
PROGMEM const uint8_t* const chrtbl_null[1] = {chr_null};

typedef struct {
    const uint8_t *chartbl;
    const uint8_t *widthtbl;
    uint8_t height;
    uint8_t baseline;
    } fontinfo;

// Now fill the structure
const PROGMEM fontinfo fontdata [] = {
  #ifdef LOAD_GLCD
   { (const uint8_t *)font, widtbl_null, 0, 0 },
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif
   // GLCD font (Font 1) does not have all parameters
   { (const uint8_t *)chrtbl_null, widtbl_null, 8, 7 },

  #ifdef LOAD_FONT2
   { (const uint8_t *)chrtbl_f16, widtbl_f16, chr_hgt_f16, baseline_f16},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

   // Font 3 current unused
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },

  #ifdef LOAD_FONT4
   { (const uint8_t *)chrtbl_f32, widtbl_f32, chr_hgt_f32, baseline_f32},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

   // Font 5 current unused
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },

  #ifdef LOAD_FONT6
   { (const uint8_t *)chrtbl_f64, widtbl_f64, chr_hgt_f64, baseline_f64},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

  #ifdef LOAD_FONT7
   { (const uint8_t *)chrtbl_f7s, widtbl_f7s, chr_hgt_f7s, baseline_f7s},
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 },
  #endif

  #ifdef LOAD_FONT8
   { (const uint8_t *)chrtbl_f72, widtbl_f72, chr_hgt_f72, baseline_f72}
  #else
   { (const uint8_t *)chrtbl_null, widtbl_null, 0, 0 }
  #endif
};

#ifdef SMOOTH_FONT
class SmoothFont {

 // Coded by Bodmer 10/2/18, see license in root directory.
 // This is part of the TFT_eSPI class and is associated with anti-aliased font functions
 // Modified by Andriy Makukha September 2018.

 public:
  SmoothFont() {};
  ~SmoothFont() { unloadFont(); };
  bool isLoaded(void);  
  uint16_t height(void) { return yAdvance; };

  // These are for the new antialiased fonts
#ifdef SMOOTH_FONT_SPIFFS
  void     loadFont(String fontName);
  fs::File fontFile;
#endif // SMOOTH_FONT_SPIFFS

  void     loadFont(const unsigned char* data);		// load 7-Shade Font
  void     unloadFont( void );
  bool     getUnicodeIndex(uint16_t unicode, uint16_t *index);
  int32_t  textWidth(const char *string);
  int16_t  fitTextLength(const char *string, uint16_t width, int8_t direction=1);
  int16_t  fitWordsLength(const char *string, uint16_t width, int8_t direction=1);

  // This is for the whole font
  uint16_t gCount = 0;			// Total number of characters
  uint16_t yAdvance = 0;		// Line advance
  uint16_t spaceWidth = 0;		// Width of a space character
  int16_t  ascent = 0;			// Height of top of 'd' above baseline, other characters may be taller
  int16_t  descent = 0;			// Offset to bottom of 'p', other characters may have a larger descent
  uint16_t maxAscent = 0;		// Maximum ascent found in font
  uint16_t maxDescent = 0;		// Maximum descent found in font
  uint8_t  palette[8] = 		// for indexed 3-bit fonts this is where 8-bit color is stored
			{ 0, 36, 73, 109, 146, 182, 219, 255 }; 

  // These are for the metrics for each individual glyph (so we don't need to seek this in file and waste time)
  uint16_t* gUnicode = NULL;	// UTF-16 code, the codes are searched so do not need to be sequential
  uint8_t*  gHeight = NULL;		// cheight
  uint8_t*  gWidth = NULL;	 	// cwidth
  uint8_t*  gxAdvance = NULL;	// setWidth
  int8_t*   gdY = NULL;			// topExtent
  int8_t*   gdX = NULL;			// leftExtent
  uint32_t* gBitmap = NULL;		// file pointer to greyscale bitmap

  uint8_t  fontLoadedType = 0;	// Type of an anti-aliased font loaded (1 - SPIFFS VLW font, 2 - 7SF font)


 private:

#ifdef SMOOTH_FONT_SPIFFS
  String   _gFontFilename;
  uint32_t readInt32(void);
#endif // SMOOTH_FONT_SPIFFS

};
#endif // SMOOTH_FONT

class IconRle3 {
  public:
	IconRle3(const unsigned char* data, uint16_t size) : size(size), data(data) {};
	
	uint16_t height();
	uint16_t width();

	uint16_t size;
	const unsigned char* data;

  protected:

	void load();

	uint16_t _height = 0;
	uint16_t _width = 0;
};

// Class functions and variables
class TFT_eSPI : public Print {

 public:

  TFT_eSPI(int16_t _W = TFT_WIDTH, int16_t _H = TFT_HEIGHT);

  void     init(uint8_t tc = TAB_COLOUR), begin(uint8_t tc = TAB_COLOUR); // Same - begin included for backwards compatibility

  // These are virtual so the TFT_eSprite class can override them with sprite specific functions
  virtual void     setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1),
		           drawPixel(int16_t x, int16_t y, uint32_t color),
                   drawChar(int16_t x, int16_t y, unsigned char c, uint32_t color, uint32_t bg, uint8_t size),
                   drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color),
                   drawFastVLine(int16_t x, int16_t y, int16_t h, uint32_t color),
                   drawFastHLine(int16_t x, int16_t y, int16_t w, uint32_t color),
                   fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color),
           		   pushColor(uint16_t color),
           		   pushColor(uint16_t color, int32_t len),
           		   pushTransparent(uint16_t color, uint8_t alpha, int32_t len),
           		   pushTransparent(int32_t len);

  virtual int16_t  drawChar(unsigned int uniCode, int x, int y, int font),
                   drawChar(unsigned int uniCode, int x, int y),
                   height(void),
                   width(void);

  // The TFT_eSprite class inherits the following functions
  void     pushColors(uint16_t  *data, uint32_t len, bool swap = true), // With byte swap option
           pushColors(uint8_t  *data, uint32_t len),
           fillScreen(uint32_t color);

  void     drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color),
           drawRoundRect(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t radius, uint32_t color),
           fillRoundRect(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t radius, uint32_t color),

           setRotation(uint8_t r),
           invertDisplay(bool i),

           drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color),
           drawCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, uint32_t color),
           fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color),
           fillCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, int32_t delta, uint32_t color),

           drawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color),
           fillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color),

           drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color),
           fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color),

           drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color),
           drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color),
           drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor),
           setBitmapColor(uint16_t fgcolor, uint16_t bgcolor), // For 1bpp sprites

           setCursor(int16_t x, int16_t y),
           setCursor(int16_t x, int16_t y, uint8_t font),
           setTextColor(uint16_t color),
           setTextColor(uint16_t fgcolor, uint16_t bgcolor),
           setTextSize(uint8_t size),

           setTextWrap(bool wrapX, bool wrapY = false),
           setTextDatum(uint8_t datum),
           setTextPadding(uint16_t x_width),

#ifdef LOAD_GFXFF
           setFreeFont(const GFXfont *f = NULL),
           setTextFont(uint8_t font),
#else
           setFreeFont(uint8_t font),
           setTextFont(uint8_t font),
#endif
		   setTextFont(SmoothFont* sFont),

           spiwrite(uint8_t),
           writecommand(uint8_t c),
           writedata(uint8_t d),

           commandList(const uint8_t *addr);

  uint8_t  readcommand8(uint8_t cmd_function, uint8_t index);
  uint16_t readcommand16(uint8_t cmd_function, uint8_t index);
  uint32_t readcommand32(uint8_t cmd_function, uint8_t index);

           // Read the colour of a pixel at x,y and return value in 565 format 
  uint16_t readPixel(int16_t x0, int16_t y0);

           // The next functions can be used as a pair to copy screen blocks (or horizontal/vertical lines) to another location
           // Read a block of pixels to a data buffer, buffer is 16 bit and the array size must be at least w * h
  void     readRect(uint32_t x0, uint32_t y0, uint16_t w, uint16_t h, uint16_t *data);
           // Write a block of pixels to the screen
  void     pushRect(uint32_t x0, uint32_t y0, uint16_t w, uint16_t h, uint16_t *data);

           // These are used to render images or sprites stored in RAM arrays
  virtual void pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, uint16_t *data);
  void     pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, uint16_t *data, uint16_t transparent);

           // These are used to render images stored in FLASH (PROGMEM)
  virtual void pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, const uint16_t *data);
  void     pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, const uint16_t *data, uint16_t transparent);

           // These are used by pushSprite for 1 and 8 bit colours
  void     pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, uint8_t  *data, uint8_t bpp);
  void     pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, uint8_t  *data, uint8_t  transparent, uint8_t bpp);

           // Swap the byte order for pushImage() - corrects endianness
  void     setSwapBytes(bool swap);
  bool     getSwapBytes(void);

           // This next function has been used successfully to dump the TFT screen to a PC for documentation purposes
           // It reads a screen area and returns the RGB 8 bit colour values of each pixel
           // Set w and h to 1 to read 1 pixel's colour. The data buffer must be at least w * h * 3 bytes
  void     readRectRGB(int32_t x0, int32_t y0, int32_t w, int32_t h, uint8_t *data);

  uint8_t  getRotation(void),
           getTextDatum(void),
           color16to8(uint16_t color565); // Convert 16 bit colour to 8 bits

  int16_t  getCursorX(void),
           getCursorY(void);

  uint16_t fontsLoaded(void),
           color565(uint8_t red, uint8_t green, uint8_t blue),   // Convert 8 bit red, green and blue to 16 bits
           color8to16(uint8_t color332);  // Convert 8 bit colour to 16 bits

  int16_t  drawNumber(long long_num,int poX, int poY, int font),
           drawNumber(long long_num,int poX, int poY),
           drawFloat(float floatNumber,int decimal,int poX, int poY, int font),
           drawFloat(float floatNumber,int decimal,int poX, int poY),

           // Handle char arrays
           drawString(const char *string, int poX, int poY, int font),
           drawString(const char *string, int poX, int poY),
           drawCentreString(const char *string, int dX, int poY, int font), // Deprecated, use setTextDatum() and drawString()
           drawRightString(const char *string, int dX, int poY, int font),  // Deprecated, use setTextDatum() and drawString()

           drawFitString(const char *string, uint16_t width, int16_t poX, int16_t poY),

           // Handle String type
           drawString(const String& string, int poX, int poY, int font),
           drawString(const String& string, int poX, int poY),
           drawCentreString(const String& string, int dX, int poY, int font), // Deprecated, use setTextDatum() and drawString()
           drawRightString(const String& string, int dX, int poY, int font);  // Deprecated, use setTextDatum() and drawString()


  int16_t  textWidth(const char *string, int font),
           textWidth(const char *string),
           textWidth(const String& string, int font),
           textWidth(const String& string),
           fontHeight(int16_t font),
           fontHeight(void);

  void     setAddrWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye);


  size_t   write(uint8_t);

  void     getSetup(setup_t& tft_settings); // Sketch provides the instance to populate

  virtual bool     drawImage(const unsigned char* imageData, int len);
  virtual bool     drawImage(const unsigned char* imageData, int len, int16_t x, int16_t y);
  virtual bool     drawImage(IconRle3& icon, int16_t x, int16_t y);
  virtual bool     drawImageRle3(const unsigned char* imageData, int len, int16_t x, int16_t y);
  virtual bool     drawImageI256(const unsigned char* imageData, int len, int16_t x, int16_t y);
//bool     drawImageJpeg(const unsigned char* imageData, int len, int16_t x, int16_t y);		// TODO

  int32_t  cursor_x, cursor_y, padX;
  uint32_t textcolor, textbgcolor;

  uint32_t bitmap_fg, bitmap_bg;

  uint8_t  textfont,  // Current selected font
           textsize,  // Current font size multiplier
           textdatum, // Text reference datum
           rotation;  // Display rotation (0-3)

  virtual bool isSprite(void) { return false; };

 private:

  inline void spi_begin() __attribute__((always_inline));
  inline void spi_end()   __attribute__((always_inline));

  inline void spi_begin_read() __attribute__((always_inline));
  inline void spi_end_read()   __attribute__((always_inline));

  void     readAddrWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye);

  uint8_t  tabcolor,
           colstart = 0, rowstart = 0; // some ST7735 displays need this changed

  volatile uint32_t *dcport, *csport;

  uint32_t cspinmask, dcpinmask, wrpinmask;

#if defined(ESP32_PARALLEL)
  uint32_t  xclr_mask, xdir_mask, xset_mask[256];
#endif

  uint32_t lastColor = 0xFFFF;

 protected:

  int32_t  win_xe, win_ye;

  uint32_t _init_width, _init_height; // Display w/h as input, used by setRotation()
  uint32_t _width, _height;           // Display w/h as modified by current rotation
  uint32_t addr_row, addr_col;

  uint32_t fontsloaded;

  uint8_t  glyph_ab,  // glyph height above baseline
           glyph_bb;  // glyph height below baseline

  bool     textwrapX, textwrapY;   // If set, 'wrap' text at right and optionally bottom edge of display
  bool     _swapBytes; // Swap the byte order for TFT pushImage()
  bool     locked, inTransaction; // Transaction and mutex lock flags for ESP32

  bool     _booted;

  uint32_t _lastColor;

#ifdef LOAD_GFXFF
  GFXfont  *gfxFont;
#endif

// Load the Anti-aliased font extension
#ifdef SMOOTH_FONT

 // Coded by Bodmer 10/2/18, see license in root directory.
 // This is part of the TFT_eSPI class and is associated with anti-aliased font functions
 // Modified by Andriy Makukha September 2018.

 protected:
  SmoothFont* smoothFont = NULL;
  bool     smoothOpaque = true;		// smooth font transparency
 
  uint8_t  decoderState = 0;   // UTF8 decoder state
  uint16_t decoderBuffer;      // Unicode code-point buffer

  uint16_t decodeUTF8(uint8_t c);

 public:
  static uint16_t decodeUTF8(uint8_t *buf, uint16_t *index, uint16_t remaining);
  virtual void drawGlyph(uint16_t code);
  void     showFont(uint32_t td);
  void   setSmoothTransparency(bool transp) { smoothOpaque = !transp; };

#endif // SMOOTH_FONT

}; // End of class TFT_eSPI


/////////////////////////////////////////////////////////////////////////////////////////////////// Sprite.h

/***************************************************************************************
// The following class creates Sprites in RAM, graphics can then be drawn in the Sprite
// and rendered quickly onto the TFT screen. The class inherits the graphics functions
// from the TFT_eSPI class. Some functions are overridden by this class so that the
// graphics are written to the Sprite rather than the TFT.
***************************************************************************************/

class TFT_eSprite : public TFT_eSPI {

 public:

  TFT_eSprite(TFT_eSPI *tft);

           // Create a sprite of width x height pixels, return a pointer to the RAM area
           // Sketch can cast returned value to (uint16_t*) for 16 bit depth if needed
           // RAM required is 1 byte per pixel for 8 bit colour depth, 2 bytes for 16 bit
  void*    createSprite(int16_t width, int16_t height, uint8_t frames = 1);  

           // Delete the sprite to free up the RAM
  void     deleteSprite(void);

           // Select the frame buffer for graphics
  void*    frameBuffer(int8_t f);
  
           // Set the colour depth to 8 or 16 bits. Can be used to change depth an existing
           // sprite, but clears it to black, returns a new pointer if sprite is re-created.
  void*    setColorDepth(int8_t b);

  void     setBitmapColor(uint16_t c, uint16_t b);

  void     fillSprite(uint32_t color);

           // Define a window to push 16 bit colour pixels into is a raster order
           // Colours are converted to 8 bit if depth is set to 8
  virtual void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1),
           pushColor(uint16_t color),
           pushColor(uint16_t color, int32_t len),
           pushTransparent(int32_t len),
           pushTransparent(uint16_t color, uint8_t alpha, int32_t len),
           // Push a pixel preformatted as a 8 or 16 bit colour (avoids conversion overhead)
		   drawChar(int16_t x, int16_t y, unsigned char c, uint32_t color, uint32_t bg, uint8_t size),
		   drawPixel(int16_t x, int16_t y, uint32_t color),
           drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color),
           drawFastVLine(int16_t x, int16_t y, int16_t h, uint32_t color),
           drawFastHLine(int16_t x, int16_t y, int16_t w, uint32_t color),
           fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);

  void     writeColor(uint16_t color),

           // Set the scroll zone, top left corner at x,y with defined width and height
           // The colour (optional, black is default) is used to fill the gap after the scroll
           setScrollRect(int32_t x, int32_t y, uint16_t w, uint16_t h, uint16_t color = TFT_BLACK),
           // Scroll the defined zone dx,dy pixels. Negative values left,up, positive right,down
           // dy is optional (default is then no up/down scroll).
           // The sprite coordinate frame does not move because pixels are moved
           scroll(int16_t dx, int16_t dy = 0);

           // Set the sprite text cursor position for print class (does not change the TFT screen cursor)
           //setCursor(int16_t x, int16_t y);

  void     setRotation(uint8_t rotation);
  uint8_t  getRotation(void);

           // Read the colour of a pixel at x,y and return value in 565 format 
  uint16_t readPixel(int16_t x0, int16_t y0);

           // Write an image (colour bitmap) to the sprite
  void     pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, uint16_t *data);
  void     pushImage(int16_t x0, int16_t y0, uint16_t w, uint16_t h, const uint16_t *data);

           // Swap the byte order for pushImage() - corrects different image endianness
  void     setSwapBytes(bool swap);
  bool     getSwapBytes(void);

           // Push the sprite to the TFT screen, this fn calls pushImage() in the TFT class.
           // Optionally a "transparent" colour can be defined, pixels of that colour will not be rendered
  void     pushSprite(int16_t x, int16_t y);
  void     pushSprite(int16_t x, int16_t y, uint16_t transparent);
  void     pushSpritePart(int16_t x, int16_t y, int16_t height);

  virtual int16_t drawChar(unsigned int uniCode, int x, int y, int font),
                  drawChar(unsigned int uniCode, int x, int y);

           // Return the width and height of the sprite
  virtual int16_t width(void),
                  height(void);

           // Used by print class to print text to cursor position
  size_t   write(uint8_t);

           // Functions associated with anti-aliased fonts
  void     drawGlyph(uint16_t code);
  void     printToSprite(String string);
  void     printToSprite(const char *cbuffer, int len);
  int16_t  printToSprite(int16_t x, int16_t y, uint16_t index);

  void     cloneDataInto(TFT_eSprite *img);		// copy sprite data into a "sister" sprite
  void     cloneDataFrom(uint8_t* buff);		// copy sprite data from buff

  virtual bool isSprite(void) { return true; };
  bool     isCreated(void) { return _created; };

  void     mirror();        // mirror horizontally

 private:

  TFT_eSPI *_tft;

 protected:

  uint8_t  _bpp;     // Bits per pixel. 16-, 8-, 3- or 1-bit pixels supported.
  uint16_t *_img;    // pointer to 16 bit sprite
  uint8_t  *_img8;   // pointer to  8 bit sprite
  uint8_t  *_img8_1; // pointer to  frame 1
  uint8_t  *_img8_2; // pointer to  frame 2

  bool     _created; // created and bits per pixel depth flags
  bool     _gFont = false; 

//  int32_t  _icursor_x, _icursor_y;
  uint8_t  _rotation = 0;
  int32_t  _xs, _ys, _xe, _ye, _xptr, _yptr; // for setWindow
  int32_t  _sx, _sy; // x,y for scroll zone
  uint32_t _sw, _sh; // w,h for scroll zone
  uint32_t _scolor;  // gap fill colour for scroll zone

  boolean  _iswapBytes; // Swap the byte order for Sprite pushImage()

  int16_t  _iwidth, _iheight; // Sprite memory image bit width and height (swapped during rotations)
  int16_t  _dwidth, _dheight; // Real display width and height (for <8bpp Sprites)
  uint16_t _awidth;			  // byte aligned pixel width  (for <8bpp Sprites, not swapped)
  uint16_t _bytewidth;        // Sprite image bit width for drawPixel (for <8bpp Sprites, not swapped)
  uint8_t  _frames;

  void* esp32Calloc(size_t nmemb, size_t size);			// run calloc, if fails - try to allocate in external RAM
  inline __attribute__((always_inline)) void rotateXY(int16_t &x, int16_t &y);

};

// Helper functions
namespace display {
  uint32_t runDecodeNumber(const unsigned char** p);
  uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc);

#ifdef ESP32

  #define JPG_BUFF_SIZE	4096					// Size of the working buffer (must be power of 2)

  extern const unsigned char* jpgImg;			// 
  extern unsigned char* jpgDecodeBuff;			// Pointer to the working buffer (must be 4-byte aligned)
  extern int jpgPos;
  extern int jpgSize;
  extern TFT_eSPI* jpgReceiver;

  /* Call-back function for tjpgd to input JPEG data */
  static UINT tjd_input (
    JDEC* jd,       /* Decompression object */
	BYTE* buff,     /* Pointer to the read buffer (NULL:skip) */
	UINT nd         /* Number of bytes to read/skip from input stream */
  );
  /* Call-back function for tjpgd to output RGB bitmap */
  static UINT tjd_output (
      JDEC* jd,       /* Decompression object of current session */
      void* bitmap,   /* Bitmap data to be output */
      JRECT* rect     /* Rectangular region to output */
  );
  /* Load JPG from memory buffer into a sprite or screen */
  int load_jpg (
      const unsigned char *img,     /* Pointer to the working buffer (must be 4-byte aligned) */
      UINT imgSize,   /* Size of the working buffer (must be power of 2) */
	  TFT_eSPI* screen
  );
#endif
};

#endif // _TFT_eSPIH_
