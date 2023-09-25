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

#include "ota.h"
#include <WiFi.h>
#include "Networks.h"
#include "esp_log.h"
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "esp_ota_ops.h"

#define CA_CERT_MAX_SZ 10*1024

static const char *defaultCaCert =
  "-----BEGIN CERTIFICATE-----\n"\
  "MIIFKjCCBBKgAwIBAgISBBe7/VBighjcsshbqXuCmn3NMA0GCSqGSIb3DQEBCwUAMDIxCzAJBgNVBAYTAlVTMRYwFAYDVQQKEw1MZXQncyBFbmNyeXB0MQswCQYDVQQDEwJSMzAeFw0yMTAxMTgxMDU3MzJaFw0yMTA0MTgxMDU3MzJaMBUxEzARBgNVBAMTCndpcGhvbmUuaW8wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDYx7lCvY5y9Km+3AlmA0Pb+jrja4NXFNWR3D2r6iMqgAutqGm9LnCl9I+295HcPTH1SWpIuXfvgdy24WhNWXU7q59Rnp6VuopvqFdfgCgMvhk10pBSN8Aq8BZsR/29p4au6pQ3tSKboFlNXYRPjJln6EPQRH8M9pME+WvJcYdglwifY1dxIVXrcVMnJjQ2lC7z120Zu21R3pOvqocR+ddKs027P1kW7Ez3ROk73oGiBZR2f+Pn+OHMy6S5c4sAex3KtRFE9GnUrZI4ZMdhR1Zu88rTutM6Iou+Z8lPJ81RLa/bTATrxtIfelG3mVz2DTZ3lIv3Vr0YreybUXH0iu3dAgMBAAGjggJVMIICUTAOBgNVHQ8BAf8EBAMCBaAwHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMAwGA1UdEwEB/wQCMAAwHQYDVR0OBBYEFPO5cniAMVAR3lVuGDjwYMANaLGCMB8GA1UdIwQYMBaAFBQusxe3WFbLrlAJQOYfr52LFMLGMFUGCCsGAQUFBwEBBEkwRzAhBggrBgEFBQcwAYYVaHR0cDovL3IzLm8ubGVuY3Iub3JnMCIGCCsGAQUFBzAChhZodHRwOi8vcjMuaS5sZW5jci5vcmcvMCUGA1UdEQQeMByCCndpcGhvbmUuaW+CDnd3dy53aXBob25lLmlvMEwGA1UdIARFMEMwCAYGZ4EMAQIBMDcGCysGAQQBgt8TAQEBMCgwJgYIKwYBBQUHAgEWGmh0dHA6Ly9jcHMubGV0c2VuY3J5cHQub3JnMIIBBAYKKwYBBAHWeQIEAgSB9QSB8gDwAHYAfT7y+I//iFVoJMLAyp5SiXkrxQ54CX8uapdomX4i8NcAAAF3FVt8CwAABAMARzBFAiEAuhqAjMB6rqFDHyejZu4cCyAosE+w8DOAlykmqt5eZ6MCIHwHoA+68RAx6JksrpuxkA/7REG9GvllRx7HiEEamHEKAHYAb1N2rDHwMRnYmQCkURX/dxUcEdkCwQApBo2yCJo32RMAAAF3FVt8vQAABAMARzBFAiEA0bo4rtb9iCrVo39EgGKnpnUdpieWeSgnlcgbeMc2eN8CIFOuqsGeQ8Opm+hhiKIPPuizbFo9WJTw2LsaQraTYbQqMA0GCSqGSIb3DQEBCwUAA4IBAQBt20nC3EPXOsR0Kj5ST6xjvZld540fJRnmIu2QiixL9bubd4KrT8IVFI6ksFs9AOOsfNksXIXYgEvR71TTKx4IezsLcKqo1SLkmtalebu/fWyLrv/dShr4IKRB+xHUywdayj8IkEpjRHjOiZH/f47y//ftPRKVd3xjnUs9PBzWeZw7+eZs/NlEJsiAWSltNNKiVof8CAci+rSaifbNHOd8qi4UVSnbAlfoyM/RMUI85XKspwnKvXqHPp19192nwpe8kIWQ28aA16JAmq0o8/eohK4/S4WewQHfdSn0A3zJdFOKhWX11skbuYpV1uxm4JGXT6mPeXdOgu9WTqgkEHCe\n"\
  "-----END CERTIFICATE-----\n"\
  "-----BEGIN CERTIFICATE-----\n"\
  "MIIFFjCCAv6gAwIBAgIRAJErCErPDBinU/bWLiWnX1owDQYJKoZIhvcNAQELBQAwTzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjAwOTA0MDAwMDAwWhcNMjUwOTE1MTYwMDAwWjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3MgRW5jcnlwdDELMAkGA1UEAxMCUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC7AhUozPaglNMPEuyNVZLD+ILxmaZ6QoinXSaqtSu5xUyxr45r+XXIo9cPR5QUVTVXjJ6oojkZ9YI8QqlObvU7wy7bjcCwXPNZOOftz2nwWgsbvsCUJCWH+jdxsxPnHKzhm+/b5DtFUkWWqcFTzjTIUu61ru2P3mBw4qVUq7ZtDpelQDRrK9O8ZutmNHz6a4uPVymZ+DAXXbpyb/uBxa3Shlg9F8fnCbvxK/eG3MHacV3URuPMrSXBiLxgZ3Vms/EY96Jc5lP/Ooi2R6X/ExjqmAl3P51T+c8B5fWmcBcUr2Ok/5mzk53cU6cG/kiFHaFpriV1uxPMUgP17VGhi9sVAgMBAAGjggEIMIIBBDAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMBIGA1UdEwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFBQusxe3WFbLrlAJQOYfr52LFMLGMB8GA1UdIwQYMBaAFHm0WeZ7tuXkAXOACIjIGlj26ZtuMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcwAoYWaHR0cDovL3gxLmkubGVuY3Iub3JnLzAnBgNVHR8EIDAeMBygGqAYhhZodHRwOi8veDEuYy5sZW5jci5vcmcvMCIGA1UdIAQbMBkwCAYGZ4EMAQIBMA0GCysGAQQBgt8TAQEBMA0GCSqGSIb3DQEBCwUAA4ICAQCFyk5HPqP3hUSFvNVneLKYY611TR6WPTNlclQtgaDqw+34IL9fzLdwALduO/ZelN7kIJ+m74uyA+eitRY8kc607TkC53wlikfmZW4/RvTZ8M6UK+5UzhK8jCdLuMGYL6KvzXGRSgi3yLgjewQtCPkIVz6D2QQzCkcheAmCJ8MqyJu5zlzyZMjAvnnAT45tRAxekrsu94sQ4egdRCnbWSDtY7kh+BImlJNXoB1lBMEKIq4QDUOXoRgffuDghje1WrG9ML+Hbisq/yFOGwXD9RiX8F6sw6W4avAuvDszue5L3sz85K+EC4Y/wFVDNvZo4TYXao6Z0f+lQKc0t8DQYzk1OXVu8rp2yJMC6alLbBfODALZvYH7n7do1AZls4I9d1P4jnkDrQoxB3UqQ9hVl3LEKQ73xF1OyK5GhDDX8oVfGKF5u+decIsH4YaTw7mP3GFxJSqv3+0lUFJoi5Lc5da149p90IdshCExroL1+7mryIkXPeFM5TgO9r0rvZaBFOvV2z0gp35Z0+L4WPlbuEjN/lxPFin+HlUjr8gRsI3qfJOQFy/9rKIJR0Y/8Omwt/8oTWgy1mdeHmmjk7j1nYsvC9JSQ6ZvMldlTTKB3zhThV1+XWYp6rjd5JW1zbVWEkLNxE7GJThEUG3szgBVGP7pSWTUTsqXnLRbwHOoq7hHwg==\n"\
  "-----END CERTIFICATE-----\n"\
  "-----BEGIN CERTIFICATE-----\n"\
  "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAwTzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygch77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6UA5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sWT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyHB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UCB5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUvKBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWnOlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTnjh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbwqHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CIrU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkqhkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZLubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KKNFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7UrTkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdCjNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVcoyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPAmRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57demyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"\
  "-----END CERTIFICATE-----\n";

