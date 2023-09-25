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

#ifndef  LORA_H
#define LORA_H

#include "Hardware.h"
#include <SPI.h>
#include <RH_RF95.h>
#include <RHSoftwareSPI.h>
#include <RadioHead.h>
#include "tinySIP.h"


class Lora {
public:
  Lora();

  void setup();


  TextMessage* parse_message(const uint8_t *message, uint8_t len);
  bool loop();
  int send_message(const char* to, const char* message);

private:
  RHSoftwareSPI* loraSPI;
  RH_RF95* rf95;
};

#endif
