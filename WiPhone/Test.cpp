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

#include "Test.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include <stdio.h>

#define TEST_BLOCKS_SD                4           // or 2048 for deeper test
#define TEST_BLOCKS_SPIFFS            4


double taylor_pi(int n) {
  // Taylor method for Pi approximation
  // Source: https://stackoverflow.com/a/32672747/5407270
  double sum = 0.0;
  int sign = 1;
  for (int i = 0; i < n; ++i) {
    sum += sign/(2.0*i+1.0);
    sign *= -1;
  }
  return 4.0*sum;
}

void print_system_info() {
  printf("System Info\r\n");
  printf(" - ESP32 SDK: %s\r\n", ESP.getSdkVersion());
  printf(" - CPU FREQ: %uMHz\r\n", getCpuFrequencyMhz());
  printf(" - APB FREQ: %0.1fMHz\r\n", getApbFrequency() / 1000000.0);
  printf(" - FLASH SIZE: %0.2fMB\r\n", ESP.getFlashChipSize() / (1024.0 * 1024));
  printf(" - RAM SIZE: %0.2fKB\r\n", ESP.getHeapSize() / 1024.0);
  printf(" - FREE RAM: %0.2fKB\r\n", ESP.getFreeHeap() / 1024.0);
  printf(" - MAX RAM ALLOC: %0.2fKB\r\n", ESP.getMaxAllocHeap() / 1024.0);
  printf(" - FREE PSRAM: %0.2fKB\r\n", ESP.getFreePsram() / 1024.0);
  // print uptime

  //size_t free_heap = esp_get_free_heap_size();
  //size_t free_iram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  //size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  //size_t free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
  //log_d("Free    total heap memory: %d", free_heap);
  //log_d("Free          IRAM memory: %d", free_iram);
  //log_d("Free  8bit-capable memory: %d", free_8bit);
  //log_d("Free 32bit-capable memory: %d", free_32bit);

  //log_d("Task Name - Status - Prio - HWM - Task - Affinity\n");
  //char stats_buffer[1024];
  //vTaskList(stats_buffer);
  //log_d("%s", stats_buffer);

  // vTaskGetRunTimeStats(stats_buffer);
  // log_d("%s", stats_buffer);

}

void print_memory(void) {
  printf("Memory Check\r\n");
  printf(" - Total: %0.2fKB\r\n - Internal: %0.2fKB\r\n - SPI RAM: %0.2fKB\r\n - DRAM: %0.2fKB\r\n",
         esp_get_free_heap_size() / 1024.0,
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT) / 1024.0,
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024.0,
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) / 1024.0;
}

void test_cpu() {
  printf("CPU test\r\n");
  for (int i=14; i<15; i++) {
    int time = millis();
    double pi = taylor_pi(exp(i));
    printf(" - %.11f %d %.1fs\r\n", pi, (int)exp(i), (millis()-time)/1000.);
  }
}

bool test_memory() {
  // Linear array expansion memory test
  bool success = true;
  LinearArray<char, LA_INTERNAL_RAM> arr;
  arr.add('a');
  arr.add('z');
  do {
    arr[arr.size() - 1] = '\0';
    if (arr.size() == 32768 || arr.size() == 2097152) {
      uint32_t hash = hash_murmur(&arr[0]);
      //printf(" - Hash(\"az...aza\") = 0x%08x (%d chars)\r\n", hash, arr.size() - 1);
      if (hash != 0x68e52bd8 && hash != 0xd94dfa9a) {
        success = false;
      }
    }
    arr[arr.size() - 1] = 'z';
  } while (arr.extend(&arr[0], arr.size()));
  printf("Memory Test: %s\r\n", success ? "Passed" : "Failed");
  return success;
}