static int compVersions ( const char * version1, const char * version2 ) {
  uint32_t major1 = 0, minor1 = 0, bugfix1 = 0, rc1 = 0;
  uint32_t major2 = 0, minor2 = 0, bugfix2 = 0, rc2 = 0;

  if (strstr(version1, "db") != NULL || strstr(version2, "db") != NULL) {
    if (strcmp(version1, version2) == 0 ) {
      return 0;
    }

    return -1;
  }

  if (strstr(version1, "rc") != NULL) {
    sscanf(version1, "%u.%u.%urc%u", &major1, &minor1, &bugfix1, &rc1);
  } else {
    sscanf(version1, "%u.%u.%u", &major1, &minor1, &bugfix1);
  }

  if (strstr(version2, "rc") != NULL) {
    sscanf(version2, "%u.%u.%urc%u", &major2, &minor2, &bugfix2, &rc2);
  } else {
    sscanf(version2, "%u.%u.%u", &major2, &minor2, &bugfix2);
  }

  log_d("v: %u %u %u %u -> %u %u %u %u", major1, minor1, bugfix1, rc1, major2, minor2, bugfix2, rc2);

  if (major1 < major2) {
    return -1;
  }
  if (major1 > major2) {
    return 1;
  }
  if (minor1 < minor2) {
    return -1;
  }
  if (minor1 > minor2) {
    return 1;
  }
  if (bugfix1 < bugfix2) {
    return -1;
  }
  if (bugfix1 > bugfix2) {
    return 1;
  }
  if (rc1 < rc2) {
    return -1;
  }
  if (rc1 > rc2) {
    return 1;
  }

  return 0;
}


