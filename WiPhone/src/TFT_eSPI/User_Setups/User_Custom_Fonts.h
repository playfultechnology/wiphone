
// Custom "Adafruit" compatible font files can be added to the "TFT_eSPI/Fonts/Custom" folder
// Fonts in a suitable format can be created using a Squix blog web based tool here:
/*
   https://blog.squix.org/2016/10/font-creator-now-creates-adafruit-gfx-fonts.html
*/

// Note: At the time of writing there is a last character code definition bug in the
// Squix font file format so do NOT try and print the tilda (~) symbol (ASCII 0x7E)
// Alternatively look at the end of the font header file and edit:  0x7E to read 0x7D
/* e.g.                                                                          vvvv
  (uint8_t  *)Orbitron_Light_32Bitmaps,(GFXglyph *)Orbitron_Light_32Glyphs,0x20, 0x7D, 32};
                                                                                 ^^^^
*/

// When font files are placed in the Custom folder then they must also be #included here:

// The comment added is a shorthand reference but this is not essential

#ifdef LOAD_GFXFF

  // New custom font file #includes
  #include "../Fonts/Custom/Orbitron_Light_24.h" // CF_OL24
  #include "../Fonts/Custom/Orbitron_Light_32.h" // CF_OL32
  #include "../Fonts/Custom/Roboto_Thin_24.h"    // CF_RT24
  #include "../Fonts/Custom/Satisfy_24.h"        // CF_S24
  #include "../Fonts/Custom/Yellowtail_32.h"     // CF_Y32

  #include "../Fonts/Custom/OpenSansCondensed_16.h"     // OS_C16
  #include "../Fonts/Custom/OpenSansCondensed_19.h"     // OS_C19
  #include "../Fonts/Custom/OpenSansCondensed_20.h"     // OS_C20

  #include "../Fonts/Custom/RobotoMonoLight_16.h"		// RO_M16
  #include "../Fonts/Custom/RobotoMonoLight_20.h"		// RO_M20

  #include "../Fonts/Custom/Nimbus_Mono_L_16.h"			// NL_M16

#endif

// Shorthand references - any coding scheme can be used, here CF_ = Custom Font
// The #defines below MUST be added to sketches to use shorthand references, so
// they are only put here for reference and copy+paste purposes!
/*
#define CF_OL24 &Orbitron_Light_24
#define CF_OL32 &Orbitron_Light_32
#define CF_RT24 &Roboto_Thin_24
#define CF_S24  &Satisfy_24
#define CF_Y32  &Yellowtail_32
#define NL_M16  &Nimbus_Mono_L_Regular_16

#define OS_C16   &Open_Sans_Condensed_Bold_16
#define OS_C19   &Open_Sans_Condensed_Bold_19
*/
