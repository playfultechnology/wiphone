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

#ifndef _NTP_CLOCK_
#define _NTP_CLOCK_

#include <time.h>
#include "config.h"
#include "Networks.h"

#define NTP_PACKET_SIZE         48
#define NTP_DEFAULT_LOCAL_PORT  1337
#define NTP_REMOTE_PORT         123
#define SEVENTY_YEARS           2208988800UL

class Clock {
public:
  Clock(long timeZoneOffset = DEFAULT_TIME_OFFSET);

  void setTimeZone(float tz)                  {
    setTimeOffset(tz*ONE_HOUR_IN_SECONDS);
  }

  void begin(int port = NTP_DEFAULT_LOCAL_PORT);
  void sendNtpPacket();
  bool update(const uint32_t& nowMillis);
  void minuteTick(const uint32_t& nowMillis);

  bool isTimeKnown()      {
    return everUpdated;
  }
  bool isUpdated()        {
    bool res = updated;
    updated = false;
    return res;
  }

  uint8_t getHour()       {
    return datetime.tm_hour;
  }
  uint8_t getMinute()     {
    return datetime.tm_min;
  }
  uint8_t getSecond()     {
    return datetime.tm_sec + (millis() - lastMillis) / 1000 % 100;
  }

  uint8_t  getDay()       {
    return datetime.tm_mday;
  }
  uint8_t  getMonth()     {
    return datetime.tm_mon+1;
  }
  uint16_t getYear()      {
    return datetime.tm_year+1900;
  }

  uint32_t getUtcTime()   {
    return utcTime;
  }
  uint32_t getUnixTime()  {
    return utcTime + timeOffsetSeconds;
  }

  uint32_t getExactUtcTime();
  uint32_t getExactUnixTime() {
    return getExactUtcTime() + timeOffsetSeconds;
  }

  static const char* getMonth3(uint8_t month);    // three-letter month name
  const char* getMonth3();                        // three-letter month name

  static void shortDate(const uint32_t& epochTime, char* s);
  static void longDate(const uint32_t& epochTime, char* s);
  static bool parseTimeZone(const char* text, float& res, const char*& error);
  void dateTimeAgo(const uint32_t& utcTime, char* s);

  static void unixToHuman(uint32_t epochTime, char* str);

  void hello();
  void startUpdates();

protected:
  static constexpr const char* defaultNtpServer = "pool.ntp.org";
  static const uint32_t ipAddressValidMillis = 86400000;  // 86400000ms is 24 hours
  static const uint32_t NTP_REQUEST_VALID_MS = 2500;

  WiFiUDP*      udpTime = NULL;                 // socket for UDP
  IPAddress     ntpServerIp;                    // IP resolved for the default NTP server

  SemaphoreHandle_t mux = NULL;                 // lock for making the clock updates atomic

  /* NOTE: the following attributes are modified and used in both the updating thread and time retrieval functions:
   *    - timeOffsetSeconds     - modified in setTimeOffset()
   *    - utcTime               - modified in minuteTick(), update()
   *    - datetime              - modified in unixToHuman()
   *    - lastMillis            - modified in minuteTick(), update()
   *  They should be modified atomically to ensure consistency with each other.
   */

  bool          sentRequest = false;            // are we waiting for NTP server's response?
  bool          everUpdated = false;            // did we ever get a response from NTP server?
  bool          updated = false;                // updated recently?
  uint32_t      timeOffsetSeconds = 0;          // timezone offset

  uint32_t      utcTime = 0;                    // inferred current time (system clock time), updated only every minute or so
  struct tm     datetime;                       // calendar date and time broken down into its components
  uint32_t      lastNtpTime = 0;                // last good NTP timestamp received from the server
  byte          ntpBuff[NTP_PACKET_SIZE];       // buffer: allocated statically to ensure that there is always enough memory for this

  // CPU clock's millis() values:

  uint32_t      lastMillis = 0;                 // last time `utcTime` variable was changed
  uint32_t      extraMillis = 0;                // accumulated delay for the minuteTick() function - this will be added to the time to make the clock more precise
  uint32_t      sentMillis;                     // when was the last request sent to the NTP server?
  uint32_t      lastDnsResolvedMillis;          // when was the last time the NTP server domain got resolved?

  void          unixToHuman();                  // convert unix epoch into human-readable values (simply converts into "struct tm")

  static void  thread(void *pvParam);

  void  setTimeOffset(long offsetSeconds);
};

extern Clock ntpClock;

#endif // _NTP_CLOCK_