static bool loadRootCACert(char *certBuf, size_t certBufSz) {
  char fname[500] = {0};

  if (SPIFFS.exists("/user.pem")) {
    strlcpy(fname, "/user.pem", sizeof(fname));
  } else {
    strlcpy(fname, "/wiphone.pem", sizeof(fname));
  }

  if (!SPIFFS.exists(fname)) {
    // Default to hard coded pem file
    strlcpy(certBuf, defaultCaCert, certBufSz);
  } else {
    File cert = SPIFFS.open(fname);

    if (!cert) {
      log_d("Unable to load pem file: %s", fname);
      return false;
    }

    int i = 0;
    memset((void*)certBuf, 0x00, certBufSz);
    while (cert.available() && i < certBufSz) {
      certBuf[i++] = cert.read();
    }
  }

  log_i("CA cert is: [%s]", certBuf);

  return true;
}

static char *getOtaIniFileName() {
  static char fileName[1024] = {0};

  if (SPIFFS.exists("/user_ota.ini")) {
    strlcpy(fileName, "/user_ota.ini", sizeof(fileName));
  } else {
    strlcpy(fileName, "/ota.ini", sizeof(fileName));
  }

  log_d("Config file: %s", fileName);

  return fileName;
}

Ota::Ota(std::string inifile) : inifileLocation_(inifile), fwVersion_(""), fwUrl_(""), lastLoad_(0), iniLocation_(std::string("https://") + std::string(DEFAULT_INI_HOST) + std::string(DEFAULT_INI_LOC)) {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  const char* l = otaIni[0].getValueSafe("serverIni", "0");

  if (strlen(l) > 3) {
    iniLocation_ = std::string(l);
  }
}

void Ota::resetIni() {
  if (SPIFFS.exists("/user_ota.ini")) {
    SPIFFS.remove("/user_ota.ini");
  }
  reset();

  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  iniLocation_ = std::string("https://") + std::string(DEFAULT_INI_HOST) + std::string(DEFAULT_INI_LOC);
  log_d("Removed user ini file");
}

void Ota::ensureUserVersion() {
  log_d("Ota::ensureUserVersion: %d", SPIFFS.exists("/user_ota.ini"));
  // Ensure we are using the user version of the config file as the user is about to edit something
  if (strcmp(getOtaIniFileName(), "/ota.ini") == 0) {
    CriticalFile userIni("/user_ota.ini");
    CriticalFile wiphoneIni("/ota.ini");

    userIni.load();
    wiphoneIni.load();

    userIni[0]["autoUpdate"] = wiphoneIni[0].getValueSafe("autoUpdate", "yes");
    userIni[0]["serverIni"] = wiphoneIni[0].getValueSafe("serverIni", "");
    userIni[0]["errorCode"] = "";
    userIni[0]["errorString"] = "";
    userIni[0]["serverVersion"] = "";
    userIni[0]["latestServerVersion"] = "";
    userIni[0]["newVersion"] = "";

    userIni.store();
    reset();
    log_d("Ota::ensureUserVersion: %d", SPIFFS.exists("/user_ota.ini"));
  }
}

