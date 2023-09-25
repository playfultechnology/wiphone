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

#include "NanoINI.h"
extern "C" {
#include "libb64/cdecode.h"
#include "libb64/cencode.h"
}

namespace NanoIni {


// ------------------------------------------------------ KeyValue class ------------------------------------------------------


KeyValue::KeyValue()
  : keyDyn(nullptr), valueDyn(nullptr)
{}

KeyValue::KeyValue(const char* key, const char* value) : KeyValue() {
  if (key!=nullptr) {
    keyDyn = extStrdup(key);
  }
  _escape(keyDyn);
  if (value!=nullptr) {
    valueDyn = extStrdup(value);
    _escape(valueDyn);
  }
}

KeyValue::KeyValue(const char* line, size_t lineLength) : KeyValue() {

  const char* firstEqual = (const char *)memchr(line, '=', lineLength);

  // Parse key
  if (firstEqual != nullptr) {
    // Key found
    if (firstEqual - line > 0) {
      keyDyn = extStrndup(line, firstEqual - line);
    }

    // Leave only value in the line
    lineLength -= firstEqual + 1 - line;
    line = firstEqual + 1;
  }

  // Parse value
  const char* newLine = (const char *) memchr(line, '\n', lineLength);
  valueDyn = extStrndup(line, newLine != nullptr ? newLine - line : lineLength);

  log_v("Key: %s / Value: %s", keyDyn != nullptr ? keyDyn : "NULL", valueDyn);
};

KeyValue::~KeyValue() {
  freeNull((void **) &keyDyn);
  freeNull((void **) &valueDyn);
}

const char* KeyValue::key() const {
  return keyDyn;
}

const char* KeyValue::value() const {
  return valueDyn != nullptr ? valueDyn : "";
}

KeyValue::operator const char*() const {
  return this->value();
}

KeyValue::operator bool() const {
  return valueDyn != nullptr;
}

const char* KeyValue::operator=(const char* newValue) {
  freeNull((void **) &valueDyn);

  if (newValue != nullptr) {
    valueDyn = extStrdup(newValue);
    _escape(valueDyn);
  }

  return valueDyn;
}

const char* KeyValue::operator=(int32_t val) {
  char buff[12];
  sprintf(buff, "%d", val);
  return this->operator=(buff);
}

const char* KeyValue::operator=(float val) {
  char buff[20];
  sprintf(buff, "%g", val);
  return this->operator=(buff);
}

// NOTE: if this operation is removed, ini[0]["t"] = other[0]["t"] would fail silently. Why?
const char* KeyValue::operator=(KeyValue& other) {
  return this->operator=(other.value() ? other.value() : "");
}

void KeyValue::_escape(char* p) {
  // Replace '\n' with '\r' in the new value
  while ((p = strchr(p, '\n')) != NULL) {
    *p++ = '\r';
  }
}

size_t KeyValue::length() const {
  size_t len = 0;
  if (valueDyn != nullptr) {
    len += 2 + strlen(valueDyn) + (keyDyn != nullptr ? strlen(keyDyn) : 0);
  }
  return len;
}

size_t KeyValue::sprint(char* dest) {
  if (valueDyn == nullptr) {
    return 0;
  }
  size_t len = 0;
  if (keyDyn != nullptr) {
    len += sprintf(dest + len, "%s", keyDyn);
  }
  len += sprintf(dest + len, "=%s\n", valueDyn);
  return len;
}


// ------------------------------------------------------ Section class ------------------------------------------------------


Section::Section()
  : titleDyn(nullptr), provisional(nullptr), keyValues()
{}

Section::Section(const char* title) : Section() {
  titleDyn = extStrdup(title);
}

/* Description:
 *     parses section from string
 */
Section::Section(const char* ss, size_t sectionLength, int pos) : Section() {

  if (ss == nullptr || sectionLength == 0) {
    return;
  }

  const char* sectionEnd = ss + sectionLength;

  // Parse title
  if (ss[0] == '[') {
    // Find end of title
    const char* titleEnd = ss + 1 + strcspn(ss + 1, "]\n");
    if (titleEnd > sectionEnd) {
      titleEnd = sectionEnd;
    }

    // Special treatment of numeric section titles
    char* numberEnd;
    long numericSectionTitle = strtol(ss+1, &numberEnd, 10);
    if (*(ss+1)!='\0' && numberEnd==titleEnd && numericSectionTitle == pos) {
      // The title is entirely numeric and coincides with the position -> ignore this title
    } else {
      // Otherwise -> valid title, remember it
      titleDyn = extStrndup(ss + 1, titleEnd - ss - 1);
    }

    ss = titleEnd + strspn(titleEnd, "]\n");      // past section title end
  }
  log_v("New section: \"%s\"", titleDyn != nullptr ? titleDyn : "");

  while (ss < sectionEnd) {
    // Parse key-values line-by-line
    const char* nextNewLine = (const char*) memchr(ss, '\n', sectionEnd - ss);
    if (nextNewLine != nullptr) {
      this->addKeyValue(new KeyValue(ss, nextNewLine - ss));
      ss = nextNewLine + 1;         // skip the '\n' character in the end of the line
    } else {
      this->addKeyValue(new KeyValue(ss, sectionEnd - ss));
      ss = sectionEnd;
    }

  }

}

Section::Section(Section& other) : Section() {
  this->deepCopy(other);
}

Section::~Section() {
  for (int i=0; i<keyValues.size(); i++) {
    delete keyValues[i];
  }
  freeNull((void **) &titleDyn);
}

KeyValue& Section::addKeyValue(KeyValue* keyVal) {
  if (!keyVal->value()) {
    _cleanUp();  // avoid adding second provisional keyValue
  }
  keyValues.add(keyVal);
  return *keyVal;
}

KeyValue& Section::addKeyValue(const char* key, const char* value) {
  return this->addKeyValue(new KeyValue(key, value));
}

KeyValue& Section::putValueFullHex(const char* key, unsigned int val) {
  char buff[10];
  sprintf(buff, "%08x", val);
  (*this)[key] = buff;
  return (*this)[key];
}

void Section::show() {
  log_v("show");
  int len = this->length();
  char buff[len+3];
  this->sprint(buff, 0, true, true);
  log_d("%s\n", buff);
}

KeyValue& Section::putValueBase64(const char* key, const char* text) {
  size_t len = strlen(text);
  size_t len64 = base64_encode_expected_len(len) + 1;
  char* buff = (char*) malloc(len64);
  if (buff) {
    base64_encodestate state;
    base64_init_encodestate(&state);
    int enclen = base64_encode_block(text, len, buff, &state);
    enclen = base64_encode_blockend((buff + enclen), &state);
    (*this)[key] = enclen ? buff : "";
    free(buff);
    return (*this)[key];
  }
  log_e("couldn't add Base64-encoded value");
  return this->addKeyValue(key, nullptr);      // add provisional empty value just to comply with the return type
}

void Section::_cleanUp() {
  if (provisional != nullptr) {
    if (!(bool) *provisional) {
      if (keyValues.size()>0 && keyValues[keyValues.size()-1] == provisional) {
        keyValues.pop();
        log_v("PROVISIONAL: popped");
      } else {
        if (keyValues.removeByValue(provisional)) {
          log_v("PROVISIONAL: removed by value");
        } else {
          log_v("PROVISIONAL: ERROR: not found");
        }
      }
      delete provisional;
    }
    provisional = nullptr;
  };
}

/* Description:
 *     remove all key-values with this `key`
 */
bool Section::remove(const char* key) {
  _cleanUp();
  bool found = false;
  for (size_t i=0; i<keyValues.size();) {
    if ((key != nullptr && keyValues[i]->key() != nullptr && !strcmp(keyValues[i]->key(), key)) ||
        (key == nullptr && keyValues[i]->key()==key)) {
      keyValues.remove(i);
      found = true;
    } else {
      i++;
    }
  }
  return found;
}

KeyValue* Section::_find(const char* key) {
  for (size_t i=0; i<keyValues.size(); i++) {
    //if (keyValues[i]->value() == nullptr) continue;
    if ((key != nullptr && keyValues[i]->key() != nullptr && !strcmp(keyValues[i]->key(), key)) ||
        (key == nullptr && keyValues[i]->key()==key)) {
      return keyValues[i];
    }
  }
  return nullptr;
}

bool Section::hasKey(const char* key) {
  KeyValue* kv = _find(key);
  if (kv && kv->value() == nullptr) {
    // the located keyValue is (was) provisional
    _cleanUp();
    return false;
  }
  return kv != nullptr;
}

int Section::getNumericValueSafe(const char* key, int def, uint8_t base) {
  KeyValue* kv = _find(key);
  if (kv != nullptr) {
    const char* val = kv->value();
    if (val && *val!='\0') {
      char* endptr;
      int res = strtol(val, &endptr, base);     // note: can be long
      if (*endptr=='\0') {
        return res;
      }
    } else if (!val) {
      // attempt to retrieve from a provisional keyValue
      _cleanUp();
    }
  }
  return def;
}

int Section::getIntValueSafe(const char* key, int def) {
  return this->getNumericValueSafe(key, def, 10);
}

int Section::getHexValueSafe(const char* key, int def) {
  return this->getNumericValueSafe(key, def, 16);
}

float Section::getFloatValueSafe(const char* key, float def) {
  KeyValue* kv = _find(key);
  if (kv != nullptr) {
    const char* val = kv->value();
    if (val && *val!='\0') {
      char* endptr;
      float res = strtof(val, &endptr);
      if (*endptr=='\0') {
        return res;
      }
    } else if (!val) {
      // attempt to retrieve from a provisional keyValue
      _cleanUp();
    }
  }
  return def;
}

const char* Section::getValueSafe(const char* key, const char* def) {
  KeyValue* kv = _find(key);
  if (kv != nullptr) {
    if (kv->value() != nullptr) {
      return kv->value();
    } else
      // attempt to retrieve from a provisional keyValue
    {
      _cleanUp();
    }
  }
  return def;
}

/*
 * Description:
 *     decode Base64-encoded value and return a copy of the string.
 */
std::string Section::getValueBase64(const char* key, const char* def) {
  KeyValue* kv = _find(key);
  if (kv != nullptr) {
    const char* val = kv->value();
    if (val && *val!='\0') {
      auto len64 = strlen(val);
      auto len = base64_decode_expected_len(len64) + 2;
      char* buff = (char*) malloc(len);
      base64_decode_chars(val, len64, buff);
      return std::string(buff);
    } else if (!val) {
      // attempt to retrieve from a provisional keyValue
      _cleanUp();
    }
  }
  return std::string(def);
}

KeyValue& Section::operator[](int index) {
  // NOTE: wrong index is unsafe
  if (index < 0) {
    index = keyValues.size()+index;                           // Python-style
    if (index < 0) {
      index = -(index-keyValues.size()) - 1;  // otherwise: complement  -1 -> 0; -2 -> 1, ...
    }
  }
  if (index >= keyValues.size()) {
    if (index == keyValues.size() || !keyValues.size()) {
      // add provisional value
      KeyValue& kv = this->addKeyValue(nullptr, nullptr);
      provisional = &kv;
    }
    index = keyValues.size()-1;     // always choose last one
  }
  return *keyValues[index];
}

KeyValue& Section::operator[](const char* key) {
  // NOTE: wrong key is unsafe
  KeyValue* p = _find(key);
  if (p != nullptr) {
    return *p;
  }

  // UNSAFE PART: add a provisional keyValue with value == NULL;
  //              doing Section["new key"] = Section["another new key"] will probably crash
  _cleanUp();     // remove previous provisional value first
  KeyValue& kv = this->addKeyValue(key, nullptr);
  provisional = &kv;
  return kv;
}

size_t Section::nValues() {
  return keyValues.size();
};

const char* Section::title() {
  return titleDyn!=nullptr ? titleDyn : "";
};

void Section::setTitle(const char* title) {
  freeNull((void **) &titleDyn);
  titleDyn = extStrdup(title);
}

void Section::deepCopy(Section& other) {
  // Reset all dynamic variables
  freeNull((void **) &titleDyn);
  _cleanUp();
  for (int i=0; i<keyValues.size(); i++) {
    delete keyValues[i];
  }
  keyValues.clear();

  // Copy title
  if (other.title()) {
    titleDyn = strdup(other.title());
  }

  // Copy all keyValues
  this->keyValues.ensure(other.nValues());
  for (int i = 0; i < other.nValues(); i++) {
    this->keyValues.add(new KeyValue(other[i].key(), other[i].value()));
  }
}

size_t Section::length() {
  _cleanUp();
  size_t len = 3 + (titleDyn != nullptr ? strlen(titleDyn) : 0);
  for (size_t i=0; i<keyValues.size(); i++) {
    len += keyValues[i]->length();
  }
  return len;
}

size_t Section::sprint(char* dest, int sectionNum, bool numericTitle, bool noTitle) {
  _cleanUp();
  size_t len = 0;
  if (!noTitle) {
    if (numericTitle) {
      len += snprintf(dest + len, 13, "[%d]", sectionNum);
    } else {
      len += sprintf(dest + len, "[%s]", titleDyn);
    }
    len += sprintf(dest + len, "\n");
  }
  for (int i=0; i<keyValues.size(); i++) {
    len += keyValues[i]->sprint(dest + len);
  }
  return len;
};


// ------------------------------------------------------ Config class ------------------------------------------------------


Config::Config() : sections() {}

Config::Config(const char* s) : Config() {
  parse(s);
}

Config::~Config() {
  for (int i=0; i<sections.size(); i++) {
    delete sections[i];
  }
}

/* Description:
 *     delete all sections (from RAM) and reset the state to empty
 */
void Config::clear() {
  for (int i=0; i<sections.size(); i++) {
    delete sections[i];
  }
  sections.clear();
}

void Config::parse(const char* s) {
  if (s==nullptr || *s=='\0') {
    return;
  }

  this->clear();

  // Parse each section
  const char newSection[] = "\n[";
  const char* sectionStart = s;
  const char* sectionEnd;
  do {
    sectionEnd = strstr(sectionStart, newSection);
    int sectionLength;
    if (sectionEnd != nullptr) {
      // Next section found
      sectionLength = sectionEnd - sectionStart + 1;
    } else {
      // This is the last section
      sectionLength = strlen(sectionStart);
    }
    this->addSection(new Section(sectionStart, sectionLength, sections.size()));
    sectionStart += sectionLength;
  } while (sectionEnd != nullptr);
}

void Config::addSection(Section* section) {
  sections.add(section);
}

void Config::addSection(const char* title) {
  sections.add(new Section(title));
}

size_t Config::addSection() {
  sections.add(new Section());
  return sections.size()-1;
}

void Config::reorderLast(int startAt, int (*cmp)(Section**, Section**)) {
  // same as LinearArray::reorderAdded, but can start at any index
  for (size_t j = startAt; j < sections.size() - 1; j++) {
    if ((*cmp)(&sections[j], &sections[sections.size() - 1]) > 0) {
      sections.insert(j, sections.pop());
      break;
    }
  }
}

void Config::sortFrom(int startAt, int (*cmp)(Section**, Section**)) {
  this->sections.sortFrom(startAt, cmp);
}

/* Description:
 *     remove section by its ordinal position
 */
bool Config::removeSection(int i) {
  if (i < sections.size()) {
    delete sections[i];
    sections.remove(i);
    return true;
  }
  return false;
}

/* Description:
 *     return pointer to the first section with this `title`
 */
Section* Config::_findSection(const char* title) {
  if (title != nullptr) {
    for (size_t i=0; i<sections.size(); i++) {
      if (!strcmp(sections[i]->title(), title)) {
        return sections[i];
      }
    }
  }
  return nullptr;
};

bool Config::hasSection(const char* title) {
  return _findSection(title) != nullptr;
}

Section& Config::operator[](int index) {
  // NOTE: wrong index is extremely unsafe
  if (index < 0) {
    index = sections.size()+index;                           // Python-style
    if (index < 0) {
      index = -(index-sections.size()) - 1;  // otherwise: complement  -1 -> 0; -2 -> 1, ...
    }
  }
  if (index >= sections.size()) {
    if (index == sections.size() || !sections.size()) {
      this->addSection();
    }
    index = sections.size()-1;     // always choose last one
  }
  return *sections[index];
}

Section& Config::operator[](const char* title) {
  // NOTE: wrong title is unsafe
  Section* p = _findSection(title);
  if (p != nullptr) {
    return *p;
  }

  // UNSAFE: if not found, add a new section with that title and return the last section;
  //         THE BEHAVIOR IS UNDEFINED IF NEW SECTION FAILED TO ALLOCATE
  this->addSection(title);
  return *sections[sections.size()-1];
}

// Relatively high-level and database-like capabilities

/* Description:
 *     find section which has `key` with value `value`
 * Return:
 *     Integer index of the first section found; -1 otherwise
 */
int Config::query(const char* key, const char* value) {
  if (key == nullptr || value == nullptr) {
    return -1;
  }
  for (int i=0; i<this->sections.size(); i++) {
    Section* section = this->sections[i];
    if (section == nullptr) {
      continue;  // paranoia
    }
    if (section->hasKey(key))
      if (!strcmp((*section)[key], value)) {
        return i;
      }
  }
  return -1;
}

/* Description:
 *     same as query, but for two keys
 * Return:
 *     Integer index of the first section found; -1 otherwise
 */
int Config::query(const char* key1, const char* value1, const char* key2, const char* value2) {
  IF_LOG(VERBOSE) {
    log_d("%s : \"%s\"", key1, value1);
    log_d("%s : \"%s\"", key2, value2);
  }
  for (int i=0; i<this->sections.size(); i++) {
    Section* section = this->sections[i];
    if (section == nullptr) {
      continue;  // paranoia
    }
    IF_LOG(VERBOSE) {
      if (section->hasKey(key1) && section->hasKey(key2)) {
        log_d("    %s : \"%s\"", key1, (*section)[key1].value());
        log_d("    %s : \"%s\"", key2, (*section)[key2].value());
      }
    }
    if (section->hasKey(key1) && section->hasKey(key2) && !strcmp((*section)[key1].value(), value1) && !strcmp((*section)[key2].value(), value2)) {
      return i;
    }
  }
  return -1;
}

/* Description:
 *     find section which has `key` with integer value `value`
 * Return:
 *     Integer index of the first section found; -1 otherwise
 */
int Config::query(const char* key, int32_t value) {
  int32_t def = value ? -1 : 0;
  IF_LOG(VERBOSE) {
    log_d("%s : \"%d\"", key, value);
  }
  for (int i=0; i<this->sections.size(); i++) {
    Section* section = this->sections[i];
    if (section == nullptr) {
      continue;  // paranoia
    }
    IF_LOG(VERBOSE) {
      if (section->hasKey(key) && section->hasKey(key)) {
        log_d("    %s : \"%s\"", key, (*section)[key].value());
      }
    }
    if (section->hasKey(key) && section->getIntValueSafe(key, def)==value) {
      return i;
    }
  }
  return -1;
}

/* Description:
 *     find section which has field with key `key`.
 *     This is intended to be used for finding sections with a unique flag. See setUniqueFlag().
 * Return:
 *     Integer index of the first section found; -1 otherwise
 */
int Config::findKey(const char* key) {
  for (int i=0; i<this->sections.size(); i++) {
    Section* section = this->sections[i];
    if (section == nullptr) {
      continue;  // paranoia
    }
    if (section->hasKey(key)) {
      return i;
    }
  }
  return -1;
}

/* Description:
 *     removes fields with key `key` from all sections
 */
void Config::removeAllKeys(const char* key) {
  for (int i=0; i<this->sections.size(); i++) {
    this->sections[i]->remove(key);
  }
}

/* Description:
 *     removes a `key` from all sections and sets it exactly in one section specified by `index` with value "1"
 * Return:
 *     true if successful
 */
bool Config::setUniqueFlag(int index, const char* key) {
  // Clear flag from all sections
  this->removeAllKeys(key);

  // Set flag in exactly one section
  if (index < 0 || index >= this->sections.size()) {
    return false;
  }
  (*this->sections[index])[key] = "1";
  return true;
}

/* Description:
 *     removes a `key` from all sections and sets it exactly in one section specified by `sectionTitle` with value "1"
 * Return:
 *     true if successful
 */
bool Config::setUniqueFlag(const char* sectionTitle, const char* key) {
  // Clear flag from all sections
  this->removeAllKeys(key);

  // Set flag in exactly one section
  Section* p = _findSection(sectionTitle);
  if (p == nullptr) {
    return false;
  }
  (*p)[key] = "1";
  return true;
}

size_t Config::nSections() const {
  return sections.size();
};

bool Config::isEmpty() const {
  return sections.size() == 0;
}

/* Desctiption:
 *    returns the length of the serialized string.
 * Complexity:
 *    O(n).
 */
size_t Config::length() {
  size_t len = 0;
  for (int i=0; i<sections.size(); i++) {
    len += sections[i]->length();
    if (*(sections[i]->title()) == '\0') {    // empty title -> it's either first section (no title) or with ordinary numeric title
      if (i==0) {
        // Subtract three characters ("[]\n") counted for the first section
        len -= 3;
      } else {
        // Account for numeric title
        char buff[11];
        snprintf(buff, 11, "%d", i);
        len += strlen(buff);
      }
    }
  }
  return len;
}

/* Description:
 *     serialize data into a C-string buffer (see p_c_str() for example usage or use that one instead).
 * Complexity:
 *     O(n).
 */
size_t Config::sprint(char* str) {
  size_t len = 0;
  for (int i=0; i<sections.size(); i++) {
    bool numericTitle = false;
    bool noTitle = false;
    if (*(sections[i]->title()) == '\0') {
      if (i==0) {
        noTitle = true;
      }
      numericTitle = true;
    }
    len += sections[i]->sprint(str + len, i, numericTitle, noTitle);
  }
  return len;
}

/* Description:
 *     return serialized C-string of exactly right size.
 * Complexity:
 *     O(n); in 2 passes (for this->length() and this->sprint()).
 * Example usage:
 *     printf("%s", ini.p_c_str().get());
 */
std::unique_ptr<char[]> Config::p_c_str() {
  char* str = (char*) extMalloc(this->length()+1);
  this->sprint(str);
  return std::unique_ptr<char[]>(str);
}

bool isSafeString(const char* str) {
  for (; *str; str++)
    if (*str < ' ' && *str != '\r' && *str != '\t' || *str >= 127) {
      return false;
    }
  return true;
}

};
