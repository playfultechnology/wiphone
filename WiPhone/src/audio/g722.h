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
 * SpanDSP - a series of DSP components for telephony
 *
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
 * $Id: g722.h,v 1.1 2012/08/07 11:33:45 sobomax Exp $
 */


/*! \file */

#if !defined(_G722_H_)
#define _G722_H_

/*! \page g722_page G.722 encoding and decoding
\section g722_page_sec_1 What does it do?
The G.722 module is a bit exact implementation of the ITU G.722 specification for all three
specified bit rates - 64000bps, 56000bps and 48000bps. It passes the ITU tests.

To allow fast and flexible interworking with narrow band telephony, the encoder and decoder
support an option for the linear audio to be an 8k samples/second stream. In this mode the
codec is considerably faster, and still fully compatible with wideband terminals using G.722.

\section g722_page_sec_2 How does it work?
???.
*/

enum {
  G722_SAMPLE_RATE_8000 = 0x0001,
  G722_PACKED = 0x0002
};


#endif
