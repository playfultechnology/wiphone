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

#ifndef _NANOINI_H
#define _NANOINI_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory>

#include "config.h"
#include "helpers.h"
#include "LinearArray.h"

/*
 * Description:
 *
 *     A little intuitive embedded-friendly class to deserialize, access, query, modify, serialize
 *     and create data in a minimalist INI format. It uses C-strings internally.
 *     (It can also be though of as a simple in-memory database, that can be stored/retrieved
 *     to/from INI files.)
 *
 *     This class is NOT thread-safe (particularly, due to "provisional" key-values).
 *
 *     The INI format consists of these simple rules:
 *       - lines starting with an opening square braket are starting a new section:
 *         - section title is inside square brackets (or anything after the opening square braket)
 *       - all other lines are key-values:
 *         - value starts immediately after the first equal sign till '\n' character
 *         - the key is everything that precedes first equal sign
 *         - during parsing: if there is no equal sign, the key is considered empty
 *         - during saving: equal sign is always added, even if the key is empty
 *
 *     Because of simplicity of this format, any file can be parsed "successfully".
 *
 * Developer notes:
 *   - THERE IS NO MULTILINE VALUES SUPPORT! Beware that adding a new or modifying a key-value
 *     will replace all '\n' characters with '\r' characters; BE CAUTIOUS
 *     - don't forget to restore '\r' into '\n' (if your data allows it)
 *     - another workaround is to store multiple values with an empty key inside a section
 *   - AVOID USING INTEGER NUMBERS AS SECTION NAMES!
 *     - on parsing: integer section names are removed if they are the same as section position
 *     - on serializing: empty section names are saved as numeric values (section positions)
 *   - use hasKey to check if key exists before accessing it, for example:
 *       ini["section1"].hasKey("key1")
 *     Reading non-existing key might cause crash. Alternatively, you can use getValueSafe() and
 *       getIntValueSafe() methods.
 *
 * ESP32 RAM storage:
 *     apart from pointers, internally this class will try to store everything into the external
 *     RAM (PSRAM) if possible, allowing to parse and serialize even relatively large INI files.
 *     This is done by using LinearArray<..., LA_EXTERNAL_RAM> objects, extStrdup and extStrndup function.
 */

namespace NanoIni {

class KeyValue {
public:
  KeyValue();
  KeyValue(const char* key, const char* value);
  KeyValue(const char* line, size_t lineLength);
  ~KeyValue();

  // Access
  size_t length() const;
  const char* key() const;
  const char* value() const;
  operator const char*() const;
  operator bool() const;
  size_t sprint(char* dest);

  // Modification
  const char* operator=(const char* newValue);
  const char* operator=(int32_t val);
  const char* operator=(float val);
  const char* operator=(KeyValue& other);

protected:
  char* keyDyn;     // can be NULL
  char* valueDyn;   // by convention, should not be NULL; if it is - it is a provisional key-value and is meant to be deleted

  void _escape(char* p);
};

class Section {
public:
  Section();
  Section(const char* title);
  Section(const char* ss, size_t sectionLength, int pos);
  Section(Section& other);    // deep copy
  ~Section();

  // Access
  size_t nValues();
  const char* title();
  bool hasKey(const char* key);
  size_t length();    // complexity: O(n)
  size_t sprint(char* dest, int sectionNum, bool numericTitle, bool noTitle);
  void show();

  int getNumericValueSafe(const char* key, int def, uint8_t base);
  int getIntValueSafe(const char* key, int def);
  int getHexValueSafe(const char* key, int def);
  float getFloatValueSafe(const char* key, float def);
  const char* getValueSafe(const char* key, const char* def = nullptr);
  std::string getValueBase64(const char* key, const char* def = nullptr);

  // Access & modification
  KeyValue& operator[](int index);
  KeyValue& operator[](const char* key);

  // Modification
  KeyValue& addKeyValue(KeyValue* keyVal);
  KeyValue& addKeyValue(const char* key, const char* value);
  KeyValue& putValueFullHex(const char* key, unsigned int val);       // encode integer as 8-character hex integer
  KeyValue& putValueBase64(const char* key, const char* text);        // encode text with base64 encoding
  bool remove(const char* key);
  void setTitle(const char* title);
  void deepCopy(Section& other);

protected:
  char* titleDyn;                         // can be NULL
  KeyValue* provisional;                  // KeyValue with NULL value that should be cleaned up on each access; used to allow Python-style declarations
  LinearArray<KeyValue*, LA_EXTERNAL_RAM> keyValues;

