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

# This script converts an image into 3-bit run-length encoded (RLE3) image.
# It supports:
# - indexed PNG images in RGB color mode (MUST have not more than 8 colors)
# - indexed PNG images in RGBA color mode (MUST have not more than 8 colors)
# - monochrome PNG images with transparency (will be quantized internally)

import sys
import cv2
import numpy as np

TRANSPARENT = (256, 256, 256)       # unreal color; can be used to set one color transparent
WHITE = (255, 255, 255)

def process_color(c):
    # For antialiased transparent images this ensures that "almost white/black" becomes white/black
    if np.sum(c[:3])<4:
        return (0,0,0,c[3]) if len(c)==4 else c
    if np.sum(c[:3])>735:
        return (255,255,255,c[3]) if len(c)==4 else c
    return c

def color8to3(color8, palette):
    c3, best = 0, abs(color8-palette[0])
    c = 1       # index of color
    for pc in palette[1:]:
        score = abs(pc-color8)
        if score <= best:
            c3, best = c, score
        c += 1
    return c3

def quantize_colors(colors):
    '''Return a palette of 8 colors that would represent all others.
    Can be used only for a list of monochrome colors (0..255).
    Applies median cut technique.'''
    if len(set(colors))<8:
        return sorted(list(set(colors)))
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
    print('Trimmed opaque:', white0-white)
    print('Trimmed transparent:', black0-black)
    # 
    palette = []
    def rec(div, depth):        # recursive
        if not len(div): return
        if depth==3:
            palette.append(sum(div)/len(div))
        else:
            m = len(div)//2
            rec(div[:m], depth+1)
            rec(div[m:], depth+1)
    rec(colors, 0)
    palette = [int(round(x)) for x in palette]
    return palette

def run_encode_number(n):
    '''
    Encode arbitrary large integer with variable length string of bytes:
    8        - data continued in the next byte
     7654321 - actual data
    '''
    res = bytearray()
    flag = 0
    while n>127:
        res.insert(0, flag | (n & 0x7F))
        flag = 0x80
        n >>= 7
    res.insert(0,  flag | (n & 0x7F))
    return res

def rle_3bit_data(data_3_bit, cnt):
    res = bytearray()
    while cnt:
        if cnt<=0xf:
            # 1 byte
            res.append((data_3_bit<<5) | cnt)
            cnt = 0
        else:
            # 2 bytes (+ possibly more)
            if cnt <= 0xfff:
                cnt2, cnt = cnt, 0
            else:
                cnt2, cnt = 0xfff, cnt-0xfff
            res.append((data_3_bit<<5) | 0x10 | (cnt2>>8))
            res.append(cnt2 & 0xff)
            if cnt: print('Second')
    return res

