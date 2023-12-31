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
 * g722.h - The ITU G.722 codec.
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2005 Steve Underwood
 *
 *  Despite my general liking of the GPL, I place my own contributions
 *  to this code in the public domain for the benefit of all mankind -
 *  even the slimy ones who might try to proprietize my work and use it
 *  to my detriment.
 *
 * Based on a single channel G.722 codec which is:
 *
 *****    Copyright (c) CMU    1993      *****
 * Computer Science, Speech Group
 * Chengxiang Lu and Alex Hauptmann
 *
 * $Id: g722_decoder.h,v 1.1 2012/08/07 11:33:45 sobomax Exp $
 */


/*! \file */

#if !defined(_G722_DECODER_H_)
#define _G722_DECODER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "g722_private.h"
#include "g722.h"

//#ifndef _G722_DEC_CTX_DEFINED
//typedef void G722_DEC_CTX;
//#define _G722_DEC_CTX_DEFINED
//#endif

G722_DEC_CTX *g722_decoder_new(int rate, int options);
int g722_decoder_destroy(G722_DEC_CTX *s);
int g722_decode(G722_DEC_CTX *s, const uint8_t g722_data[], int len, int16_t amp[]);    // returns number of 16-bit words in decoded buffer (320 for 20ms)


#ifdef __cplusplus
}
#endif

#endif // _G722_DECODER_H_
