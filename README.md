# wiphone
Code for the ESP32-based "wiphone" 

### Configuring the Arduino IDE ###
These steps are described at https://wiphone.io/docs/WiPhone/latest/technical_manual/software/programming_manual.html but repeated here for convenience:

a.) _File > Preferences_ and add ```https://wiphone.io/static/releases/arduino_platforms/package_WiPhone_index.json``` into the Additional Board Manager URLs field

b.) Then load the Boards Manager, search for "wiphone", and install the latest boards package (currently v0.14)

This is based on v0.8.30 of the firmware supplied by WiPhone, which is described as being provided under a "WiPhone Public Licence", with the following URL: https://wiphone.io/WiPhone_Public_License_v1.0.txt
Unfortunately, that URL is borked. But, based on all associated marketing description provided with the product as an open-source, hackable phone, I'm providing it here in good faith under the standard GNU public licence.
