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

#include "helpers.h"
#include <stdlib.h>
#ifdef ESP32
#include "esp_heap_caps.h"
#include "Arduino.h"
#endif

void freeNull(void** p) {
  /*
   * Similar solution and explanation of why it's better to set pointers to zero here:
   *    https://stackoverflow.com/q/1879550/5407270
   *    https://stackoverflow.com/a/1879597/5407270
   * Typical usage:
   *    freeNull((void **) &ptr);
   * NOTE: it is beneficial to parse code for correctness of calling this method from time to time
   *    grep -r "freeNull" . --include=*.cpp | grep -v "&"
   *    grep -r "freeNull" . --include=*.cpp | grep -v "**"
   */
  if(*p!=NULL) {
    free(*p);
    *p = NULL;
  }
}

unsigned long elapsed(unsigned long now, unsigned long last) {
  if (now >= last) {
    return now - last;
  }
  return 0xffffffffu - last + now;
}

bool elapsedMillis(unsigned long now, unsigned long last, unsigned long period) {
  if (now >= last + period) {
    return true;
  }
  if (last > 1440000000 && now < last - 1440000000) {    // events are more than 400 hours apart (16.7 days)
    // time overflow occured
    if (0xffffffffu - last + now >= period) {
      return true;
    }
  }
  return false;
}

/* Description:
 *     return assumed time difference between two close events
 * Return:
 *     msTime1 - msTime2 assuming that events are close to each other in time and allowing overlows.
 *     If result is negative, msTime1 happened before msTime2.
 */
long timeDiff(unsigned long msTime1, unsigned long msTime2) {
  unsigned long diff1, diff2;
  if (msTime1 >= msTime2) {
    diff1 = 0xffffffffu - msTime1 + msTime2;      // positive difference if msTime2 overflowed
    diff2 = msTime1 - msTime2;                    // positive difference if msTime1 is genuinely ahead of msTime2
    return (diff1 > diff2) ? diff2 : -diff1;
  } else {
    diff1 = 0xffffffffu - msTime2 + msTime1;      // positive difference if msTime1 overflowed
    diff2 = msTime2 - msTime1;                    // positive difference if msTime2 is genuinely ahead of msTime1
    return (diff1 > diff2) ? -diff2 : diff1;
  }
}

uint32_t hash_murmur(const char *str) {
  // TODO: counting strlen makes it 2-pass hashing; probably, could be optimized?
  // .     (practically, strlen doesn't seem to add much overhead)
  return MurmurHash3_32(str, strlen((const char *) str), 5381);
}

void md5Compress(const char* str, size_t len, HASHHEX resp) {
  if (len>HASHHEXLEN) {
    struct MD5Context Md5Ctx;
    HASH hash;

    MD5Init(&Md5Ctx);
    MD5Update(&Md5Ctx, (const unsigned char*) str, len);
    MD5Final((unsigned char*) hash, &Md5Ctx);
    CvtHex(hash, resp);
  } else if (resp!=str) {
    strncpy(resp, str, len);
    resp[len] = '\0';
  }
};

uint8_t conv100to255(int x) {
  if (x <= 0) {
    return 0;
  }
  if (x >= 100) {
    return 255;
  }
  return (uint8_t)(x*2.55);
}

// Helper function (ROL5) - typically performed in one CPU operation
uint32_t rotate5(uint32_t x) {
  return (x << 5) | (x >> 27);
}

/* Description:
 *     Python's .rstrip() method mimiced
 */
char *strrstrip(char *str) {
  size_t len = strlen(str);
  if (!len) {
    return str;
  }
  char *e = str + len - 1;
  while (isspace(*e))
    if (--e<str) {
      break;
    }
  *(e + 1) = '\0';
  return str;
}

// Run malloc, if fails - try to allocate in external RAM (PSRAM)
void* intMalloc(size_t size) {
  void* p = NULL;
#ifdef ESP32
  if (p==NULL) {
    log_v("not allocated, trying external memory");
    p = heap_caps_malloc(size, ((size<4) ? MALLOC_CAP_8BIT : MALLOC_CAP_32BIT) | MALLOC_CAP_SPIRAM);
  }
#else
  p = malloc(size);
#endif // ESP32
  return p;
}

// Run calloc, if fails - try to allocate in external RAM (PSRAM)
void* intCalloc(size_t n, size_t size) {
  void* p = NULL;
#ifdef ESP32
  if (p==NULL) {
    log_v("not allocated, trying external memory");
    p = heap_caps_calloc(n, size, ((size==1 && n<4) ? MALLOC_CAP_8BIT : MALLOC_CAP_32BIT) | MALLOC_CAP_SPIRAM);
  }
#else
  p = calloc(n, size);
#endif // ESP32
  return p;
}

// Run realloc, if fails - try to allocate in external RAM (PSRAM)
void* intRealloc(void *ptr, size_t size) {
  void* p = NULL;
#ifdef ESP32
  if (p==NULL) {
    log_v("not allocated, trying external memory");
    p = heap_caps_realloc(ptr, size, ((size<4) ? MALLOC_CAP_8BIT : MALLOC_CAP_32BIT) | MALLOC_CAP_SPIRAM);
  }
#else
  log_v("allocated using internal memory");
  p = realloc(ptr, size);
#endif // ESP32
  return p;
}

