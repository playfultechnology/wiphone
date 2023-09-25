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

#ifndef _WIPHONE_TEST_H_
#define _WIPHONE_TEST_H_

/* Description:
 *     a collection of testing and experimental routines
 */

#include "FS.h"
#include "SD.h"
#include "SPIFFS.h"
#include "LinearArray.h"
#include "NanoINI.h"
#include "tinySIP.h"
#include "WiFiClient.h"
#include "src/ringbuff.h"
#include "Hardware.h"
#include "clock.h"
//#include "GUI.h"


void print_system_info();
void print_memory();
void test_cpu();
bool test_memory();
void test_ring_buffer();

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
bool createDir(fs::FS &fs, const char * path);
bool removeDir(fs::FS &fs, const char * path);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);

void testFileIO(fs::FS &fs, const char *path, int writeBlocks);
bool testFilesystem(fs::FS &fs, int writeBlocks);
bool test_sd_card(void);
void test_sd_card(int writeBlocks);
bool test_internal_flash(int writeBlocks);

void test_thread(void *pvParam);
void start_test_thread();
void tinySipUnitTest();
void test_wifi_info();

void test_http(void *pvParam);
void start_http_client();

void test_random();
bool easteregg_tests(char lastKeys[], bool anyPressed);


#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
void showRunTimeStats();
#endif

#endif // _WIPHONE_TEST_H_
