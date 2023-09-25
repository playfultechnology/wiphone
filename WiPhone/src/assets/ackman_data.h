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

// Inspired by original Pac-Man level, which is 26x29.
// This one is slightly different, because it is 23x26.

const char* ackmanLevels[] PROGMEM =  {
  "..........XXX.........."
  ".XXX.XXXX.XXX.XXXX.XXX."
  "oXXX.XXXX.XXX.XXXX.XXXo"
  ".XXX.XXXX.XXX.XXXX.XXX."
  "......................."
  ".XXX.XX.XXXXXXX.XX.XXX."
  ".XXX.XX.XXXXXXX.XX.XXX."
  ".....XX...XXX...XX....."
  "XXXX.XXXX XXX XXXX.XXXX"
  "XXXX.XXXX XXX XXXX.XXXX"
  "XXXX.XX    B    XX.XXXX"
  "XXXX.XX XX---XX XX.XXXX"
  "XXXX.XX XR    X XX.XXXX"
  "<   ... X XMX X ...   >"
  "XXXX.XX X    SX XX.XXXX"
  "XXXX.XX XXXXXXX XX.XXXX"
  "..........XXX.........."
  ".XXX.XXXX.XXX.XXXX.XXX."
  ".XXX.XXXX.XXX.XXXX.XXX."
  "o..X...... P ......X..o"
  "XX.XXXX.XXXXXXX.XXXX.XX"
  "XX.XXXX.XXXXXXX.XXXX.XX"
  "..........XXX.........."
  ".XXXXXXXX.XXX.XXXXXXXX."
  ".XXXXXXXX.XXX.XXXXXXXX."
  ".......................",

  "<...XX....XXX....XX...>"
  "XXX.XX.XX.XXX.XX.XX.XXX"
  "XXX.XX.XX.XXX.XX.XX.XXX"
  ".......XX.XXX.XX......."
  ".XX.XX.XX.....XX.XX.XX."
  "oXX.XX.XXXXXXXXX.XX.XXo"
  ".XX.XX.... P ....XX.XX."
  "....XXXXX.XXX.XXXXX...."
  ".XXXXXXXX.XXX.XXXXXXXX."
  ".XXXXXXXX.XXX.XXXXXXXX."
  ".....XXXX.XXX.XXXX....."
  ".XXX.XXXX.XXX.XXXX.XXX."
  ".XXX...............XXX."
  ".XXX.XX.XXX.XXX.XX.XXX."
  ".XXX.XX.XXX.XXX.XX.XXX."
  ".XXX.XX    B    XX.XXX."
  ".....XX XXX-XXX XX....."
  "XX.XXXX XR M SX XXXX.XX"
  "XX.XXXX XXXXXXX XXXX.XX"
  "{ .....         ..... }"
  "XX.XX.XXXXX.XXXXX.XX.XX"
  "XX.XX.XXXXX.XXXXX.XX.XX"
  "o..XX.............XX..o"
  ".XXXX.XX.XXXXX.XX.XXXX."
  ".XXXX.XX.XXXXX.XX.XXXX."
  "......................."
};
