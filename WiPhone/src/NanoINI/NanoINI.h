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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory>

/*
 * Description:
 *
 *     A little intuitive embedded-friendly class to deserialize, access, modify, serialize and
 *     create data in a minimalist INI format. It uses C-strings internally.
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
 *     Developer notes:
 *       - THERE IS NO MULTILINE VALUES SUPPORT! Beware that adding a new or modifying a key-value
 *         will replace all '\n' characters with '\r' characters; BE CAUTIOUS
 *         - one possible workaround is to store multiple values with an empty key inside a section
 *       - AVOID USING INTEGER NUMBERS AS SECTION NAMES!
 *         - on parsing: integer section names are removed if they are the same as section position TODO
 *         - on serializing: empty section names are saved as numeric values (section positions)
 *       - use hasKey to check if key exists before accessing it, for example:
 *         ini["section1"].hasKey("key1")
 */

namespace NanoINI {

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
  if (*p!=NULL) {
    free(*p);
    *p = NULL;
  }
}

template <class T>
class LinearArray {
public:
  LinearArray() : arrayDyn(nullptr), arraySize(0), arrayAllocSize(0) {};

  T pop(void) {
    if (arraySize > 0) {
      return arrayDyn[(arraySize--)-1];
    }
    return T(0);
  };

  bool removeByValue(T element) {
    for (size_t i=0; i < arraySize; i++) {
      if (element==arrayDyn[i]) {
        for (size_t j=i; j < arraySize - 1; j++) {
          arrayDyn[j] = arrayDyn[j+1];
        }
        arraySize--;
        return true;
      }
    }
    return false;
  };

  bool add(T element) {

    // First: ensure enough memory is allocated
    bool succ = true;
    if (arraySize >= arrayAllocSize) {

      // More memory needs to be allocated
      if (arrayAllocSize > 0) {

        // Double the amount of storage
        arrayAllocSize *= 2;
        T* tmp = (T*) realloc(arrayDyn, arrayAllocSize*sizeof(T));
        if (tmp!=NULL) {
          //printf("realloced: %zu\r\n", arrayAllocSize);
          arrayDyn = tmp;
        } else {
          succ = false;
          arrayAllocSize /= 2;
        }

      } else {

        // Initial allocation
        arrayAllocSize = 2;
        arrayDyn = (T*) malloc(arrayAllocSize*sizeof(T));
        if (arrayDyn==NULL) {
          succ = false;
          arrayAllocSize = 0;
        } else {
          //printf("alloced: %zu\r\n", arrayAllocSize);
        }

      }
    }

    // Second: actually save to array
    if (succ) {
      arrayDyn[arraySize++] = element;
    }

    return succ;
  };

  size_t size() {
    return arraySize;
  };

  T operator[](int index) const {
    return arrayDyn[index];
  };

  T& operator[](int index) {
    return arrayDyn[index];
  };

protected:
  T* arrayDyn;
  size_t arraySize;
  size_t arrayAllocSize;
};

class KeyValue {
public:
  KeyValue()
    : keyDyn(nullptr), valueDyn(nullptr)
  {};

  KeyValue(const char* key, const char* value) : KeyValue() {
    if (key!=nullptr) {
      keyDyn = strdup(key);
    }
    _escape(keyDyn);
    if (value!=nullptr) {
      valueDyn = strdup(value);
      _escape(valueDyn);
    }
  };

  KeyValue(const char* line, size_t lineLength) : KeyValue() {

    const char* firstEqual = (const char *) memchr(line, '=', lineLength);

    // Parse key
    if (firstEqual != nullptr) {
      // Key found
      if (firstEqual - line > 0) {
        keyDyn = strndup(line, firstEqual - line);
      }

      // Leave only value in the line
      lineLength -= firstEqual + 1 - line;
      line = firstEqual + 1;
    }

    // Parse value
    const char* newLine = (const char *) memchr(line, '\n', lineLength);
    valueDyn = strndup(line, newLine != nullptr ? newLine - line : lineLength);

    if (keyDyn != nullptr) {
      printf("Key: %s, ", keyDyn);
    }
    printf("Value: %s\r\n", valueDyn);
  };

  ~KeyValue() {
    freeNull((void **) &keyDyn);
    freeNull((void **) &valueDyn);
  };

  const char* key(void) const {
    return keyDyn;
  };

  const char* value(void) const {
    return valueDyn != nullptr ? valueDyn : "";
  };

  operator const char*(void) const {
    return this->value();
  };

  operator bool(void) const {
    return valueDyn != nullptr;
  };

  const char* operator=(const char* newValue) {
    freeNull((void **) &valueDyn);

    if (newValue != nullptr) {
      valueDyn = strdup(newValue);
      _escape(valueDyn);
    }

    return valueDyn;
  }

  void _escape(char* p) {
    // Replace '\n' with '\r' in the new value
    while ((p = strchr(p, '\n')) != NULL) {
      *p++ = '\r';
    }
  }

  size_t length(void) const {
    size_t len = 0;
    if (valueDyn != nullptr) {
      len += 2 + strlen(valueDyn) + (keyDyn != nullptr ? strlen(keyDyn) : 0);
    }
    return len;
  }

  size_t sprint(char* dest) {
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

protected:
  char* keyDyn;     // can be NULL
  char* valueDyn;   // by convention, should not be NULL; if it is - it is a provisional key-value and is meant to be deleted
};

class Section {
public:
  Section()
    : titleDyn(nullptr), provisional(nullptr), keyValues()
  {};

  Section(const char* title) : Section() {
    titleDyn = strdup(title);
  };

