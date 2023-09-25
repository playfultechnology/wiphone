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

#include "clock.h"
#include "helpers.h"

#define LONG_TIME 0xffff

Clock ntpClock;

Clock::Clock(long timeOffsetSeconds) : timeOffsetSeconds(timeOffsetSeconds) {
  udpTime = new WiFiUDP();
  ntpServerIp = (uint32_t) 0;
  unixToHuman();

  mux = xSemaphoreCreateMutex();
  if (mux == NULL) {
    log_e("FAILED TO CREATED MUTEX");
  }
}

void Clock::startUpdates() {
  xTaskCreate(&Clock::thread, "ntp_thread", 8192, this, tskIDLE_PRIORITY, NULL);
}

void Clock::thread(void *pvParam) {
  Clock* clck = (Clock*) pvParam;

  clck->udpTime->begin(NTP_DEFAULT_LOCAL_PORT);
  while (1) {
    // TODO: increase delay, if there is no Internet
    bool updated = clck->update(millis());
    uint32_t delayMs = updated ? TIME_UPDATE_DELAY_MS : TIME_UPDATE_RETRY_DELAY_MS;
    vTaskDelay(delayMs / portTICK_PERIOD_MS);
  }

  clck->udpTime->stop();
  vTaskDelete(NULL);
}

