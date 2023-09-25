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

import sys
inp = sys.argv[1]
out = sys.argv[2]

data = open(inp,'rb').read()

with open(out,'w') as file:
    file.write('''const unsigned char data[%d] = {\n''' % len(data))
    cnt = 0
    for byte in data:
        file.write(' 0x%02x,' % byte)
        cnt += 1
        if cnt >= 16:
            file.write('\n')
            cnt = 0
    if cnt:
        file.write('\n')
    file.write('};')


