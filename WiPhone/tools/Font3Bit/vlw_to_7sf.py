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


#!/usr/bin/env python3

import struct
from sys import argv
from RLE3 import rle_3bit_data, run_encode_number

SHOW_GLYPHS = 1

# 7SF (7-Shade Font, 3-bit antialised run-length encoded font format)

## VLW font format (description by Bodmer):
## 
##    The vlw font format does not appear to be documented anywhere, so some reverse
##    engineering has been applied!
##
##    Header of vlw file comprises 6 uint32_t parameters (24 bytes total):
##      1. The gCount (number of character glyphs)
##      2. A version number (0xB = 11 for the one I am using)
##      3. The font size (in points, not pixels)
##      4. Deprecated mboxY parameter (typically set to 0)
##      5. Ascent in pixels from baseline to top of "d"
##      6. Descent in pixels from baseline to bottom of "p"
##
##    Next are gCount sets of values for each glyph, each set comprises 7 int32t parameters (28 bytes):
##      1. Glyph Unicode stored as a 32 bit value
##      2. Height of bitmap bounding box
##      3. Width of bitmap bounding box
##      4. gxAdvance for cursor (setWidth in Processing)
##      5. dY = distance from cursor baseline to top of glyph bitmap (signed value +ve = up)
##      6. dX = distance from cursor to left side of glyph bitmap (signed value -ve = left)
##      7. padding value, typically 0
##
##    The bitmaps start next at 24 + (28 * gCount) bytes from the start of the file.
##    Each pixel is 1 byte, an 8 bit Alpha value which represents the transparency from
##    0xFF foreground colour, 0x00 background. The sketch uses a linear interpolation
##    between the foreground and background RGB component colours. e.g.
##        pixelRed = ((fgRed * alpha) + (bgRed * (255 - alpha))/255
##    To gain a performance advantage fixed point arithmetic is used with rounding and
##    division by 256 (shift right 8 bits is faster).
##
##    After the bitmaps is:
##       1 byte for font name string length (excludes null)
##       a zero terminated character string giving the font name
##       1 byte for Postscript name string length
##       a zero/one terminated character string giving the font name
##       last byte is 0 for non-anti-aliased and 1 for anti-aliased (smoothed)
##
##    Then the font name seen by Java when it's created
##    Then the postscript name of the font
##    Then a boolean to tell if smoothing is on or not.


def get32(data, offset):
    r  = ord(data[offset])<<24
    r |= ord(data[offset+1])<<16
    r |= ord(data[offset+2])<<8
    r |= ord(data[offset+3])

index_colors = [int(round(255./7*x)) for x in range(8)]
print(index_colors)
def color8to3(color8, palette):
    c3, best = 0, abs(color8-palette[0])
    c = 1       # index of color
    for pc in palette[1:]:
        score = abs(pc-color8)
        if score <= best:
            c3, best = c, score
        c += 1
    return c3

def rle_3bit_string(stream):
    res = b''
    for ind, cnt in stream:
        res += rle_3bit_data(ind, cnt)
    return res

read32 = lambda f: struct.unpack('>i', f.read(4))[0]

def quantize(colors):
    '''Apply median cut technique'''
    colors.sort()
    # Trim black and white
    white = white0 = colors.count(colors[-1])
    black = black0 = colors.count(colors[0])
    while True:
        ln = len(colors)
        if white > black:
            if white > len(colors)//8:
                colors.pop()
                white -= 1
        else:
            if black > len(colors)//8:
                colors.pop(0)
                black -= 1
        if len(colors) == ln:
            break
    print('Trimmed white:', white0-white)
    print('Trimmed black:', black0-black)
    # 
    palette = []
    def rec(div, depth):
        #nonlocal palette
        if depth==3:
            palette.append(float(sum(div))/len(div))
        else:
            m = len(div)//2
            rec(div[:m], depth+1)
            rec(div[m:], depth+1)
    rec(colors, 0)
    palette = [int(round(x)) for x in palette]
    print('Palette:', palette)
    return palette

