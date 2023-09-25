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

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.
//
// Original source:
//     https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.h

#ifndef _MURMURHASH3_H_
#define _MURMURHASH3_H_

#include <stdint.h>

uint32_t MurmurHash3_32  ( const void * key, int len, uint32_t seed = 5381 );

#endif // _MURMURHASH3_H_