const char* Ota::getIniUrl() {
  return iniLocation_.c_str();
}

const char* Ota::getIniHost() {
  log_d("ini url: [%s] [%d]", iniLocation_.c_str());
  if (iniLocation_.rfind("https://", 0) != std::string::npos) {
    std::size_t epos = iniLocation_.find('/', 8);

    if (epos != std::string::npos) {
      size_t len = iniLocation_.length();
      char *host = (char*)extMalloc(len);
      memset(host, 0x00, len);
      strlcpy(host, iniLocation_.substr(8, epos - 8).c_str(), len);
      return host;
    }
  }

  size_t len = strlen(DEFAULT_INI_HOST) + 1;
  char *host = (char*)extMalloc(len);
  strlcpy(host, DEFAULT_INI_HOST, len);
  return host;
}

const char* Ota::getIniPath() {
  if (iniLocation_.rfind("https://", 0) != std::string::npos) {
    std::size_t spos = iniLocation_.find('/', 8);

    if (spos != std::string::npos) {
      size_t len = iniLocation_.length();
      char *path = (char*)extMalloc(len);
      memset(path, 0x00, len);
      strlcpy(path, iniLocation_.substr(spos).c_str(), len);
      return path;
    }
  }

  size_t len = strlen(DEFAULT_INI_LOC) + 1;
  char *path = (char*)extMalloc(len);
  strlcpy(path, DEFAULT_INI_LOC, len);
  return path;
}

const char* Ota::getServerVersion() {
  static char s[300] = {0};
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  strlcpy(s, otaIni[0].getValueSafe("latestServerVersion", "0"), sizeof(s));

  return s;
}

const char* Ota::getLastErrorCode() {
  static char s[300] = {0};
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  strlcpy(s, otaIni[0].getValueSafe("errorCode", "0"), sizeof(s));

  return s;
}

const char* Ota::getLastErrorString() {
  static char s[300] = {0};
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  strlcpy(s, otaIni[0].getValueSafe("errorString", "0"), sizeof(s));

  return s;
}

void Ota::saveAutoUpdate(bool autoUpdate) {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  otaIni[0]["autoUpdate"] = autoUpdate ? "yes" : "no";
  otaIni.store();
}

bool Ota::autoUpdateEnabled() {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  log_d("autoUpdateEnabled: %s", otaIni[0].getValueSafe("autoUpdate", "0"));

  if (strcmp(otaIni[0].getValueSafe("autoUpdate", "0"), "no") == 0) {
    return false;
  }

  return true;
}

bool Ota::userRequestedUpdate() {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  if (strcmp(otaIni[0].getValueSafe("userRequested", "0"), "yes") == 0) {
    return true;
  }

  return false;
}

void Ota::setUserRequestedUpdate(bool userUpdate) {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  otaIni[0]["userRequested"] = userUpdate ? "yes" : "no";

  if (userUpdate) {
    otaIni[0]["serverVersion"] = "";
    otaIni[0]["newVersion"] = "";
  }

  otaIni.store();
}

bool Ota::updateExists(bool loadIni) {
  log_i("#### Ota::updateExists: %d %d", loadIni, lastLoad_);
  if (lastLoad_ == 0) {
    if (loadIni && !loadIniFile()) {
      log_i("# Returning false");
      return false;
    }
  }

  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  fwVersion_ = std::string(otaIni[0].getValueSafe("latestServerVersion", "0"));

  if (fwVersion_ == "0") {
    log_i("#### Returning false: %s", fwVersion_.c_str());
    return false;
  }

  log_d("### Ota version in ini file: [%s] [%s] [%s]", otaIni[0].getValueSafe("latestServerVersion", "0"),
        otaIni[0].getValueSafe("oldVersion", "0"), fwVersion_.c_str());

  if (fwVersion_ != "" && fwVersion_ == std::string(otaIni[0].getValueSafe("newVersion", "0"))) {
    otaIni[0]["errorString"] =  "Install prev failed";
    otaIni[0]["errorCode"] =  "-900";
    otaIni.store();
    log_d("#### Ignoring fw update as it failed last time");
    return false;
  }

  const char *svs = fwVersion_.c_str();
  char *lvs = FIRMWARE_VERSION;

  log_i("Current version: %s server version: %s", lvs, svs);

  int diff = compVersions(lvs, svs);

  if (diff < 0) {
    return true;
  }

  return false;
}