void test_ring_buffer() {
  printf("RING BUFFER TEST\r\n");
  printf("================\r\n");

  RingBuffer<char> ring(5);
  char str[ring.capacity()];
  char *dyn;
  bool correct;

  // Test 1: a
  ring.put('a');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "a") && !ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 2: ab
  ring.put('b');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "ab") && !ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 3: abcd
  ring.put('c');
  ring.put('d');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "abcd") && !ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 4: abcde
  ring.put('e');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "abcde") && ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 5: abcdeZ
  ring.put('Z');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "abcde") && ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 6: bcdeZ
  ring.get();
  ring.put('Z');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "bcdeZ") && ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 7: ZYX
  ring.get();
  ring.put('Y');
  ring.get();
  ring.get();
  ring.put('X');
  ring.get();
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "ZYX") && !ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 8: YX
  ring.get();
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "YX") && !ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 9: YXab
  ring.put('a');
  ring.put('b');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "YXab") && !ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 10: cdXab
  ring.put('c');
  ring.get();
  ring.put('d');
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "Xabcd") && ring.full() && !ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 11: ""
  ring.get();
  ring.get();
  ring.get();
  ring.get();
  ring.get();
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "") && !ring.full() && ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  // Test 12: ""
  ring.put('c');
  ring.reset();
  ring.getCopy(str);
  dyn = ring.getCopy();
  correct = !strcmp(dyn, str) && !strcmp(str, "") && !ring.full() && ring.empty();
  printf("%4s: %s\r\n", correct ? "OK" : "FAIL", str);
  free(dyn);

  printf("================\r\n");
}


// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  TEST FILESYSTEMS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  log_d("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if(!root) {
    printf("Failed to open directory\r\n");
    return;
  }
  if(!root.isDirectory()) {
    printf("Not a directory\r\n");
    return;
  }

  File file = root.openNextFile();
  while(file) {
    if(file.isDirectory()) {
      printf("  DIR : ");
      printf("%s\r\n", file.name());
      if(levels) {
        listDir(fs, file.name(), levels -1);
      }
    } else {
      printf("  FILE: ");
      printf(file.name());
      printf("  SIZE: ");
      printf("%lukB\r\n", file.size() / 1024);
    }
    file = root.openNextFile();
  }
}

bool createDir(fs::FS &fs, const char * path) {
  log_d("Creating Dir: %s\r\n", path);
  bool result;
  if ((result = fs.mkdir(path))) {
    printf("Dir created\r\n");
  } else {
    printf("mkdir FAILED\r\n");
  }
  return result;
}

bool removeDir(fs::FS &fs, const char * path) {
  log_d("Removing Dir: %s\r\n", path);
  bool result;
  if ((result = fs.rmdir(path))) {
    printf("Dir removed\r\n");
  } else {
    printf("rmdir FAILED\r\n");
  }
  return result;
}

