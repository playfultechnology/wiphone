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


#!/bin/bash

# Prerequisites:
# - exiftool
# - ImageMagik (convert)

FILENAME="$1"

if [ -e "$FILENAME" ]; then
  # Drop Adobe14 (JFIF cannot be added)
  exiftool -Adobe= "$FILENAME"
  
  # Convert all to JFIF
  exiftool "-jfif:all<all" "$FILENAME"
  
  # Drop EXIF and others, leave only JFIF
  exiftool -all= --jfif:all "$FILENAME"
  
  # Convert from progressive to baseline (and JFIF 1.02 to JFIF 1.01), don't convert to grayscale
  convert "$FILENAME" -interlace none -colorspace sRGB -type truecolor "$FILENAME"
else
  echo "Error: image \"$FILENAME\" does not exist"
fi
