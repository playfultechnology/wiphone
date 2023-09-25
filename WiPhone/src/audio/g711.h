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

/*
  Source: https://github.com/openitu/STL/tree/dev/src/g711

  ============================================================================
   File: G711.H
  ============================================================================

                            UGST/ITU-T G711 MODULE

                          GLOBAL FUNCTION PROTOTYPES

   History:
   10.Dec.91  v1.0  First version <hf@pkinbg.uucp>
   08.Feb.92  v1.1  Non-ANSI prototypes added <tdsimao@venus.cpqd.ansp.br>
   11.Jan.96    v1.2    Fixed misleading prototype parameter names in
                        alaw_expand() and ulaw_compress(); changed to
      smart prototypes <simao@ctd.comsat.com>,
      and <Volker.Springer@eedn.ericsson.se>
   31.Jan.2000  v3.01   [version no.aligned with g711.c] Updated list of
                        compilers for smart prototypes
  ============================================================================

   24.Apr.2019          Modified types
*/
#ifndef G711_defined
#define G711_defined 301

#ifdef __cplusplus
extern "C" {
#endif

/* Smart function prototypes: for [ag]cc, VaxC, and [tb]cc */
#if !defined(ARGS)
#if (defined(__STDC__) || defined(VMS) || defined(__DECC)  || defined(MSDOS) || defined(__MSDOS__)) || defined (__CYGWIN__) || defined (_MSC_VER)
#define ARGS(s) s
#else
#define ARGS(s) ()
#endif
#endif


/* Function prototypes */
void alaw_compress ARGS ((long lseg, const short *linbuf, unsigned char *logbuf));
void alaw_expand ARGS ((long lseg, const unsigned char *logbuf, short *linbuf));
void ulaw_compress ARGS ((long lseg, const short *linbuf, unsigned char *logbuf));
void ulaw_expand ARGS ((long lseg, const unsigned char *logbuf, short *linbuf));

/* Definitions for better user interface (?!) */
#define IS_LIN 1
#define IS_LOG 0

#ifdef __cplusplus
}
#endif

#endif
/* .......................... End of G711.H ........................... */
