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

OUTPUT_FN="icons.h"

declare -a array=(
        "lock"
        # Header icons
	"phone_small_w" "phone_small_w_crossed"     # SIP icons
        "missed_w"                                  # missed call
	"incoming_message_w"                        # incoming message
        # - battery icons
	"batt_w_0" "batt_w_1" "batt_w_2" "batt_w_3" "batt_w_4" "batt_w_5" "batt_w_full"
	#"batt_b_0" "batt_b_1" "batt_b_2" "batt_b_3" "batt_b_4" "batt_b_5"
	"batt_l" "batt_r" "batt_s"      # components of the "infinite" battery icon in the Kickstarter video
        # - WiFi icons
	"wifi_w_0" "wifi_w_1" "wifi_w_2" "wifi_w_3"
        # Splash screen
	"splash_base" "splash_1" "splash_2" "splash_3" "splash_4"
        # Headpic & phone icon. Used in the Phonebook & SIP accounts.
	"phone_w" "phone_b" 
	"person_w" "person_b"
	"message_w" "message_b"     # used in contact view
	"calling_w" "calling_b"     # used in contact view
        # Used in SIP accounts app
        "delete_r" "delete_w" "edit_w" "edit_b"
        # Big icons for the main menu
	"Games_b" "Games_w"
	"Tools_b" "Tools_w"
	"Reboot_b" "Reboot_w"
	"Call_log_b" "Call_log_w"           # not used currently
	"Messages_b" "Messages_w"
	"Settings_b" "Settings_w"
	"Phonebook_b" "Phonebook_w"
        # Big icons for the Messages app
	"Inbox_b" "Inbox_w"
	"Outbox_b" "Outbox_w"
	"Write_b" "Write_w"
        # WiPhone as an RC car (a.k.a. Carphone)
	"no_walking" "stop" "forward" "reverse" "left" "right"
        # Chess icons
	"pawn_0" "knight_0" "bishop_0" "rook_0" "queen_0" "king_0"
	"pawn_1" "knight_1" "bishop_1" "rook_1" "queen_1" "king_1"
        "cell_0" "cell_1" "sel_0" "sel_1" "select_piece"
	#"skull_5" "skull_10" "skull_15"		#	set TRANSPARENT to (0, 1, 0)  ?O
)

cnt=${#array[@]}
fns=""
binary_fns=""
success=1
for (( i=0; i<${cnt}; i++ ));
do
	echo "${array[$i]}"
	./RLE3.py "${array[$i]}.png" "${array[$i]}.rle3"
        if [ $? -ne 0 ]
        then 
            echo "ERROR: failed to convert PNG to RLE3; make sure ${array[$i]}.png exists"
            success=0
            break 
        fi
	./to_c.py "${array[$i]}.rle3" "${array[$i]}.h"
	awk -v NAME="${array[$i]}"  'NR==1 && $0 !~ "PROGMEM" { print ""; gsub(/data/, "icon_" NAME); $4 = $4 " PROGMEM" } 1' "${array[$i]}".h > .tmp.h; mv .tmp.h "${array[$i]}".h
	fns="$fns ${array[$i]}.h"
	binary_fns="$binary_fns ${array[$i]}.rle3"
done

if [ $success -eq 1 ]
then
    echo "// Collection of icons in RLE3 format" > "$OUTPUT_FN"
    cat $fns >> "$OUTPUT_FN"
    echo "Final icons collection size:"
    cat $binary_fns | wc -c
    echo "Output written to: $OUTPUT_FN"
fi

