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

/*
 * DRV8833 for ESP32 - a class for controlling the DRV8833 dual motor H-bridge
 *   driver by Texas Instruments.
 *
 * Inspired by the DRV8833 library by Aleksandr J. Spackman, 2015.
 *
 */

#include "Arduino.h"
#include "DRV8833.h"

void DRV8833::attachMotorA(unsigned char pinA1, unsigned char pinA2) {
  if (!this->motorAAttached) {
    ledcAttachPin(pinA1, LEDC_CHANNEL_A1);
    ledcAttachPin(pinA2, LEDC_CHANNEL_A2);
    ledcSetup(LEDC_CHANNEL_A1, LEDC_FREQUENCY, LEDC_RESOLUTION);
    ledcSetup(LEDC_CHANNEL_A2, LEDC_FREQUENCY, LEDC_RESOLUTION);

    // Initialize
    ledcWrite(pinA1, LEDC_LOW);
    ledcWrite(pinA2, LEDC_LOW);

    this->motorAAttached = true;
  }
}

void DRV8833::attachMotorB(unsigned char pinB1, unsigned char pinB2) {
  if (!this->motorBAttached) {
    ledcAttachPin(pinB1, LEDC_CHANNEL_B1);
    ledcAttachPin(pinB2, LEDC_CHANNEL_B2);
    ledcSetup(LEDC_CHANNEL_B1, LEDC_FREQUENCY, LEDC_RESOLUTION);
    ledcSetup(LEDC_CHANNEL_B2, LEDC_FREQUENCY, LEDC_RESOLUTION);

    // Initialize
    ledcWrite(pinB1, LEDC_LOW);
    ledcWrite(pinB2, LEDC_LOW);

    this->motorBAttached = true;
  }
}

void DRV8833::motorAReverse() {
  if (this->motorAAttached) {
    ledcWrite(LEDC_CHANNEL_A1, LEDC_LOW);
    ledcWrite(LEDC_CHANNEL_A2, LEDC_HIGH);
  }
}

void DRV8833::motorAForward() {
  if (this->motorAAttached) {
    ledcWrite(LEDC_CHANNEL_A1, LEDC_HIGH);
    ledcWrite(LEDC_CHANNEL_A2, LEDC_LOW);
  }
}

void DRV8833::motorAStop() {
  if (this->motorAAttached) {
    ledcWrite(LEDC_CHANNEL_A1, LEDC_HIGH);
    ledcWrite(LEDC_CHANNEL_A2, LEDC_HIGH);
  }
}

void DRV8833::motorBReverse() {
  if (this->motorBAttached) {
    ledcWrite(LEDC_CHANNEL_B1, LEDC_LOW);
    ledcWrite(LEDC_CHANNEL_B2, LEDC_HIGH);
  }
}

void DRV8833::motorBForward() {
  if (this->motorBAttached) {
    ledcWrite(LEDC_CHANNEL_B1, LEDC_HIGH);
    ledcWrite(LEDC_CHANNEL_B2, LEDC_LOW);
  }
}

void DRV8833::motorBStop() {
  if (this->motorBAttached) {
    ledcWrite(LEDC_CHANNEL_B1, LEDC_HIGH);
    ledcWrite(LEDC_CHANNEL_B2, LEDC_HIGH);
  }
}

