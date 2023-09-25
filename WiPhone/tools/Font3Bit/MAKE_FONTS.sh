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

VLW_FONTS_DIR="../../src/TFT_eSPI/Tools/Create_Smooth_Font/FontFiles"

declare -a array=(
    "OpenSans-CondBold20"
    "Akrobat-Bold16"
    "Akrobat-Bold18"
    "Akrobat-Bold20"
    "Akrobat-Bold22"
    "Akrobat-Bold24"
    "Akrobat-SemiBold20"
    "Akrobat-SemiBold22"
    "Akrobat-ExtraBold22"
    "Akrobat-Bold32"
    "Akrobat-Bold90"
)

echo "===================== VLW -> 7SF ====================="
cnt=${#array[@]}
for (( i=0; i<${cnt}; i++ ));
do
    echo "Converting ${array[$i]}.vlw"
    if test -f "$VLW_FONTS_DIR/${array[$i]}.vlw"; then
        ./vlw_to_7sf.py "$VLW_FONTS_DIR/${array[$i]}.vlw" "${array[$i]}.7sf"
    else
        echo "$VLW_FONTS_DIR/${array[$i]}.vlw does not exist"
    fi
done

echo "===================== 7SF -> H ====================="
for (( i=0; i<${cnt}; i++ ));
do
    echo "Converting ${array[$i]}.7sf"
    if test -f "${array[$i]}.7sf"; then
        ./to_c.py "${array[$i]}.7sf" "${array[$i]}.h"
        awk -v NAME="${array[$i]}" 'NR==1 && $0 !~ "PROGMEM" { print ""; gsub(/data/, NAME); gsub(/-/,"_"); $4 = $4 " PROGMEM" } 1' "${array[$i]}.h" > tmp.h
        mv tmp.h "${array[$i]}.h"
    else
        echo "${array[$i]}.7sf does not exist"
    fi
done

echo "===================== CONCATENATING ====================="
h_files=""
for (( i=0; i<${cnt}; i++ ));
do
    if test -f "${array[$i]}.h"; then
        h_files="$h_files ${array[$i]}.h"
    else
        echo "ERROR: ${array[$i]}.h does not exist"
    fi
done

echo "$h_files"
echo "// Collection of fonts in 7SF (7-Shade Font) format" > fonts.h
cat $h_files >> fonts.h