def convert(input_fn, output_fn):
    '''Convert VLW font file to 7SF'''
    with open(input_fn, 'rb') as inf, open(output_fn, 'wb') as ouf:
        gCount = read32(inf)
        version = read32(inf)
        fontSize = read32(inf)
        mboxY = read32(inf)
        ascent = read32(inf)
        descent = read32(inf)
        ab = 0
        bb = 0

        ouf.write(b'7SF')
        ouf.write(run_encode_number(gCount))
        ouf.write(run_encode_number(fontSize))
        ouf.write(run_encode_number(ascent))
        ouf.write(run_encode_number(descent))

        print('Character count = {}'.format(gCount))
        print('Version = {}'.format(version))
        print('Font size = {} pt'.format(fontSize))

        gData = []
        for i in range(gCount):
            glyph = {}
            glyph['unicode'] = inf.read(4)
            glyph['height'] = read32(inf)
            glyph['width'] = read32(inf)
            glyph['gxAdvance'] = read32(inf)
            glyph['dY'] = read32(inf)
            glyph['dX'] = read32(inf)
            glyph['padding'] = read32(inf)
            gData.append(glyph)

            ab = max(ab, glyph['dY'])
            bb = max(bb, glyph['height'] - glyph['dY'])

            ouf.write(bytes(glyph['unicode'][3:4]))
            ouf.write(bytes(glyph['unicode'][2:3]))
            ouf.write(bytes(glyph['unicode'][1:2]))
            ouf.write(bytes(glyph['unicode'][0:1]))
            ouf.write(run_encode_number(glyph['height']))
            ouf.write(run_encode_number(glyph['width']))
            ouf.write(run_encode_number(glyph['gxAdvance']))
            ouf.write(run_encode_number(glyph['dY'] if glyph['dY']>=0 else 256+glyph['dY']))
            ouf.write(run_encode_number(glyph['dX'] if glyph['dX']>=0 else 256+glyph['dX']))

        # Read all glyphs
        all_colors = []
        for glyph in gData:
            n = glyph['height'] * glyph['width']
            glyph['data'] = data = inf.read(n)
            for c in data:
                all_colors.append(c)

            if SHOW_GLYPHS:
                print(glyph['unicode'], repr(glyph['unicode']), glyph['height'], glyph['width'], \
                      glyph['gxAdvance'], glyph['dX'], glyph['dX'])
                c = 0
                for y in range(glyph['height']):
                    for x in range(glyph['width']):
                        if data[c]==0:
                            print(' ',end='')
                        elif data[c]==255:
                            print('#',end='')
                        else:
                            print('.',end='')
                        c += 1
                    print()

        # Quantize colors and write the palette
        print('Min color = ', min(all_colors))
        print('Max color = ', max(all_colors))
        print('Index colors = ', index_colors)
        palette = quantize(all_colors)
        print('5-bit palette = ', [x>>3 for x in palette])

        # Push palette colors up one step (optimization for dim High Color display)
        # - Ensure all colors are different
        palette2 = palette[:]
        same = False
        for c in range(1,len(palette2)):
            while (palette2[c-1]>>3)>=(palette2[c]>>3) and (palette2[c]>>3)+1<32:
                palette2[c] += 8
                same = True
        for c in range(len(palette2)-2, -1, -1):
            while (palette2[c]>>3)>=(palette2[c+1]>>3):
                palette2[c] -= 8
                same = True
        if same:
            print('5-bit palette2 = ', [x>>3 for x in palette2])
        # - Push each color up for displaying purposes
        palette3 = palette2[:]
        max_push = 1
        c = len(palette3)-1
        while c>0:
            push = max_push
            pushed = 0
            while push>0 and palette3[c]+8<=255 and (c+1>=len(palette3) or (palette3[c]>>3)+1 < (palette3[c+1]>>3)):
                palette3[c] += 8
                pushed += 1
                push -= 1
            if pushed and (palette3[c-1]>>3)<=20:
                print('Pushed:', pushed)
                max_push += 1
            c -= 1
        print('Pushed 5-bit palette3 = ', [x>>3 for x in palette3])

        # Write palette
        ouf.write(bytes([len(palette3)]))
        for c in palette3:
            ouf.write(bytes([c]))

        # Write all glyphs
        for glyph in gData:
            # Prepare 3-bit data
            data3 = []
            for c8 in glyph['data']:
                c3 = color8to3(c8, palette)        # colorization is done using original palette, output is done using pushed palette
                if data3 and data3[-1][0]==c3:
                    data3[-1][1] += 1
                else:
                    data3.append([c3,1])

            # Encode 
            data3 = rle_3bit_string(data3)
            print('Glyph: {} bytes -> {} bytes'.format(len(glyph['data']), len(data3)))
            ouf.write(run_encode_number(len(data3)))
            ouf.write(data3)

        print('Above baseline =', ab)
        print('Below baseline =', bb)



if __name__=='__main__':
    convert(argv[1], argv[2])