bool Ota::doUpdate() {
  if (lastLoad_ == 0 || (millis() - lastLoad_) > OTA_UPDATE_CHECK_INTERVAL) {
    if (!loadIniFile()) {
      return false;
    }
  }

  if (fwUrl_ == "") {
    return false;
  }

  if (wifiState.isConnected()) {
    log_d("##### Doing OTA");

    const char* url = fwUrl_.c_str();

    if (url == NULL) {
      return false;
    }

    WiFiClientSecure client;
    char *rootCACertificate = (char*)extMalloc(CA_CERT_MAX_SZ);
    if (!loadRootCACert(rootCACertificate, CA_CERT_MAX_SZ)) {
      log_e("Unable to load cert file");
      return false;
    }

    client.setCACert(rootCACertificate);

    log_i("Doing firmware update: [%s]", url);

    CriticalFile otaIni(getOtaIniFileName());
    otaIni.load();
    otaIni[0]["newVersion"] = fwVersion_.c_str();
    otaIni[0]["oldVersion"] = FIRMWARE_VERSION;
    otaIni[0]["hadJustUpdated"] = "yes";
    otaIni.store();

    t_httpUpdate_return ret = httpUpdate.update(client, url);

    char errorCode[30] = {0};

    switch (ret) {
    case HTTP_UPDATE_FAILED:
      snprintf(errorCode, sizeof(errorCode), "%d", httpUpdate.getLastError());
      log_i("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      otaIni[0]["newVersion"] = "";
      otaIni[0]["oldVersion"] = "";
      otaIni[0]["errorString"] =  httpUpdate.getLastErrorString().c_str();
      otaIni[0]["errorCode"] =  errorCode;
      otaIni[0]["userRequested"] = "";
      otaIni.store();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      log_i("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      log_i("HTTP_UPDATE_OK");
      otaIni[0]["errorString"] =  "";
      otaIni[0]["errorCode"] = "";
      break;
    }

    if (rootCACertificate != NULL) {
      free(rootCACertificate);
    }
  }

  return false;
}

bool Ota::hasJustUpdated() {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  if (strcmp(otaIni[0].getValueSafe("hadJustUpdated", "0"), "yes") == 0) {
    log_d("We've just updated: [%s]", otaIni[0].getValueSafe("newVersion", "0"));

    if (strcmp(otaIni[0].getValueSafe("newVersion", "0"), otaIni[0].getValueSafe("oldVersion", "0")) == 0) {
      log_e("Failed an update: %s %s", otaIni[0].getValueSafe("newVersion", "0"), otaIni[0].getValueSafe("oldVersion", "0"));
      otaIni[0]["errorString"] =  "Rollback";
      otaIni[0]["errorCode"] = "-900";
      otaIni.store();
    }
    return true;
  }

  log_d("Not booted after update: [%s]", otaIni[0].getValueSafe("newVersion", "0"));
  return false;
}

bool Ota::commitUpdate() {
  log_d("Ota::commitUpdate");
  esp_ota_mark_app_valid_cancel_rollback();
  IniFile otaIni(getOtaIniFileName());
  otaIni.load();
  otaIni[0]["newVersion"] = "";
  otaIni[0]["oldVersion"] = "";
  otaIni[0]["errorString"] =  "";
  otaIni[0]["errorCode"] = "";
  otaIni[0]["hadJustUpdated"] = "";
  otaIni.store();
  return true;
}

void Ota::setIniUrl(const char* url) {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();
  otaIni[0]["serverIni"] = url;
  otaIni.store();
}

void Ota::backgroundUpdateCheck() {

}

void Ota::reset() {
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();
  otaIni[0]["errorCode"] = "";
  otaIni[0]["errorString"] = "";
  otaIni[0]["serverVersion"] = "";
  otaIni[0]["latestServerVersion"] = "";
  otaIni[0]["newVersion"] = "";
  otaIni[0]["hadJustUpdated"] = "";

  otaIni.store();
}

bool Ota::loadIniFile() {
  lastLoad_ = millis();
  CriticalFile otaIni(getOtaIniFileName());
  otaIni.load();

  const char* l = otaIni[0].getValueSafe("serverIni", "0");

  log_d("Ini location read from file: [%s]", l);

  if (strlen(l) > 4) {
    log_d("Setting initfileLocation");
    inifileLocation_ = std::string(l);
    iniLocation_ = inifileLocation_;
  }


  const char *hostname = getIniHost();
  std::string hname = std::string(hostname);


  const char *inipath = getIniPath();
  std::string path = std::string(inipath);

  char *request = (char*)extMalloc(8192);
  snprintf(request, 8192,
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Connection: close\r\n\r\n",
           inipath, hostname);

  free((void*)inipath);
  free((void*)hostname);

  log_d("Loading OTA ini file: [%s] [%s] [%s]", inifileLocation_.c_str(), hname.c_str(), path.c_str());
  log_d("Request is: [%s]", request);

  //WiFiClient client;
  WiFiClientSecure *client = new WiFiClientSecure;

  if (client == NULL) {
    log_e("Unable to allocate client");
    otaIni[0]["errorCode"] = "-300";
    otaIni[0]["errorString"] = "Can't alloc client";
    otaIni.store();
    return false;
  }

  char *rootCACertificate = (char*)extMalloc(CA_CERT_MAX_SZ);
  if (!loadRootCACert(rootCACertificate, CA_CERT_MAX_SZ)) {
    log_e("Unable to load cert file");
    return false;
  }

  client->setCACert(rootCACertificate);
  if (!client->connect(hname.c_str(), 443)) {
    log_e("Unable to connect to server");
    delete client;
    otaIni[0]["errorCode"] = "-301";
    otaIni[0]["errorString"] = "Can't connect to server";
    otaIni.store();
    return false;
  }

  log_d("After connected to server");

  client->print(request);
  unsigned long timeout = millis();
  while (client->available() == 0) {
    if (millis() - timeout > 15000) {
      log_e("Client Timeout !");
      client->stop();
      delete client;
      otaIni[0]["errorCode"] = "-302";
      otaIni[0]["errorString"] = "Timeout";
      otaIni.store();
      return false;
    }
  }

  free(request);

  log_d("Data available");

  bool content = false;
  std::string iniData = "";

  while (client->available()) {
    String l = client->readStringUntil('\r');
    log_d("Read: %s %d", l.c_str(), content);

    if (l.indexOf("404 Not Found") > 0) {
      otaIni[0]["errorCode"] = "-404";
      otaIni[0]["errorString"] = "Not found";
      otaIni.store();
      return false;
    }

    if (!content && l.length() < 2) {
      content = true;
    } else if (content) {
      iniData += std::string(l.c_str());
    }
  }

  log_d("Done reading");

  log_d("Ini file: %s", iniData.c_str());

  NanoIni::Config fwVersion(iniData.c_str());

  const char *svs = fwVersion[0].getValueSafe("version", "0");
  char *lvs = FIRMWARE_VERSION;

  int diff = compVersions(lvs, svs);
  const char* url = fwVersion[0].getValueSafe("url", "0");

  log_d("Diff is: %d, url: %s", diff, url);

  lastLoad_ = millis();
  {
    CriticalFile otaIni(getOtaIniFileName());
    otaIni.load();
    otaIni[0]["latestServerVersion"] = svs;
    otaIni[0]["errorCode"] = "";
    otaIni[0]["errorString"] = "";
    otaIni.store();
  }

  delete client;

  if (rootCACertificate != NULL) {
    free(rootCACertificate);
  }

  if (diff < 0) {
    log_i("Found a fimware update: %s to %s", lvs, svs);

    fwUrl_ = std::string(url);
    fwVersion_ = std::string(svs);

    CriticalFile otaIni = CriticalFile(getOtaIniFileName());
    otaIni.load();
    otaIni[0]["serverVersion"] = svs;
    otaIni[0]["latestServerVersion"] = svs;
    otaIni.store();

    return true;
  }

  return false;
}
