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

#ifndef __CONFIG_H
#define __CONFIG_H

#define WIPHONE_PRODUCTION          // configure code for production, not development and testing

//#define TEST_DB_PINS                // this requires USER_SERIAL to be not defined, or not used.
// later need to only configure the daughterboard UART when we are actually using it, and disable it when not using

#ifndef WIPHONE_PRODUCTION
//#define BATTERY_BLINKING_OFF        // turn off battery blinking while charging (useful in debugging)
#define DIAGNOSTICS_ONLY            // limit the UI to the Diagnostics screen
//#define STEAL_THE_USER_BUTTONS      // this will switch between 2 different sets of definitions for the user button functions
#endif

#define FIRMWARE_VERSION "0.8.30"

#define BUILD_GAMES

#define IF_LOG(level)                       if (ARDUHAL_LOG_LEVEL >= ESP_LOG_ ## level || LOG_LOCAL_LEVEL >= ESP_LOG_ ## level)
#define LOG_MEM_STATUS printf("Free Memory: %d\r\n", ESP.getFreeHeap())

// hopefully you can find this instead of spending 45 minutes (for the second time) figuring out how to set the log level:
// In the Arduino IDE, choose from the dropdown located at: Tools>Core Debug Level
//
// some keywords: ESP_LOGE ESP_LOGW ESP_LOGI ESP_LOGD ESP_LOGV esp_log_level_set esp_log.h CONFIG_LOG_DEFAULT_LEVEL


/* ================== Timings ================== */

#define KEYPAD_IDLE_MS                2000u       // 2 s
#define KEYPAD_LEDS_ON_MS             5000u       // 5 s
#define HANGUP_TIMEOUT_MS             5000u       // 5 s
#define BATTERY_CHECK_PERIOD_MS       15000u      // 15 s
#define USB_CHECK_PERIOD_MS           750u        // 0.75 s - this is also the the period of blinking for charging icon
#define WIFI_CHECK_PERIOD_MS          2000u       // 2 s
#define TIME_UPDATE_DELAY_MS          600000u     // 10 minutes: after what time to connect to NTP server to update the clock?
#define TIME_UPDATE_RETRY_DELAY_MS    500u        // 0.5 s: after what time to check back for a reply from NTP server?
#define TIME_UPDATE_MINUTE_MS         60000u      // 1 minute: how often to update system clock and generate TIME_UPDATE_EVENT (must be 1 minute, unless you know what you are doing)
#define WIFI_RETRY_PERIOD_MS          20000u      // 20 s


/* ================== Keyboard constants ================== */

#define WIPHONE_SHIFT_KEY     '#'
#define WIPHONE_SYMBOLS_KEY   '*'
#define WIPHONE_UNLOCK_KEY2   '*'

// Key codes
#define WIPHONE_KEY_BACK    8       // top right button
#define WIPHONE_KEY_OK      12      // button in the middle
#define WIPHONE_KEY_UP      17
#define WIPHONE_KEY_LEFT    18
#define WIPHONE_KEY_RIGHT   19
#define WIPHONE_KEY_DOWN    20
#define WIPHONE_KEY_SELECT  21      // top left button
#define WIPHONE_KEY_CALL    22      // below SELECT (on the left side)
#define WIPHONE_KEY_END     23      // below BACK (on the right side)
#define WIPHONE_KEY_F1      24
#define WIPHONE_KEY_F2      25
#define WIPHONE_KEY_F3      26
#define WIPHONE_KEY_F4      27

/* ================== Timezone ================== */

//#define TIME_OFFSET_UTC             0           // Coordinated Universal Time
//#define TIME_OFFSET_UTC_PLUS_1      3600        // Europe/Berlin, EU time
//#define TIME_OFFSET_UTC_PLUS_8      28800       // Asia/Shanghai, China time
//#define TIME_OFFSET_UTC_MINUS_5     18000       // America/New_York, winter time
//#define TIME_OFFSET_UTC_MINUS_6     21600       // America/Chicago, winter time
//#define TIME_OFFSET_UTC_MINUS_8     -28800      // America/Los_Angeles, West Coast winter time

#define ONE_HOUR_IN_SECONDS         3600
#define DEFAULT_TIME_OFFSET         (0 * ONE_HOUR_IN_SECONDS)    // UTC+0

//#define UDP_SIP

#endif // __CONFIG_H
