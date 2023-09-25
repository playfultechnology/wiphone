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

#ifndef OTA_H
#define OTA_H

#include <WiFi.h>
#include "Networks.h"
#include "esp_log.h"
#include <Update.h>
#include <string>

#define OTA_UPDATE_CHECK_INTERVAL 60*1000*60
#define DEFAULT_INI_HOST "wiphone.io"
#define DEFAULT_INI_LOC "/static/releases/firmware/WiPhone-phone.ini"

class Ota {
public:
  Ota(std::string inifile);

  bool updateExists(bool loadIni=true);
  bool doUpdate();
  bool hasJustUpdated();
  bool commitUpdate();
  void backgroundUpdateCheck();

  void saveAutoUpdate(bool autoUpdate);
  bool autoUpdateEnabled();
  bool userRequestedUpdate();
  void setUserRequestedUpdate(bool userUpdate);
  void setIniUrl(const char* url);
  void ensureUserVersion();
  void resetIni();

  const char* getIniUrl();
  const char* getIniHost();
  const char* getIniPath();
  const char* getServerVersion();
  const char* getLastErrorCode();
  const char* getLastErrorString();

  void reset();

private:
  std::string inifileLocation_;
  std::string fwUrl_;
  std::string fwVersion_;
  uint32_t lastLoad_;

  std::string iniLocation_;

  bool loadIniFile();
};


#endif // OTA_H