  void _cleanUp();
  KeyValue* _find(const char* key);
};

/* MAIN CLASS: INI parser, interface, serializer */

class Config {
public:
  Config();
  Config(const char* s);
  ~Config();

  // This allows iterating over sections like:
  //     for (auto it = ini.iterator(1); it.valid(); ++it) { processSection(*it); }
  // See this for relation to std::iterator<std::forward_iterator_tag>:
  //     https://stackoverflow.com/a/39767072/5407270

  class SectionsIterator {   // : public std::iterator<std::forward_iterator_tag, T, int, T*, T&>

    typedef SectionsIterator iterator;

  public:
    SectionsIterator(Config& ini) : ini_(ini), pos_(0) {}
    SectionsIterator(Config& ini, int i) : ini_(ini), pos_(i) {}
    ~SectionsIterator() {}

    iterator  operator++(int) { /* postfix */
      int p = pos_++;
      return SectionsIterator(ini_, p);
    }
    iterator& operator++() {  /* prefix */
      ++pos_;
      return *this;
    }
    Section&  operator* () const                    {
      return ini_[pos_];
    }
    Section*  operator->() const                    {
      return &ini_[pos_];
    }
    iterator  operator+ (int v)   const             {
      return SectionsIterator(ini_, pos_ + v);
    }
    operator int() const                  {
      return pos_;
    }
    bool      valid() const                         {
      return pos_ < ini_.nSections();
    }

  protected:
    int pos_;
    Config& ini_;

  };

  size_t nSections() const;
  bool isEmpty() const;
  SectionsIterator iterator() {
    return SectionsIterator(*this, 0);
  }
  SectionsIterator iterator(int startAt) {
    return SectionsIterator(*this, startAt);
  }

  /* Description:
   *     delete all sections (from RAM) and reset the state to empty
   */
  void clear();
  void parse(const char* s);
  void addSection(Section* section);
  void addSection(const char* title);
  size_t addSection();
  void reorderLast(int startAt, int (*cmp)(Section**, Section**));
  void sortFrom(int startAt, int (*cmp)(Section**, Section**));

  /* Description:
   *     remove section by its ordinal position, shifting following sections to the left
   */
  bool removeSection(int i);

  /* Description:
   *     return pointer to the first section with this `title`
   */
  Section* _findSection(const char* title);
  bool hasSection(const char* title);
  Section& operator[](int index);               // allows negative indexes
  Section& operator[](const char* title);

  // Relatively high-level and database-like interfaces

  /* Description:
   *     find section which has `key` with value `value`
   * Return:
   *     Integer index of the first section found; -1 otherwise
   */
  int query(const char* key, const char* value);
  int query(const char* key1, const char* value1, const char* key2, const char* value2);
  int query(const char* key, int32_t value);

  /* Description:
   *     find section which has field with key `key`.
   *     This is intended to be used for finding sections with a unique flag. See setUniqueFlag().
   * Return:
   *     Integer index of the first section found; -1 otherwise
   */
  int findKey(const char* key);

  /* Description:
   *     removes fields with key `key` from all sections
   */
  void removeAllKeys(const char* key);
  void clearUniqueFlag(const char* key) {
    this->removeAllKeys(key);
  }

  /* Description:
   *     removes a `key` from all sections and sets it exactly in one section specified by `index` with value "1"
   * Return:
   *     true if successful
   */
  bool setUniqueFlag(int section, const char* key);

  /* Description:
   *     removes a `key` from all sections and sets it exactly in one section specified by `sectionTitle` with value "1"
   * Return:
   *     true if successful
   */
  bool setUniqueFlag(const char* sectionTitle, const char* key);

  /* Desctiption:
   *    returns the length of the serialized string.
   */
  size_t length();

  /* Description:
   *     serialize data into a C-string buffer (see p_c_str() for example usage or use that one instead).
   */
  size_t sprint(char* str);

  /* Description:
   *     return serialized C-string of exactly right size.
   * Example usage:
   *     printf("%s", ini.p_c_str().get());
   */
  std::unique_ptr<char[]> p_c_str();

protected:
  LinearArray<Section*, LA_EXTERNAL_RAM> sections;
};

bool isSafeString(const char* str);

};   // namespace NanoINI

#endif // _NANOINI_H
