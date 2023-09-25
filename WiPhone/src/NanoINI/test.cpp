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
#include "NanoINI.h"

#define LINE "==============================================================================="

using namespace NanoINI;

void showIniContents(Config& ini) {
  for (int i=0; i<ini.nSections(); i++) {
    printf("ini[%d].title() = \"%s\"\n", i, ini[i].title());
    for (int j=0; j<ini[i].nValues(); j++) {
      if (!(bool) ini[i][j]) {
        continue;
      }
      printf("ini[%d][%d] = ", i, j);
      const char* key = ini[i][j].key();
      if (key != nullptr) {
        if (ini[i][key] != nullptr && !strcmp((const char*) ini[i][j], ini[i][key])) {
          printf("ini[%d][\"%s\"] = ", i, key);
        } else {
          printf("ERROR = ");
        }
      }
      printf("\"%s\"", (const char*) ini[i][j]);
      printf("\n");
    }
  }
}

int main() {
  {
    const char str1[] = "B=1\n[sect1]\nA=sample\nC=program\n[]this is ignored\n=1\n2=\n3=c\nkkk\n[sect3\nHello";

    printf(LINE " INI INPUT\n");
    printf(str1);

    printf(LINE " PARSING\n");
    Config ini(str1);

    printf(LINE " ACCESSING\n");
    showIniContents(ini);

    printf(LINE " MODIFYING\n");
    ini[2][0] = "new value";
    ini[2].addKeyValue("newKey", "new value");
    const char key1[] = "ratherLargeKeyThatWillBeStoredInMemory";
    if ((bool) ini[3][key1]) {
      printf("FOUND\n");
    }
    if (!(bool) ini[3][key1]) {
      printf("NOT FOUND\n");
    }
    ini[3].addKeyValue(key1, "1");
    if ((bool) ini[3][key1]) {
      printf("FOUND\n");
    }
    if (!(bool) ini[3][key1]) {
      printf("NOT FOUND\n");
    }
    showIniContents(ini);

    printf(LINE " SERIALIZING\n");
    {
      size_t len = ini.length();
      printf("Length: %zu\n", len);
      char buff[len*2];
      ini.sprint(buff);
      printf("Real length: %zu\n", strlen(buff));
      printf("%s", buff);
      printf("%s", ini.p_c_str().get());
    }
  }

  {
    printf(LINE " CREATING\n");
    Config cfg;
    cfg.addSection();
    cfg[0]["max"] = "2";
    cfg.addSection("contact1");
    cfg.addSection("contact2");
    cfg[1]["name"] = "Ben Wilson";
    cfg[1]["sip"] = "sip:esp32@linphone.org";
    cfg[2]["name"] = "Andriy Makukha";
    cfg[2]["sip"] = "sip:andriy@sip2sip.info";

    printf(LINE " SERIALIZING\n");
    {
      size_t len = cfg.length();
      printf("Length: %zu\n", len);
      char buff[len*2];
      cfg.sprint(buff);
      printf("Real length: %zu\n", strlen(buff));
      printf("%s", buff);
      printf("%s", cfg.p_c_str().get());
    }

    printf(LINE " MODIFYING\n");
    cfg[1].setTitle("");
    cfg[2].setTitle("");

    printf(LINE " SERIALIZING\n");
    {
      size_t len = cfg.length();
      printf("Length: %zu\n", len);
      char buff[len*2];
      cfg.sprint(buff);
      printf("Real length: %zu\n", strlen(buff));
      printf("%s", buff);
      printf("%s", cfg.p_c_str().get());
    }
  }

  printf(LINE "\n");
  return 0;
}