  Section(const char* ss, size_t sectionLength) : Section() {

    if (ss == nullptr || sectionLength == 0) {
      return;
    }

    const char* sectionEnd = ss + sectionLength;

    // Parse title
    if (ss[0] == '[') {
      const char* titleEnd = ss + 1 + strcspn(ss + 1, "]\n");
      if (titleEnd > sectionEnd) {
        titleEnd = sectionEnd;
      }
      titleDyn = strndup(ss + 1, titleEnd - ss - 1);
      ss = titleEnd;      // advance pointer by title length
    }
    printf("New section: \"%s\"\r\n", titleDyn != nullptr ? titleDyn : "");

    // Parse key-values
    if (titleDyn != nullptr) {
      // Skip first line if the title was found
      ss = (const char*) memchr(ss, '\n', sectionEnd - ss);
      if (ss != nullptr) {
        ss++;  // skip the '\n' character if it was found
      }
    }
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

  };

  ~Section() {
    for (int i=0; i<keyValues.size(); i++) {
      delete keyValues[i];
    }
    freeNull((void **) &titleDyn);
  };

  KeyValue& addKeyValue(KeyValue* keyVal) {
    _cleanUp();
    keyValues.add(keyVal);
    return *keyVal;
  };

  KeyValue& addKeyValue(const char* key, const char* value) {
    return this->addKeyValue(new KeyValue(key, value));
  };

  void _cleanUp(void) {
    if (provisional != nullptr) {
      if (!(bool) *provisional) {
        if (keyValues.size()>0 && keyValues[keyValues.size()-1] == provisional) {
          keyValues.pop();
          printf("PROVISIONAL: popped\n");
        } else {
          if (keyValues.removeByValue(provisional)) {
            printf("PROVISIONAL: removed by value\n");
          } else {
            printf("PROVISIONAL: ERROR: not found\n");
          }
        }
        delete provisional;
      }
      provisional = nullptr;
    };
  };

  KeyValue* _find(const char* key) {
    _cleanUp();
    for (size_t i=0; i<keyValues.size(); i++) {
      //if (keyValues[i]->value() == nullptr) continue;
      if ((key != nullptr && keyValues[i]->key() != nullptr && !strcmp(keyValues[i]->key(), key)) ||
          (key == nullptr && keyValues[i]->key()==key)) {
        return keyValues[i];
      }
    }
    return nullptr;
  }

  bool hasKey(const char* key) {
    return _find(key) != nullptr;
  };

  KeyValue& operator[](int index) {
    // NOTE: wrong index is unsafe
    return *keyValues[index];
  };

  KeyValue& operator[](const char* key) {
    // NOTE: wrong key is unsafe
    KeyValue* p = _find(key);
    if (p != nullptr) {
      return *p;
    }

    // UNSAFE PART: add an empty key value; this will cause keys to be stored in the array,
    //              perhaps unnecessarily
    KeyValue& kv = this->addKeyValue(key, nullptr);
    provisional = &kv;
    return kv;
  };

  size_t nValues(void) {
    return keyValues.size();
  };

  const char* title(void) {
    return titleDyn!=nullptr ? titleDyn : "";
  };

  void setTitle(const char* title) {
    freeNull((void **) &titleDyn);
    titleDyn = strdup(title);
  }

  size_t length(void) {
    _cleanUp();
    size_t len = 3 + (titleDyn != nullptr ? strlen(titleDyn) : 0);
    for (size_t i=0; i<keyValues.size(); i++) {
      len += keyValues[i]->length();
    }
    return len;
  }

  size_t sprint(char* dest, int section, bool numericTitle, bool noTitle) {
    _cleanUp();
    size_t len = 0;
    if (!noTitle) {
      if (numericTitle) {
        len += snprintf(dest + len, 13, "[%d]", section);
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

protected:
  char* titleDyn;                       // can be NULL
  KeyValue* provisional;                // KeyValue with NULL value that should be cleaned up on each access
  LinearArray<KeyValue*> keyValues;
};

class Config {
public:

  Config() : sections() {};

  Config(const char* s) : Config() {
    if (s==nullptr || *s=='\0') {
      return;
    }

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
      this->addSection(new Section(sectionStart, sectionLength));
      sectionStart += sectionLength;
    } while (sectionEnd != nullptr);
  };

  ~Config() {
    for (int i=0; i<sections.size(); i++) {
      delete sections[i];
    }
  };

  void addSection(Section* section) {
    //printf("adding section\r\n");
    sections.add(section);
  };

  void addSection(const char* title) {
    sections.add(new Section(title));
  };

  void addSection(void) {
    sections.add(new Section());
  };

  Section& operator[](int index) {
    return *sections[index];
  };

  size_t nSections(void) {
    return sections.size();
  };

  size_t length(void) {
    size_t len = 0;
    for (int i=0; i<sections.size(); i++) {
      len += sections[i]->length();
      if (*(sections[i]->title()) == '\0') {    // empty title
        if (i==0) {
          len -= 3;
        } else {
          char buff[11];
          snprintf(buff, 11, "%d", i);
          len += strlen(buff);
        }
      }
    }
    return len;
  };

  size_t sprint(char* str) {
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
  };

  /*
   * Example usage:
   *     printf("%s", ini.p_c_str().get());
   */
  std::unique_ptr<char[]> p_c_str() {
    char* str = (char*) malloc(this->length()+1);
    this->sprint(str);
    return std::unique_ptr<char[]>(str);
  };

protected:
  LinearArray<Section*> sections;
};

}   // namespace NanoINI
