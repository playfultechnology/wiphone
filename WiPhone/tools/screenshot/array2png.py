#!/usr/bin/env python
# encoding: utf-8

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


import csv
from PIL import Image

'''
Takes a string representing a 5:6:5 16-bit image (each pixel a 16-bit hexadecimal string) and converts to png.

String is taken from "screenshot.txt" in the current working directory. Output to output.png.
'''

f = 'screenshot.txt'

csv.register_dialect('myDialect',
delimiter = ' ',
skipinitialspace=True)

lines = []
with open(f, 'rt') as r:
    csv_reader = csv.reader(r, dialect='myDialect')
    for line in csv_reader:
        line = [int(pixel, 16) for pixel in line]
        lines.append(line)
print( 'Data width: %s' % len(lines[-1]))
print( 'Data height: %s' % len(lines))

pixels = []
for line in lines:
    for p in line:
        # convert 5:6:5 to 8:8:8
        r = (p & int('0b1111100000000000', 2)) >> 11
        g = (p & int('0b0000011111100000', 2)) >> 5
        b = p & int('0b0000000000011111', 2)

        # doesn't work in python 3
        r = (r*256)/32
        g = (g*256)/64
        b = (b*256)/32

        p = (b << 16) + (g << 8) + r
        pixels.append(p)

im2 = Image.new('RGB', (240,320))
im2.putdata(pixels)
im2.save('output.png')
print('wrote output.png')