char* intStrdup(const char* str) {
  char* dup = nullptr;
#ifdef ESP32
  if (dup == nullptr) {
    log_v("not allocated, trying external memory");
    size_t len = strlen(str);
    dup = (char*) heap_caps_malloc(len+1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (dup != nullptr) {
      memcpy(dup, str, len+1);
    }
  }
#else
  dup = strdup(str);
#endif
  return dup;
}

char* intStrndup(const char* str, size_t n) {
  char* dup = nullptr;
#ifdef ESP32
  if (dup == nullptr) {
    log_v("not allocated, trying external memory");
    size_t len = strnlen(str, n);
    dup = (char*) heap_caps_malloc(len+1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (dup != nullptr) {
      memcpy(dup, str, len);
      dup[len] = '\0';
    }
  }
#else
  dup = strndup(str, n);
#endif
  return dup;
}

void* extMalloc(size_t size) {
  void* p;
#ifdef ESP32
  p = heap_caps_malloc(size, ((size<4) ? MALLOC_CAP_8BIT : MALLOC_CAP_32BIT) | MALLOC_CAP_SPIRAM);
  if (p==NULL)
#endif // ESP32
  {
    log_v("not allocated, trying internal memory");
    p = malloc(size);         // try to allocate memory in internal RAM
  }
  return p;
}

void* extCalloc(size_t n, size_t size) {
  void* p;
#ifdef ESP32
  p = heap_caps_calloc(n, size, ((size==1 && n<4) ? MALLOC_CAP_8BIT : MALLOC_CAP_32BIT) | MALLOC_CAP_SPIRAM);
  if (p==NULL)
#endif // ESP32
  {
    log_v("not allocated, trying internal memory");
    p = calloc(n, size);         // try to allocate memory in internal RAM
  }
  return p;
}

void* extRealloc(void *ptr, size_t size) {
  void* p;
#ifdef ESP32
  p = heap_caps_realloc(ptr, size, ((size<4) ? MALLOC_CAP_8BIT : MALLOC_CAP_32BIT) | MALLOC_CAP_SPIRAM);
  if (p==NULL)
#endif // ESP32
  {
    log_v("not allocated, trying internal memory");
    p = realloc(ptr, size);      // try to allocate memory in internal RAM
  }
  return p;
}

char* extStrdup(const char* str) {
  char* dup;
#ifdef ESP32
  size_t len = strlen(str);
  dup = (char*) heap_caps_malloc(len+1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
  if (dup != nullptr) {
    memcpy(dup, str, len+1);
  } else
#endif // ESP32
  {
    log_v("not allocated, trying internal memory");
    dup = strdup(str);
  }
  return dup;
}

char* extStrndup(const char* str, size_t n) {
  char* dup;
#ifdef ESP32
  size_t len = strnlen(str, n);
  dup = (char*) heap_caps_malloc(len+1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
  if (dup != nullptr) {
    memcpy(dup, str, len);
    dup[len] = '\0';
  } else
#endif // ESP32
  {
    log_v("not allocated, trying internal memory");
    dup = strndup(str, n);
  }
  return dup;
}

// Internal RAM preferred

template <>
void* wMalloc<false>(size_t size) {
  return intMalloc(size);
}

template <>
void* wCalloc<false>(size_t n, size_t size) {
  return intCalloc(n, size);
}

template<>
void* wRealloc<false>(void *ptr, size_t size) {
  return intRealloc(ptr, size);
}

template<>
char* wStrdup<false>(const char* s) {
  return intStrdup(s);
}

// External PSRAM preferred

template <>
void* wMalloc<true>(size_t size) {
  return extMalloc(size);
}

template <>
void* wCalloc<true>(size_t n, size_t size) {
  return extCalloc(n, size);
}

template<>
void* wRealloc<true>(void *ptr, size_t size) {
  return extRealloc(ptr, size);
}

template<>
char* wStrdup<true>(const char* s) {
  return extStrdup(s);
}

// Collect random bits
void RandomNumberGenerator::feed(uint32_t x) {
  this->mcg_state = (rotate5(this->mcg_state >> 32) ^ x) | (this->mcg_state << 32);
  // Ensure the seed is odd: set first bit, but clear one higher bit to ensure that the number is not degreding into 2^64-1
  if (!(this->mcg_state & 1)) {
    uint32_t b = 0x80000000;
    while (b && !(b & this->mcg_state)) {
      b >>= 1;
    }
    if (b) {
      this->mcg_state ^= b;
    }
    b |= 1;
  }
}

uint32_t RandomNumberGenerator::random(void) {
  // pcg32_fast algorithm
  uint64_t x = this->mcg_state;
  unsigned count = (unsigned)(x >> 61); // 61 = 64 - 3
  this->mcg_state = x * RandomNumberGenerator::multiplier;
  x ^= x >> 22;
  return (uint32_t)(x >> (22 + count)); // 22 = 32 - 3 - 7
}

// Generate random characters
void RandomNumberGenerator::randChars(char *dest, size_t len) {
  const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  uint32_t s=0;
  while (len-- > 0) {
    if (s<=sizeof(charset)) {
      s = this->random();
    }
    size_t i = s % (sizeof(charset)-1);
    s /= (sizeof(charset)-1);
    *dest++ = charset[i];
  }
  *dest = '\0';
}

//std::random_device randDevice;        // doesn't work
RandomNumberGenerator Random;
