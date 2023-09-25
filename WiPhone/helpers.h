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

#ifndef __HELPERS_H
#define __HELPERS_H

#include <random>
#include <cstddef>
#include "src/digcalc.h"
#include "src/MurmurHash3_32.h"

uint32_t rotate5(uint32_t x);                                   // rotate by a prime number of bits
uint32_t hash_murmur(const char *str);                          // simple hash function for C-strings. Uses MurmurHash by Austin Appleby. Only 35% slower than DJB2 on ESP32, 27% slower than SDBM.
void md5Compress(const char* str, size_t len, HASHHEX hash);    // convert string into up to 32-character string (either as hexadecimal hash representation or string itself)
uint8_t conv100to255(int x);

// Timer and time difference helpers

unsigned long elapsed(unsigned long now, unsigned long last);
bool elapsedMillis(unsigned long now, unsigned long last, unsigned long period);        // have a `period` of milliseconds passed since `last` millis? (handles overflow)
long timeDiff(unsigned long msTime1, unsigned long msTime2);                            // return assumed time difference between two close events

// Dynamic memory functions

void freeNull(void** p);                      // free pointer and set it to NULL after freeing
char* strrstrip(char* str);                   // Python's .rstrip()

void* intMalloc(size_t size);                 // allocate memory in internal RAM, if failed - in external PSRAM
void* intCalloc(size_t n, size_t size);       // allocate & zero memory in internal RAM, if failed - in external PSRAM
void* intRealloc(void *ptr, size_t size);     // reallocate memory in internal RAM, if failed - in external PSRAM
char* intStrdup(const char* str);
char* intStrndup(const char* str, size_t n);

void* extMalloc(size_t size);                 // allocate memory in external PSRAM, if failed - in internal RAM
void* extCalloc(size_t n, size_t size);       // allocate & zero memory in external PSRAM, if failed - in internal RAM
void* extRealloc(void *ptr, size_t size);     // reallocate memory in external PSRAM, if failed - in internal RAM
char* extStrdup(const char* str);
char* extStrndup(const char* str, size_t n);

template <bool B>
void* wMalloc(size_t size);                   // allocate memory, if parameter is true - identical to extMalloc, false - identical to intMalloc
template <bool B>
void* wCalloc(size_t n, size_t size);         // allocate & zero memory, if parameter is true - identical to extCalloc, false - identical to intCalloc
template <bool B>
void* wRealloc(void *ptr, size_t size);       // reallocate memory, if parameter is true - identical to extRealloc, false - identical to intRealloc
template <bool B>
char* wStrdup(const char* str);

/* Description:
 *     Since SIP protocol relies on random tags, we need a good and fast random number generator.
 *     This structure implements Permuted Congruential Generator (pcg32_fast) algorithm, as well as collects random bits from hardware events for the seed.
 *     In our tests this implementation sometimes significantly outperforms the standard Mersenne Twister (std::19937) on ESP32, in other cases - performs slighly worse.
 *       This is because MT performance is unstable. Not clear why.
 *     This implementation generates 10M numbers in around 2645 ms, while std::mt19937 needs between 2441 and 3398 ms depending on the exact use case.
 */
class RandomNumberGenerator {
public:
  void feed(uint32_t x);
  uint32_t random(void);
  void randChars(char *dest, size_t len);
protected:
  static uint64_t const multiplier = 6364136223846793005u;
  uint64_t mcg_state = 0xFeedCeedCafeF00Du;                   // must be odd
};

extern RandomNumberGenerator Random;
//extern std::random_device randDevice;

#endif // __HELPERS_H