void readFile(fs::FS &fs, const char * path) {
  log_d("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file) {
    printf("Failed to open file for reading\r\n");
    return;
  }

  printf("Read from file: \r\n");
  while(file.available()) {
    printf("%c", file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  log_d("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    printf("Failed to open file for writing\r\n");
    return;
  }
  if(file.print(message)) {
    printf("File written\r\n");
  } else {
    printf("Write FAILED\r\n");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  log_d("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    printf("Failed to open file for appending\r\n");
    return;
  }
  if(file.print(message)) {
    printf("Message appended\r\n");
  } else {
    printf("Append FAILED\r\n");
  }
  file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  log_d("Renaming file %s to %s\r\n", path1, path2);
  if (fs.rename(path1, path2)) {
    printf("File renamed\r\n");
  } else {
    printf("Rename FAILED\r\n");
  }
}

void deleteFile(fs::FS &fs, const char * path) {
  log_d("Deleting file: %s\r\n", path);
  if(fs.remove(path)) {
    printf("File deleted\r\n");
  } else {
    printf("Delete FAILED\r\n");
  }
}

void testFileIO(fs::FS &fs, const char *path, int writeBlocks) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    printf("%u bytes read for %u ms\r\n", flen, end);
    file.close();
  } else {
    printf("Failed to open file for reading\r\n");
  }


  file = fs.open(path, FILE_WRITE);
  if (!file) {
    printf("Failed to open file for writing\r\n");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < writeBlocks; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  printf("%u bytes written for %u ms\r\n", writeBlocks * 512, end);
  file.close();
}

bool testFilesystem(fs::FS &fs, int writeBlocks) {
  listDir(fs, "/", 0);
  createDir(fs, "/mydir");
  listDir(fs, "/", 0);
  removeDir(fs, "/mydir");
  listDir(fs, "/", 2);
  writeFile(fs, "/hello.txt", "Hello ");
  appendFile(fs, "/hello.txt", "World!\r\n");
  readFile(fs, "/hello.txt");
  deleteFile(fs, "/foo.txt");
  renameFile(fs, "/hello.txt", "/foo.txt");
  readFile(fs, "/foo.txt");
  if (writeBlocks > 0) {
    testFileIO(fs, "/test.txt", writeBlocks);
  }
  return true;    // TODO
}

bool test_sd_card(void) {
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    SD.end();
    if (!SD.begin(SD_CARD_CS_PIN, SPI, SD_CARD_FREQUENCY)) {
      printf("SD: card mount failed\r\n");
      return false;
    }
  } else {
    cardType = SD.cardType();
  }
  //printf("SD: card mounted\r\n");
  if (cardType == CARD_NONE) {
    return false;
  }
  if (!(cardType == CARD_MMC || cardType == CARD_SD || cardType == CARD_SDHC)) {
    printf("SD: unrecognised card type\r\n");
    return false;
  }

  //printf("SD: card OK\r\n");
  if (!(SD.remove("/test.txt"))) {
    printf("SD: cannot delete test file\r\n");
    //return false;
  }
  File file = SD.open("/test.txt", FILE_WRITE);
  if(!file) {
    printf("SD: cannot open test file for writing\r\n");
    return false;
  }

  char buf[4];

  file.print("HI!");
  file.close();
  //file.seek(0);
  // seeking does not work, need to close and re-open for some reason
  file = SD.open("/test.txt");
  int i = 0;
  while(file.available()) {
    char c = file.read();
    buf[i] = c;
    i++;
  }
  buf[i] = 0;

  if (strcmp(buf, "HI!") != 0) {
    printf("SD: cannot read back same data\r\n");
    return false;
  }

  return true;
}

void test_sd_card(int writeBlocks) {
  printf("-------------------- SD card test --------------------\r\n");

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    printf("- error: no SD card attached");
    goto remount;
  } else {
    printf("- SD card type: ");
    if (cardType == CARD_MMC) {
      printf("MMC\r\n");
    } else if (cardType == CARD_SD) {
      printf("SDSC\r\n");
    } else if (cardType == CARD_SDHC) {
      printf("SDHC\r\n");
    } else {
      printf("UNKNOWN\r\n");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    printf("- SD card size: %lluMB\r\n", cardSize);
    printf("- total space: %lluMB\r\n", SD.totalBytes() / (1024 * 1024));
    printf("- used space: %lluMB\r\n", SD.usedBytes() / (1024 * 1024));

    if (!testFilesystem(SD, TEST_BLOCKS_SD)) {
      goto remount;
    }
  }
  printf("-------------------- ------------ --------------------\r\n");

  return;

remount:
  SD.end();
  if (!SD.begin(SD_CARD_CS_PIN, SPI, SD_CARD_FREQUENCY)) {
    printf("Card remount FAILED\r\n");
  } else {
    printf("Card remounted!\r\n");
  }
  return;
}

bool test_internal_flash(int writeBlocks) {
  printf("-------------------- Internal flash test --------------------\r\n");
  bool res = testFilesystem(SPIFFS, TEST_BLOCKS_SPIFFS);

  {
    // Test INI files
    IniFile iniFile("/counter.dat");
    iniFile.load();
    if (!iniFile.isEmpty()) {
      iniFile.show();
    } else {
      iniFile[0]["counter"] = "0";      // unsafe initialization
    }
    uint32_t cnt = atoi(iniFile[0].getValueSafe("counter", "0"));
    char buff[11];
    sprintf(buff, "%d", cnt + 1);
    iniFile[0]["counter"] = buff;
    if (!iniFile.isEmpty()) {
      iniFile.show();
      iniFile.store();
    }
  }

  {
    // Test INI files
    // TODO: remove this in production
    IniFile ini("/dummy.ini");
    ini.load();
    if (!ini.isEmpty() && ini.length() > 500000) {
      ini.remove();
    }
    int ns = ini.addSection();
    ini[ns]["greeting"] = "Hello, World!";
    ini.show();
    ini.store();
  }

  printf("-------------------- ------------ --------------------\r\n");

  return res;
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  TEST THREAD  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void test_thread(void *pvParam) {
  char pszNonce[] = "5aec56209ef1e575ebf23149fee3d257925d1d1b";
  char pszCNonce[] = "";
  char pszUser[] = "andriy";
  char pszRealm[] = "sip2sip.info";
  char pszPass[] = "secret";
  char pszAlg[] = "md5";
  char szNonceCount[9] = "";
  char pszMethod[] = "INVITE";
  char pszQop[] = "";
  char pszURI[] = "sip:echo@conference.sip2sip.info";

  HASHHEX HA1;
  HASHHEX HA2 = "";
  HASHHEX Response;

  DigestCalcHA1(pszAlg, pszUser, pszRealm, pszPass, pszNonce, pszCNonce, HA1);
  log_d("HA1 = %s", HA1);
  DigestCalcResponse(HA1, pszNonce, szNonceCount, pszCNonce, pszQop, pszMethod, pszURI, HA2, Response);
  log_d("Response = %s", Response);

  char test[] = "abcdefghijklmnopqrstuvwxyz01234567890";
  log_d("Test = %s", test);
  md5Compress(test, strlen(test), test);
  log_d("Hash = %s", test);

  const uint32_t sz = 65535 >> 1;
  char *testDyn1, *testDyn2, *testDyn3;
  //HASHHEX res;
  char *res;
  res = (char *) pvPortMalloc(33);

  // fill testDyn1, testDyn2, testDyn3
  testDyn1 = (char *) pvPortMalloc(sz + 1);
  if (testDyn1) {
    char c = 'a';
    for (int i = 0; i < sz; i++) {
      testDyn1[i] = c++;
      if (c > 'z') {
        c = 'a';
      }
    }
    testDyn1[sz] = '\0';
  } else {
    log_d("testDyn1: not inited");
  }

  testDyn2 = (char *) pvPortMalloc(sz + 1);
  if (testDyn2) {
    char c = 'a';
    for (int i = 0; i < sz; i++) {
      testDyn2[i] = c++;
      if (c > 'z') {
        c = 'a';
      }
    }
    testDyn2[sz] = '\0';
  } else {
    log_d("testDyn2: not inited");
  }

  testDyn3 = (char *) pvPortMalloc(sz + 1);
  if (testDyn3) {
    char c = 'a';
    for (int i = 0; i < sz; i++) {
      testDyn3[i] = c++;
      if (c > 'z') {
        c = 'a';
      }
    }
    testDyn3[sz] = '\0';
  } else {
    log_d("testDyn3: not inited");
  }

  // Continuous test
  uint32_t c = 0;
  while (1) {
    if (testDyn1) {
      memset(res, '\0', sizeof(res) - 1);
      md5Compress(testDyn1, strlen(testDyn1), res);
      if (strncmp(res, "6f1270a284aa3d42702d2b0f18afdc5b", HASHHEXLEN >> 1)) {
        log_d("ERROR: hash1 = %s", res);
        log_d("cnt = %d", c);
        break;
      }
    }

    if (testDyn2) {
      memset(res, '\0', sizeof(res) - 1);
      md5Compress(testDyn2, strlen(testDyn2), res);
      if (strcmp(res, "6f1270a284aa3d42702d2b0f18afdc5b")) {
        log_d("ERROR: hash2 = %s", res);
        log_d("cnt = %d", c);
        break;
      }
    }

    if (testDyn3) {
      memset(res, '\0', sizeof(res) - 1);
      md5Compress(testDyn3, strlen(testDyn3), res);
      if (strcmp(res, "6f1270a284aa3d42702d2b0f18afdc5b")) {      // 612e36c3369713772e6b59a4b7d24b54
        log_d("ERROR: hash3 = %s", res);
        log_d("cnt = %d", c);
        break;
      }
    }

//    // Draw random circles
//    uint32_t r = Random.random();
//    uint16_t col = r >> 3;
//    if (GETRED(col) < GETGREEN(col)) {
//      if (GETRED(col) < GETBLUE(col))
//        col |= RED;
//      else
//        col |= BLUE;
//    } else {
//      if (GETGREEN(col) < GETBLUE(col))
//        col |= GREEN;
//      else
//        col |= BLUE;
//    }
//    gui.circle(r % TFT_WIDTH, (r >> 8) % TFT_HEIGHT, ((r >> 16) % 64) + 1, col);      // TODO: remove macros

    c++;
    if (!(c % 10000)) {
      log_d("cnt = %d", c);
    }

  }

  log_d("freeing");
  if (res) {
    vPortFree(res);
  }
  if (testDyn1) {
    vPortFree(testDyn1);
  }
  if (testDyn2) {
    vPortFree(testDyn2);
  }
  if (testDyn3) {
    vPortFree(testDyn3);
  }


//  char test2[] = "abcdefghijklmnopqrstuvwxyz012345";
//  Serial.print("Test = ");
//  Serial.print(test2);
//  Serial.println();
//  md5Compress(test2, strlen(test2), test2);
//  Serial.print("Hash = ");
//  Serial.print(test2);
//  Serial.println();
//
//  Serial.print("Test = ");
//  Serial.print(test2);
//  Serial.println();
//  md5Compress(test2, strlen(test2), hash);
//  Serial.print("Hash = ");
//  Serial.print(hash);
//  Serial.println();

  vTaskDelete(NULL);
}

void start_test_thread() {
  log_d("Creating thread");
  UBaseType_t priority;
  //priority = 5;
  priority = tskIDLE_PRIORITY;
  xTaskCreate(&test_thread, "test_thread", 4096, NULL, priority, NULL);
  log_d("- done creating thread");
}

void tinySipUnitTest() {
#ifdef TINY_SIP_DEBUG
  TinySIP sip;
  sip.unitTest();
#endif // TINY_SIP_DEBUG
}

/* Description:
 *     retrieve WiFi information for device certification.
 */
void test_wifi_info() {
  printf("WiFi Info\r\n");
  // Development: get WiFi power
  int8_t power;
  if (esp_wifi_get_max_tx_power(&power) == ESP_OK) {
    printf(" - max. transmit power: %d\r\n", power);
  } else {
    printf(" - error: max. power not retrieved\r\n");
  }
  wifi_country_t country;
  if (esp_wifi_get_country(&country) == ESP_OK) {
    printf(" - country.cc: %s\r\n", country.cc);
    printf(" - country.nchan: %d\r\n", country.nchan);
    printf(" - country.schan: %d\r\n", country.schan);
  } else {
    printf(" - error: wifi country not retrieved");
  }
  uint8_t bitmap;
  if (esp_wifi_get_protocol(WIFI_IF_STA, &bitmap) == ESP_OK) {
    printf(" - WIFI_PROTOCOL_11B = %d\r\n", bitmap & WIFI_PROTOCOL_11B);
    printf(" - WIFI_PROTOCOL_11G = %d\r\n", bitmap & WIFI_PROTOCOL_11G);
    printf(" - WIFI_PROTOCOL_11N = %d\r\n", bitmap & WIFI_PROTOCOL_11N);
  } else {
    printf(" - error: wifi protocol not retrieved\r\n");
  }
  wifi_bandwidth_t wifi_bandwidth;
  if (esp_wifi_get_bandwidth(WIFI_IF_STA, &wifi_bandwidth) == ESP_OK) {
    printf(" - wifi bandwidth: %s\r\n", (wifi_bandwidth == WIFI_BW_HT20) ? "20" : (wifi_bandwidth == WIFI_BW_HT40) ? "40" : "unk");
  } else {
    printf(" - error: wifi bandwidth not retrieved\r\n");
  }
  wifi_sta_list_t sta_list;
  if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
    printf(" - phy_11b: %d\r\n", sta_list.sta[0].phy_11b);
    printf(" - phy_11g: %d\r\n", sta_list.sta[0].phy_11g);
    printf(" - phy_11n: %d\r\n", sta_list.sta[0].phy_11n);
    printf(" - phy_lr:  %d\r\n", sta_list.sta[0].phy_lr);
  } else {
    printf(" - error: AP sta_list not retrieved\r\n");
  }
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  HTTP CLIENT  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void test_http(void *pvParam) {
  const char host[] = "httpbin.org/get";

  // Show resolved IP address just for the sake of it
  IPAddress ipAddr = resolveDomain(host);
  if ((uint32_t) ipAddr != 0) {
    log_d("Resolved: %s", ipAddr.toString().c_str());
  }

  uint32_t cnt = 0;
  while(cnt < 4) {
    WiFiClient tcp;
    printf("HTTP: %d\r\n", ++cnt);
    if (tcp.connect(host, 80)) {
      printf("On the Web! Socket: %d\r\n", tcp.fd());

      // HTTP header
      tcp.print("GET / HTTP/1.1\r\n");
      tcp.print("Host: httpbin.org/get\r\n");
      tcp.print("User-Agent: tinySIP\r\n");
      tcp.print("Accept: text/html\r\n");
      tcp.print("\r\n");

      uint32_t nothing = 0;
      while (tcp.connected() & nothing < 1000) {
        vTaskDelay(10 / portTICK_PERIOD_MS);   // 10 ms wait
        int32_t avail, rcvd;
        uint8_t buff[1024];
        if ((avail = tcp.available()) > 0) {
          while (avail > 0) {
            rcvd = tcp.read(buff, 1023);
            if (rcvd <= 0) {
              break;
            }
            avail -= rcvd;
            Serial.print("TCP received: ");
            Serial.println(rcvd);
            char* p = strstr((const char*)buff, "\r\n");
            if (p > 0) {
              char buff2[1024];
              strncpy(buff2, (const char*)buff, (size_t)(p - (char *)&buff));
              buff2[(size_t)(p - (char *)&buff)] = '\0';
              printf("Line:\r\n%s\r\nFull:\r\n%s\r\n", buff2, (const char *) buff);
            } else {
              Serial.println("No CRNL found");
            }
          }
        } else {
          nothing++;
        }
      }
      Serial.println("HTTP - DONE");
    } else {
      Serial.println("HTTP - FAILED connection");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);   // 1 s wait
  }
  vTaskDelete(NULL);
}

void start_http_client() {
  if (wifiState.isConnected()) {
    xTaskCreate(&test_http, "test_http", 8192, NULL, tskIDLE_PRIORITY, NULL);
  } else {
    printf("WiFi not connected\r\n");
  }
}

// Test random numbers
void test_random() {
  int cnt = 1000;
  uint32_t res;
  uint32_t started;

  log_d("Random test:");

  started = millis();
  for (int i=0; i<cnt; i++) {
    res ^= Random.random();
  }
  log_d("%lu millis, res = %08x", millis()-started, res);
}

// Description:
//     Print run-time stats for threads to serial.
//     See the custom vTaskGetRunTimeStats example here:
//         https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/freertos.html
//         https://esp32.com/viewtopic.php?t=3674  (prints additional info)
//     In order for this to work on Arduino, need to add the flag:
//         -DCONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
//     to the `build.extra_flags` in platform.txt or boards.txt
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
void showRunTimeStats() {
  TaskStatus_t *pxTaskStatusArray;
  volatile UBaseType_t uxArraySize, i;
  uint32_t ulTotalRunTime;

  // Take a snapshot of the number of tasks in case it changes while this function is executing.
  uxArraySize = uxTaskGetNumberOfTasks();
  log_d("Tasks count: %lu", uxArraySize);

  // Allocate a TaskStatus_t structure for each task.
  pxTaskStatusArray = (TaskStatus_t *) pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
  if (pxTaskStatusArray != NULL) {
    // Generate raw status information about each task.
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

    // For each populated position in the pxTaskStatusArray array, print the data in human-readable form
    for (i=0; i<uxArraySize; i++) {
      float fStatsAsPercentage = ulTotalRunTime > 0 ? (float) pxTaskStatusArray[i].ulRunTimeCounter * 100 / ulTotalRunTime : 0.0;
      log_d("%s\t\t%lu\t\t%.1f%%", pxTaskStatusArray[i].pcTaskName,
            pxTaskStatusArray[i].ulRunTimeCounter,
            fStatsAsPercentage);
    }

    // Free the dynamically allocated memory
    vPortFree(pxTaskStatusArray);
  }
}
#endif

bool easteregg_tests(char lastKeys[], bool anyPressed) {
  if (!memcmp(lastKeys + 2, "001**", 5)) {    // **100##   +
    log_d("Easter egg = 100: starting an HTTP client");
    start_http_client();
  } else if (!memcmp(lastKeys + 2, "901**", 5)) {    // **109##
    log_d("Easter egg = 109: SD card test");
    test_sd_card(0);
  } else if (!memcmp(lastKeys + 2, "011**", 5)) {    // **110##
    log_d("Easter egg = 110: Internal flash test");
    test_internal_flash(4);
  } else if (!memcmp(lastKeys + 2, "111**", 5)) {    // **111##   +
    log_d("Easter egg = 111: tinySIP unit test");
    tinySipUnitTest();
  } else if (!memcmp(lastKeys + 2, "211**", 5)) {    // **112##
    log_d("Easter egg = 112: memory test");
    test_memory();
  } else if (!memcmp(lastKeys + 2, "311**", 5)) {    // **113##
    log_d("Easter egg = 113: test CPU");
    test_cpu();
  } else if (!memcmp(lastKeys + 2, "411**", 5)) {    // **114##
    log_d("Easter egg = 114: wifi info");
    test_wifi_info();
  } else if (!memcmp(lastKeys + 2, "321**", 5)) {    // **123##   +
    log_d("Easter egg = 123: starting a test thread");
    start_test_thread();
    anyPressed = false;     // this Easter egg updates the screen -> do not update the screen
  }
  return anyPressed;
}