def RLE3_encode(input_image_fn, output_fn):
    '''
    Encode 24-bit image with at most 8 colors with 3-bit RLE encoding (1 plane)
    FORMAT:
        `R` `L` `E` `3`
        WIDTH (run-encoded)
        HEIGHT (run-encoded)
        N_COLORS (1 byte)
        8        - alpha channel
         7654321 - number of colors
        R1 B1 C1 [A1]
        R2 B2 C2 [A2]
        ...
        RN BN CN [AN]
        IMAGEDATA...
    Image data encoding:
        876      - color index
           5     - data continued into one next byte
            4321 - actual data
    Continuation byte:
        87654321 - actual data (continued from previous byte)
    '''
    img = cv2.imread(input_image_fn, cv2.IMREAD_UNCHANGED)
    print('Image shape:', img.shape)
    if img.shape[2]==4:
        img = cv2.cvtColor(img, cv2.COLOR_BGRA2RGBA)
        ALPHA_CHANNEL = True
    else:
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        ALPHA_CHANNEL = False

    color_index = {}          # color to index
    cnt = 0

    # Index colors
    H, W = img.shape[:2]
    print('Image shape:', img.shape)
    base_color = None
    for y in range(img.shape[0]):
        for x in range(img.shape[1]):
            color = process_color(img[y][x])
            if ALPHA_CHANNEL:
                color = (color[0], color[1], color[2], color[3])
                if color[3] > 0:
                    if base_color is None:
                        base_color = color[:3]
                        print('Base color:', base_color)
                    elif base_color and base_color != color[:3]:
                        print('Rejected base color:', color[:3])
                        base_color = False
            else:
                color = (color[0], color[1], color[2])
            if color not in color_index:
                cnt += 1
                color_index[color] = cnt
                try:
                    print (cnt, color, np.sum(color[:3]))
                except:
                    print (cnt, color)
    if (cnt==0 or cnt>8) and not base_color:
        raise ValueError('wrong color number')

    # Prepare index data
    color_data = []         # list of pairs (index, cnt)
    alpha_color_data = []   # list of alpha values for monochrome images
    index_color = dict((x[1],x[0]) for x in color_index.items())       # index to color
    for y in range(img.shape[0]):
        for x in range(img.shape[1]):
            color = process_color(img[y][x])
            if ALPHA_CHANNEL:
                color = (color[0], color[1], color[2], color[3])
                if base_color:
                    alpha_color_data.append(color[3])
            else:
                color = (color[0], color[1], color[2])
            index = color_index[color]
            if color_data and color_data[-1][0]==index:
                color_data[-1][1] += 1
            else:
                color_data.append([index, 1])

    print ('Color changes: ', len(color_data))

    # Quantize
    if base_color and alpha_color_data:
        palette = quantize_colors(alpha_color_data)
        print ('Palette:', palette)
        print ('5-bit palette = ', [x>>3 for x in palette])

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
        if same: print ('5-bit palette2 = ', [x>>3 for x in palette2])
        ## - Push each color up for displaying purposes
        if sum(base_color)>376:
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
                    print('Pushed by', pushed,'- increasing max push')
                    max_push += 1
                c -= 1
            print ('Pushed 5-bit palette3 = ', [x>>3 for x in palette3])
        else:
            palette3 = palette2[:]

        # Convert colors to the generated palette
        # (change color_data and index_color)
        # - First: create translation dictionaries
        index_index2 = {}       # old index -> new index
        index2_color = {}       # new index -> color
        used_index2 = set()
        for indx in index_color:
            color = index_color[indx]               # old color
            indx2 = color8to3(color[3], palette2)+1 # nex index
            index_index2[indx] = indx2
            color2 = (color[0], color[1], color[2], palette3[indx2-1])
            index2_color[indx2] = color2
            used_index2.add(indx2)
            print(indx,color,'->',indx2,color2)
        # - Second: translate
        # Translate color_data
        for i in range(len(color_data)):
            indx = color_data[i][0]
            indx2 = index_index2[indx]
            color_data[i][0] = indx2
        # Translate index_color
        index_color = index2_color
        # Remove unused indeces
        unused = [x for x in range(1,9) if x not in used_index2]
        used = sorted(list(used_index2))
        cnt = len(used)
        print (cnt, 'used colors')
        while unused:
            j = used.pop()
            i = unused.pop(0)
            if j<=i: break
            print(j,'->',i)
            index_color[i] = index_color.pop(j)
            for k in range(len(color_data)):
                if color_data[k][0] == j:
                    color_data[k][0] = i

    # Write RLE3 image
    with open(output_fn, 'wb') as out:
        out.write(b'RLE3')
        out.write(run_encode_number(W))
        out.write(run_encode_number(H))
        clrs = cnt | ((TRANSPARENT or ALPHA_CHANNEL) and 0x80 or 0x00)
        out.write(bytes([clrs]))           # byte for number of colors + transparency

        # Write colors table
        for i in range(1,cnt+1):
            color = index_color[i]
            if not ALPHA_CHANNEL:
                print('%2d - %x %x %x' % (i, color[0], color[1], color[2]))
                out.write(bytes(color[:3]))
                if TRANSPARENT:
                    if color[:3]==TRANSPARENT:
                        print('  - TRANSPARENT', color[:3])
                    out.write( bytes([0 if color[:3]==TRANSPARENT else 255]) )
            else:
                print('%2d - %x %x %x %x' % (i, color[0], color[1], color[2], color[3]))
                out.write(bytes(color[:4]))

        # Write colors data
        S = ''      # DEBUG
        DARKEST = min(sum(index_color[k][:3]) for k in index_color)
        for index, cnt in color_data:
            #print (index-1, cnt)       # DEBUG
            out.write(rle_3bit_data(index-1, cnt))
            # DEBUG
            if ALPHA_CHANNEL and index_color[index][3]==0 or index_color[index][:3]==TRANSPARENT or \
               not ALPHA_CHANNEL and index_color[index][:3]==WHITE:
                ch = ' '
            elif (not ALPHA_CHANNEL or index_color[index][3]==255) and sum(index_color[index][:3])==DARKEST:
                ch = '#'
            else:
                ch = chr(ord('0') + index) # '.'
            S += ch*cnt

        # Show shape
        if W < 80:
            while S:
                print('|',S[:W],'|')
                S = S[W:]



if __name__=='__main__':
    input_fn = sys.argv[1]
    output_fn = sys.argv[2]
    RLE3_encode(input_fn, output_fn)
