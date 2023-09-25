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

#ifndef DRV8833_H
#define DRV8833_H

#include "Arduino.h"

#define LEDC_LOW    170     // setting this to 0 makes the motor work at maximum speed
#define LEDC_HIGH   255

#define LEDC_CHANNEL_A1 1
#define LEDC_CHANNEL_A2 2
#define LEDC_CHANNEL_B1 3
#define LEDC_CHANNEL_B2 4

#define LEDC_FREQUENCY  20000
#define LEDC_RESOLUTION 8     // bit resolution for ledc functions of ESP32


class DRV8833 {
public:
  // Initialization
  void attachMotorA(unsigned char pinA1, unsigned char pinA2);
  void attachMotorB(unsigned char pinB1, unsigned char pinB2);

  // Motor control
  void motorAReverse();
  void motorAForward();
  void motorAStop();

  void motorBReverse();
  void motorBForward();
  void motorBStop();

private:
  bool motorAAttached = false;
  bool motorBAttached = false;
};

#endif // DRV8833_H
