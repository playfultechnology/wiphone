# Copyright © 2019, 2020, 2021, 2022 HackEDA, Inc.

# Licensed under the WiPhone Public License v.1.0 (the "License"); you
# may not use this file except in compliance with the License. You may
# obtain a copy of the License at
# https://wiphone.io/WiPhone_Public_License_v1.0.txt.

# Unless required by applicable law or agreed to in writing, software,
# hardware or documentation distributed under the License is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
# either express or implied. See the License for the specific language
# governing permissions and limitations under the License.


This directory contains the icons used in the WiPhone interface.

MAKE_ICONS.sh script converts all the required PNG icons into RLE3 format, converts those to
constant C-arrays and merges all of those together into the output file "icons.h". We prefer a
single header file to using actual files to allow WiPhone to work properly even if SPIFFS is absent.

Prerequisites:
- Python3
- OpenCV for Python3:
    python3 -m pip install opencv-python
- bash & awk (NOTE: we didn't test this on Windows)

Notes:
- RLE3 format is an embedded-friendly 3-bit run-length encoding format which allows only 8 colors.
  For description of the format see RLE3.py file, particularly description of the RLE3_encode function.
- not all PNG images can be converted into RLE3 format. Only two types of images actually can:
    1) the ones that have only a single color with different levels of transparency (up to 256);
    2) ones that have at most 8 colors (including transparency).
  If neither of those conditions hold, the RLE3.py script won't know how to reduce your file to 8 colors.

TODO:
- remember which files were converted already for faster compilation
