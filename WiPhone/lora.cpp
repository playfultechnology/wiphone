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

#include "lora.h"
#include "GUI.h"

#define LORA_MESSAGE_MIN_LEN (sizeof(uint32_t) * 2) + (sizeof(uint8_t) * 2)
#define LORA_MESSAGE_MAGIC 0x6c6d
#define LORA_MAX_MESSAGE_LEN 230

typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint32_t to;
  uint32_t from;
  char     message[LORA_MAX_MESSAGE_LEN];
}
lora_message;

extern uint32_t chipId;
extern GUI gui;

Lora::Lora() {

}

void Lora::setup() {
  #ifdef LORA_MESSAGING
  log_i("Initialising LoRa: %d", ESP.getFreeHeap());
  loraSPI = new RHSoftwareSPI();
  rf95 = new RH_RF95(RFM95_CS, RFM95_INT, *loraSPI);

  loraSPI->setPins(HSPI_MISO, HSPI_MOSI, HSPI_SCLK);
  pinMode(RFM95_RST, OUTPUT);
  rf95->init();
  rf95->setFrequency(RF95_FREQ);
  rf95->setTxPower(23, false);

  log_v("Free memory after LoRa: %d %d", ESP.getFreeHeap(), heap_caps_get_free_size(MALLOC_CAP_32BIT));
  #endif
}

TextMessage* Lora::parse_message(const uint8_t *message, uint8_t len) {
  /**
   * Message format:
   * uint16_t - magic number (0x6c6d)
   * uint32_t - to address
   * uint32_t - from address
   * char*    - message text (null terminated string)
   */
  lora_message *msg = (lora_message*)message;

  if (len >= LORA_MESSAGE_MIN_LEN && msg->magic == LORA_MESSAGE_MAGIC) {
    if (msg->to != chipId) {
      if (msg->to > 0 ) {
        return NULL; // Message not for us
      }
    }

    char to_str[30] = {0}, from_str[30] = {0};
    snprintf(to_str, sizeof(to_str), "LORA:%X", msg->to);
    snprintf(from_str, sizeof(from_str), "LORA:%X", msg->from);

    log_i("LoRa message: to: %s from: %s msg: %s", to_str, from_str, msg->message);

    return new TextMessage(msg->message, from_str, to_str, 1604837104);
  }

  return NULL;
}

bool Lora::loop() {
  if (rf95->available()) {
    // Should be a message for us now
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN] = {0};
    uint8_t len = sizeof(buf);

    if (rf95->recv(buf, &len)) {
      TextMessage *msg = parse_message(buf, len);
      if (msg != NULL) {
        gui.flash.messages.saveMessage(msg->message, msg->from, msg->to, true, ntpClock.getUnixTime());    // time == 0 for unknown real time
        delete msg;
        return true;
      }
    } else {
      log_e("LoRa: Unable to receive data");
    }
  }

  return false;
}

int Lora::send_message(const char* to, const char* message) {
  if (strlen(message) > LORA_MAX_MESSAGE_LEN) {
    log_e("Unable to send LoRa message - too large: %d", strlen(message));
    return -1;
  }

  lora_message msg;
  msg.magic = LORA_MESSAGE_MAGIC;
  msg.from = chipId;
  msg.to = strtol(to, NULL, 16);
  strlcpy(msg.message, message, LORA_MAX_MESSAGE_LEN);

  rf95->send((uint8_t*)&msg, strlen(message)+LORA_MESSAGE_MIN_LEN+1);
  rf95->waitPacketSent();

  log_d("LoRa message sent to: %X from: %X", msg.to, msg.from);
}