// Description:
//     Initially written for cooperative multitasking, it either sends an NTP request and exits or checks for NTP response and exits.
bool Clock::update(const uint32_t& nowMillis) {
  // Check if previous request is still valid, if not - send a new one
  if (sentRequest && udpTime->parsePacket()<=0 && elapsedMillis(nowMillis, sentMillis, NTP_REQUEST_VALID_MS)) {
    sentRequest = false;  // declare old request invalid
  }

  // Send request if not sent already
  if (!sentRequest) {

    // Check if we already know the NTP server IP address
    if ((uint32_t)ntpServerIp==0 || elapsedMillis(nowMillis, lastDnsResolvedMillis, Clock::ipAddressValidMillis)) {        // update NTP server IP address every 24 hours or so
      // TODO: resolveDomain returns only one possible IP (`dig -t A pool.ntp.org` shows much more); change this and use multiple IPs
      // TODO: query multiple IPs from the pool to ensure correct time
      ntpServerIp = resolveDomain(defaultNtpServer);
      if ((uint32_t)ntpServerIp==0) {
        log_i("NTP: could not resolve domain");
        return false;
      }
      log_i("NTP: domain resolved");
      lastDnsResolvedMillis = nowMillis;
    }

    // Send NTP packet
    // Source: https://github.com/arduino-libraries/NTPClient/blob/master/NTPClient.cpp
    // Credits: (c) 2015, Fabrice Weinberg
    memset(ntpBuff, 0, sizeof(ntpBuff));
    // Initialize values needed to form NTP request
    ntpBuff [0] = 0b11100011;   // LI, Version, Mode
    ntpBuff [1] = 0;     // Stratum, or type of clock
    ntpBuff [2] = 6;     // Polling Interval
    ntpBuff [3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    ntpBuff[12] = 49;
    ntpBuff[13] = 0x4E;
    ntpBuff[14] = 49;
    ntpBuff[15] = 52;
    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    udpTime->beginPacket(ntpServerIp, NTP_REMOTE_PORT);
    udpTime->write(ntpBuff, NTP_PACKET_SIZE);
    udpTime->endPacket();
    log_i("NTP: request sent");

    sentRequest = true;
    sentMillis = nowMillis;
    return false;
  }

  // Request sent -> check if response received
  int cb = udpTime->read(ntpBuff, NTP_PACKET_SIZE);
  if (cb<=0) {
    return false;
  }

  // Response received
  // Extract time (since 1 January 1900)
  log_i("NTP: parsing response");
  uint32_t hi = word(ntpBuff[40], ntpBuff[41]);
  uint32_t lo = word(ntpBuff[42], ntpBuff[43]);
  uint32_t ntpTime = (hi << 16) | lo;
  log_i("%u from NTP, bytes = %d", ntpTime, cb);

  // Check for errors
  if (ntpTime==0) {
    return false;  //
  }
  if (ntpTime==lastNtpTime) {
    return false;  // server returned same time as before -> ignore it
  }

  // AVOID CONTEXT SWITCHES: updating time values
  if (xSemaphoreTake(mux, LONG_TIME) == pdTRUE) {

    // No errors detected
    updated = everUpdated = true;
    lastNtpTime = ntpTime;
    lastMillis = nowMillis;
    utcTime = ntpTime - SEVENTY_YEARS;          // UTC time in Unix epoch format (ntpTime starts at 1900, so at 1970 it would be 70 years)

    // Convert unix epoch into human-readable values
    unixToHuman();

    // Resume context switches
    xSemaphoreGive(mux);

  } else {
    log_e("failed to obtain clock mutex");
  }

  return true;
}

void Clock::unixToHuman() {
  time_t t = utcTime + timeOffsetSeconds;
  localtime_r(&t, &datetime);

  //log_v("Unix timestamp = %u", utcTime + timeOffsetSeconds);
  //log_v("%02d:%02d:%02d %02d-%02d-%04d", getHour(), getMinute(), getSecond(), getDay(), getMonth(), getYear());
}

void Clock::unixToHuman(uint32_t epoch, char* str) {    // static function
  struct tm dt;
  time_t t = epoch;
  localtime_r(&t, &dt);
  sprintf(str, "%04d-%02d-%02d %02d:%02d:%02d", dt.tm_year + 1900, dt.tm_mon+1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
}

/*
 * Update time based on the CPU milliseconds clock.
 * Should be called roungly once per minute after each NTP update.
 * If this is not called in a longer time, it will calculate time since last call and update clock accordingly.
 */
void Clock::minuteTick(const uint32_t& nowMillis) {
  log_v("Tick: %d millis %d %d", nowMillis - lastMillis, ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));

  // AVOID CONTEXT SWITCHES: updating time values
  bool locked = xSemaphoreTake(mux, LONG_TIME) == pdTRUE;
  if (!locked) {
    log_e("failed to obtain clock mutex");
  }

  // Calculate passed time, millis
  uint32_t passedMs = nowMillis;
  if (nowMillis >= lastMillis) {
    passedMs -= lastMillis;
  } else
    // Milliseconds overflow (happens once per 24.85 days)
  {
    passedMs += 0xFFFFFFFF - lastMillis;
  }

  // Propagate passed time
  utcTime += passedMs / 1000;
  extraMillis += passedMs % 1000;
  if (extraMillis >= 1000) {
    utcTime += extraMillis / 1000;
    extraMillis %= 1000;
  }
  lastMillis = nowMillis;

  unixToHuman();

  // Resume context switches
  if (locked) {
    xSemaphoreGive(mux);
  }
}

uint32_t Clock::getExactUtcTime() {
  // AVOID CONTEXT SWITCHES: updating time values
  bool locked = xSemaphoreTake(mux, LONG_TIME) == pdTRUE;
  if (!locked) {
    log_e("failed to obtain clock mutex");
  }

  // Calculate passed time, millis
  uint32_t passedMs = millis();
  if (passedMs >= lastMillis) {
    passedMs -= lastMillis;
  } else
    // Milliseconds overflow (happens once per 24.85 days)
  {
    passedMs += 0xFFFFFFFF - lastMillis;
  }
  uint32_t exactTime = utcTime + passedMs / 1000;

  // Resume context switches
  if (locked) {
    xSemaphoreGive(mux);
  }

  return exactTime;
}

void Clock::setTimeOffset(long offsetSeconds)  {
  // AVOID CONTEXT SWITCHES: updating time values
  bool locked = xSemaphoreTake(mux, LONG_TIME) == pdTRUE;
  if (!locked) {
    log_e("failed to obtain clock mutex");
  }

  timeOffsetSeconds = offsetSeconds;
  unixToHuman();

  // Resume context switches
  if (locked) {
    xSemaphoreGive(mux);
  }
}

const char* Clock::getMonth3(uint8_t month) {
  switch(month) {
  case 1:
    return "Jan";
  case 2:
    return "Feb";
  case 3:
    return "Mar";
  case 4:
    return "Apr";
  case 5:
    return "May";
  case 6:
    return "Jun";
  case 7:
    return "Jul";
  case 8:
    return "Aug";
  case 9:
    return "Sep";
  case 10:
    return "Oct";
  case 11:
    return "Nov";
  case 12:
    return "Dec";
  default:
    return "N/A";      // suppresses warning
  }

}

const char* Clock::getMonth3() {
  return Clock::getMonth3(getMonth());
}

/* Description:
 *     prints day and month into `s` in "DD MON" format (5-6 characters)
 */
void Clock::shortDate(const uint32_t& epochTime, char* s) {
  time_t tm = epochTime;
  struct tm tmTime;
  localtime_r(&tm, &tmTime);
  sprintf(s, "%d %s", tmTime.tm_mday, getMonth3(tmTime.tm_mon+1));
}

/* Description:
 *     prints day, month and year into `s` in "DD MON YYYY" format (10-11 characters)
 */
void Clock::longDate(const uint32_t& epochTime, char* s) {
  time_t tm = epochTime;
  struct tm tmTime;
  localtime_r(&tm, &tmTime);
  sprintf(s, "%d %s %d", tmTime.tm_mday, getMonth3(tmTime.tm_mon+1), tmTime.tm_year+1900);
}

/* Description:
 *      converts time zone into floating point time offset. Accepts input like "-02:30" and float values like "-2.5" (which are equivalent)
 * Return:
 *      false on failure; error message is in `error`
 *      true otherwise; parsed value in `res` - offset from UTC in hours (e.g., -2, -3.75, 6.5)
 */
bool Clock::parseTimeZone(const char* text, float& res, const char*& error) {
  if (!text || *text=='\0') {
    error = "Empty string: number expected";
    return false;
  }

  // Find the end: skip all the trailing spaces
  size_t len = strlen(text);
  const char *end = text + len - 1;
  while (isspace(*end))
    if (--end<text) {
      break;
    }
  end++;

  // Skip all the spaces and unimportant characters on the left
  text += strspn(text, " +\t");

  if (strchr(text, ':')) {

    // HH:MM format (e.g. +02:30)

    if (text && *text!='\0') {
      char* endptr;

      // Convert hours
      res = strtof(text, &endptr);
      if (*endptr!=':') {
        error = "Hours error";
        return false;
      }

      // Convert and add minutes
      float minutes = strtof(endptr+1, &endptr);
      if (endptr!=end) {
        error = "Minutes error";
        return false;
      }
      minutes /= 60;
      res += (res < 0) ? -minutes : minutes;

      return true;
    }

  } else {

    // Floating point format (e.g. +2.5)

    if (text && *text!='\0') {
      char* endptr;
      res = strtof(text, &endptr);
      if (endptr!=end) {
        error = "Input error: type an integer";
        return false;
      }
      return true;
    }

  }
  error = "Timezone error";
  return false;
}

/* Description:
 *     prints how long ago the supplied time point (`tm`) happened.
 *     Used in the MessagesApp.
 */
void Clock::dateTimeAgo(const uint32_t& tm, char* str) {
  str[0] = '\0';
  if (utcTime>=tm) {
    if (utcTime-tm < 60) {
      strcpy(str, "<1 min");
    } else if (utcTime-tm < 3600) {
      int val = (utcTime-tm)/60;
      sprintf(str, "%d min%s", val, val>1 ? "s" : "");
    } else if (utcTime-tm < 86400) {
      int val = (utcTime-tm)/3600;
      sprintf(str, "%d hour%s", val, val>1 ? "s" : "");
    } else if (utcTime-tm < 86400*30) {
      int val = (utcTime-tm)/86400;
      sprintf(str, "%d day%s", val, val>1 ? "s" : "");
    } else if (utcTime-tm < 86400*365) {
      shortDate(tm, str);
    } else {
      longDate(tm, str);
    }
  }
}
