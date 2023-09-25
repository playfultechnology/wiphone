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

#include "GUI.h"
#include "tinySIP.h"
#include "ota.h"
#include "Test.h"

// Static images
#include "src/assets/image.h"
#include "src/assets/image_256.h"
#include "src/assets/image_jpg.h"
#include "src/assets/ackman_data.h"
#include <esp_wifi.h>

// TODO:
// - optimization: return DO_NOTHING for irrelevant events
// - optimization: if event was processed by a widget, return false if it is irrelevant (state not changed)
// - migrate from C-string (strcpy, strcmp, strdup) to std::string or to String?

LCD* static_lcd = NULL;
bool UDP_SIP = false;
bool loudSpkr = false;
bool wifiOn = true;


uint16_t GUI::batteryExtraLength = 0;

GUI::GUI() {
  // Widgets
  widgetsArray = NULL;
  maxWidgets = 0;
  numWidgets = 0;

  header = new HeaderWidget("???", state);
  footer = new FooterWidget("???", "???", state);
  mainMenu = NULL;

  // Cursor position
  xPos = 0;
  yPos = 0;

  // Timing
  mil = 0;

  // Current state
  menuDrawn = false;
  menuNewItems = false;
};

GUI::~GUI() {
  if (header) {
    delete header;
  }
  if (footer) {
    delete footer;
  }
  if (mainMenu) {
    delete mainMenu;
  }

  cleanAppDynamic();
}

void GUI::cleanAppDynamic() {
  if (runningApp!=NULL) {
    delete runningApp;
    runningApp = NULL;
  }
  if (callApp!=NULL) {
    delete callApp;
    callApp = NULL;
  }

  // Free memory from widgets
  deleteWidgets();
}

void GUI::deleteWidgets() {
  if (maxWidgets) {
    for (uint16_t i=0; i<numWidgets; i++) {
      if (widgetsArray[i]!=NULL) {
        delete widgetsArray[i];
      }
    }
    freeNull((void **) &widgetsArray);
  }
  maxWidgets = 0;
  numWidgets = 0;
}

bool GUI::addWidget(GUIWidget* w) {
  // TODO: can use LinearArray
  // Ensure the array is big enough
  if (numWidgets>=maxWidgets) {
    GUIWidget** p;
    if (maxWidgets>0) {
      if (maxWidgets<=8) {
        maxWidgets <<= 1;
      } else {
        maxWidgets += 8;
      }
      p = (GUIWidget**) realloc(widgetsArray, sizeof(GUIWidget*) * maxWidgets);
    } else {
      maxWidgets = 1;
      p = (GUIWidget**) malloc(sizeof(GUIWidget*) * maxWidgets);
    }
    if (p==NULL) {
      return false;
    }
    widgetsArray = p;
  }
  widgetsArray[numWidgets++] = w;
  return true;
}

void GUI::loadSettings() {
  state.loadSipAccount();
  log_d("fromName  = %s", state.fromNameDyn);
  log_d("fromUri   = %s", state.fromUriDyn);
  log_d("proxyPass = %s", state.proxyPassDyn);
}

/*
 * Description:
 *     Loads messages twice: at the start and after current time becomes known.
 */
void GUI::reloadMessages() {
  flash.messages.unload();
  flash.messages.load(ntpClock.isTimeKnown() ? ntpClock.getExactUnixTime() : 0);
  state.unreadMessages = flash.messages.hasUnread();
}

void GUI::init(void (*lcdOnOffCallback)(bool)) {
  state.setInputState(InputType::Numeric);

  // Init screen and show GUI splash screen
  lcd.begin();
  lcdOnOff = lcdOnOffCallback;
  static_lcd = &lcd;

  // Create screen sprite
  page = new TFT_eSprite(&lcd);
  page->setColorDepth(16);
  page->createSprite(lcd.width(), lcd.height());
  if (page->isCreated()) {
    screen = page;
  } else {
    screen = &lcd;
    log_d("page sprite not created, falling back to direct on-screen drawing");
  }

  // Load background image
  bgImage = new TFT_eSprite(&lcd);      // create dummy sprite at first (must be fed to ClockApp at least as a placeholder)
  bgImage->setColorDepth(16);
  if (page && page->isCreated()) {
    // Store background image for future use
    bgImage->createSprite(lcd.width(), lcd.height());
    if (bgImage->isCreated()) {
      // Try to load background (wallpaper) image from file
      bool succ = false;
      if (SD.exists(GUI::backgroundFile) || SPIFFS.exists(GUI::backgroundFile)) {
        File bgImgFile = SD.exists(GUI::backgroundFile) ? SD.open(GUI::backgroundFile) : SPIFFS.open(GUI::backgroundFile);

        // Read entire image file into the external RAM, as long as it's not too big (less than 1 MB)
        int bytes;
        char buff[1025];
        LinearArray<char, LA_EXTERNAL_RAM> fileContent;
        do {
          bytes = bgImgFile.readBytes(buff, sizeof(buff)-1);
          if (bytes > 0) {
            fileContent.extend(buff, bytes);  // NOTE: this creates small unnecessary overhead for small files
          }
        } while (bytes == sizeof(buff)-1 && fileContent.size() < GUI::backgroundFileMaxSize);
        log_d("Read %d bytes from image file \"%s\"", fileContent.size(), GUI::backgroundFile);

        // Check loaded file size
        if (fileContent.size() < GUI::backgroundFileMaxSize) {
          // Attempt to draw
          if (bgImage->drawImage((uint8_t*) &fileContent[0], fileContent.size())) {
            log_v("image file loaded");
            succ = true;
          } else {
            log_e("failed to display background image");
          }
        }
      }

      if (!succ) {
        log_e("image file fallback");
        bgImage->drawImage(image_i256, sizeof(image_i256));
      }
    }
  }

  // Enter menu
  enterMenu(1);       // start in Main menu
#ifdef DIAGNOSTICS_ONLY
  enterApp(GUI_APP_DIAGNOSTICS);
  return;
#endif // DIAGNOSTICS_ONLY
  enterApp(GUI_APP_SPLASH);

  // Initialize clock app to show in the locked state
  clockApp = new ClockApp(*screen, *bgImage, state);
};

void GUI::pushScreen(TFT_eSPI* disp) {
  if (disp->isSprite()) {
    ((TFT_eSprite*)disp)->pushSprite(0, 0);
  }
};

void GUI::pushScreenPart(TFT_eSPI* disp, uint16_t yOff, uint16_t height) {
  if (disp->isSprite()) {
    ((TFT_eSprite*)disp)->pushSpritePart(0, yOff, height);
  }
};

void GUI::frameToSerial() {
  if (page && page->isSprite()) {
    for (int y=0; y<page->height(); y++) {
      for (int x=0; x<page->width(); x++) {
        printf(" 0x%04x", page->readPixel(x,y));
      }
      printf("\r\n");
    }
  }
}

void GUI::toggleScreen() {
#ifdef WIPHONE_INTEGRATED
  lcdOn = !lcdOn;
  log_d("Turning %s backlight", lcdOn ? "ON" : "OFF");
  (*lcdOnOff)(lcdOn);

  // Setting brightness from software attempts
  // Variant 1: DISPOFF register - turns the screen into completely white
  //lcd.writecommand(lcdOn ? ST7789_DISPON : ST7789_DISPOFF);
  // Variant 2: brightness register - doesn't work
  //lcd.writecommand(ST7789_WRDISBV);
  //lcd.writedata(lcdOn ? 0b01010001 : 0);

  // Turn on backlight (dimmed with PWM) - WiPhone #2
  //ledcAttachPin(LCD_LED_PIN, 1);        // GPIO, ledChannel
  //ledcSetup(1, 5000, 8);                // ledChannel, freq, resolution (bits: 8 or 16)
  //ledcWrite(1, 0);                      // ledChannel, dutyCycle
#endif // WIPHONE_INTEGRATED  
}

void GUI::longBatteryAnimation() {
  const int extraSections = 19;
  for (GUI::batteryExtraLength=1; GUI::batteryExtraLength<4*extraSections; GUI::batteryExtraLength++) {   // add 20 sections to the battery
    if (runningApp && !runningApp->isWindowed()) {
      this->redrawScreen(false, false, true);  // redraw entire screen
    } else {
      this->redrawScreen(true, false, false);  // redraw header only
    }
  }
  GUI::batteryExtraLength--;
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  CONTROL STATE  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

ControlState::ControlState(void)
  : userSerialBuffer(USER_SERIAL_BUFFER_SIZE) {
  sipState = CallState::NotInited;

  // Initialize dynamic
  // SIP account
  fromNameDyn  = NULL;
  fromUriDyn   = NULL;//strdup("0@00");//NULL;     // SIP URI
  proxyPassDyn = NULL;

  // Callee
  calleeNameDyn = NULL;
  calleeUriDyn  = NULL;//strdup("1@00");//NULL;
  lastReasonDyn = NULL;

  // Not subscribed to "any event"
  msAppTimerEventPeriod = 0;
  msAppTimerEventLast = 0;

  // Messages
  unreadMessages = false;

  // Battery
  battUpdated = false;
  battVoltage = 0;
  battSoc = 0;
};

ControlState::~ControlState() {
  clearDynamicSip();
  clearDynamicCallee();
}

void ControlState::clearDynamicSip() {
  freeNull((void **) &fromNameDyn);
  freeNull((void **) &fromUriDyn);
  fromUriDyn = NULL;//strdup("0@00");//NULL;     // SIP URI
  freeNull((void **) &proxyPassDyn);
  freeNull((void **) &lastReasonDyn);
}

void ControlState::clearDynamicCallee() {
  freeNull((void **) &calleeNameDyn);
  freeNull((void **) &calleeUriDyn);
  calleeUriDyn  = NULL;//strdup("1@00");//NULL;
}

bool ControlState::loadSipAccount() {
  log_d("loadSipAccount ControlState");

  clearDynamicSip();

  bool foundAccount = false;
  CriticalFile ini(SipAccountsApp::filename);
  if ((ini.load() || ini.restore()) && !ini.isEmpty()) {
    // Check version of the file format
    if (ini[0].hasKey("v") && !strcmp(ini[0]["v"], "1")) {
      log_d("SIP accounts file found");
      IF_LOG(VERBOSE)
      ini.show();
      for (auto si = ini.iterator(); si.valid(); ++si) {
        if (si->hasKey("m")) {       // primary account
          log_d("primary sip account found");
          setSipAccount(si->getValueSafe("d", ""),
                        si->getValueSafe("s", ""),
                        si->getValueSafe("p", ""),
                        si->getValueSafe("u", ""));// u stands for udp-sip tcp-sip selection
          foundAccount = true;
          break;
        }
      }
    } else {
      log_e("SIP accounts file corrup or unknown format");
      IF_LOG(VERBOSE)
      ini.show();
    }
  } else {
    log_d("creating SIP accounts file");
    ini[0]["desc"] = "WiPhone SIP accounts";
    ini[0]["v"] = "1";
    ini.store();
  }
  if (!foundAccount) {
    log_d("SIP account not found");
    setSipAccount("", "", "", "");
  }
  return foundAccount;
}

void ControlState::setInputState(InputType newInputType) {
  inputType = newInputType;
  inputCurKey = 0;
  inputCurSel = 0;
  inputShift = false;
  inputSeq[0] = '\0';
}

/* Description:
 *    set SIP account settings from function parameters
 */
void ControlState::setSipAccount(const char* dispName, const char* uri, const char* passwd, const char* UDP_TCP_SIP_Selection) {
  clearDynamicSip();

//TODO UDP_SIP set global UDP TCP SIP value on reading ini file.

  // Don't do anything if new credentials (namely SIP URI and password) are the same as existing ones
  bool sipAccountSame = (uri && fromUriDyn && !strcmp(uri, fromUriDyn)) && //Mesut: this should be AND operand
                        (passwd && proxyPassDyn && !strcmp(passwd, proxyPassDyn) &&
                         (UDP_TCP_SIP_Selection && global_UDP_TCP_SIP && !strcmp(UDP_TCP_SIP_Selection, global_UDP_TCP_SIP)));      // a strong condition
  sipAccountChanged = !sipAccountSame;

  log_d("UDP_TCP_SIP_Selection: %s", UDP_TCP_SIP_Selection);
  if(global_UDP_TCP_SIP) {
    log_d("globalUDP_TCP_SIP: %s", global_UDP_TCP_SIP);
  }

  if (sipAccountChanged) {
    fromNameDyn  = (dispName!=NULL) ? strdup(dispName) : strdup("");
    fromUriDyn   = (uri!=NULL) ? strdup(uri) : /*strdup("0@00")*/NULL;//to do put test value here
    proxyPassDyn = (passwd!=NULL) ? strdup(passwd) : strdup("");
    global_UDP_TCP_SIP = (UDP_TCP_SIP_Selection!=NULL) ? strdup(UDP_TCP_SIP_Selection) : strdup("");
    if(!strcmp(global_UDP_TCP_SIP, "UDP-SIP")) {
      UDP_SIP = true;
    } else {
      UDP_SIP = false;
    }
    log_d("new globalUDP_TCP_SIP: %s", global_UDP_TCP_SIP);
  }

  if (sipAccountChanged) {
    log_d("SIP ACCOUNT CHANGED UDP-SIP:%d", UDP_SIP);
    sipRegistered = false;
  }
}

void ControlState::removeSipAccount() {
  sipAccountChanged =  fromNameDyn &&  fromUriDyn &&  proxyPassDyn &&
                       *fromNameDyn && *fromUriDyn && *proxyPassDyn;

  freeNull((void **) &fromNameDyn);
  freeNull((void **) &fromUriDyn);
  freeNull((void **) &proxyPassDyn);
}

/* Description:
 *    set a callee before making a call
 */
void ControlState::setRemoteNameUri(const char* dispName, const char* uri) {
  this->clearDynamicCallee();
  this->calleeNameDyn = (dispName!=NULL) ? strdup(dispName) : strdup("");
  if (uri==NULL) {
    this->calleeUriDyn = NULL;//strdup("1@00");//to do create a default value
  } else if (strchr(uri, '@') == NULL && this->fromUriDyn != NULL && strchr(this->fromUriDyn, '@')!=NULL) {
    // A number provided -> form a SIP URI for the current server
    size_t len = strlen(uri) + strlen(this->fromUriDyn);
    char buff[len+5];
    char* currentServer = strchr(this->fromUriDyn, '@');

    //check whether uri starts with "sips:" or "SIPS:" or "SIP:" which can not be called (tested)
    if(strncmp(uri, "sips:", 5) == 0 || strncmp(uri, "SIPS:", 5) == 0 || strncmp(uri, "SIP:", 4) == 0) {
      //TODO display error popup which says we do not support it and do nothing.
      //now, we set the buff for sake of safety purposes
      sprintf(buff, "%s%s", uri, currentServer);
    } else {
      //check whether uri starts with "sip:"
      if(strncmp(uri, "sip:", 4) == 0) {
        sprintf(buff, "%s%s", uri, currentServer);
      } else {
        sprintf(buff, "sip:%s%s", uri, currentServer);
      }
    }
    this->calleeUriDyn = strdup(buff);
  } else {
    this->calleeUriDyn = strdup(uri);
  }
}

void ControlState::setSipReason(const char* text) {
  freeNull((void **) &lastReasonDyn);
  lastReasonDyn = (text!=NULL) ? strdup(text) : strdup("");
  log_d("setSipReason: %s", lastReasonDyn);
}

void showCallState(CallState state) {
  switch (state) {
  case CallState::NotInited:
    log_d("NotInited");
    break;
  case CallState::Idle:
    log_d("Idle");
    break;
  case CallState::InvitingCallee:
    log_d("InvitingCallee");
    break;
  case CallState::InvitedCallee:
    log_d("InvitedCallee");
    break;
  case CallState::RemoteRinging:
    log_d("RemoteRinging");
    break;
  case CallState::Call:
    log_d("Call");
    break;
  case CallState::HangUp:
    log_d("HangUp");
    break;
  case CallState::HangingUp:
    log_d("HangingUp");
    break;
  case CallState::HungUp:
    log_d("HungUp");
    break;

  case CallState::BeingInvited:
    log_d("BeingInvited");
    break;
  case CallState::Accept:
    log_d("Accept");
    break;
  case CallState::Decline:
    log_d("Decline");
    break;

  case CallState::Error:
    log_d("Error");
    break;
  default:
    log_d("UNKNOWN=");
    log_d("%d", (int) state);
    break;
  }
}

void ControlState::setSipState(CallState newState) {
  log_d("CALL STATE TRANSITION: ");
  showCallState(sipState);
  log_d(" -> ");
  showCallState(newState);
  log_d("");
  sipState = newState;
}

bool ControlState::scheduleEvent(EventType event, uint32_t msTriggerAt) {
  if (eventQueue.size() >= ControlState::MAX_EVENTS) {
    return false;
  }

  // Find a spot to insert the event into
  uint32_t pos = eventQueue.size();
  for (auto it = eventQueue.iterator(); it.valid(); ++it) {
    if (it->msTriggerAt - msTriggerAt < (UINT32_MAX>>4)) {       // Actually what we want to check is: it->msTriggerAt > msTriggerAt
      pos = (int)it;
      break;
    }
  }

  // Insert the new event
  log_v("schedule event: %x (%u), at %d", event, msTriggerAt, pos);
  QueuedEvent qe;
  qe.event = event;
  qe.msTriggerAt = msTriggerAt;
  eventQueue.insert(pos, qe);
  return true;
}

EventType ControlState::popEvent(uint32_t msNow) {
  if (eventQueue.size() && msNow - eventQueue[0].msTriggerAt < (UINT32_MAX>>4)) {     // Actually what we want to check is: msNow > eventQueue[0].msTriggerAt
    EventType res = eventQueue[0].event;
    eventQueue.remove(0);
    log_v("pop event: 0x%x", res);
    return res;
  }
  return EventType(0);
}

void ControlState::unscheduleEvent(EventType eventType) {
  for (int i = 0; i < eventQueue.size(); i++) {
    if (eventQueue[i].event == eventType) {
      log_d("removed event: %d", i);
      eventQueue.remove(i--);
    }
  }
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  INPUT  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

/* Description:
 *      determine the input and change the input state for displaying
 * Input:
 *      key - button recently pressed
 * Return values:
 *      r1  - first return character
 *      r2  - second return charater (for example, when "a" is currently selected and then DOWN is pressed, r1 will be "a", r2 will be DOWN)
 * Implicit return values (global state variables):
 *      state.inputShift
 *      state.inputCurKey
 *      state.inputCurSel
 *      state.inputSeq
 */
void GUI::alphanumericInputEvent(EventType key, EventType& r1, EventType& r2) {
  log_v("alphanumericInputEvent:", key);

  r1 = 0;
  r2 = 0;
  if (state.inputType!=InputType::AlphaNum) {
    guiError("unsupported");
    return;
  }
  if (key==WIPHONE_SHIFT_KEY) {
    // just toggle Shift and exit
    state.inputShift = !state.inputShift;
    return;
  }

  // Check if any button is "active" now (meaning a button is pressed and selection of its symbols is displayed)
  if (state.inputCurKey && state.inputCurKey == key) {
    // same button is active now -> cycle to next candidate
    state.inputCurSel = (state.inputCurSel + 1) % strlen(state.inputSeq);
  } else {
    if (state.inputCurKey) {
      // different button is active now -> return the current candidate (currently selected character)
      if (state.inputCurSel<strlen(state.inputSeq)) {
        r1 = state.inputSeq[state.inputCurSel];
      }
      if (state.inputShift) {
        r1 = toupper(r1);
      }
    }

    if (key>=32 && key<=126) {
      // make new printable button active
      int8_t i = -1;
      if (key>='0' && key<='9') {
        i = key - '0';
      } else if (key==WIPHONE_SYMBOLS_KEY) {
        i = 10;
      } else {
        guiError("unknown key");
      }
      if (i>=0) {
        strcpy(state.inputSeq, alphNum[i]);
        state.inputCurKey = key;
        state.inputCurSel = 0;
      }
    } else {
      // non-printable button pressed
      state.inputCurKey = 0;    // no button is active
      r2 = key;
    }
  }
  if (!r1) {
    r1 = r2;
    r2 = 0;
  }
}

appEventResult GUI::processEvent(uint32_t now, EventType event) {
  appEventResult res = DO_NOTHING;

  // Pre-process
  EventType keyNext = 0;   // one keypress can generate two inputs sometimes
  if (IS_KEYBOARD(event)) {
    this->msLastKeypadEvent = now;

    // Process unlocking
    log_d("locked = %d, event = %x", state.locked, event);
    if (state.locked) {
      if (inCall()) {
        log_d("IN A CALL. UNLOCKED ON ANY KEY");
        // if we are in a call (probably receiving one), unlock on any key
        state.locked = false;
        res |= REDRAW_SCREEN | LOCK_UNLOCK;
      } else {
        state.unscheduleEvent(UNLOCK_CLEAR_EVENT);
        if (event == WIPHONE_KEY_OK) {
          state.unlockButton1 = event;
          state.scheduleEvent(UNLOCK_CLEAR_EVENT, now + 2500);
          res |= REDRAW_FOOTER;
          log_d("OK pressed: %d", state.unlockButton1);
        } else if (state.unlockButton1 == WIPHONE_KEY_OK) {
          if (event == WIPHONE_UNLOCK_KEY2) {
            // Unlocked successfully
            state.locked = false;
            res |= REDRAW_SCREEN | LOCK_UNLOCK;
          }
          if (state.unlockButton1) {
            state.unlockButton1 = 0;
            res |= REDRAW_FOOTER;
            log_d("state.unlockButton1 cleared");
          }
        } else if (state.unlockButton1) {
          state.unlockButton1 = 0;
          res |= REDRAW_FOOTER;
          log_d("state.unlockButton1 cleared");
        }
        event = keyNext = 0;
      }
    }

    // Restore screen brightness
    if (state.screenBrightness < state.brightLevel) {
      // If screen is completely OFF -> forget this key, just reveal the screen
      if (state.screenBrightness <= 0) {
        event = keyNext = 0;
        state.screenWakeUp = true;
        res |= REDRAW_ALL;
      } else {
        // Maximize screen brightness according to the settings
#if GPIO_EXTENDER == 1509
        lcdLedOnOff(true, conv100to255(state.brightLevel));
#endif // GPIO_EXTENDER == 1509
        state.screenBrightness = state.brightLevel;
      }
    }

    // Reset screen dimming and sleeping events
    state.unscheduleEvent(SCREEN_SLEEP_EVENT);
    state.unscheduleEvent(SCREEN_DIM_EVENT);

    // Schedule screen dimming
    if (state.doDimming()) {
      // Dimming is effectively ON -> schedule dimming event
      state.scheduleEvent(SCREEN_DIM_EVENT, now + state.dimAfterMs);
    }
    if (state.doSleeping()) {
      // Sleeping is effectively ON -> schedule sleep event
      state.scheduleEvent(SCREEN_SLEEP_EVENT, now + state.sleepAfterMs);
    }

    // Decode button
    if (state.inputType!=InputType::Numeric) {
      alphanumericInputEvent(event, event, keyNext);
      res |= REDRAW_FOOTER;
    }

  } else if (event == SCREEN_DIM_EVENT) {
    if (state.doDimming()) {
      if (state.screenBrightness > state.dimLevel) {
        // Adjust screen brightness
        state.screenBrightness -= 5;
#if GPIO_EXTENDER == 1509
        log_d("@ SCREEN_DIM_EVENT: %d <- %d", state.dimLevel, state.screenBrightness);
        lcdLedOnOff(true, conv100to255(state.screenBrightness));
#endif // GPIO_EXTENDER == 1509

        // Schedule next event
        if (state.screenBrightness > state.dimLevel) {
          log_d("event scheduled");
          state.scheduleEvent(SCREEN_DIM_EVENT, now + 33);
        }
      } else {
        log_d("SCREEN_DIM_EVENT - no effect");
      }
    } else {
      // Event happened, but dimming is effectively OFF -> reset screen to bright level
      state.screenBrightness = state.brightLevel;

#if GPIO_EXTENDER == 1509
      log_d("@ SCREEN_DIM_EVENT 2");
      lcdLedOnOff(true, conv100to255(state.screenBrightness));
#endif // GPIO_EXTENDER == 1509
    }
    event = EventType(0);
  } else if (event == SCREEN_SLEEP_EVENT) {
    if (state.doSleeping()) {
      // Screen sleep is ON: turn the screen OFF
      log_d("SCREEN OFF @ SCREEN_SLEEP_EVENT");
      lcdLedOnOff(false);
      state.screenBrightness = 0;     // screen is sleeping
      if (state.locking) {
        state.locked = true;
        res |= REDRAW_SCREEN | LOCK_UNLOCK;
      }
    }
  } else if (event == UNLOCK_CLEAR_EVENT) {
    log_d("UNLOCK_CLEAR_EVENT");
    if (state.unlockButton1) {
      state.unlockButton1 = 0;
      res |= REDRAW_FOOTER;
      log_d("state.unlockButton1 cleared");
    }
    event = EventType(0);
  }

  while(event) {

    // Since one keypress can generate two keyboard events, we need a loop to process them both

    // Logging
    if (event != APP_TIMER_EVENT || state.msAppTimerEventPeriod>1000) {
      const char* p = nullptr;
      if (event>=0x20 && event<0x7F) {
        log_i("key=%c", event);  // printable key
      } else if (event<0x7F) {
        // Show non-printable keys
        switch (event) {
        case WIPHONE_KEY_OK:
          p = "OK";
          break;
        case WIPHONE_KEY_UP:
          p = "Up";
          break;
        case WIPHONE_KEY_DOWN:
          p = "Down";
          break;
        case WIPHONE_KEY_BACK:
          p = "Back";
          break;
        case WIPHONE_KEY_LEFT:
          p = "Left";
          break;
        case WIPHONE_KEY_RIGHT:
          p = "Right";
          break;
        case WIPHONE_KEY_SELECT:
          p = "Select";
          break;
        case WIPHONE_KEY_CALL:
          p = "Call";
          break;
        case WIPHONE_KEY_END:
          p = "End";
          break;
        case WIPHONE_KEY_F1:
          p = "F1";
          break;
        case WIPHONE_KEY_F2:
          p = "F2";
          break;
        case WIPHONE_KEY_F3:
          p = "F3";
          break;
        case WIPHONE_KEY_F4:
          p = "F4";
          break;
        case 0:
          p = "NUL";
          break;
        default:
          break;
        }
        if (p != nullptr) {
          log_i("key=%s", p);
        } else {
          log_i("key=0x%x", event);
        }
      } else {
        // Show other events
        switch (event) {
        case KEYBOARD_TIMEOUT_EVENT:
          p = "KEYBOARD_TIMEOUT_EVENT";
          break;
        case APP_TIMER_EVENT:
          p = "APP_TIMER_EVENT";
          break;
        case BATTERY_UPDATE_EVENT:
          p = "BATTERY_UPDATE_EVENT";
          break;
        case CALL_UPDATE_EVENT:
          p = "CALL_UPDATE_EVENT";
          break;
        case WIFI_ICON_UPDATE_EVENT:
          p = "WIFI_ICON_UPDATE_EVENT";
          break;
        case TIME_UPDATE_EVENT:
          p = "TIME_UPDATE_EVENT";
          break;
        case USER_SERIAL_EVENT:
          p = "USER_SERIAL_EVENT";
          break;
        case REGISTRATION_UPDATE_EVENT:
          p = "REGISTRATION_UPDATE_EVENT";
          break;
        case BATTERY_BLINK_EVENT:
          p = "BATTERY_BLINK_EVENT";
          break;
        case USB_UPDATE_EVENT:
          p = "USB_UPDATE_EVENT";
          break;
        default:
          break;
        }
        if (p != nullptr) {
          //log_i("%s", p);
        } else {
          log_i("unnamed event: 0x%x", event);
        }
      }
    }

    bool messageIconAppears = false;
    if (event == POWER_OFF_EVENT) {

      powerOffScreen = true;
      this->drawPowerOff();

    } else if (event == POWER_NOT_OFF_EVENT) {

      log_v("POWER_NOT_OFF_EVENT");
      powerOffScreen = false;

    } else if (event == NEW_MESSAGE_EVENT) {

      if (!state.unreadMessages) {
        messageIconAppears = flash.messages.hasUnread();  // update (show) message icon
      }

      // Reload messages to display in the messages app (this will also set state.unreadMessages)
      this->reloadMessages();

    }

    // Feed event into specific "apps"
    if (callApp!=NULL) {

      // Special case for calling app
      appEventResult appRes = callApp->processEvent(event);
      if (appRes & EXIT_APP) {
        exitCall();
        res |= (!runningApp || runningApp->isWindowed()) ? REDRAW_ALL : REDRAW_SCREEN;
        // TODO: restore header/footer
      } else {
        res |= appRes;
      }

      // Special cases for redrawing
      if ((event == TIME_UPDATE_EVENT || event == WIFI_ICON_UPDATE_EVENT) && callApp->isWindowed()) {
        res |= REDRAW_HEADER;
      } else if (event == POWER_NOT_OFF_EVENT) {
        res |= callApp->isWindowed() ? REDRAW_ALL : REDRAW_SCREEN;  // TODO: maybe just prohibit drawing headers over non-windowed apps
      } else if (messageIconAppears && callApp->isWindowed()) {
        res |= REDRAW_HEADER;
      }

    } else if (runningApp!=NULL) {

      appEventResult appRes = runningApp->processEvent(event);
      if (appRes & EXIT_APP) {
        ActionID_t appID = runningApp->getId();
        // Running app exited
        log_d("deleting app");
        delete runningApp;
        runningApp = NULL;

        // Go back to the main menu
        if (appID == GUI_APP_SPLASH) {
          // Special case: splash screen exited -> go to clock
          log_d("CLOCK, event = %x", event);
          enterApp(GUI_APP_CLOCK);
        } else if (appRes & ENTER_DIAL_APP) {
          log_d("DIALING");
          enterApp(GUI_APP_DIALING);
          if (runningApp) {
            runningApp->processEvent(event);
          }
        } else {
          log_d("MENU, event = %x", event);
          enterApp(GUI_APP_MENU);

          // Restore header and footer text
          int16_t menuIndex = findMenu(curMenuId);
          if (menuIndex>=0) {
            header->setTitle(menu[menuIndex].title);
            footer->setButtons(menu[menuIndex].leftButton, menu[menuIndex].rightButton);
          }
        }

        res |= (runningApp && runningApp->isWindowed()) ? REDRAW_ALL : REDRAW_SCREEN;
      } else {
        res |= appRes;
      }

      // Special cases for redrawing
      if ((event == TIME_UPDATE_EVENT || event == WIFI_ICON_UPDATE_EVENT) && runningApp && runningApp->isWindowed()) {
        res |= REDRAW_HEADER;
      } else if (event == POWER_NOT_OFF_EVENT) {
        res |= runningApp->isWindowed() ? REDRAW_ALL : REDRAW_SCREEN;
      } else if (messageIconAppears && runningApp->isWindowed()) {
        res |= REDRAW_HEADER;
      }

    } else if (curApp == GUI_APP_MENU) {

      if (LOGIC_BUTTON_BACK(event)) {
        if (curMenuId==1) {
          enterApp(GUI_APP_CLOCK);
        } else {
          exitMenu();
        }
        res |= REDRAW_SCREEN;
      } else if (NONKEY_EVENT_ONE_OF(event, TIME_UPDATE_EVENT | BATTERY_UPDATE_EVENT | WIFI_ICON_UPDATE_EVENT | BATTERY_BLINK_EVENT | USB_UPDATE_EVENT)) {
        res |= REDRAW_HEADER;
      } else if (event == POWER_NOT_OFF_EVENT) {
        res |= REDRAW_ALL;
      } else if (messageIconAppears) {
        res |= REDRAW_HEADER;
      } else if (event >= '0' && event <='9' || event == '*' || event == '#') {
        enterApp(GUI_APP_DIALING);
        if (runningApp) {
          runningApp->processEvent(event);
        }
        res |= REDRAW_ALL;
      } else if (mainMenu) {
        mainMenu->processEvent(event);
        uint16_t ID = mainMenu->readChosen();
        if (LOGIC_BUTTON_OK(event)) {
          int16_t ci = findMenu(ID);
          if (ci<0) {
            guiError("menu failed");
            return res;
          }
          if (menu[ci].action == GUI_ACTION_SUBMENU) {
            enterMenu(menu[ci].ID);
          } else if (menu[ci].action & GUI_BASE_APP) {
            enterApp(menu[ci].action);
            if (runningApp) {
              res |= runningApp->isWindowed() ? REDRAW_ALL : REDRAW_SCREEN;  // initialize screen for the newly launched app
            }
          } else if (menu[ci].action == GUI_ACTION_RESTART) {
            ESP.restart();
          }
        }
        res |= REDRAW_SCREEN;
      }

    } else if (LOGIC_BUTTON_BACK(event)) {
      // Some other app (non-class app)
      enterApp(GUI_APP_MENU);
      res |= REDRAW_ALL;
    }

    // second key (if any)
    event = keyNext;
    keyNext = 0;
  };
  return res;
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  FONT LOADER  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

FontCollection fonts;

FontCollection::FontCollection() {
  memset(fnt, 0, sizeof(fnt));
};

FontCollection::~FontCollection() {
  for (uint8_t i=0; i<sizeof(fnt)/sizeof(SmoothFont*); i++)
    if (fnt[i]) {
      delete fnt[i];
    }
}

SmoothFont* FontCollection::operator[](int index) {
  if (!fnt[index]) {
    fnt[index] = new SmoothFont;
    switch(index) {
    case OPENSANS_COND_BOLD_20:
      fnt[OPENSANS_COND_BOLD_20]->loadFont(OpenSans_CondBold20);
      break;
    case AKROBAT_BOLD_16:
      fnt[AKROBAT_BOLD_16]->loadFont(Akrobat_Bold16);
      break;
    case AKROBAT_BOLD_18:
      fnt[AKROBAT_BOLD_18]->loadFont(Akrobat_Bold18);
      break;
    case AKROBAT_BOLD_20:
      fnt[AKROBAT_BOLD_20]->loadFont(Akrobat_Bold20);
      break;
    case AKROBAT_BOLD_22:
      fnt[AKROBAT_BOLD_22]->loadFont(Akrobat_Bold22);
      break;
    case AKROBAT_BOLD_24:
      fnt[AKROBAT_BOLD_24]->loadFont(Akrobat_Bold24);
      break;
    case AKROBAT_SEMIBOLD_20:
      fnt[AKROBAT_SEMIBOLD_20]->loadFont(Akrobat_SemiBold20);
      break;
    case AKROBAT_SEMIBOLD_22:
      fnt[AKROBAT_SEMIBOLD_22]->loadFont(Akrobat_SemiBold22);
      break;
    case AKROBAT_EXTRABOLD_22:
      fnt[AKROBAT_EXTRABOLD_22]->loadFont(Akrobat_ExtraBold22);
      break;
    case AKROBAT_BOLD_32:
      fnt[AKROBAT_BOLD_32]->loadFont(Akrobat_Bold32);
      break;
    case AKROBAT_BOLD_90:
      fnt[AKROBAT_BOLD_90]->loadFont(Akrobat_Bold90);
      break;
    }
  }
  return fnt[index];
}

SmoothFont* FontCollection::operator[](FontIndex_t index) {
  return this->operator[]((int) index);
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  DRAWING  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void GUI::redrawScreen(bool redrawHeader, bool redrawFooter, bool redrawScreen, bool redrawAll) {
  //log_v("GUI: %d %d %d %d", redrawHeader, redrawFooter, redrawScreen, redrawAll);
  if (!redrawHeader && !redrawFooter && !redrawScreen && !redrawAll) {
    log_d("nothing to redraw");
    return;
  }

  if (powerOffScreen) {
    return;  // don't draw anything while expecting a power shutdown
  }

  // Step 1: redraw main (middle) part of screen

  bool hfDrawn = false;
  LCD* appScreen = screen;
  if (state.locked && (redrawFooter || redrawHeader) && clockApp!=NULL) {
    redrawScreen = true;
    redrawHeader = redrawFooter = false;
  }

  if (redrawScreen) {

    if (callApp!=NULL) {

      callApp->resetPush();
      callApp->redrawScreen(redrawAll);
      appScreen = &(callApp->getScreen());
      if (redrawAll && callApp->isWindowed()) {
        redrawHeader = redrawFooter = true;
      }

    } else if (state.locked && clockApp!=NULL) {

      clockApp->resetPush();
      clockApp->redrawScreen(redrawAll);
      appScreen = &(clockApp->getScreen());
      if (!clockApp->isWindowed()) {
        hfDrawn = true;
      }

    } else if (runningApp!=NULL) {

      runningApp->resetPush();
      runningApp->redrawScreen(redrawAll);
      appScreen = &(runningApp->getScreen());

      if (!runningApp->isWindowed()) {
        hfDrawn = true;
      } else if (redrawAll && runningApp->isWindowed()) {
        redrawHeader = redrawFooter = true;
      }

    } else if (curApp == GUI_APP_MENU) {

      // Menu appearance: special case to allow background image

      //log_d("REDRAW main menu");
      if (!menuDrawn) {
        if (bgImage && bgImage->isCreated() && screen->isSprite()) {
          bgImage->cloneDataInto((TFT_eSprite*) screen);
        } else {
          // Clear menu area
          screen->fillRect(0, header->height(), lcd.width(), lcd.height()-footer->height()-footer->height(), THEME_BG);
        }
        ((GUIWidget*)header)->redraw(*screen);
        ((GUIWidget*)footer)->redraw(*screen);
        hfDrawn = true;
      }

      if (mainMenu) {
        ((GUIWidget*)mainMenu)->redraw(*screen);
      }

      if (!screen->isSprite()) {
        menuDrawn = true;
        menuNewItems = false;
      }

    } else {

      // Unknown app screen (this should never happen)
      log_d("REDRAW unknown");
      screen->fillScreen(THEME_BG);
      screen->setTextColor(THEME_COLOR, THEME_BG);
      screen->setTextFont(fonts[OPENSANS_COND_BOLD_20]);
      screen->setTextDatum(MC_DATUM);
      char buff[20];
      sprintf(buff, "APP:%d", curApp & (~(GUI_BASE_APP)));
      screen->drawString(buff, screen->width()/2, screen->height()/2);

    }
  }

  // Step 2: redraw header or footer into the screen sprite

  if (!hfDrawn) {
    if (redrawHeader) {
      //log_d("REDRAW header");
//      if (callApp != NULL) {
//        if (callApp->isWindowed() && ((WindowedApp*) callApp)->getTitle())
//          this->header->setTitle(((WindowedApp*) callApp)->getTitle());
//      } else if (runningApp != NULL) {
//        if (runningApp->isWindowed() && ((WindowedApp*) runningApp)->getTitle())
//          this->header->setTitle(((WindowedApp*) runningApp)->getTitle());
//      }
      ((GUIWidget*) this->header)->redraw(*screen);
    }
    if (redrawFooter) {
      //log_d("REDRAW footer");
//      if (callApp != NULL) {
//        if (callApp->isWindowed() && (((WindowedApp*) callApp)->getLeftButton() || ((WindowedApp*) callApp)->getRightButton()))
//          this->footer->setButtons(((WindowedApp*) callApp)->getLeftButton(), ((WindowedApp*) callApp)->getRightButton());
//      } else if (runningApp != NULL) {
//        if (runningApp->isWindowed() && (((WindowedApp*) runningApp)->getLeftButton() || ((WindowedApp*) runningApp)->getRightButton()))
//          this->footer->setButtons(((WindowedApp*) runningApp)->getLeftButton(), ((WindowedApp*) runningApp)->getRightButton());
//      }
      ((GUIWidget*) this->footer)->redraw(*screen);
    }
  }

  // Step 3: push relevant parts of screen (if using sprite)

  if (state.screenBrightness != 0 || state.screenWakeUp) {      // Optimization: do not push the screen out if the screen is sleeping

    if (redrawScreen) {
      //log_d("REDRAW pushing");
      // In case screen was redrawn, we just push the entire screen for simplicity (cutting away header and footer doesn't save much time; but TODO: possible optimization)
      if (callApp != NULL) {
        callApp->pushScreen();
      } else if (appScreen->isSprite()) {
        this->pushScreen(appScreen);
      }
    }
    // When appScreen is a sprite and entire screen was pushed out above, then no need to draw header and footer parts separately
    if (!redrawScreen || !appScreen->isSprite()) {
      if (redrawHeader) {
        //log_d("REDRAW pushing header");
        this->pushScreenPart(screen, ((GUIWidget*)header)->getParentOffY(), ((GUIWidget*)header)->height());
      }
      if (redrawFooter) {
        //log_d("REDRAW pushing footer");
        this->pushScreenPart(screen, ((GUIWidget*)footer)->getParentOffY(), ((GUIWidget*)footer)->height());
      }
    }
    if (state.screenWakeUp) {
      state.screenWakeUp = false;
      // Maximize screen brightness according to the settings
#if GPIO_EXTENDER == 1509
      lcdLedOnOff(true, conv100to255(state.brightLevel));
#endif // GPIO_EXTENDER == 1509
      state.screenBrightness = state.brightLevel;
    }

  }
};

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  MENU HELPERS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

int16_t GUI::findMenu(uint16_t ID) {
  int16_t r;
  for (r=0; r<sizeof(menu)/sizeof(GUIMenuItem); r++) {
    if (menu[r].ID == ID) {
      return r;
    }
  }
  return -1;
};

int16_t GUI::findMenuIcons(uint16_t ID) {
  int16_t r;
  for (r=0; r<sizeof(menuIcons)/sizeof(GUIMenuItemIcons); r++) {
    if (menuIcons[r].ID == ID) {
      return r;
    }
  }
  return -1;
};

int16_t GUI::findSubMenu(uint16_t ID, uint16_t sel) {
  int16_t r, c = 0;
  for (r=0; r<sizeof(menu)/sizeof(GUIMenuItem); r++) {
    if (menu[r].parent == ID) {
      if (sel==c) {
        return r;
      }
      c++;
    }
  }
  return -1;
};

/* Description:
 *      set "automatic" menu variables
 */
void GUI::enterMenu(uint16_t ID) {
  log_d("entering menu = ", ID);

  curMenuId = ID;
  if (ID & GUI_BASE_APP) {
    return;
  }

  if (mainMenu) {
    delete mainMenu;
    mainMenu = NULL;
  };

  int16_t menuIndex = findMenu(ID);
  if (menuIndex<0) {
    guiError("enterMenu failed");
    return;
  }

  // Create and populate new widget
  int i, j;
  MenuOptionIconned* option;
  mainMenu = new MenuWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(),
                            "**EMPTY**", fonts[AKROBAT_EXTRABOLD_22], N_MENU_ITEMS, 8, false);
  mainMenu->setStyle(MenuWidget::DEFAULT_STYLE, WHITE, NONE, BLACK, WHITE);    // note: regular background color ignored
  for(i=0; i<sizeof(menu)/sizeof(GUIMenuItem); i++) {
    if (menu[i].parent == menu[menuIndex].ID) {
      j = findMenuIcons(menu[i].ID);
      if (j<0) {
        option = new MenuOptionIconned(menu[i].ID, 1, menu[i].title);
      } else {
        option = new MenuOptionIconned(menu[i].ID, 1, menu[i].title, NULL, menuIcons[j].icon1, menuIcons[j].iconSize1, menuIcons[j].icon2, menuIcons[j].iconSize2);
      }
      if (option && !mainMenu->addOption(option)) {
        delete option;
        break;
      }
    }
  }

  // Set the automatic values
  menuNewItems = true;
  menuDrawn = false;        // wee need this to update header and footer; TODO: too strong? (optimize with menuNewItems)

  // TODO: remove
  curMenuSel = 0;
  curMenuSize = 0;
  curMenuOffset = 0;

  // Calculate size of the menu
  for (i=0; i<sizeof(menu)/sizeof(GUIMenuItem); i++) {
    if (menu[i].parent == menu[menuIndex].ID) {
      curMenuSize++;
    }
  }

  // Update header and footer text
  header->setTitle(menu[menuIndex].title);
  footer->setButtons(menu[menuIndex].leftButton, menu[menuIndex].rightButton);
};

void GUI::exitMenu() {
  int16_t mi = findMenu(curMenuId);
  log_d("exiting menu: %d", curMenuId);
  if (mi<0) {
    guiError("menu failed");
    return;
  }
  if (menu[mi].parent>0) {          // if it is a descendant of Main Menu
    enterMenu(menu[mi].parent);     // switch to parent menu      TODO: get parameters from stack  (exitMenu)
  }
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  APP HELPERS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

/* Description:
 *     create call app as callee
 */
void GUI::becomeCallee() {
  if (callApp==NULL) {
    // Entering call
    // TODO: preserve header/footer
    callApp = new CallApp(this->audio, this->lcd, this->state, false, this->header, this->footer);     // false for callee
  }
}

void GUI::exitCall() {
  if (callApp!=NULL) {
    delete callApp;
    callApp = NULL;
  }
}

bool GUI::inCall() {
  log_d("SIP STATE: ");
  showCallState(state.sipState);
  log_d("");
  return !(state.sipState == CallState::NotInited || state.sipState == CallState::Idle || state.sipState == CallState::Error );
}

void GUI::enterApp(ActionID_t app) {
  log_d("entering app");

  // Free dynamic memory
  cleanAppDynamic();
  flash.end();

  // Change to new app
  switch(app) {
  case GUI_APP_MYAPP:
    runningApp = new MyApp(audio, *screen, state, header, footer);
    break;
  case GUI_APP_OTA:
    runningApp = new OtaApp(*screen, state, header, footer);
    break;
  case GUI_APP_MENU:
    state.setInputState(InputType::Numeric);
    menuDrawn = false;
    break;
  case GUI_APP_CLOCK:
    runningApp = new ClockApp(*screen, *bgImage, state);
    break;
  case GUI_APP_DIALING:
    // NOTE: passing physical screen `lcd` for creating the CallApp recursively
    runningApp = new DialingApp(audio, *screen, lcd, state, header, footer);
    break;
  case GUI_APP_PHONEBOOK:
    // NOTE: passing physical screen `lcd` for creating the CallApp recursively
    runningApp = new PhonebookApp(audio, *screen, lcd, state, flash, header, footer);
    break;
  case GUI_APP_MESSAGES:
    runningApp = new MessagesApp(*screen, state, flash, header, footer);
    break;
  case GUI_APP_SIP_ACCOUNTS:
    runningApp = new SipAccountsApp(*screen, state, flash, header, footer);
    break;
  case GUI_APP_NOTEPAD:
    runningApp = new NotepadApp(*screen, state, flash, header, footer);
    break;
  case GUI_APP_CIRCLES:
    runningApp = new CircleApp(*screen, state);       // TODO: process throw
    break;
  case GUI_APP_DIGITAL_RAIN:
    runningApp = new DigitalRainApp(lcd, state);
    break;
  case GUI_APP_UART_PASS:
    runningApp = new UartPassthroughApp(lcd, state, header, footer);
    break;
  case GUI_APP_WIDGETS:
    runningApp = new WidgetDemoApp(lcd, state);
    break;
  case GUI_APP_PICS_DEMO:
    runningApp = new PicturesDemoApp(*screen, state);
    break;
  case GUI_APP_FONT_DEMO:
    runningApp = new FontDemoApp(lcd, state);
    break;
  case GUI_APP_DESIGN_DEMO:
    runningApp = new DesignDemoApp(lcd, state);
    break;
  case GUI_APP_MIC_TEST:
    // NOTE: `lcd` - this app draws to the physical screen directly
    runningApp = new MicTestApp(audio, lcd, state, header, footer);
    break;
  case GUI_APP_RECORDER:
    // NOTE: `lcd` - this app draws to the physical screen directly
    runningApp = new RecorderApp(audio, lcd, state, header, footer);
    break;
  case GUI_APP_DIAGNOSTICS:
    // NOTE: `lcd` - this app draws to the physical screen directly
    //runningApp = new DiagnosticsApp(audio, lcd, state, header, footer);
    runningApp = new DiagnosticsApp(audio, lcd, state);
    break;
#ifdef BUILD_GAMES
  case GUI_APP_FIDE_CHESS:
    runningApp = new ChessApp(audio, *screen, state, header, footer);
    break;
  case GUI_APP_CHESS960:
    runningApp = new ChessApp(audio, *screen, state, header, footer, ChessApp::Chess960);
    break;
  case GUI_APP_HILL_CHESS:
    runningApp = new ChessApp(audio, *screen, state, header, footer, ChessApp::KingOfTheHill);
    break;
  case GUI_APP_ACKMAN:
    // NOTE: `lcd` - this app draws to the physical screen directly
    runningApp = new AckmanApp(audio, lcd, state);
    break;
#endif
#ifdef LED_BOARD
  case GUI_APP_LED_MIC:
    runningApp = new LedMicApp(audio, *screen, state, header, footer);
    break;
#endif
  case GUI_APP_EDITWIFI:
    runningApp = new EditNetworkApp(*screen, state, NULL, header, footer);
    break;
  case GUI_APP_TIME_CONFIG:
    runningApp = new TimeConfigApp(*screen, state, header, footer);
    break;
  case GUI_APP_SCREEN_CONFIG:
    runningApp = new ScreenConfigApp(*screen, state, header, footer);
    break;
  case GUI_APP_NETWORKS:
    runningApp = new NetworksApp(*screen, state, header, footer);
    break;
  case GUI_APP_UDP:
    runningApp = new UdpSenderApp(*screen, state, flash, header, footer);
    break;
  case GUI_APP_AUDIO_CONFIG:
    runningApp = new AudioConfigApp(audio, *screen, state, header, footer);
    break;
  case GUI_APP_PARCEL:
    runningApp = new ParcelApp(*screen, state, flash, header, footer);
    break;
#ifdef MOTOR_DRIVER
  case GUI_APP_MOTOR:
    runningApp = new MotorDriverApp(*screen, state, header, footer);
    break;
#endif
  case GUI_APP_PIN_CONTROL:
    runningApp = new PinControlApp(*screen, state, header, footer);
    break;
  case GUI_APP_SPLASH:
    runningApp = new SplashApp(*screen, state);
    break;
  default:
    break;
  }

  // Set current app
  curApp = app;
  log_d("entered app: %d", curApp);
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  APPEARANCE HELPERS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void GUI::guiError(const char* s) {
  log_e("guiError: %s", s);
  lcd.setTextColor(WHITE, RED);
  lcd.setTextFont(2);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.drawString(s, 10, 10);
  delay(5000);
};

void GUI::circle(uint16_t x, uint16_t y, uint16_t r, uint16_t col) {
  lcd.fillCircle(x, y, r, col);
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  APP CLASSES  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  WiPhone app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

WiPhoneApp::WiPhoneApp(LCD& lcd, ControlState& state) : lcd(lcd), controlState(state), registeredWidgets(0) {
  anyEventLastStack = controlState.msAppTimerEventLast;
  anyEventPeriodStack = controlState.msAppTimerEventPeriod;

  //controlState.msAppTimerEventPeriod = 0;    // unsubscribe from periodic "anyevent"
};

WiPhoneApp::~WiPhoneApp() {
  log_v("destroy WiPhoneApp");
  controlState.msAppTimerEventLast = anyEventLastStack;
  controlState.msAppTimerEventPeriod = anyEventPeriodStack;

  int cnt = 0;
  for (size_t i=0; i<registeredWidgets.size(); i++) {
    if (registeredWidgets[i]!=NULL) {
      delete registeredWidgets[i];
      registeredWidgets[i] = NULL;
      cnt++;
    }
  }
  if (cnt) {
    log_d("WIDGETS DELETED: %d", cnt);
  }
}

void WiPhoneApp::registerWidget(GUIWidget* w) {
  registeredWidgets.add(w);
}

void WiPhoneApp::pushScreen() {
  if (!pushed && lcd.isSprite()) {
    ((TFT_eSprite&)lcd).pushSprite(0, 0);
    pushed = true;
  }
};

void WiPhoneApp::addLabelInput(uint16_t &yOff, LabelWidget*& label, TextInputWidget*& input, const char* labelText, const uint32_t inputSize, InputType inputType) {
  label = new LabelWidget(0, yOff, lcd.width(), 25, labelText, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += label->height();
  input = new TextInputWidget(0, yOff, lcd.width(), 35, controlState, inputSize, fonts[AKROBAT_BOLD_20], inputType, 8);
  yOff += input->height();
}

void WiPhoneApp::addLabelPassword(uint16_t &yOff, LabelWidget*& label, PasswordInputWidget*& input, const char* labelText, const uint32_t inputSize, InputType inputType) {
  label = new LabelWidget(0, yOff, lcd.width(), 25, labelText, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += label->height();
  input = new PasswordInputWidget(0, yOff, lcd.width(), 35, controlState, inputSize, fonts[AKROBAT_BOLD_20], inputType, 8);
  yOff += input->height();
}

void WiPhoneApp::addInlineLabelInput(uint16_t &yOff, uint16_t labelWidth, LabelWidget*& label, TextInputWidget*& input, const char* labelText, const uint32_t inputSize, InputType inputType) {
  label = new LabelWidget(0, yOff, labelWidth, 25, labelText, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::RIGHT_TO_LEFT, 8);
  input = new TextInputWidget(labelWidth, yOff, lcd.width()-labelWidth, 25, controlState, inputSize, fonts[AKROBAT_BOLD_18], inputType, 3);
  yOff += input->height();
}

void WiPhoneApp::addDoubleLabelInput(uint16_t &yOff, LabelWidget*& label1, TextInputWidget*& input1, const char* labelText1, const uint32_t inputSize1,
                                     LabelWidget*& label2, TextInputWidget*& input2, const char* labelText2, const uint32_t inputSize2, InputType inputType) {
  const auto padX = 2;
  // First set
  label1 = new LabelWidget(0, yOff, lcd.width()/2-padX, 25, labelText1, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  label2 = new LabelWidget(lcd.width()/2+padX, yOff, lcd.width() - (lcd.width()/2-padX), 25, labelText2, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += label1->height();
  input1 = new TextInputWidget(0, yOff, lcd.width()/2-2, 35, controlState, inputSize1, fonts[AKROBAT_BOLD_20], inputType, 8);
  input2 = new TextInputWidget(lcd.width()/2+padX, yOff, lcd.width() - (lcd.width()/2-padX), 35, controlState, inputSize2, fonts[AKROBAT_BOLD_20], inputType, 8);
  yOff += input1->height();
}

void WiPhoneApp::addLabelSlider(uint16_t &yOff, LabelWidget*& label, IntegerSliderWidget*& input, const char* labelText, int minVal, int maxVal, const char* unit, int steps) {
  log_d("adding a label and a slider");
  label = new LabelWidget(0, yOff, lcd.width(), 25, labelText, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += label->height();
  input = new IntegerSliderWidget(0, yOff, lcd.width(), 25, minVal, maxVal, (maxVal-minVal)/steps, true, unit);
  yOff += input->height();
}

void WiPhoneApp::addInlineLabelSlider(uint16_t &yOff, uint16_t labelWidth, LabelWidget*& label, IntegerSliderWidget*& input, const char* labelText,
                                      int minVal, int maxVal, const char* unit, int steps) {
  label = new LabelWidget(0, yOff, labelWidth, 25, labelText, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::RIGHT_TO_LEFT, 8);
  input = new IntegerSliderWidget(labelWidth, yOff, lcd.width() - labelWidth, 25, minVal, maxVal, (maxVal-minVal)/steps, true, unit);
  yOff += input->height();
}

void WiPhoneApp::addInlineLabelYesNo(uint16_t &yOff, uint16_t labelWidth, LabelWidget*& label, YesNoWidget*& input, const char* labelText) {
  label = new LabelWidget(0, yOff, labelWidth, 25, labelText, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::RIGHT_TO_LEFT, 8);
  input = new YesNoWidget(labelWidth, yOff, lcd.width()-labelWidth, 25, fonts[AKROBAT_BOLD_18]);
  yOff += input->height();
}

void WiPhoneApp::addRuler(uint16_t &yOff, RulerWidget*& ruler, uint16_t addOffset) {
  ruler = new RulerWidget(5, yOff+addOffset, lcd.width()-10);
  yOff += ruler->height() + 2*addOffset;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Focusable app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void FocusableApp::addFocusableWidget(FocusableWidget* w) {
  if (w==nullptr || !focusableWidgets.add(w)) {
    log_e("registering focusable FAILED");
  }
}

void FocusableApp::nextFocus(bool forward) {
  log_v("nextFocus FocusableApp");
  int i;
  for (i=0; i<focusableWidgets.size(); i++) {
    if (focusableWidgets[i]->getFocus()) {
      break;
    }
  }
  if (i<focusableWidgets.size()) {
    focusableWidgets[i]->setFocus(false);
    // Find next widget to focus at
    for (int c=0; c<focusableWidgets.size(); c++) {
      if (forward) {
        if (++i>=focusableWidgets.size()) {
          i = 0;
        }
      } else {
        if (--i<0) {
          i = focusableWidgets.size()-1;
        }
      }
      if (focusableWidgets[i]->getActive()) {
        break;
      }
    }
    if (focusableWidgets[i]->getActive()) {
      focusableWidgets[i]->setFocus(true);
    }
  }
}

FocusableWidget* FocusableApp::getFocused() {
  log_v("getFocused FocusableApp");
  for (int i=0; i<focusableWidgets.size(); i++) {
    if (focusableWidgets[i]->getFocus()) {
      return focusableWidgets[i];
    }
  }
  return nullptr;
}

void FocusableApp::setFocus(FocusableWidget* w) {
  log_v("setFocus FocusableApp");
  for (int i=0; i<focusableWidgets.size(); i++) {
    focusableWidgets[i]->setFocus((focusableWidgets[i] == w) ? true : false);
  }
}

void FocusableApp::deactivateFocusable() {
  log_v("deactivateFocusable FocusableApp");
  for (int i=0; i<focusableWidgets.size(); i++) {
    focusableWidgets[i]->deactivate();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Windowed app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

WindowedApp::WindowedApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WiPhoneApp(lcd, state), header(header), footer(footer) {
//  this->title = NULL;
//  this->leftButtonName = NULL;
//  this->rightButtonName = NULL;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Theaded app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ThreadedApp::ThreadedApp(LCD& lcd, ControlState& state)
  : WiPhoneApp(lcd, state) {
  xHandle = NULL;
  // Example:
  //xTaskCreate(&ThreadedApp::thread, "digitalrain", 8192, this, tskIDLE_PRIORITY, &xHandle);
}

ThreadedApp::~ThreadedApp() {
  if (xHandle != NULL) {
    log_d("deleting task");
    vTaskDelete(xHandle);
    xHandle = NULL;
  }
}

appEventResult ThreadedApp::processEvent(EventType event) {
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  // Take event and put into queue
  // TODO
  return DO_NOTHING;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Ota app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

OtaApp::OtaApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer) :
  WindowedApp(lcd, state, header, footer), FocusableApp(2), updateAvailable(false), manualUpdateRequested(false), manualCheckRequested(false), installBtnAdded(false) {
  log_d("OtaAPP constructor");

  Ota ota("");

  controlState.msAppTimerEventLast = millis();
  controlState.msAppTimerEventPeriod = 500;

  // Create and arrange widgets
  header->setTitle("Firmware Update");
  footer->setButtons("Save", "Clear");

  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  // State caption in the middle
  const uint16_t spacing = 4;
  uint16_t yOff = header->height() + 26;

  urlLabel = new LabelWidget(0, yOff, lcd.width(), 25, "URL:", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += urlLabel->height();

  url = new TextInputWidget(0, yOff, lcd.width(), 35, controlState, 100, fonts[AKROBAT_BOLD_20], InputType::AlphaNum, 8);

  url->setText(ota.getIniUrl());
  yOff += url->height();

  autoLabel = new LabelWidget(0, yOff, lcd.width(), 25, "Auto Update:", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += autoLabel->height();

  autoUpdate = new ChoiceWidget(0, yOff, lcd.width(), 35);
  autoUpdate->addChoice("Yes");
  autoUpdate->addChoice("No");
  yOff += autoUpdate->height();

  if (ota.autoUpdateEnabled()) {
    autoUpdate->setValue(0);
  } else {
    autoUpdate->setValue(1);
  }

  char device_version[400] = {0};
  snprintf(device_version, sizeof(device_version), "Dev: %s  Srv: ", FIRMWARE_VERSION);

  deviceVersion = new LabelWidget(0, yOff, lcd.width(), 25, device_version, WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += deviceVersion->height();

  lastInstall = new LabelWidget(0, yOff, lcd.width(), 25, "", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += lastInstall->height();

  checkForUpdates = new ButtonWidget(0, yOff, "Check");
  reset = new ButtonWidget(60, yOff, "Reset");
  installUpdates = new ButtonWidget(120, yOff, "Install");

  setDataFromOtaFile(ota);

  addFocusableWidget(url);
  addFocusableWidget(autoUpdate);
  addFocusableWidget(checkForUpdates);
  addFocusableWidget(reset);
  addFocusableWidget(installUpdates);

  if (ota.updateExists(false)) {
    installBtnAdded = true;
  }

  setFocus(url);
}

void OtaApp::setDataFromOtaFile(Ota &o, bool errorAsUpdate) {
  char last_install[400] = {0};
  const char* error_code = o.getLastErrorCode();
  const char* error_string = o.getLastErrorString();

  if (errorAsUpdate && (strcmp(error_code, "") == 0 || strcmp(error_code, "0") == 0)) {
    if (o.updateExists(false)) {
      snprintf(last_install, sizeof(last_install), "Update available");
    } else {
      snprintf(last_install, sizeof(last_install), "No Updates");
    }
  } else {
    if (strcmp(error_code, "") == 0 || strcmp(error_code, "0") == 0) {
      snprintf(last_install, sizeof(last_install), "No error", FIRMWARE_VERSION);
    } else {
      snprintf(last_install, sizeof(last_install), "Error: %s - %s", error_code, error_string);
    }
  }


  lastInstall->setText(last_install);

  char device_version[400] = {0};
  snprintf(device_version, sizeof(device_version), "Dev: %s  Srv: %s", FIRMWARE_VERSION, o.getServerVersion());
  deviceVersion->setText(device_version);
}

OtaApp::~OtaApp() {
  delete urlLabel;
  delete url;
  delete autoLabel;
  delete autoUpdate;
  delete deviceVersion;
  delete lastInstall;
  delete checkForUpdates;
  delete reset;
  delete installUpdates;
}
appEventResult OtaApp::processEvent(EventType event) {
  appEventResult res = DO_NOTHING;
  FocusableWidget* focusedWidget = getFocused();

  if (manualUpdateRequested) {
    Ota o("");
    o.setUserRequestedUpdate(true);
    o.reset();
    ESP.restart();
  }

  if (manualCheckRequested) {
    manualCheckRequested = false;
    bool updates = true;
    Ota o("");
    if (o.updateExists(true)) {
      if (!installBtnAdded) {
        addFocusableWidget(installUpdates);
        installBtnAdded = true;
      }
    }

    setDataFromOtaFile(o, updates);
    res |= REDRAW_SCREEN;
    log_d("Returning from ota check");
    return res;
  }

  if (focusedWidget != checkForUpdates) {
    footer->setButtons("Save", "Clear");
    res |= REDRAW_SCREEN;
  }

  if (event == WIPHONE_KEY_END) {     // the button below BACK, which, it turn, is used as backspace
    return EXIT_APP;
  } else if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN) {
    nextFocus(event == WIPHONE_KEY_DOWN);
  } else if (event == WIPHONE_KEY_OK && focusedWidget == checkForUpdates) {
    manualCheckRequested = true;
    lastInstall->setText("Checking...");
    res |= REDRAW_SCREEN;
  } else if (event == WIPHONE_KEY_OK && focusedWidget == reset) {
    Ota o("");
    o.resetIni();
    url->setText(o.getIniUrl());
    if (o.autoUpdateEnabled()) {
      autoUpdate->setValue(0);
    } else {
      autoUpdate->setValue(1);
    }
    res |= REDRAW_SCREEN;
  } else if (event == WIPHONE_KEY_OK && focusedWidget == installUpdates) {
    manualUpdateRequested = true;
    lastInstall->setText("Restarting...");
    res |= REDRAW_SCREEN;
  } else if (LOGIC_BUTTON_OK(event)) {

    // Save the current settings
    Ota o("");
    o.ensureUserVersion();
    o.setIniUrl(url->getText());

    switch (autoUpdate->getValue()) {
    case 0: // yes
      o.saveAutoUpdate(true);
      break;
    case 1: // no
      o.saveAutoUpdate(false);
      break;
    }

    return EXIT_APP;
  } else {
    ((GUIWidget*) getFocused())->processEvent(event);
    res |= REDRAW_SCREEN;
  }

  return res;
}

void OtaApp::redrawScreen(bool redrawAll) {
  log_d("Redraw screen");
  //if (!screenInited || redrawAll) {
  log_d("redraw all");
  // Initialize screen
  ((GUIWidget*) clearRect)->redraw(lcd);

  ((GUIWidget*) urlLabel)->redraw(lcd);
  ((GUIWidget*) url)->redraw(lcd);
  ((GUIWidget*) autoLabel)->redraw(lcd);
  ((GUIWidget*) autoUpdate)->redraw(lcd);
  ((GUIWidget*) deviceVersion)->redraw(lcd);
  ((GUIWidget*) lastInstall)->redraw(lcd);
  ((GUIWidget*) checkForUpdates)->redraw(lcd);
  ((GUIWidget*) reset)->redraw(lcd);

  if (installBtnAdded) {
    ((GUIWidget*) installUpdates)->redraw(lcd);
  }
  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  MyApp demo app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MyApp::MyApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(2), audio(audio) {
  log_d("MyApp create");
  const char* s;

  // Create and arrange widgets
  header->setTitle("MyApp Demo");
  footer->setButtons("Yes", "No");
  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  // State caption in the middle
  const uint16_t spacing = 4;
  uint16_t yOff = header->height() + 26;
  demoCaption = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_BOLD_20]->height(),
                                "Hello World", WP_ACCENT_S, WP_COLOR_1, fonts[AKROBAT_BOLD_20], LabelWidget::CENTER);
  yOff += demoCaption->height() + (spacing*2);

  // Make an icon
  iconRect = new RectIconWidget((lcd.width()-50)>>1, yOff, 50, 50, WP_ACCENT_1, icon_person_w, sizeof(icon_person_w));
  yOff += iconRect->height() + (spacing*2);

  /*// Name and URI above
  s = controlState.calleeNameDyn!=NULL ? (const char*) controlState.calleeNameDyn : "";
  nameCaption =  new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_EXTRABOLD_22]->height(), s, WP_COLOR_0, WP_COLOR_1, fonts[AKROBAT_EXTRABOLD_22], LabelWidget::CENTER);
  yOff += nameCaption->height() + spacing;
  s = controlState.calleeUriDyn!=NULL ? (const char*) controlState.calleeUriDyn : "";
  uriCaption =  new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_BOLD_20]->height(), s, WP_DISAB_0, WP_COLOR_1, fonts[AKROBAT_BOLD_20], LabelWidget::CENTER);*/

  // Debug string: shows some simple debug info
  //yOff += uriCaption->height() + 20;
  //s = controlState.lastReasonDyn != NULL ? (const char*) controlState.lastReasonDyn : "";
  s = "I'm Awesome!";
  debugCaption = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_BOLD_16]->height(), s, WP_DISAB_0, WP_COLOR_1, fonts[AKROBAT_BOLD_16], LabelWidget::CENTER);

  //reasonHash = hash_murmur(controlState.lastReasonDyn);
}

MyApp::~MyApp() {
  log_d("destroy MyApp");

  delete demoCaption;
  delete debugCaption;
  //sdelete debugCaption_loudSpkr;
  //delete nameCaption;
  //delete uriCaption;
}

appEventResult MyApp::processEvent(EventType event) {
  log_d("processEvent MyApp");
  appEventResult res = DO_NOTHING;

  if (LOGIC_BUTTON_BACK(event)) {

    demoCaption->setText("Back Button");
    res |= REDRAW_SCREEN;

  } else if (LOGIC_BUTTON_OK(event)) {

    demoCaption->setText("OK Button");
    footer->setButtons("OH", "NO");
    res |= REDRAW_SCREEN | REDRAW_FOOTER;

  } else if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN) {

    int8_t earpieceVol, headphonesVol, loudspeakerVol;
    audio->getVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    int8_t d = event == WIPHONE_KEY_UP ? 6 : -6;
    earpieceVol += d;
    headphonesVol += d;
    loudspeakerVol += d;
    audio->setVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    audio->getVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    char buff[70];
    snprintf(buff, sizeof(buff), "Speaker %d dB, Headphones %d dB, Loudspeaker %d dB",  earpieceVol, headphonesVol, loudspeakerVol);
    debugCaption->setText(buff);
    
  }

  return res;
}

void MyApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen MyApp");

  if (!screenInited || redrawAll) {
    log_d("redraw all");
    // Initialize screen
    ((GUIWidget*) clearRect)->redraw(lcd);

    ((GUIWidget*) iconRect)->redraw(lcd);
    ((GUIWidget*) demoCaption)->redraw(lcd);
    ((GUIWidget*) debugCaption)->redraw(lcd);
    //((GUIWidget*) nameCaption)->redraw(lcd);
    //((GUIWidget*) uriCaption)->redraw(lcd);

    //lcd.fillRect(95, 240, 50, 20, WP_ACCENT_1);     // DEBUG: very strange bug with white pixels over black border

  } else {

    // Refresh only updated labels
    if (demoCaption->isUpdated()) {
      log_d("stateCaption updated");
      ((GUIWidget*) demoCaption)->redraw(lcd);
    }
    if (debugCaption->isUpdated()) {
      log_d("debugCaption updated");
      ((GUIWidget*) debugCaption)->redraw(lcd);
    }
    /*if (nameCaption->isUpdated()) {
      log_d("nameCaption updated");
      ((GUIWidget*) nameCaption)->redraw(lcd);
    }
    if (uriCaption->isUpdated()) {
      log_d("uriCaption updated");
      ((GUIWidget*) uriCaption)->redraw(lcd);
    }*/
  }
  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  UART passthrough app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -
UartPassthroughApp::UartPassthroughApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(2), screenInited(false), startedSerial(false),
    xHandle0(NULL), xHandle1(NULL) {
  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  controlState.msAppTimerEventLast = millis();
  controlState.msAppTimerEventPeriod = 500;

  // State caption in the middle
  const uint16_t spacing = 4;
  uint16_t yOff = header->height() + 26;

  baudLabel = new LabelWidget(0, yOff, lcd.width(), 25, "Baud:", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += baudLabel->height();

  baud = new TextInputWidget(0, yOff, lcd.width(), 35, controlState, 100, fonts[AKROBAT_BOLD_20], InputType::AlphaNum, 8);
  yOff += baud->height();

  echoLabel = new LabelWidget(0, yOff, 100, 25, "Echo:", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);

  echo = new ChoiceWidget(105, yOff, lcd.width()-110, 35);
  echo->addChoice("Yes");
  echo->addChoice("No");
  echo->setValue(1);
  yOff += echo->height();

  startStop = new ButtonWidget(0, yOff, "Start");

  addFocusableWidget(baud);
  addFocusableWidget(echo);
  addFocusableWidget(startStop);

  setFocus(baud);
}

UartPassthroughApp::~UartPassthroughApp() {
  delete baudLabel;
  delete baud;
  delete startStop;
  delete echo;
  delete echoLabel;

  if (xHandle0 != NULL) {
    vTaskDelete(xHandle0);
    xHandle0 = NULL;
  }

  if (xHandle1 != NULL) {
    vTaskDelete(xHandle1);
    xHandle1 = NULL;
  }

  if (startedSerial) {
    uart_driver_delete(UART_NUM_0);
    uart_driver_delete(UART_NUM_1);

    startedSerial = false;
  }

  const uart_config_t uart_config = {
    .baud_rate = SERIAL_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  int RX_BUF_SIZE =  1024;

  uart_param_config(UART_NUM_0, &uart_config);
  uart_driver_install(UART_NUM_0, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
}

void UartPassthroughApp::thread(void *pvParam) {
  uartThreadParams *params = (uartThreadParams*)pvParam;

  uint8_t* data = (uint8_t*) malloc(1024);
  while (1) {
    const int rxBytes = uart_read_bytes(params->rxPort, data, 1024, 1000 / 300);
    if (rxBytes > 0) {
      data[rxBytes] = 0;
      uart_write_bytes(params->txPort, (const char*)data, rxBytes);
    }
  }
  free(data);
}

void UartPassthroughApp::redrawScreen(bool redrawAll) {
  // Initialize screen
  if (redrawAll || !screenInited) {
    ((GUIWidget*) clearRect)->redraw(lcd);

    ((GUIWidget*) baudLabel)->redraw(lcd);
    ((GUIWidget*) baud)->redraw(lcd);
    ((GUIWidget*) startStop)->redraw(lcd);
    ((GUIWidget*) echo)->redraw(lcd);
    ((GUIWidget*) echoLabel)->redraw(lcd);
  } else {
    if (baud->isUpdated()) {
      ((GUIWidget*) baud)->redraw(lcd);
    }
    if (startStop->isUpdated()) {
      ((GUIWidget*) startStop)->redraw(lcd);
    }
    if (echo->isUpdated()) {
      ((GUIWidget*) echo)->redraw(lcd);
    }
  }

  screenInited = true;
}

appEventResult UartPassthroughApp::processEvent(EventType event) {
  appEventResult res = DO_NOTHING;
  FocusableWidget* focusedWidget = getFocused();

  if (LOGIC_BUTTON_OK(event) && focusedWidget == startStop) {
    if (!startedSerial) {
      startedSerial = true;
      ((ButtonWidget*) focusedWidget)->setText("stop");

      const uart_config_t uart_config = {
        .baud_rate = atoi(baud->getText()),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
      };

      int RX_BUF_SIZE =  1024;

      uart_param_config(UART_NUM_0, &uart_config);
      uart_driver_install(UART_NUM_0, RX_BUF_SIZE * 2, 0, 0, NULL, 0);

      uart_param_config(UART_NUM_1, &uart_config);
      uart_set_pin(UART_NUM_1, USER_SERIAL_TX, USER_SERIAL_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
      uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);

      switch (echo->getValue()) {
      case 1: // no
        uart0Thread.rxPort = UART_NUM_0;
        uart0Thread.txPort = UART_NUM_1;
        uart1Thread.rxPort = UART_NUM_1;
        uart1Thread.txPort = UART_NUM_0;
        xTaskCreate(&UartPassthroughApp::thread, "uart0", 1024, &uart0Thread, tskIDLE_PRIORITY + 1, &xHandle0);
        xTaskCreate(&UartPassthroughApp::thread, "uart1", 1024, &uart1Thread, tskIDLE_PRIORITY + 1, &xHandle1);
        break;
      case 0: // yes
        uart0Thread.rxPort = UART_NUM_0;
        uart0Thread.txPort = UART_NUM_0;
        xTaskCreate(&UartPassthroughApp::thread, "uart0", 1024, &uart0Thread, tskIDLE_PRIORITY + 1, &xHandle0);
        break;
      }
    } else {
      startedSerial = false;
      ((ButtonWidget*) focusedWidget)->setText("start");

      if (xHandle0 != NULL) {
        vTaskDelete(xHandle0);
        xHandle0 = NULL;
      }

      if (xHandle1 != NULL) {
        vTaskDelete(xHandle1);
        xHandle1 = NULL;
      }
    }

    res |= REDRAW_SCREEN;
  } else if (event == WIPHONE_KEY_END) {
    return EXIT_APP;
  } else if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN) {
    nextFocus(event == WIPHONE_KEY_DOWN);
    res |= REDRAW_SCREEN;
  } else {
    if (focusedWidget != NULL) {
      ((GUIWidget*) focusedWidget)->processEvent(event);
      res |= REDRAW_SCREEN;
    }
  }

  return res;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Digital Rain app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -
DigitalRainApp::DigitalRainApp(LCD& lcd, ControlState& state)
  : ThreadedApp(lcd, state), sprite(&lcd) {
  log_d("DigitalRainApp::DigitalRainApp");
  BaseType_t xStatus = xTaskCreate(&DigitalRainApp::thread, "digitalrain", 4096, this, tskIDLE_PRIORITY + 1, &xHandle);

  if (xStatus != pdPASS) {
    //vTaskDelete(xHandle);
    //xHandle = NULL;
    log_e("xTaskCreate returned (%d)", (int32_t)xStatus);
    return;
  }

  log_d("DigitalRainApp created task");

  sprite.setColorDepth(16);
  sprite.createSprite(6, 8);
  if (!sprite.isCreated()) {
    log_d("error: char sprite not created");
  }
  sprite.setTextDatum(TL_DATUM);
  sprite.setTextSize(1);
  sprite.setTextFont(1);
}

DigitalRainApp::~DigitalRainApp() {
}

void DigitalRainApp::thread(void *pvParam) {
  DigitalRainApp* parent = (DigitalRainApp*) pvParam;
  parent->clear();
  uint32_t cnt = 0;
  while (1) {
    parent->draw();
    if (cnt++ % 25 == 0) {
      log_d("thread");
    }
    vTaskDelay(30);
  }
}

char DigitalRainApp::randPrintable() {
  char chr = Random.random() & 0xFF;
  if (!chr || chr == ' ' || chr == 255) {
    chr = ((Random.random() & 0x7F) | 0x80) - 1;
  }
  return chr;
}

void DigitalRainApp::clear() {
  // Clear screen
  lcd.fillScreen(TFT_BLACK);

  // Initialize "digitalrain"
  for (auto j = 0; j < 39; j++)
    for (auto i = 0; i < 40; i++) {
      text[j][i] = this->randPrintable();
      brightness[j][i] = Random.random() % 65;
    }
}

void DigitalRainApp::drawMirroredChar(char c, uint16_t x, uint16_t y, colorType color) {
  sprite.fillSprite(TFT_BLACK);
  sprite.setTextColor(color, TFT_BLACK);
  sprite.drawChar(c, 0, 0);
  if (c < 128) {
    sprite.mirror();
  }
  sprite.pushSprite(x, y);
}

void DigitalRainApp::draw() {

  // Create new "drops" from the top
  int x = Random.random() % 49;
  if (x < 40) {
    brightness[0][x] = 64;
  }

  // Redraw all characters moving brightness down (rain)
  const uint8_t decay = 4;
  for (int j = 38; j >= 0; j--) {
    for (int i = 0; i < 40; i++) {

      // Change brightness
      bool draw;
      if (j > 0 && brightness[j-1][i] >= 64) {
        draw = true;
        brightness[j][i] = 64;
      } else {
        draw = brightness[j][i] > 0;
        brightness[j][i] = (brightness[j][i] > decay) ? brightness[j][i] - decay : 0;
      }

      // "Corrupt" digital rain
      if (Random.random() % 100 <= 5) {
        // Clean current character from screen
        if (draw) {
          this->drawMirroredChar(text[j][i], i*6, j*8, TFT_BLACK);
        }

        // Assign new character
        text[j][i] = this->randPrintable();
      }

      // Draw character
      if (draw) {
        colorType color = (brightness[j][i] < 64) ? brightness[j][i] << 5 : TFT_WHITE;
        this->drawMirroredChar(text[j][i], i*6, j*8, color);
      }

    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Notepad app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

NotepadApp::NotepadApp(LCD& lcd, ControlState& state, Storage& flashParam, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), flash(flashParam) {
  log_i("create NotepadApp");

  header->setTitle("Note Page");
  footer->setButtons("Save", "Clear");

  const int16_t padding =  4;
  textArea = new MultilineTextWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(),
                                     "Empty page", state, NotepadApp::maxNotepadSize, fonts[OPENSANS_COND_BOLD_20], InputType::AlphaNum, padding, padding);
  textArea->setColors(WP_COLOR_0, WP_COLOR_1);

  // Get notepad string dynamically
  const char* appNoteStringDyn = NULL;
  flash.loadString(cNotepadFlashPage, "note", appNoteStringDyn);
  if (appNoteStringDyn && *appNoteStringDyn) {
    // Set widget text
    textArea->setText(appNoteStringDyn);
    freeNull((void **) &appNoteStringDyn);
  }

  textArea->setFocus(true);     // to reveal the cursor
}

NotepadApp::~NotepadApp() {
  log_i("destroy NotepadApp");
  delete textArea;
}

appEventResult NotepadApp::processEvent(EventType event) {
  log_i("processEvent NotepadApp: %d", (int)event);
  if (LOGIC_BUTTON_OK(event)) {
    // Save notepad state
    const char* str = textArea->getText();
    if (str!=NULL) {
      log_v("saving note: ", str);
      flash.storeString(cNotepadFlashPage, "note", str);
    }

    return EXIT_APP;
  }
  if (event == WIPHONE_KEY_END) {
    // TODO: confirm dialog box
    return EXIT_APP;
  }
  textArea->processEvent(event);
  return REDRAW_SCREEN;
}

void NotepadApp::redrawScreen(bool redrawAll) {
  log_i("redraw NotepadApp");
  ((GUIWidget*) textArea)->redraw(lcd);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Dialing app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

DialingApp::DialingApp(Audio* audio, LCD& disp, LCD& hardDisp, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(disp, state, header, footer), FocusableApp(1), audio(audio), hardDisp(hardDisp) {
  log_d("create DialingApp");

  header->setTitle("Dialing");
  footer->setButtons("Call", "Clear");

  uint16_t yOff = header->height();
  this->errorLabel = new LabelWidget(0, yOff, lcd.width(), 65, "", TFT_RED, WP_COLOR_0, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += this->errorLabel->height();

  const int16_t xPad =  5;
  const int16_t yPad =  3;
  textArea = new MultilineTextWidget(0, yOff, lcd.width(), lcd.height() - footer->height() - yOff,
                                     NULL, state, 70, fonts[AKROBAT_BOLD_32], InputType::Numeric, xPad, yPad);
  textArea->verticalCentering(true);
  textArea->setColors(WP_COLOR_1, WP_COLOR_0);

  // Focusables
  this->addFocusableWidget(textArea);
  this->setFocus(textArea);   // to reveal the cursor
}

DialingApp::~DialingApp() {
  log_d("destroy DialingApp");
  if (callApp != NULL) {
    delete callApp;
  }
  delete textArea;
}

appEventResult DialingApp::processEvent(EventType event) {
  log_d("processEvent DialingApp: %d", event);
  if (this->callApp != NULL) {
    appEventResult res = this->callApp->processEvent(event);
    if (res & EXIT_APP) {
      delete callApp;
      callApp = NULL;
      screenInited = false;
      header->setTitle("Dialing");
      footer->setButtons("Call", "Clear");
      return REDRAW_ALL;
    }
    return res;
  } else if (LOGIC_BUTTON_OK(event)) {
    if (controlState.isCallPossible()) {
      // Make a call
      log_d("CALLING %s", textArea->getText());

      controlState.setRemoteNameUri("Dialed number", textArea->getText());
      controlState.setSipReason("");
      controlState.setSipState(CallState::InvitingCallee);

      //this->deactivateFocusable();

      // Create call app as caller
      callApp = new CallApp(this->audio, this->hardDisp, this->controlState, true, this->header, this->footer);       // true for caller

      return REDRAW_ALL;
    } else {
      this->error = true;
      this->errorLabel->setText("Not connected to SIP server");
    }
  } else if (event == WIPHONE_KEY_END) {
    return EXIT_APP;
  }
  if (event == '*') {
    event = '+';  // temporary solution to allow + input
  }
  textArea->processEvent(event);
  return REDRAW_SCREEN;
}

void DialingApp::redrawScreen(bool redrawAll) {
  log_i("redraw DialingApp");
  if (this->callApp != NULL) {
    this->callApp->redrawScreen(redrawAll);
    return;
  }
  if (!this->screenInited || this->error || redrawAll) {
    ((GUIWidget*) errorLabel)->redraw(lcd);
    this->errorLabel->setText("");
    this->error = false;
  }
  ((GUIWidget*) textArea)->redraw(lcd);
  this->screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  UDP sender app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UdpSenderApp::UdpSenderApp(LCD& lcd, ControlState& state, Storage& pref, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(14), flash(pref) {
  log_d("create UdpSenderApp");

  udp = new WiFiUDP();
  udp->begin(UDP_CLIENT_PORT);

  uint16_t yOff = header->height();
  bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);

  // Create and arrange widgets
  addLabelInput(yOff, labels[0], inputs[0], "Destination IP:", 16);
  addLabelInput(yOff, labels[1], inputs[1], "Port:", 6);
  addLabelInput(yOff, labels[2], inputs[2], "Text:", 100);
  yOff += 9;
  sendButton = new ButtonWidget(2, yOff, "Send");

  options = NULL;
  memset(shortcutLabels, 0, sizeof(shortcutLabels));
  memset(shortcutInputs, 0, sizeof(shortcutInputs));

  // Load preferences
  const char* ip = NULL;
  const char* text = NULL;
  int32_t port = -1;
  flash.loadUdpSender(ip, port, text);
  if (ip) {
    log_d("Loaded ip:   %s", ip);
  }
  if (port>=0) {
    log_d("Loaded port: %d", port);
  }
  if (text) {
    log_d("Loaded text: %s", text);
  }

  // Set text
  if (ip) {
    inputs[0]->setText(ip);
  }
  if (port>=0) {
    char buff[12];
    buff[0] = '\0';
    if (port>=0) {
      sprintf(buff, "%d", port);
    }
    inputs[1]->setText(buff);
  }
  if (text) {
    inputs[2]->setText(text);
  }

  // Free memory
  freeNull((void **) &ip);
  freeNull((void **) &text);

  // Set focusables
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    addFocusableWidget(inputs[i]);
  }
  addFocusableWidget(sendButton);

  // Start with main screen
  changeState(MAIN);

}

UdpSenderApp::~UdpSenderApp() {
  log_d("destroy UdpSenderApp");

  // Save preferences
  const char* ip   = inputs[0]->getText();
  const char* port = inputs[1]->getText();
  const char* text = inputs[2]->getText();
  int32_t portVal;
  if (port) {
    errno = 0;
    portVal = strtol(port, NULL, 10);
    if (errno) {
      portVal = -1;
    } else {
      portVal = (uint16_t) portVal;
    }
  } else {
    portVal = -1;
  }
  flash.storeUdpSender(ip, portVal, text);

  // Delete dynamic
  udp->stop();
  delete udp;
  delete bgRect;
  delete sendButton;
  for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
    delete labels[i];
  }
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    delete inputs[i];
  }

  if (options) {
    delete options;
  }
  for(uint16_t i=0; i<sizeof(shortcutLabels)/sizeof(LabelWidget*); i++)
    if (shortcutLabels[i]) {
      delete shortcutLabels[i];
    }
  for(uint16_t i=0; i<sizeof(shortcutInputs)/sizeof(TextInputWidget*); i++)
    if (shortcutInputs[i]) {
      delete shortcutInputs[i];
    }
}

void UdpSenderApp::changeState(UdpSenderState_t newState) {
  if (newState == MAIN) {

    deactivateFocusable();
    for (int k=0; k<3; k++) {
      inputs[k]->activate();
    }
    sendButton->activate();
    setFocus(inputs[0]);

    header->setTitle("UDP sender");
    footer->setButtons("Options", "Clear");

  } else if (newState == OPTIONS) {

    // Create widget
    if (!options) {
      options = new OptionsMenuWidget(0, header->height(), lcd.width(), lcd.height()-header->height()-footer->height());
      options->addOption("Shortcuts");
      addFocusableWidget(options);
    }

    // Deactivate / Activate
    deactivateFocusable();
    options->activate();
    setFocus(options);

    // Change settings
    header->setTitle("Options");
    footer->setButtons(NULL, "Back");

  } else if (newState == SHORTCUTS) {

    // Create widgets
    if (!shortcutLabels[0]) {
      // Create widgets for shortcuts screen
      uint16_t yOff = header->height()+2;
      char buff[3];
      buff[1] = ':';
      buff[2] = '\0';
      for (int k=0; k<9; k++) {
        buff[0] = '1' + k;
        addInlineLabelInput(yOff, 30, shortcutLabels[k], shortcutInputs[k], buff, 50);
        addFocusableWidget(shortcutInputs[k]);
        yOff += 2;
      }
    }

    // Deactivate / Activate
    deactivateFocusable();
    for (int k=0; k<9; k++) {
      shortcutInputs[k]->activate();
    }
    setFocus(shortcutInputs[0]);

    // Change settings
    header->setTitle("Shortcuts");
    footer->setButtons("Back", "Clear");

  }

  screenInited = false;
  appState = newState;
}

appEventResult UdpSenderApp::processEvent(EventType event) {
  log_d("processEvent UdpSenderApp: ", event);
  appEventResult res = REDRAW_SCREEN;

  // TODO: clean up this code (group by state)
  if ((event == WIPHONE_KEY_END && appState != SHORTCUTS) || (appState == OPTIONS && event == WIPHONE_KEY_BACK)) {

    if (appState == MAIN) {
      return EXIT_APP;
    } else if (appState == OPTIONS) {
      changeState(MAIN);
      res |= REDRAW_ALL;
    }

  } else if (appState == SHORTCUTS && (event == WIPHONE_KEY_END || event == WIPHONE_KEY_OK || event == WIPHONE_KEY_SELECT)) {

    changeState(OPTIONS);
    res |= REDRAW_ALL;

  } else if (appState == MAIN && event == WIPHONE_KEY_SELECT) {

    // Enter options menu
    changeState(OPTIONS);
    res |= REDRAW_ALL;

  } else if (appState == MAIN && (event == WIPHONE_KEY_CALL || (getFocused() == sendButton && (LOGIC_BUTTON_OK(event) || (event>='1' && event<='9'))) )) {

    // Send UDP packet

    // What text to send?
    const char* text = NULL;
    if (event>='1' && event<='9') {
      if (shortcutInputs[event-'1']) {
        text = shortcutInputs[event-'1']->getText();
      }
    } else {
      text = inputs[2]->getText();
    }

    if (text && strlen(text)) {

      // Really send UDP packet

      IPAddress ipAddr;
      ipAddr.fromString(inputs[0]->getText());
      const char* portString = inputs[1]->getText();
      uint16_t port = atoi(portString);

      if ((uint32_t)ipAddr != 0) {
        udp->beginPacket(ipAddr, port);
        udp->write((const byte*) text, strlen(text));
        udp->endPacket();

        log_d("UDP sent: %s", text);
      }

    } else {

      log_d("No text to send");

    }

    return DO_NOTHING;

  } else if (appState == OPTIONS && LOGIC_BUTTON_OK(event)) {

    // Enter shortcuts screen
    changeState(SHORTCUTS);
    res |= REDRAW_ALL;

  } else if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    nextFocus(event == WIPHONE_KEY_DOWN);
    if (getFocused()==sendButton) {
      controlState.setInputState(InputType::Numeric);  // TODO: automate changing InputType relevantly
    } else {
      controlState.setInputState(InputType::AlphaNum);
    }

  } else {

    // Pass event to a focused widget
    ((GUIWidget*) getFocused())->processEvent(event);

  }
  return res;
}

void UdpSenderApp::redrawScreen(bool redrawAll) {
  log_i("redraw UdpSenderApp");
  if (appState == MAIN) {

    if (!screenInited || redrawAll) {
      ((GUIWidget*) bgRect)->redraw(lcd);
      // Draw labels
      for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
        ((GUIWidget*) labels[i])->redraw(lcd);
      }
    }

    // Redraw input widgets
    for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
      ((GUIWidget*) inputs[i])->redraw(lcd);
    }

    ((GUIWidget*) sendButton)->redraw(lcd);

  } else if (appState == OPTIONS) {

    if (options) {
      ((GUIWidget*) options)->redraw(lcd);
    }

  } else if (appState == SHORTCUTS) {

    if (!screenInited || redrawAll) {
      ((GUIWidget*) bgRect)->redraw(lcd);
      // Draw labels
      for(uint16_t i=0; i<sizeof(shortcutLabels)/sizeof(LabelWidget*); i++) {
        ((GUIWidget*) shortcutLabels[i])->redraw(lcd);
      }
    }

    // Redraw input widgets
    for(uint16_t i=0; i<sizeof(shortcutInputs)/sizeof(TextInputWidget*); i++) {
      ((GUIWidget*) shortcutInputs[i])->redraw(lcd);
    }

  }
  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Audio config app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

AudioConfigApp::AudioConfigApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(2), audio(audio), ini(Storage::ConfigsFile) {
  log_d("create AudioConfigApp");

  header->setTitle("Audio settings");
  footer->setButtons("Save", "Back");

  // Create and arrange widgets
  uint16_t yOff = header->height() + 5;
  addLabelSlider(yOff, labels[2], sliders[2], "Loudspeaker volume:", Audio::MuteVolume, Audio::MaxLoudspeakerVolume, "dB");
  yOff += 4;
  addLabelSlider(yOff, labels[1], sliders[1], "Headphones volume:", Audio::MuteVolume, Audio::MaxVolume, "dB");
  yOff += 4;
  addLabelSlider(yOff, labels[0], sliders[0], "Ear speaker volume:", Audio::MuteVolume, Audio::MaxVolume, "dB");

  // Load preferences
  int8_t earpieceVol, headphonesVol, loudspeakerVol;
  audio->getVolumes(earpieceVol, headphonesVol, loudspeakerVol);
  if ((ini.load() || ini.restore()) && !ini.isEmpty()) {
    // Check version of the file format
    // if (ini[0].hasKey("v")){ //&& !strcmp(ini[0]["v"], "1")) {
    //   log_d("configs file found");
    //   IF_LOG(VERBOSE)
    //   ini.show();
      if (ini.hasSection("audio")) {
        log_d("getting audio info");
        earpieceVol = ini["audio"].getIntValueSafe(earpieceVolField, earpieceVol);
        headphonesVol = ini["audio"].getIntValueSafe(headphonesVolField, headphonesVol);
        loudspeakerVol = ini["audio"].getIntValueSafe(loudspeakerVolField, loudspeakerVol);
      }
    //}
     else {
      log_e("configs file corrup or unknown format");
      IF_LOG(VERBOSE)
      ini.show();
    }
  } else {
    log_d("creating configs file");
    ini[0]["desc"] = "WiPhone general configs";
    ini[0]["v"] = "1";
    ini.addSection("audio");
    ini["audio"][earpieceVolField] = earpieceVol;
    ini["audio"][headphonesVolField] = headphonesVol;
    ini["audio"][loudspeakerVolField] = loudspeakerVol;
    ini.store();
  }

  // Set values
  sliders[0]->setValue(earpieceVol);
  sliders[1]->setValue(headphonesVol);
  sliders[2]->setValue(loudspeakerVol);

  // Set focusables
  addFocusableWidget(sliders[2]);
  addFocusableWidget(sliders[1]);
  addFocusableWidget(sliders[0]);

  setFocus(sliders[2]);
}

AudioConfigApp::~AudioConfigApp() {
  log_d("destroy AudioConfigApp");

  ini.backup();

  // Delete dynamic
  for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
    delete labels[i];
  }
  for(uint16_t i=0; i<sizeof(sliders)/sizeof(IntegerSliderWidget*); i++) {
    delete sliders[i];
  }
}

appEventResult AudioConfigApp::processEvent(EventType event) {
  log_d("processEvent AudioConfigApp: %04x", event);
  appEventResult res = REDRAW_SCREEN;

  if (event == WIPHONE_KEY_SELECT) {
    // Save preferences
    int speakerVol = sliders[0]->getValue();
    int headphonesVol = sliders[1]->getValue();
    int loudspeakerVol = sliders[2]->getValue();
    if (!ini.hasSection("audio")) {
      ini.addSection("audio");
    }
    ini["audio"][earpieceVolField] = speakerVol;
    ini["audio"][headphonesVolField] = headphonesVol;
    ini["audio"][loudspeakerVolField] = loudspeakerVol;
    ini.store();
    audio->setVolumes(speakerVol, headphonesVol, loudspeakerVol);
  }

  // TODO: clean up this code (group by state)
  if (event == WIPHONE_KEY_END || event == WIPHONE_KEY_BACK || event == WIPHONE_KEY_SELECT) {
    return EXIT_APP;
  }

  if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    nextFocus(event == WIPHONE_KEY_DOWN);

  } else {

    // Pass event to a focused widget
    ((GUIWidget*) getFocused())->processEvent(event);

  }
  return res;
}

void AudioConfigApp::redrawScreen(bool redrawAll) {
  if (!screenInited) {
    redrawAll = true;
  }
  if (redrawAll) {
    // Background
    lcd.fillRect(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);
    // Draw labels
    for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
      ((GUIWidget*) labels[i])->redraw(lcd);
    }
  }

  // Redraw input widgets
  for(uint16_t i=0; i<sizeof(sliders)/sizeof(IntegerSliderWidget*); i++) {
    ((GUIWidget*) sliders[i])->refresh(lcd, redrawAll);
  }

  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Parcel app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ParcelApp::ParcelApp(LCD& lcd, ControlState& state, Storage& pref, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(14), flash(pref) {
  log_d("create ParcelApp");

  //udp = new WiFiUDP();      // TODO

  uint16_t yOff = header->height();
  bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);

  // Create and arrange widgets
  log_d("creating widgets");
  addLabelInput(yOff, labels[0], inputs[0], "Name:", 16);
  addLabelInput(yOff, labels[1], inputs[1], "Parcel #:", 6);
  yOff += 9;
  sendButton = new ButtonWidget(2, yOff, "Send");

  log_d("null");
  options = NULL;
  memset(configsLabels, 0, sizeof(configsLabels));
  memset(configsInputs, 0, sizeof(configsInputs));

  // Load preferences
  log_d("loading preferences");
  const char* ip = NULL;
  int32_t port = -1;
  flash.loadString(this->storagePage, "ip", ip);
  if (ip) {
    log_d("Loaded ip");
    log_d("Loaded ip: empty = %s", ip[0] ? "true" : "false");
    log_d("Loaded ip: %s", ip);
  }

  log_d("loading port");
  flash.loadInt(this->storagePage, "port", port);
  if (port>0) {
    log_d("Loaded port: %d", port);
  }

  // Set text
  log_d("setting text");
  if (configsInputs[0] && ip) {
    configsInputs[0]->setText(ip);
  }
  if (configsInputs[1] && port>0) {
    char buff[12];
    buff[0] = '\0';
    if (port>=0) {
      sprintf(buff, "%d", port);
    }
    configsInputs[1]->setText(buff);
  }

  // Free memory
  log_d("freeing");
  freeNull((void **) &ip);

  // Set focusables
  log_d("focusables");
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    addFocusableWidget(inputs[i]);
  }
  addFocusableWidget(sendButton);

  // Start with main screen
  changeState(MAIN);
  log_d("init finished");

  yOff += sendButton->height();
  labels[2] = new LabelWidget(0, yOff, lcd.width(), 25, "Status", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_16], LabelWidget::LEFT_TO_RIGHT, 8);
  log_d("label added");
}

ParcelApp::~ParcelApp() {
  log_d("destroy ParcelApp");

  /*
    // Save preferences
    const char* ip   = inputs[0]->getText();
    const char* port = inputs[1]->getText();
    const char* text = inputs[2]->getText();
    int32_t portVal;
    if (port) {
      errno = 0;
      portVal = strtol(port, NULL, 10);
      if (errno) portVal = -1;
      else portVal = (uint16_t) portVal;
    } else {
      portVal = -1;
    }
    flash.storeUdpSender(ip, portVal, text);
  */
  // Delete dynamic
  //delete udp;
  delete bgRect;
  delete sendButton;
  for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
    delete labels[i];
  }
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    delete inputs[i];
  }

  if (options) {
    delete options;
  }
  for(uint16_t i=0; i<sizeof(configsLabels)/sizeof(LabelWidget*); i++)
    if (configsLabels[i]) {
      delete configsLabels[i];
    }
  for(uint16_t i=0; i<sizeof(configsInputs)/sizeof(TextInputWidget*); i++)
    if (configsInputs[i]) {
      delete configsInputs[i];
    }
}

void ParcelApp::changeState(ParcelAppState_t newState) {
  /*
  if (newState == MAIN) {

    deactivateFocusable();
    for (int k=0;k<3;k++)
      inputs[k]->activate();
    sendButton->activate();
    setFocus(inputs[0]);

    header->setTitle("UDP sender");
    footer->setButtons("Options", "Clear");

  } else if (newState == OPTIONS) {

    // Create widget
    if (!options) {
      options = new OptionsMenuWidget(0, header->height(), lcd.width(), lcd.height()-header->height()-footer->height());
      options->addOption("Shortcuts");
      addFocusableWidget(options);
    }

    // Deactivate / Activate
    deactivateFocusable();
    options->activate();
    setFocus(options);

    // Change settings
    header->setTitle("Options");
    footer->setButtons(NULL, "Back");

  } else if (newState == SHORTCUTS) {

    // Create widgets
    if (!shortcutLabels[0]) {
      // Create widgets for shortcuts screen
      uint16_t yOff = header->height()+2;
      char buff[3];
      buff[1] = ':'; buff[2] = '\0';
      for (int k=0;k<9;k++) {
        buff[0] = '1' + k;
        addInlineLabelInput(yOff, 30, shortcutLabels[k], shortcutInputs[k], buff, 50);
        addFocusableWidget(shortcutInputs[k]);
        yOff += 2;
      }
    }

    // Deactivate / Activate
    deactivateFocusable();
    for (int k=0;k<9;k++)
      shortcutInputs[k]->activate();
    setFocus(shortcutInputs[0]);

    // Change settings
    header->setTitle("Shortcuts");
    footer->setButtons(NULL, "Clear");

  }

  screenInited = false;
  appState = newState;
  */
}

appEventResult ParcelApp::processEvent(EventType event) {
  log_d("processEvent ParcelApp: ", event);
  appEventResult res = REDRAW_ALL;

  if (event == WIPHONE_KEY_END || (appState == OPTIONS && event == WIPHONE_KEY_BACK)) {

    if (appState == MAIN) {
      return EXIT_APP;
    }
    /*
        else if (appState == OPTIONS) {
          changeState(MAIN);
          res |= REDRAW_ALL;
        } else if (appState == SHORTCUTS) {
          changeState(OPTIONS);
          res |= REDRAW_ALL;
        }
    */
  } else if (appState == MAIN && event == USER_SERIAL_EVENT) {
    char* str = controlState.userSerialBuffer.getCopy();
    controlState.userSerialBuffer.reset();
    log_d("====================================================================");
    log_d("%s", str);
    log_d("====================================================================");
    free(str);
    if (!strncasecmp(str, "NAME:", 5)) {
      inputs[0]->setText(str+5);
    } else {
      inputs[1]->setText(str);
    }
    res |= REDRAW_ALL;
  }
  /*
  else if (appState == MAIN && event == WIPHONE_KEY_SELECT) {

    // Enter options menu
    changeState(OPTIONS);
    res |= REDRAW_ALL;

  } else if (appState == MAIN && (event == WIPHONE_KEY_CALL || getFocused() == sendButton && (LOGIC_BUTTON_OK(event) || (event>='1' && event<='9')))) {

    // Send UDP packet

    // What text to send?
    const char* text = NULL;
    if (event>='1' && event<='9') {
      if (shortcutInputs[event-'1'])
        text = shortcutInputs[event-'1']->getText();
    } else {
      text = inputs[2]->getText();
    }

    if (text && strlen(text)) {

      // Really send UDP packet

      IPAddress ipAddr;
      ipAddr.fromString(inputs[0]->getText());
      const char* portString = inputs[1]->getText();
      uint16_t port = atoi(portString);

      udp->begin(UDP_CLIENT_PORT);
      udp->beginPacket(ipAddr, port);
      udp->write((const byte*) text, strlen(text));
      udp->endPacket();

      log_d("UDP sent: %s", text);

    } else {

      log_d("No text to send");

    }

    return DO_NOTHING;

  } else if (appState == OPTIONS && LOGIC_BUTTON_OK(event)) {

    // Enter shortcuts screen
    changeState(SHORTCUTS);
    res |= REDRAW_ALL;

  } else if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    nextFocus(event == WIPHONE_KEY_DOWN);
    if (getFocused()==sendButton) controlState.setInputState(InputType::Numeric);        // TODO: automate changing InputType relevantly
    else controlState.setInputState(InputType::AlphaNum);

  } else {

    // Pass event to a focused widget
    ((GUIWidget*) getFocused())->processEvent(event);

  }
  */
  return res;
}

void ParcelApp::redrawScreen(bool redrawAll) {
  log_i("redraw ParcelApp");
  if (appState == MAIN) {

    if (!screenInited || redrawAll) {
      ((GUIWidget*) bgRect)->redraw(lcd);
      // Draw labels
      for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
        ((GUIWidget*) labels[i])->redraw(lcd);
      }
    }

    // Redraw input widgets
    for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
      ((GUIWidget*) inputs[i])->redraw(lcd);
    }

    ((GUIWidget*) sendButton)->redraw(lcd);

  } else if (appState == OPTIONS) {

    if (options) {
      ((GUIWidget*) options)->redraw(lcd);
    }

  } else if (appState == CONFIGURE) {

    if (!screenInited || redrawAll) {
      ((GUIWidget*) bgRect)->redraw(lcd);
      // Draw labels
      for(uint16_t i=0; i<sizeof(configsLabels)/sizeof(LabelWidget*); i++) {
        ((GUIWidget*) configsLabels[i])->redraw(lcd);
      }
    }

    // Redraw input widgets
    for(uint16_t i=0; i<sizeof(configsInputs)/sizeof(TextInputWidget*); i++) {
      ((GUIWidget*) configsInputs[i])->redraw(lcd);
    }

  }
  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Motor driver app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#ifdef MOTOR_DRIVER

MotorDriverApp::MotorDriverApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer) {
  log_d("create MotorDriverApp");

  header->setTitle("Motor driver");
  footer->setButtons(NULL, "Back");

  udp = new WiFiUDP();
  udp->begin(UDP_SERVER_PORT);
  log_d("UDP server on port %d", UDP_SERVER_PORT);

  // Create text widget
  const uint8_t pad = 4;
  uint16_t yOff = header->height();
  text = new MultilineTextWidget(0, yOff, lcd.width(), 58, "Empty",
                                 state, 300, fonts[AKROBAT_BOLD_20], InputType::AlphaNum, pad, pad);
  text->setColors(WP_ACCENT_1, WP_COLOR_1);

  // Set text
  IPAddress ipAddr = WiFi.localIP();
  char buff[100];
  sprintf(buff, "Send UDP messages to:\n%d.%d.%d.%d:%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3], UDP_SERVER_PORT);
  text->setText(buff);

  // Create white background
  yOff += text->height();
  bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);

  // Create traffic sign widget
  setDirection(NEVER_MOVED);

  // Set periodic event
  controlState.msAppTimerEventLast = millis();
  controlState.msAppTimerEventPeriod = 25;

  digitalWrite(MotorEN , HIGH);
}

MotorDriverApp::~MotorDriverApp() {
  log_d("destroy MotorDriverApp");
  digitalWrite(MotorEN , LOW);

  udp->stop();
  delete udp;

  // Delete widgets
  delete bgRect;
  delete text;
  if (sign) {
    delete sign;
  }
}

void MotorDriverApp::setDirection(Direction newDir) {
  if (sign) {
    delete sign;
  }
  log_d("new direction: %d", (int) newDir);
  if (newDir == NEVER_MOVED) {
    sign = new RectIconWidget(30, header->height() + text->height(), 180, 180, WHITE, icon_no_walking, sizeof(icon_no_walking));
  } else if (newDir == STOP) {
    sign = new RectIconWidget(30, header->height() + text->height(), 180, 180, WHITE, icon_stop, sizeof(icon_stop));
  } else if (newDir == FORWARD) {
    sign = new RectIconWidget(30, header->height() + text->height(), 180, 180, WHITE, icon_forward, sizeof(icon_forward));
  } else if (newDir == REVERSE) {
    sign = new RectIconWidget(30, header->height() + text->height(), 180, 180, WHITE, icon_reverse, sizeof(icon_reverse));
  } else if (newDir == LEFT) {
    sign = new RectIconWidget(30, header->height() + text->height(), 180, 180, WHITE, icon_left, sizeof(icon_left));
  } else if (newDir == RIGHT) {
    sign = new RectIconWidget(30, header->height() + text->height(), 180, 180, WHITE, icon_right, sizeof(icon_right));
  }
  direction = newDir;
}

appEventResult MotorDriverApp::processEvent(EventType event) {
  // Exit keys
  appEventResult res = DO_NOTHING;
  if (event == APP_TIMER_EVENT) {

    Direction newDir = direction;
    if (udp->parsePacket()>0) {
      char buff[100];
      int cb = udp->read(buff, sizeof(buff)-1);
      if (cb>0) {
        buff[cb] = '\0';
        log_d("UDP received: %s", buff);
#if defined(MOTOR_DRIVER) && MOTOR_DRIVER == 8833
        if (buff[0]=='f') {
          motorDriver.motorAForward();
        } else if (buff[0]=='r') {
          motorDriver.motorAReverse();
        } else {
          motorDriver.motorAStop();
        }
        if (buff[1]=='f') {
          motorDriver.motorBForward();
        } else if (buff[1]=='r') {
          motorDriver.motorBReverse();
        } else {
          motorDriver.motorBStop();
        }
#endif
        moving = buff[0]=='f' || buff[0]=='r' || buff[1]=='f' || buff[1]=='r';
        if (moving) {
          started = millis();
          if (buff[0]=='f' && buff[1]=='f') {
            newDir = FORWARD;
          } else if (buff[0]=='r' && buff[1]=='r') {
            newDir = REVERSE;
          } else if (buff[0]=='f' || buff[1]=='r') {
            newDir = RIGHT;
          } else if (buff[0]=='r' || buff[1]=='f') {
            newDir = LEFT;
          }
        } else {
          newDir = STOP;
        }
      }
    }
    if (moving && elapsedMillis(millis(), started, 500)) {
      moving = false;
#if defined(MOTOR_DRIVER) && MOTOR_DRIVER == 8833
      motorDriver.motorAStop();
      motorDriver.motorBStop();
#endif
      newDir = STOP;
      log_d("stopped");
    }
    if (newDir != direction) {
      setDirection(newDir);
      res |= REDRAW_SCREEN;
    }

  } else if (LOGIC_BUTTON_BACK(event)) {

    // Just exit
    return EXIT_APP;

  }

  return res;
}

void MotorDriverApp::redrawScreen(bool redrawAll) {
  log_i("redraw MotorDriverApp");
  if (!screenInited) {
    ((GUIWidget*) bgRect)->redraw(lcd);
    ((GUIWidget*) text)->redraw(lcd);
  }
  if (sign) {
    ((GUIWidget*) sign)->redraw(lcd);
  }
  screenInited = true;
}

#endif // MOTOR_DRIVER
// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Pin control app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PinControlApp::PinControlApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer) {
  log_d("create PinControlApp");

  header->setTitle("UDP On/Off");
  footer->setButtons(NULL, "Back");

  udp = new WiFiUDP();
  udp->begin(UDP_SERVER_PORT);
  log_d("UDP server on port %d", UDP_SERVER_PORT);

  // Create white background
  int yOff = header->height();
  bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_0);
  yOff += marginY;
  ledLabel = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_EXTRABOLD_22]->height(), "LED Off", WP_COLOR_1, WP_COLOR_0, fonts[AKROBAT_EXTRABOLD_22], LabelWidget::CENTER);

  // Set periodic event
  controlState.msAppTimerEventLast = millis();
  controlState.msAppTimerEventPeriod = 25;
}

PinControlApp::~PinControlApp() {
  log_d("destroy PinControlApp");
  delete udp;

  // Delete widgets
  delete bgRect;
  delete ledLabel;
}

appEventResult PinControlApp::processEvent(EventType event) {
  // Exit keys
  appEventResult res = DO_NOTHING;
  if (event == APP_TIMER_EVENT) {

    // Check for incoming commands
    if (udp->parsePacket()>0) {
      char buff[100];
      int cb = udp->read(buff, sizeof(buff)-1);
      if (cb>0) {
        buff[cb] = '\0';
        log_d("UDP received: %s", buff);
        if (buff[0]=='N') {

          controlState.ledPleaseTurnOn = true;
          isOn = true;
          res |= REDRAW_ALL;

          delete bgRect;
          delete ledLabel;
          int yOff = header->height();
          bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);
          yOff += marginY;
          ledLabel = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_EXTRABOLD_22]->height(), "LED On", WP_COLOR_0, WP_COLOR_1, fonts[AKROBAT_EXTRABOLD_22], LabelWidget::CENTER);

        } else if (buff[0]=='F') {

          controlState.ledPleaseTurnOff = true;
          ledLabel->setText("LED: OFF");
          isOn = false;
          res |= REDRAW_ALL;

          delete bgRect;
          delete ledLabel;
          int yOff = header->height();
          bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_0);
          yOff += marginY;
          ledLabel = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_EXTRABOLD_22]->height(), "LED Off", WP_COLOR_1, WP_COLOR_0, fonts[AKROBAT_EXTRABOLD_22], LabelWidget::CENTER);

        }
      }
    }

  } else if (LOGIC_BUTTON_BACK(event)) {

    // Just exit
    return EXIT_APP;

  }

  return res;
}

void PinControlApp::redrawScreen(bool redrawAll) {
  ((GUIWidget*) bgRect)->redraw(lcd);
  ((GUIWidget*) ledLabel)->redraw(lcd);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Phonebook app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PhonebookApp::PhonebookApp(Audio* audio, LCD& lcd, LCD& hardDisp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer, bool pick)
  : WindowedApp(lcd, state, header, footer), FocusableApp(2), audio(audio), hardDisp(hardDisp), flash(flash), standAloneApp(!pick), combinedAddress(NULL) {
  log_d("create PhonebookApp");

  // Menu widgets
  menu    = NULL;
  options = NULL;

  // VIEWING widgets
  const int16_t pad =  8;
  uint16_t yOff = header->height();
  rect   = new RectWidget(0, header->height(), 50 + pad, 50 + (2*pad), WHITE);      // headpic background
  headpic= new RectIconWidget(pad, header->height() + pad, 50, 50, WP_ACCENT_1, icon_person_w, sizeof(icon_person_w));
  contactName = new MultilineTextWidget(rect->width(), header->height(), lcd.width()-rect->width(), rect->height(),
                                        "(no name)", state, 200, fonts[AKROBAT_EXTRABOLD_22], InputType::AlphaNum, pad, pad);
  contactName->setColors(WP_COLOR_0, WP_COLOR_1);
  contactName->verticalCentering(true);
  yOff += rect->height();

  phonePic = new RectIconWidget(0, yOff, 36, 46, WHITE, icon_phone_b, sizeof(icon_phone_b));
  addressView = new MultilineTextWidget(phonePic->width(), yOff, lcd.width() - phonePic->width(), 46,
                                        "(no number)", state, 200, fonts[AKROBAT_BOLD_18], InputType::AlphaNum, 4, 4);
  addressView->setColors(WP_COLOR_0, WP_COLOR_1);
  addressView->verticalCentering(true);
  yOff += phonePic->height();

  viewMenu = new MenuWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(),
                            "Phonebook is empty", fonts[AKROBAT_BOLD_20], 3, 8);
  viewMenu->setStyle(MenuWidget::DEFAULT_STYLE, WP_COLOR_0, WP_COLOR_1, WP_COLOR_1, WP_ACCENT_1);
  viewMenu->addOption("Call", NULL, 1001, 1, icon_calling_b, sizeof(icon_calling_b), icon_calling_w, sizeof(icon_calling_w));
  viewMenu->addOption("Send message", NULL, 1002, 1, icon_message_b, sizeof(icon_message_b), icon_message_w, sizeof(icon_message_w));

  // ADDING / EDITING widgets
  yOff = header->height();
  addLabelInput(yOff, dispNameLabel, dispNameInput, "Name:", 100);
  addLabelInput(yOff, sipUriLabel, sipUriInput, "SIP URI:", 100);
  addLabelInput(yOff, loraLabel, loraInput, "LoRa address:", 100);

  clearRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);

  // Populate menu
  createLoadMenu();

  // Focusables
  addFocusableWidget(dispNameInput);
  addFocusableWidget(sipUriInput);
  addFocusableWidget(loraInput);

  // Initialize state
  changeState(SELECTING);
}

PhonebookApp::~PhonebookApp() {
  log_d("destroy PhonebookApp");

  flash.phonebook.backup();

  if (callApp) {
    delete callApp;
  }

  // SELECTING
  if (menu) {
    delete menu;
  }
  if (emptyLabel) {
    delete emptyLabel;
  }

  // VIEWING
  delete rect;
  delete headpic;
  delete phonePic;
  delete contactName;
  delete addressView;
  delete viewMenu;

  // OPTIONS
  if (options) {
    delete options;
  }

  // ADDING / EDITING
  delete clearRect;
  delete dispNameLabel;
  delete dispNameInput;
  delete sipUriLabel;
  delete sipUriInput;
  delete loraLabel;
  delete loraInput;

  if (combinedAddress != NULL) {
    delete combinedAddress;
  }
}

const char* PhonebookApp::getSelectedSipUri() {
  if (currentKey>0 && currentKey <=  flash.phonebook.nSections()) {
    return flash.phonebook[currentKey].getValueSafe("s","");            // return SIP URI
  }
  log_e("wrong currentKey");
  return "";
}

const char* PhonebookApp::getSelectedLoraAddress() {
  if (currentKey>0 && currentKey <=  flash.phonebook.nSections()) {
    return flash.phonebook[currentKey].getValueSafe("l","");  // return LoRa address
  }
  log_e("wrong currentKey");
  return "";
}

appEventResult PhonebookApp::changeState(PhonebookAppState_t newState) {
  log_d("changeState PhonebookApp");
  if (newState == SELECTING) {

    log_d("SELECTING");

    // Deactivate / Activate
    deactivateFocusable();
    menu->activate();
    menu->setDrawOnce();
    setFocus(menu);

    // Change settings
    header->setTitle("Phonebook");
    footer->setButtons("Add", "Back");

  } else if (newState == OPTIONS) {

    log_d("OPTIONS");

    // Deactivate / Activate
    deactivateFocusable();
    if (!options) {
      options = new OptionsMenuWidget(0, header->height(), lcd.width(), lcd.height()-header->height()-footer->height());
      if (options) {
        options->addOption("Edit", 0x101);
        options->addOption("Delete", 0x102);
        options->addOption("Call", 0x103);
        options->addOption("Send message", 0x104);
      }
    }
    options->activate();
    setFocus(options);

    // Change settings
    header->setTitle("Options");
    footer->setButtons(NULL, "Back");

  } else if (newState == CALLING) {

    // Do nothing, all the call initiation is done becomeCaller()

  } else {      // ADDING / VIEWING / EDITING

    // Load from flash
    if (currentKey>0) {
      log_d("viewing / editing -> load data from flash");

      // VIEWING / EDITING

      // Load data from flash
      if (currentKey <=  flash.phonebook.nSections()) {
        // Name
        const char* name = flash.phonebook[currentKey].getValueSafe("n","");
        dispNameInput->setText(name);
        contactName->setText(name);
        // SIP URI
        const char* uri = flash.phonebook[currentKey].getValueSafe("s","");
        sipUriInput->setText(uri);
        addressView->setText(uri);

        // LoRa address
        const char* lora = flash.phonebook[currentKey].getValueSafe("l","");
        loraInput->setText(lora);
      }

      // Reset selection in the viewing menu
      if (newState==VIEWING) {
        viewMenu->reset();
      }

    } else {

      // ADDING
      dispNameInput->setText("");
      sipUriInput->setText("");
      loraInput->setText("");
    }

    // Deactivate
    menu->deactivate();

    if (newState == ADDING || newState == EDITING) {
      log_d("ADDING / EDITING");

      footer->setButtons("Save", "Clear");

      // Activate / deactivate
      dispNameInput->activate();
      sipUriInput->activate();
      loraInput->activate();

      // Change settings
      header->setTitle(newState == EDITING ? "Edit contact" : "Create contact");
      setFocus(dispNameInput);

    } else if (newState == VIEWING) {
      log_d("VIEWING");

      footer->setButtons("Options", "Back");

      // Activate / deactivate
      dispNameInput->deactivate();
      sipUriInput->deactivate();
      loraInput->deactivate();

      // Change settings
      header->setTitle("View contact");

    }
  }
  appState = newState;
  screenInited = false;
  return REDRAW_ALL;
}

void PhonebookApp::createLoadMenu() {
  log_d("createLoadMenu PhonebookApp");

  // Create new menu widget
  if (menu!=NULL) {
    delete menu;
  }
  menu = new MenuWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(),
                        "Phonebook is empty", fonts[AKROBAT_EXTRABOLD_22], N_MENU_ITEMS);
  menu->setStyle(MenuWidget::DEFAULT_STYLE, WP_COLOR_0, WP_COLOR_1, WP_COLOR_1, WP_ACCENT_1);   // in original design it used WP_ACCENT_0, but this doesn't make sense: too bright, text cannot be read

  // Add all individual addresses
  if (flash.phonebook.isLoaded() || flash.loadPhonebook()) {
    for (auto si = flash.phonebook.iterator(1); si.valid(); ++si) {
      MenuOptionPhonebook* option = new MenuOptionPhonebook((int)si, 1, si->getValueSafe("n", ""), si->getValueSafe("s", ""));
      if (option && !menu->addOption(option)) {
        delete option;
        break;
      }
    }
  }

}

appEventResult PhonebookApp::processEvent(EventType event) {
  log_i("processEvent PhonebookApp");

  appEventResult res = DO_NOTHING;

  if (messageApp != NULL) {

    if ((res = messageApp->processEvent(event)) & EXIT_APP) {
      changeState(appState);    // appState doesn't change, we just use this to update Header and Footer widgets
      delete messageApp;
      messageApp = NULL;
      res = REDRAW_ALL;
    }

  } else if (appState==SELECTING) {
    // Exit keys
    if (LOGIC_BUTTON_BACK(event)) {
      return EXIT_APP;
    }

    if (event == WIPHONE_KEY_CALL) {
      MenuOption::keyType sel = menu->currentKey();
      if (sel>0) {
        currentKey = sel;
        if (this->standAloneApp) {
          this->becomeCaller();
          res |= REDRAW_ALL;
        } else {
          return EXIT_APP;
        }
      }
    } else if (event == WIPHONE_KEY_SELECT) {
      currentKey = 0;   // empty / new
      res |= changeState(ADDING);
    } else {
      // Probably navigation or search
      menu->processEvent(event);
      MenuOption::keyType sel = menu->readChosen();
      if (sel>0) {
        currentKey = sel;
        if (this->standAloneApp) {
          res |= changeState(VIEWING);
        } else {
          return EXIT_APP;
        }
      }
      res |= REDRAW_SCREEN;
    }

  } else if (appState==CALLING) {

    if(!wifiState.isConnected() || WiFi.status() != WL_CONNECTED) {
      if (callApp!=NULL) {
        callApp->setStateCaption("No WiFi Conn");
        callApp->redrawScreen(true);
        delay(1000);
        delete callApp;
        callApp = NULL;
      }
      log_e("WIPHONE_KEY_CALL: call not possible due to wifi lost");
      return EXIT_APP;
    } else if (!controlState.isCallPossible()) {
      if (callApp!=NULL) {
        callApp->setStateCaption("No SIP Conn");
        callApp->redrawScreen(true);
        delay(1000);
        delete callApp;
        callApp = NULL;
      }
      log_e("WIPHONE_KEY_CALL: call not possible due to that no SIP conn.");
      changeState(SELECTING);     // TODO: remove CALLING state, use callApp as an indication of call; "change" to current appState

      //res |= REDRAW_ALL;

      return EXIT_APP;
    }
    if (callApp!=NULL) {
      if ((res = callApp->processEvent(event)) & EXIT_APP) {
        // Idle -> Exit
        changeState(SELECTING);     // TODO: remove CALLING state, use callApp as an indication of call; "change" to current appState
        delete callApp;
        callApp = NULL;
        res |= REDRAW_ALL;
      }
    }

  } else if (appState==OPTIONS) {

    res |= REDRAW_SCREEN;
    if (LOGIC_BUTTON_BACK(event)) {
      res |= changeState(VIEWING);
    } else {
      options->processEvent(event);
      MenuOption::keyType sel = options->readChosen();
      if (sel>0) {
        if (sel==0x101) {
          // "Edit" option selected
          res |= changeState(EDITING);
        } else if (sel==0x102) {
          // "Delete" option selected
          if (flash.phonebook.removeSection(currentKey)) {
            flash.phonebook.store();
          }
          createLoadMenu();
          res |= changeState(SELECTING);
        } else if (sel==0x103) {
          // "Call" option selected
          this->becomeCaller();
          res |= REDRAW_ALL;
        } else if (sel==0x104) {
          // "Send message" option selected
          this->sendMessage();
          res |= REDRAW_ALL;
        }
      }
    }

  } else if (appState == VIEWING) {

    if (LOGIC_BUTTON_BACK(event)) {

      // Exit
      res |= changeState(SELECTING);

    } else if (event == WIPHONE_KEY_SELECT) {

      // Show options for this phonebook entry
      res |= changeState(OPTIONS);

    } else {

      res |= REDRAW_SCREEN;
      viewMenu->processEvent(event);
      MenuOption::keyType sel = viewMenu->readChosen();
      if (sel>0) {
        if (sel==1001) {
          // "Call" menu item
          this->becomeCaller();
          res |= REDRAW_ALL;
        } else if (sel==1002) {
          // Send message
          this->sendMessage();
          res |= REDRAW_ALL;
        }
      }

    }

  } else if (appState==ADDING || appState==EDITING) {      // ADDING / EDITING

    if (LOGIC_BUTTON_OK(event)) {

      log_v("modifying phonebook");
      if (currentKey) {
        flash.phonebook.removeSection(currentKey);
      }
      flash.phonebook.addSection();
      flash.phonebook[-1]["n"] = dispNameInput->getText();

      //check whether uri starts with "sips:" or "SIPS:" or "SIP:" which cannot be called (tested)
      if(strncmp(sipUriInput->getText(), "sips:", 4) == 0 || strncmp(sipUriInput->getText(), "SIPS:", 4) == 0 || strncmp(sipUriInput->getText(), "SIP:", 4) == 0) {
        //TODO display error popup which says we do not support it.
      } else {
        //add "sip:" to sip-uri by default
        if( ! (strncmp(sipUriInput->getText(), "sip:", 4) == 0)) {
          std::string tmpStr(sipUriInput->getText());
          tmpStr = std::string("sip:") + tmpStr;
          sipUriInput->setText(tmpStr.c_str());
        }
      }
      flash.phonebook[-1]["s"] = sipUriInput->getText();
      flash.phonebook[-1]["l"] = loraInput->getText();
      flash.phonebook.reorderLast(1, &(Storage::phonebookCompare));

      // Save phonebook
      log_v("saving");
      bool saved = flash.phonebook.store();

      if (saved) {
        log_v("saved -> ressetting");
        dispNameInput->setText("");
        sipUriInput->setText("");
        loraInput->setText("");
        createLoadMenu();
        res |= changeState(appState==EDITING ? VIEWING : SELECTING);
      } else {
        // TODO: display something to the user on failure
      }

    } else if (event == WIPHONE_KEY_END) {

      res |= changeState(appState==EDITING ? VIEWING : SELECTING);

    } else if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

      // Change focus
      nextFocus();
      res |= REDRAW_SCREEN;

    } else {

      // Pass button to whatever is focused
      GUIWidget* focused = getFocused();
      if (focused) {
        focused->processEvent(event);
      }
      res |= REDRAW_SCREEN;

    }
  }

  return res;
}

void PhonebookApp::becomeCaller() {
  // Load callee address from flash
  if (currentKey > 0 && currentKey < flash.phonebook.nSections() && *flash.phonebook[currentKey].getValueSafe("s","")!='\0') {
    // Kick-start a call
    controlState.setRemoteNameUri(flash.phonebook[currentKey].getValueSafe("n",""),
                                  flash.phonebook[currentKey]["s"]);
    controlState.setSipReason("");
    controlState.setSipState(CallState::InvitingCallee);
  } else {
    //TODO show popup
    log_e("cannot call without sip info!");

    //this callApp object is just created for only showing a popup
    callApp = new CallApp(this->audio, this->hardDisp, this->controlState, true, this->header, this->footer);

    if (callApp!=NULL) {
      callApp->setStateCaption("No SIP URI");
      callApp->redrawScreen(true);
      delay(1000);
      delete callApp;
      callApp = NULL;
    }


    return;
  }

  // GUI
  log_d("CALLING");
  deactivateFocusable();

  // Create call app as caller
  callApp = new CallApp(this->audio, this->hardDisp, this->controlState, true, this->header, this->footer);       // true for caller

  /*if(!controlState.calleeUriDyn || controlState.calleeUriDyn[0] == 0)
  {
    //TODO show popup
    log_e("cannot call without sip info!");
    return;
  }*/


  changeState(CALLING);
}

const char* PhonebookApp::getCombinedAddress() {
  const char* sip  = this->getSelectedSipUri();
  const char* lora = this->getSelectedLoraAddress();

  size_t slen = strlen(sip);
  size_t llen = strlen(lora);

  if (slen > 3 && llen > 3) {
    if (combinedAddress != NULL) {
      free(combinedAddress);
    }

    size_t clen = slen+llen+10;
    combinedAddress = (char*)malloc(clen);
    memset(combinedAddress, 0x00, clen);

    strlcpy(combinedAddress, "LORA:", clen);
    strlcat(combinedAddress, lora, clen);
    strlcat(combinedAddress, "!", clen);
    strlcat(combinedAddress, sip, clen);

    return combinedAddress;
  } else if (slen > 3) {
    return sip;
  } else {
    return lora;
  }
}

void PhonebookApp::sendMessage() {
  messageApp = new CreateMessageApp(lcd, controlState, flash, header, footer, this->getCombinedAddress());
}

void PhonebookApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen PhonebookApp");

  if (messageApp!=NULL) {
    messageApp->redrawScreen(redrawAll);

  } else if (appState==SELECTING || appState==OPTIONS) {
    if (!screenInited || redrawAll) {
      ((GUIWidget*) rect)->redraw(lcd);
    }
    if (appState==SELECTING) {
      ((GUIWidget*) menu)->redraw(lcd);
    } else {
      ((GUIWidget*) options)->redraw(lcd);
    }
  } else if (appState==CALLING) {

    if (callApp!=NULL) {
      callApp->redrawScreen(redrawAll);
    }

  } else if (appState==VIEWING) {
    if (!screenInited || redrawAll) {
      ((GUIWidget*) rect)->redraw(lcd);
      ((GUIWidget*) headpic)->redraw(lcd);
      ((GUIWidget*) contactName)->redraw(lcd);
      ((GUIWidget*) phonePic)->redraw(lcd);
      ((GUIWidget*) addressView)->redraw(lcd);
    }
    ((GUIWidget*) viewMenu)->redraw(lcd);

  } else {    // ADDING / EDITING
    ((GUIWidget*) dispNameLabel)->redraw(lcd);
    ((GUIWidget*) sipUriLabel)->redraw(lcd);
    ((GUIWidget*) dispNameInput)->redraw(lcd);
    ((GUIWidget*) sipUriInput)->redraw(lcd);
    ((GUIWidget*) loraInput)->redraw(lcd);
    ((GUIWidget*) loraLabel)->redraw(lcd);

    if (!screenInited || redrawAll) {
      ((GUIWidget*) clearRect)->redraw(lcd);
    }
  }

  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Sip Accounts app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SipAccountsApp::SipAccountsApp(LCD& lcd, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(4), ini(filename) {
  log_d("create SipAccountsApp");

  udpTcpSipSelection = nullptr;
  // Load SIP accounts from flash
  if ((ini.load() || ini.restore()) && !ini.isEmpty()) {
    // Check version of the file format
    if (!ini[0].hasKey("v") || strcmp(ini[0]["v"], "1")) {
      log_e("file corrupt or unknown file version");
      IF_LOG(VERBOSE)
      ini.show();
      //ini.remove();
    }
  }

  if (ini.isEmpty()) {
    // Create new one:
    //    section [0] is a header.
    //    unnamed sections [1] ... [N] are different SIP accounts havig these fields:
    //      "s=..." SIP URI
    //      "d=..." display name
    //      "p=..." password
    ini.addSection();
    ini[0]["desc"] = "WiPhone SIP accounts";   // comment
    ini[0]["v"] = "1";                         // version
    if (ini.store()) {
      log_d("new file created");
    }
  }

  // Menu widgets
  menu     = NULL;
  viewMenu = NULL;

  // VIEWING widgets
  const int16_t pad =  8;
  uint16_t yOff = header->height();
  rect   = new RectWidget(0, header->height(), 50 + pad, 50 + (2*pad), WHITE);      // headpic background
  headpic= new RectIconWidget(pad, header->height() + pad, 50, 50, WP_ACCENT_1, icon_person_w, sizeof(icon_person_w));
  contactName = new MultilineTextWidget(rect->width(), header->height(), lcd.width()-rect->width(), rect->height(),
                                        "(no name)", state, 200, fonts[AKROBAT_EXTRABOLD_22], InputType::AlphaNum, pad, pad);
  contactName->setColors(WP_COLOR_0, WP_COLOR_1);
  contactName->verticalCentering(true);
  yOff += rect->height();

  phonePic = new RectIconWidget(0, yOff, 36, 46, WHITE, icon_phone_b, sizeof(icon_phone_b));
  addressView = new MultilineTextWidget(phonePic->width(), yOff, lcd.width() - phonePic->width(), 46,
                                        "(no number)", state, 200, fonts[AKROBAT_BOLD_18], InputType::AlphaNum, 4, 4);
  addressView->setColors(WP_COLOR_0, WP_COLOR_1);
  addressView->verticalCentering(true);
  yOff += phonePic->height();

  viewMenu = new MenuWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(),
                            "No SIP accounts", fonts[AKROBAT_BOLD_20], 3, 8);
  viewMenu->setStyle(MenuWidget::DEFAULT_STYLE, WP_COLOR_0, WP_COLOR_1, WP_COLOR_1, WP_ACCENT_1);

  // ADDING / EDITING widgets
  yOff = header->height();

  // - create and arrange widgets
  clearRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);
  addLabelInput(yOff, inputLabels[0], inputs[0], "Name:", 100);
  addDoubleLabelInput(yOff, inputLabels[1], inputs[1], "User:", 50, inputLabels[2], inputs[2], "Server:", 50);
  addLabelInput(yOff, inputLabels[3], inputs[3], "SIP URI:", 100);
  addLabelPassword(yOff, inputLabels[4], passwordInput, "Password:", lcd.width()/2);

  udpTcpSipSelection = new ChoiceWidget(lcd.width()/2, yOff-passwordInput->height(), lcd.width()/2, 35);
  udpTcpSipSelection->addChoice("UDP-SIP");
  udpTcpSipSelection->addChoice("TCP-SIP");
  yOff += udpTcpSipSelection->height();

  // Populate menu
  createLoadMenu();

  // Focusables
  for (int i=0; i<sizeof(inputs)/sizeof(inputs[0])-1; i++) {   // don't add SIP URI into focusable - it's autofilled
    addFocusableWidget(inputs[i]);
  }
  addFocusableWidget(passwordInput);
  addFocusableWidget(udpTcpSipSelection);

  // Initialize state
  changeState(SELECTING);
}

SipAccountsApp::~SipAccountsApp() {
  log_d("destroy SipAccountsApp");

  ini.backup();

  // SELECTING
  if (menu) {
    delete menu;
  }
  if (emptyLabel) {
    delete emptyLabel;
  }

  // VIEWING
  delete rect;
  delete headpic;
  delete phonePic;
  delete contactName;
  delete addressView;
  delete viewMenu;

  // ADDING / EDITING
  delete clearRect;
  for (int i=0; i<sizeof(inputLabels)/sizeof(LabelWidget*); i++) {
    delete inputLabels[i];
  }
  for (int i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    delete inputs[i];
  }
  delete passwordInput;
  if(udpTcpSipSelection) {
    delete udpTcpSipSelection;
  }

}

void SipAccountsApp::changeState(SipAccountsAppState_t newState) {
  log_d("changeState SipAccountsApp");
  if (newState == SELECTING) {

    log_d("SELECTING");

    // Deactivate / Activate
    deactivateFocusable();
    menu->activate();
    menu->setDrawOnce();
    setFocus(menu);

    // Change settings
    header->setTitle("SIP accounts");
    footer->setButtons("Add", "Back");

  } else {      // ADDING / VIEWING / EDITING

    // Load from flash
    if (currentKey>0) {

      // VIEWING / EDITING

      // Display account data
      bool primary = false;
      if (currentKey < ini.nSections()) {
        if (ini[currentKey].hasKey("d")) {
          // Display name
          inputs[0]->setText(ini[currentKey]["d"]);
          contactName->setText(ini[currentKey]["d"]);
        }
        if (ini[currentKey].hasKey("s")) {
          // SIP URI
          const char* sipUri = ini[currentKey]["s"];
          addressView->setText(sipUri);
          AddrSpec spec = AddrSpec(sipUri);
          inputs[1]->setText(spec.userinfo());
          inputs[2]->setText(spec.hostPort());
          inputs[3]->setText(sipUri);
        }
        if (ini[currentKey].hasKey("p")) {
          // Password
          passwordInput->setText(ini[currentKey]["p"]);
        }
        //read the UDP-SIP selection from ini (as backward-compatible)
        bool tmpUDP_SIP = false;
        if (ini[currentKey].hasKey("u")) {
          if(strcmp(ini[currentKey]["u"], "UDP-SIP") == 0) {
            tmpUDP_SIP = true;
          } else {
            tmpUDP_SIP = false;
          }
        } else {
          tmpUDP_SIP = false;
        }
        if(tmpUDP_SIP) {
          udpTcpSipSelection->setValue(0);
        } else {
          udpTcpSipSelection->setValue(1);
        }
        primary = ini[currentKey].hasKey("m");
      }

      // Create new viewing menu
      if (newState==VIEWING) {
        viewMenu->deleteAll();
        viewMenu->addOption("Edit", NULL, 1003, 1, icon_edit_b, sizeof(icon_edit_b), icon_edit_w, sizeof(icon_edit_w));
        viewMenu->addOption(primary ? "Unmake primary" : "Make primary", NULL, 1004, 1, icon_edit_b, sizeof(icon_edit_b), icon_edit_w, sizeof(icon_edit_w));
        viewMenu->addOption("Delete", NULL, 1009, 1, icon_delete_r, sizeof(icon_delete_r), icon_delete_w, sizeof(icon_delete_w));
      }

    } else {

      // ADDING
      for (int i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
        inputs[i]->setText("");
      }
      passwordInput->setText("");
      udpTcpSipSelection->setValue(1); //TCP-SIP

    }

    // Deactivate
    menu->deactivate();

    if (newState == ADDING || newState == EDITING) {
      log_d("ADDING / EDITING");

      footer->setButtons("Save", "Clear");

      // Activate / deactivate
      for (int i=0; i<sizeof(inputs)/sizeof(inputs[0])-1; i++) {   // don't add SIP URI into focusable - it's autofilled
        inputs[i]->activate();
      }
      passwordInput->activate();
      udpTcpSipSelection->activate();

      // Change settings
      header->setTitle(newState == EDITING ? "Edit account" : "Create account");
      setFocus(inputs[0]);

    } else if (newState == VIEWING) {
      log_d("VIEWING");

      footer->setButtons("Select", "Back");

      // Activate / deactivate
      for (int i=0; i<sizeof(inputs)/sizeof(TextInputWidget*)-1; i++) {   // don't add SIP URI into focusable - it's autofilled
        inputs[i]->deactivate();
      }
      passwordInput->deactivate();
      udpTcpSipSelection->deactivate();

      // Change settings
      header->setTitle("View account");

    }
  }
  appState = newState;
  screenInited = false;
}

void SipAccountsApp::createLoadMenu() {
  log_d("createLoadMenu SipAccountsApp");

  // Create new menu widget
  if (menu!=NULL) {
    delete menu;
  }
  menu = new MenuWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(),
                        "No SIP accounts", fonts[AKROBAT_EXTRABOLD_22], N_MENU_ITEMS);
  menu->setStyle(MenuWidget::DEFAULT_STYLE, WP_COLOR_0, WP_COLOR_1, WP_COLOR_1, WP_ACCENT_1);   // in original design it used WP_ACCENT_0, but this doesn't make sense: too bright, text cannot be read

  // Add all individual addresses
  for (auto si = ini.iterator(1); si.valid(); ++si) {
    MenuOptionIconned* option = new MenuOptionIconned((int)si, 1, si->getValueSafe("d"), si->getValueSafe("s"),
        icon_person_b, sizeof(icon_person_b), icon_person_w, sizeof(icon_person_w),
        7,  si->hasKey("m") ? (controlState.sipRegistered ? WP_ACCENT_G : WP_ACCENT_S) : WP_ACCENT_0 );
    if (option && !menu->addOption(option)) {
      delete option;
      break;
    }
  }
}

appEventResult SipAccountsApp::processEvent(EventType event) {
  //log_d("processEvent SipAccountsApp");

  appEventResult res = REDRAW_SCREEN;     // TODO: return DO_NOTHING on irrelevant events

  if (appState==SELECTING) {
    // Exit keys
    if (LOGIC_BUTTON_BACK(event)) {
      return EXIT_APP;
    }

    if (event == WIPHONE_KEY_CALL) {
      MenuOption::keyType sel = menu->currentKey();
      if (sel>0) {
        currentKey = sel;
        // TODO: make this server primary and connect to this server?
      }
    } else if (event == WIPHONE_KEY_SELECT) {
      currentKey = 0;   // empty / new
      changeState(ADDING);
      res |= REDRAW_ALL;
    } else if (event == REGISTRATION_UPDATE_EVENT) {
      // TODO: this can optimized to not recreate menu from scratch
      this->createLoadMenu();
      MenuOption::keyType sel = menu->currentKey();
    } else {
      // Probably navigation or search
      menu->processEvent(event);
      MenuOption::keyType sel = menu->readChosen();
      if (sel>0) {
        currentKey = sel;
        changeState(VIEWING);
        res |= REDRAW_ALL;
      }
    }

  } else if (appState == VIEWING) {

    if (LOGIC_BUTTON_BACK(event)) {

      // Exit
      changeState(SELECTING);
      res |= REDRAW_ALL;

    } else {

      viewMenu->processEvent(event);
      MenuOption::keyType sel = viewMenu->readChosen();

      if (sel>0) {

        if (sel==1003) {

          // "Edit" option selected for a SIP account

          changeState(EDITING);
          res |= REDRAW_ALL;

        } else if (sel==1004) {

          // "Primary" toggle for a SIP account

          if (currentKey < ini.nSections()) {

            bool primary = ini[currentKey].hasKey("m");

            // Remore primary flag from all the records
            ini.clearUniqueFlag("m");

            // Set a primary flag for current record (if it was absent before)
            if (!primary) {
              ini[currentKey]["m"] = "y";  // make current record primary ("main = yes")
            }

            // Change current SIP account in memory
            controlState.setSipAccount(ini[currentKey].getValueSafe("d", ""),
                                       ini[currentKey].getValueSafe("s", ""),
                                       ini[currentKey].getValueSafe("p", ""),
                                       ini[currentKey].getValueSafe("u", ""));
            res |= REDRAW_HEADER;   // redraw SIP icon (if any)

            // Store changes
            if (ini.store()) {
              log_v("saved");
            } else {
              log_e("failed to save");
            }
            createLoadMenu();
            changeState(VIEWING);
            res |= REDRAW_ALL;
          }
        } else if (sel==1009) {
          // "Delete" option selected
          if (currentKey < ini.nSections() && ini[currentKey].hasKey("m")) {
            // This is a primary account -> remove it from memory, don't reconnect
            controlState.removeSipAccount();
          }
          if (ini.removeSection(currentKey)) {
            if (ini.store()) {
              log_v("saved");
            } else {
              log_e("failed to save");
            }
          }
          createLoadMenu();
          changeState(SELECTING);
          res |= REDRAW_ALL;
        }
      }

    }

  } else if (appState==ADDING || appState==EDITING) {      // ADDING / EDITING

    if (LOGIC_BUTTON_OK(event)) {
      //get current UDP_SIP value (UDP - TCP selection choicewidget)
      bool tmpUDP_SIP = false;
      if (udpTcpSipSelection != NULL) {
        log_e("udptcpsipselection: %d", udpTcpSipSelection->getValue());
        switch (udpTcpSipSelection->getValue()) {
        case 0: // UDP-SIP
          tmpUDP_SIP = true;
          break;
        case 1: // TCP-SIP
          tmpUDP_SIP = false;
          break;
        default:
          log_e("Unknown UDP-SIP - TCP-SIP selection: %d", udpTcpSipSelection->getValue());
          tmpUDP_SIP = false;
        }
      }

      log_d("saving SIP accounts");
      bool saved = false;
      if (!currentKey) {
        // Save a new account
        int s = ini.addSection();
        ini[s]["d"] = inputs[0]->getText() ? inputs[0]->getText() : "";
        ini[s]["s"] = inputs[3]->getText() ? inputs[3]->getText() : "";
        ini[s]["p"] = passwordInput->getText() ? passwordInput->getText() : "";
        //set udp-tcp selection
        if(tmpUDP_SIP) {
          ini[s]["u"] = "UDP-SIP";
        } else {
          ini[s]["u"] = "TCP-SIP";
        }
        saved = true;
      } else if (currentKey < ini.nSections()) {

        // Replace existing account
        ini[currentKey]["d"] = inputs[0]->getText() ? inputs[0]->getText() : "";
        ini[currentKey]["s"] = inputs[3]->getText() ? inputs[3]->getText() : "";
        ini[currentKey]["p"] = passwordInput->getText() ? passwordInput->getText() : "";
        //set udp-tcp selection
        if(tmpUDP_SIP) {
          ini[currentKey]["u"] = "UDP-SIP";
          log_e("ini[currentKey][u] = UDP-SIP");
        } else {
          ini[currentKey]["u"] = "TCP-SIP";
          log_e("ini[currentKey][u] = TCP-SIP");
        }

        // Change current SIP account in memory
        if (ini[currentKey].hasKey("m")) {
          // this is a primary account -> update it in memory
          controlState.setSipAccount(ini[currentKey].getValueSafe("d", ""),
                                     ini[currentKey].getValueSafe("s", ""),
                                     ini[currentKey].getValueSafe("p", ""),
                                     ini[currentKey].getValueSafe("u", ""));
          res |= REDRAW_HEADER;   // redraw SIP icon (if any)
        }

        // TODO: sort and update currentKey to newKey
        saved = true;
      }
      if (saved) {
        // Save the accounts to persistent storage
        saved = ini.store();
        if (saved) {
          log_v("saved");
        } else {
          log_e("failed to save");
        }
      }

      if (saved) {
        log_d("saved -> ressetting");
        for (int i=0; i<sizeof(inputs)/sizeof(inputs[0]); i++) {
          inputs[i]->setText("");
        }
        passwordInput->setText("");
        udpTcpSipSelection->setValue(0);//TCP-SIP
        createLoadMenu();
        changeState(appState==EDITING ? VIEWING : SELECTING);
        res |= REDRAW_ALL;
      }

    } else if (event == WIPHONE_KEY_END) {

      changeState(appState==EDITING ? VIEWING : SELECTING);
      res |= REDRAW_ALL;

    } else if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

      // Change focus
      nextFocus(event == WIPHONE_KEY_DOWN);

    } else {

      // Pass event to whatever widget is focused
      ((TextInputWidget*) getFocused())->processEvent(event);
      if (getFocused()==inputs[1] || getFocused()==inputs[2]) {
        // user name or server name changed -> update SIP URI
        const char* userName = inputs[1]->getText();
        const char* server = inputs[2]->getText();
        if (userName && server && *userName && *server) {
          char buff[100];
          snprintf(buff, sizeof(buff)/sizeof(buff[0]), "sip:%s@%s", userName, server);
          inputs[3]->setText(buff);
        } else if (userName && *userName) {
          char buff[100];
          snprintf(buff, sizeof(buff)/sizeof(buff[0]), "sip:%s", userName);
          inputs[3]->setText(buff);
        } else if (server && *server) {
          char buff[100];
          snprintf(buff, sizeof(buff)/sizeof(buff[0]), "@%s", server);
          inputs[3]->setText(buff);
        } else {
          inputs[3]->setText("");
        }
      }

    }
  }
  return res;
}

void SipAccountsApp::redrawScreen(bool redrawAll) {
  //log_d("redrawScreen SipAccountsApp");

  if (appState==SELECTING) {
    if (!screenInited || redrawAll) {
      ((GUIWidget*) rect)->redraw(lcd);
    }
    ((GUIWidget*) menu)->redraw(lcd);

  } else if (appState==VIEWING) {

    if (!screenInited || redrawAll) {
      ((GUIWidget*) rect)->redraw(lcd);
      ((GUIWidget*) headpic)->redraw(lcd);
      ((GUIWidget*) contactName)->redraw(lcd);
      ((GUIWidget*) phonePic)->redraw(lcd);
      ((GUIWidget*) addressView)->redraw(lcd);
    }
    ((GUIWidget*) viewMenu)->redraw(lcd);

  } else {    // ADDING / EDITING

    if (!screenInited || redrawAll) {
      ((GUIWidget*) clearRect)->redraw(lcd);
      for (int i=0; i<sizeof(inputLabels)/sizeof(inputLabels[0]); i++) {
        ((GUIWidget*) inputLabels[i])->redraw(lcd);
      }
    }

    // Redraw input widgets
    for (int i=0; i<sizeof(inputs)/sizeof(inputs[0]); i++) {
      ((GUIWidget*) inputs[i])->redraw(lcd);
    }
    ((GUIWidget*) passwordInput)->redraw(lcd);
    ((GUIWidget*) udpTcpSipSelection)->redraw(lcd);

  }


  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Call app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

CallApp::CallApp(Audio* audio, LCD& lcd, ControlState& state, bool isCaller, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(2), audio(audio),ini(Storage::ConfigsFile), caller(isCaller) {
  log_d("CallApp create");
  const char* s;

  // Create and arrange widgets
  header->setTitle(isCaller ? "Calling" : "Call");
  footer->setButtons(isCaller ? "Loud Spkr" : "Accept", isCaller ? "Hang up" : "Reject");
  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  // State caption in the middle
  const uint16_t spacing = 4;
  uint16_t yOff = header->height() + 26;
  stateCaption = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_BOLD_20]->height(),
                                 isCaller ? "Making a call..." : "Inbound call...", isCaller ? WP_ACCENT_1 : WP_ACCENT_S, WP_COLOR_1, fonts[AKROBAT_BOLD_20], LabelWidget::CENTER);
  yOff += stateCaption->height() + (spacing*2);

  log_i("CallApp LastReason icon_person_w");
  // Headpic icon
  iconRect = new RectIconWidget((lcd.width()-50)>>1, yOff, 50, 50, isCaller ? WP_ACCENT_1 : WP_ACCENT_S, icon_person_w, sizeof(icon_person_w));
  yOff += iconRect->height() + (spacing*2);

  log_i("CallApp Name and URI above");
  // Name and URI above
  s = controlState.calleeNameDyn!=NULL ? (const char*) controlState.calleeNameDyn : "";
  nameCaption =  new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_EXTRABOLD_22]->height(), s, WP_COLOR_0, WP_COLOR_1, fonts[AKROBAT_EXTRABOLD_22], LabelWidget::CENTER);
  yOff += nameCaption->height() + spacing;
  s = controlState.calleeUriDyn!=NULL ? (const char*) controlState.calleeUriDyn : "";
  uriCaption =  new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_BOLD_20]->height(), s, WP_DISAB_0, WP_COLOR_1, fonts[AKROBAT_BOLD_20], LabelWidget::CENTER);

  log_i("CallApp Name and uriCaption");
  // Debug string: shows SIP response messages and volume
  yOff += uriCaption->height() + 20;
  s = controlState.lastReasonDyn != NULL ? (const char*) controlState.lastReasonDyn : "";
  debugCaption = new LabelWidget(0, yOff, lcd.width(), fonts[AKROBAT_BOLD_16]->height(), s, WP_DISAB_0, WP_COLOR_1, fonts[AKROBAT_BOLD_16], LabelWidget::CENTER);
  
  reasonHash = hash_murmur(s);
  log_i("hash_murmur");  
  audio->chooseSpeaker(LOUDSPEAKER);
}

CallApp::~CallApp() {
  log_d("destroy CallApp");

  delete stateCaption;
  delete debugCaption;
  //delete debugCaption_loudSpkr;
  delete nameCaption;
  delete uriCaption;
}

appEventResult CallApp::processEvent(EventType event) {
  log_d("processEvent CallApp");
  appEventResult res = DO_NOTHING;
  if (event == WIPHONE_KEY_END) {
    if(!controlState.sipRegistered) {
      log_i("processEvent EXIT_APP");
      return EXIT_APP;
    }

    if (controlState.sipState == CallState::Call or controlState.sipState == CallState::InvitingCallee) {
      log_i("processEvent1 HungUp");
      controlState.setSipState(CallState::HungUp);
      delay(10);
    } else {
      log_i("processEvent EXIT_APP");
      return EXIT_APP;
    }
  }

  if (LOGIC_BUTTON_BACK(event)) {
    log_i("LOGIC_BUTTON_BACK CallApp");
    if(!controlState.sipRegistered) {
      log_i("processEvent EXIT_APP");
      return EXIT_APP;
    }
    // Reject / Hang up
    if (controlState.sipState == CallState::BeingInvited) {
      stateCaption->setText("Declining");
      controlState.setSipState(CallState::Decline);
      res |= REDRAW_SCREEN;
    } else if (controlState.sipState != CallState::Idle && controlState.sipState != CallState::HangUp &&
               controlState.sipState != CallState::HangingUp && controlState.sipState != CallState::HungUp) {
      stateCaption->setText("Hanging up");
      footer->setButtons(NULL, "Hanging");
      controlState.setSipState(CallState::HangUp);
      res |= REDRAW_SCREEN;
    }

  } else if (LOGIC_BUTTON_OK(event)) {

    // Accept call
    if (controlState.sipState == CallState::BeingInvited) {
      stateCaption->setText("Accepting");
      footer->setButtons("Loud Spkr", "Hang up");
      controlState.setSipState(CallState::Accept);
      res |= REDRAW_SCREEN | REDRAW_FOOTER;
      audio->chooseSpeaker(EARSPEAKER);
    }

  } else if (event == CALL_UPDATE_EVENT) {

    uint32_t hash;

    if(controlState.lastReasonDyn != NULL) {
      hash = hash_murmur(controlState.lastReasonDyn);
    }

    if (reasonHash != hash) {
      debugCaption->setText(controlState.lastReasonDyn);
      reasonHash = hash;
      res |= REDRAW_SCREEN;
    }

    if (controlState.sipState == CallState::Idle) {

      log_d("exiting call app");
      return EXIT_APP;

    } else if (controlState.sipState == CallState::Call) {

      // Notify about start of the call
      stateCaption->setText("Call in progress");
      res |= REDRAW_SCREEN;

    } else if (controlState.sipState == CallState::HungUp) {
      log_i("Hung up");
      // Notify about termination of the call
      stateCaption->setText("Hung up");
      res |= REDRAW_SCREEN;

    }

    if (!screenInited) {
      res |= REDRAW_ALL;
    }

  } else if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN) {

    int8_t earpieceVol, headphonesVol, loudspeakerVol;
    //audio->getVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    if ((ini.load() ) && !ini.isEmpty()) {
      if (ini.hasSection("audio")) {
          log_d("getting audio info");
          earpieceVol = ini["audio"].getIntValueSafe(earpieceVolField, earpieceVol);
          headphonesVol = ini["audio"].getIntValueSafe(headphonesVolField, headphonesVol);
          loudspeakerVol = ini["audio"].getIntValueSafe(loudspeakerVolField, loudspeakerVol);
        }
      //}
      else {
        log_e("configs file corrup or unknown format");
        IF_LOG(VERBOSE)
        ini.show();
      }
    }
    log_d("Volumes are earspkr %d headphone %d loudspkr %d", earpieceVol,headphonesVol,loudspeakerVol );
    
    int8_t d = event == WIPHONE_KEY_UP ? 6 : -6;
    earpieceVol += d;
    headphonesVol += d;
    loudspeakerVol += d;
    uint8_t precentage = 0x0;
    uint8_t precentageLoud = 0x0;
    //audio->setVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    //audio->getVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    char buff[50];

 
    if (!ini.hasSection("audio")) {
        ini.addSection("audio");
      }
    ini["audio"][earpieceVolField] = earpieceVol;
    ini["audio"][headphonesVolField] = headphonesVol;
    ini["audio"][loudspeakerVolField] = loudspeakerVol;
    if (ini.store()) {
      log_d("new audio settings are saved");
    }
    ini.unload();
    audio->setVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    audio->getVolumes(earpieceVol, headphonesVol, loudspeakerVol);
    log_d("New Volumes are earspkr %d headphone %d loudspkr %d", earpieceVol,headphonesVol,loudspeakerVol );
    
    if (earpieceVol == -69){
      precentage = 0x0;
      precentageLoud = 0x0;
    } 
    if ((earpieceVol == -66) || (earpieceVol == -63) ) {
      precentage = 0x04;
      precentageLoud = 8;
    } 
    if ((earpieceVol == -60) || (earpieceVol == -57)) {
      precentage = 12;
      precentageLoud = 16;
    } 
    if ((earpieceVol == -54) || (earpieceVol == -51)) {
      precentage = 20;
      precentageLoud = 24;
    } 
    if ((earpieceVol == -48) || (earpieceVol == -45)) {
      precentage = 28;
      precentageLoud = 32;
    } 
    if ((earpieceVol == -42) || (earpieceVol == -39)) {
      precentage = 36;
      precentageLoud = 40;
    } 
    if ((earpieceVol == -36) || (earpieceVol == -33)) {
      precentage = 44;
      precentageLoud = 48;
    } 
    if ((earpieceVol == -30) || (earpieceVol == -27)) {
      precentage = 52;
      precentageLoud = 56;
    } 
    if ((earpieceVol == -24) || (earpieceVol == -21)) {
      precentage = 60;
      precentageLoud = 64;
    } 
    if ((earpieceVol == -18) || (earpieceVol == -15)) {
      precentage = 68;
      precentageLoud = 72;
    } 
    if ((earpieceVol == -12) || (earpieceVol == -9)) {
      precentage = 76;
      precentageLoud = 80;
    } 
    if ((earpieceVol == -6) || (earpieceVol == -3)) {
      precentage = 84;
      precentageLoud = 90;
    } 
    if ((earpieceVol == 0) || (earpieceVol == 3)) {
      precentage = 92;
      precentageLoud = 100;
    } 
    if (earpieceVol == 6) {
      precentage = 100;
      precentageLoud = 100;
    }
    log_d("precentage is %d %%", precentage);
    log_d("earpieceVol is %d %%", earpieceVol);
    if(loudSpkr == false){
      snprintf(buff, sizeof(buff), "Speaker %d %%, Headphones %d %%", precentage, precentage);
    } else {
      snprintf(buff, sizeof(buff), "    Loudspeaker %d %%",  precentageLoud);
    }
    
    precentage = 0x0;
    precentageLoud = 0x0;
    debugCaption->setText(buff);
    //debugCaption_loudSpkr->setText(loudSpkrBuff);

    res |= REDRAW_SCREEN;
  } 
  
  if (event == WIPHONE_KEY_SELECT) {
    if (controlState.sipState == CallState::Call) {
      if (loudSpkr == false){
        footer->setButtons("Ear Spkr", "Hang up");
        res |= REDRAW_SCREEN | REDRAW_FOOTER;
        audio->chooseSpeaker(!EARSPEAKER);
        loudSpkr = true;
      } else {
        footer->setButtons("Loud Spkr", "Hang up");
        res |= REDRAW_SCREEN | REDRAW_FOOTER;
        audio->chooseSpeaker(EARSPEAKER);
        loudSpkr = false;
      }
      
    }
  }
  log_d("res inisde processevent is %x", res);
  return res;
}

void CallApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen CallApp");

  if (!screenInited || redrawAll) {
    log_d("redraw all");
    // Initialize screen
    ((GUIWidget*) clearRect)->redraw(lcd);

    ((GUIWidget*) iconRect)->redraw(lcd);
    ((GUIWidget*) stateCaption)->redraw(lcd);
    //((GUIWidget*) debugCaption_loudSpkr)->redraw(lcd);
    ((GUIWidget*) debugCaption)->redraw(lcd);
    
    ((GUIWidget*) nameCaption)->redraw(lcd);
    ((GUIWidget*) uriCaption)->redraw(lcd);

    //lcd.fillRect(95, 240, 50, 20, WP_ACCENT_1);     // DEBUG: very strange bug with white pixels over black border

  } else {

    // Refresh only updated labels
    if (stateCaption->isUpdated()) {
      log_d("stateCaption updated");
      ((GUIWidget*) stateCaption)->redraw(lcd);     // TODO: either remove isUpdated, or replace with refresh
    }
    if (debugCaption->isUpdated()) {
      log_d("debugCaption updated");
      ((GUIWidget*) debugCaption)->redraw(lcd);
      //char updateBuff[30] = {0x0};
      //(const char*)updateBuff = controlState.lastReasonDyn != NULL ? (const char*) controlState.lastReasonDyn : "";
      // debugCaption->setText((controlState.lastReasonDyn != NULL ? (const char*) controlState.lastReasonDyn : ""));
      // ((GUIWidget*) debugCaption)->redraw(lcd);
    }
    // if (debugCaption_loudSpkr->isUpdated()) {
    //   log_d("debugCaption_loudSpkr updated");
    //   ((GUIWidget*) debugCaption_loudSpkr)->redraw(lcd);
      
    //   debugCaption_loudSpkr->setText("");
    //   ((GUIWidget*) debugCaption_loudSpkr)->redraw(lcd);
    // }
    if (nameCaption->isUpdated()) {
      log_d("nameCaption updated");
      ((GUIWidget*) nameCaption)->redraw(lcd);
    }
    if (uriCaption->isUpdated()) {
      log_d("uriCaption updated");
      ((GUIWidget*) uriCaption)->redraw(lcd);
    }
  }
  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  EditNetwork app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

EditNetworkApp::EditNetworkApp(LCD& lcd, ControlState& state, const char* SSID, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(5), ini(Networks::filename) {
  log_d("EditNetworkApp");

  if (ini.load() || ini.restore()) {
    if (ini.isEmpty() || !ini[0].hasKey("v") || strcmp(ini[0]["v"], "1")) {
      log_d("unknown version or corrupt \"%s\" file", ini.filename());
    }
  } else {
    ini[0]["desc"] = "WiPhone WiFi networks";
    ini[0]["v"] = "1";
  }
  IF_LOG(VERBOSE)
  ini.show();

  // if SSID is NULL - it is run as a standalone app (if yes - edit the current network)
  standAloneApp = false;
  if (SSID == NULL) {
    standAloneApp = true;

    // Load current network name (connected or previously connected)
    if (wifiState.ssid() != NULL) {
      SSID = wifiState.ssid();
      log_v("SSID: %s", SSID);
    }
  }

  // Is this network connected?
  connectedNetwork = false;
  if ( SSID != NULL && (SSID == wifiState.ssid() || (wifiState.ssid()!=NULL && !strcmp(wifiState.ssid(), SSID)) ) ) {       // network name coincides
    if (wifiState.isConnected()) {
      log_d("network is connected");
      connectedNetwork = true;
    }
  }

  // Is this network in list of known networks?
  knownNetwork = ini.query("s", SSID) >= 0;         // find a section with field "s" (SSID) equal to the current network SSID

  // Create and arrange general widgets
  header->setTitle("Edit Network");
  footer->setButtons("Connect", "Clear");

  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  uint16_t yOff = header->height() + 5;

  // Add form
  this->addLabelInput(yOff, ssidLabel, ssidInput, "SSID:", 50);
  this->addLabelInput(yOff, passLabel, passInput, "Password:", 100);

  const uint16_t spacing = 6; // 4
  const uint16_t xOff = 2;
  yOff += spacing*2;
  lcd.setTextFont(fonts[OPENSANS_COND_BOLD_20]);
  uint16_t buttonSize1 = lcd.textWidth("Forget") + BUTTON_PADDING;
  uint16_t buttonSize2 = lcd.textWidth("Disconnect ") + BUTTON_PADDING;
  saveButton = new ButtonWidget(xOff, yOff, "Save");

  forgetButton = NULL;
  connectionButton = NULL;
  wifiOnOff = NULL;
  if (knownNetwork) {
    forgetButton = new ButtonWidget(xOff + saveButton->width() + 2*spacing, yOff, "Forget");

    yOff += saveButton->height() + spacing*2;
    connectionButton = new ButtonWidget(xOff, yOff, connectedNetwork ? "Disconnect" : "Connect", ButtonWidget::textWidth("Connecting")+18);

    wifiOnOff = new ChoiceWidget(0, yOff+connectionButton->height(), lcd.width(), 35);
    wifiOnOff->addChoice("WIFI-ON");
    wifiOnOff->addChoice("WIFI-OFF");
    if(wifiOn){
      wifiOnOff->setValue(0);
    } else {
      wifiOnOff->setValue(1);
    }

    yOff += wifiOnOff->height();
  
  } else {
    
    wifiOnOff = new ChoiceWidget(0, yOff+saveButton->height(), lcd.width(), 35);
    wifiOnOff->addChoice("WIFI-ON");
    wifiOnOff->addChoice("WIFI-OFF");
    if(wifiOn){
      wifiOnOff->setValue(0);
    } else {
      wifiOnOff->setValue(1);
    }

    yOff += wifiOnOff->height();
  
  }

  // Load password / populate text
  if (SSID != NULL) {
    ssidInput->setText(SSID);
    int index = ini.query("s", SSID);           // "s" - stands for "SSID"
    if (index>=0 && ini[index].hasKey("p")) {   // "p" - stands for "password"
      passInput->setText(ini[index]["p"]);
    }
  } else {
    ssidInput->setText("");
    passInput->setText("");
  }

  // Focusables
  addFocusableWidget(ssidInput);
  addFocusableWidget(passInput);
  addFocusableWidget(saveButton);
  if (forgetButton != NULL) {
    addFocusableWidget(forgetButton);
  }
  if (connectionButton != NULL) {
    addFocusableWidget(connectionButton);
  }
  if (wifiOnOff != NULL) {
    addFocusableWidget(wifiOnOff);
  }
  
  setFocus(ssidInput);
  screenInited = false;
}

EditNetworkApp::~EditNetworkApp() {
  log_d("destroy EditNetworkApp");

  ini.backup();

  delete clearRect;
  delete ssidLabel;
  delete ssidInput;
  delete passLabel;
  delete passInput;
  delete saveButton;
  if(wifiOnOff){
    delete wifiOnOff;
  }
  
}

appEventResult EditNetworkApp::processEvent(EventType event) {
  log_d("processEvent EditNetworkApp");

  bool quit = false;

  FocusableWidget* focusedWidget = getFocused();

  if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    // Change focus
    nextFocus(event == WIPHONE_KEY_DOWN);

  } else if (event == WIPHONE_KEY_END) {

    quit = true;

  } else if (LOGIC_BUTTON_OK(event) && focusedWidget == saveButton) {

    // If "OK" was pressed while one of saveButton selected

    // Save new WiFi credentials to NVS
    log_d("save button pressed");

    // Reflect changes to NanoINI
    int index = ini.query("s", ssidInput->getText());       // "s" key stands for "SSID"
    if (index >= 0) {
      ini[index]["p"] = passInput->getText();               // update password for a known network ("p" key")
    } else {
      int i = ini.addSection();
      ini[i]["s"] = ssidInput->getText();
      ini[i]["p"] = passInput->getText();
      // TODO: maybe we don't always want to set the network as preferred?
      ini.setUniqueFlag(i, "m");                            // "m" (for "main") is the preferred network flag
    }

    // Save to file, reload current network
    {
      ini.store();
      log_d("saved network");

      log_d("disconnecting");
      wifiState.disconnect();

      // Update the WiFi credentials from NVS
      // TODO: clean it up
      wifiState.loadPreferred();
      wifiState.loadNetworkSettings(ssidInput->getText());

      // Quit from the app
      quit = true;
    }

  } else if (LOGIC_BUTTON_OK(event) && forgetButton!=NULL && focusedWidget == forgetButton) {

    log_d("forget button pressed");
    int i = ini.query("s", ssidInput->getText());     // TODO: consider that there might be multiple networks with this name
    if (i>=0) {
      bool removed = false;
      if (ini.removeSection(i)) {
        log_d("Network forgotten: %s", ssidInput->getText());
        removed = ini.store();
      }
      if (!removed) {
        log_d("COULD NOT BE REMOVED: %s", ssidInput->getText());
      }
      quit = true;
    }
    wifiState.disable();

  } else if (event==WIPHONE_KEY_CALL || event==WIPHONE_KEY_SELECT || (LOGIC_BUTTON_OK(event) && connectionButton!=NULL && focusedWidget == connectionButton) ) {

    // Conect to the network event

    // TODO: move actual connecting / unconnecting to the main cycle?
    // TODO: after the credentials have changed, disable "Connect" button

    log_d("connection button pressed");
    if (connectedNetwork) {
      log_d("disconnecting");
      //wifiState.disconnect();
      wifiState.disable();
      quit = true;

      int index = ini.query("s", ssidInput->getText());       // "s" key stands for "SSID"
      if (index >= 0) {
        ini[index]["disabled"] = "true";
        ini.store();
      } 
    } else {
      if (wifiState.connectTo(ssidInput->getText())) {
        log_d("connecting: %s", ssidInput->getText());

        int index = ini.query("s", ssidInput->getText());       // "s" key stands for "SSID"
        if (index >= 0) {
          ini[index]["disabled"] = "false";
          ini.store();
        } 

        // Change button appearance
        connectionButton->setText("Connecting");
        //((GUIWidget*) connectionButton)->redraw(lcd);         // TODO: works, but doesn't separate event processing from redrawing well (move connecting logic elsewhere)

        int i = ini.query("s", ssidInput->getText());                   // "s" for "SSID"
        if (i >= 0 && ini.setUniqueFlag(i, "m") && ini.store()) {       // "m" for "main" (preferred network)
          log_d("set as preferred network");
        }

        // Wait for result
        // TODO: move actual connecting elsewhere
        log_d("waiting for connectionEvent");
        for (uint8_t j=0; j<50 && !wifiState.isConnectionEvent(); j++) {
          delay(100);
        }

        // Quit or stay
        if (wifiState.isConnectionEvent()) {
          log_d("connection event happened");
          delay(100);
          quit = true;
        } else {
          log_d("connection timeout");

          // Restore button appearance
          connectionButton->setText("Connect");
          //((GUIWidget*) connectionButton)->redraw(lcd);       // Works, but doesn't separate event processing from redrawing well
        }

      } else {
        log_d("could not connect: %s", ssidInput->getText());
      }
    }

  } else {

    // Pass button to whatever is focused

    if (focusedWidget != NULL) {
      focusedWidget->processEvent(event);
    }

  }

  
  if (wifiOnOff != NULL) {
      log_e("wifiOnOff: %d", wifiOnOff->getValue());
      esp_err_t err;
      switch (wifiOnOff->getValue()) {
      case 0: // wifi ON
        wifiOn = true;
        err = esp_wifi_start();
        if(err != ESP_OK) {
          log_e("WIFI cann't be started");
        } else {
          log_d("WIFI will Start");
          
          connectedNetwork = false;
          
          if(ssidInput->getText() != NULL){
            if (wifiState.connectTo(ssidInput->getText())) {
              log_d("connecting: %s", ssidInput->getText());

              int i = ini.query("s", ssidInput->getText());                   // "s" for "SSID"
              if (i >= 0 && ini.setUniqueFlag(i, "m") && ini.store()) {       // "m" for "main" (preferred network)
                log_d("set as preferred network");
              }

              log_d("waiting for connectionEvent");
              for (uint8_t j=0; j<50 && !wifiState.isConnectionEvent(); j++) {
                delay(100);
              }

              if (wifiState.isConnectionEvent()) {
                log_d("connection event happened");
                delay(100);
                connectionButton->setText("Disconnect");
              } else {
                log_d("connection timeout");
              }

            } else {
              log_d("could not connect: %s", ssidInput->getText());
            }
          }
        }
        break;
      case 1: // wifi OFF
        wifiOn = false;
        err = esp_wifi_stop();
        if(err != ESP_OK) {
          log_e("WIFI cann't be stopped");
        } else {
          log_d("WIFI will be stopped");
          connectedNetwork = true;
          if (connectedNetwork) {
          log_d("disconnecting");
          
          wifiState.disable();
          }
    
        }
        break;
      default:
        log_e("Unknown UDP-SIP - TCP-SIP selection: %d", wifiOnOff->getValue());
        
      }
    }

  
  

  return quit ? EXIT_APP : REDRAW_ALL;
}

void EditNetworkApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen EditNetworkApp");

  if (!screenInited || redrawAll) {
    ((GUIWidget*) clearRect)->redraw(lcd);
    ((GUIWidget*) ssidLabel)->redraw(lcd);
    ((GUIWidget*) passLabel)->redraw(lcd);
  }
  ((GUIWidget*) ssidInput)->redraw(lcd);
  ((GUIWidget*) passInput)->redraw(lcd);
  ((GUIWidget*) saveButton)->redraw(lcd);
  if (forgetButton != NULL) {
    ((GUIWidget*) forgetButton)->redraw(lcd);
  }
  if (connectionButton != NULL) {
    ((GUIWidget*) connectionButton)->redraw(lcd);
  }
  if (wifiOnOff != NULL) {
    ((GUIWidget*) wifiOnOff)->redraw(lcd);
  }
  
  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  TimeConfig app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

TimeConfigApp::TimeConfigApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(1), ini(Storage::ConfigsFile) {
  log_d("TimeConfigApp");

  if (ini.load() || ini.restore()) {
    if (ini.isEmpty() || !ini[0].hasKey("v") || strcmp(ini[0]["v"], "1")) {
      log_d("unknown version or corrupt \"%s\" file", ini.filename());
    }
  } else {
    ini[0]["desc"] = "WiPhone general configs";
    ini[0]["v"] = "1";
  }
  IF_LOG(VERBOSE)
  ini.show();

  if (!ini.hasSection("time")) {
    log_e("adding section `time`");
    ini.addSection("time");
    ini["time"]["zone"] = "-0";
  }

  // Create and arrange general widgets
  header->setTitle("Time setting");
  footer->setButtons("Save", "Clear");

  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  uint16_t yOff = header->height() + 5;

  // Add form
  this->addInlineLabelInput(yOff, 120, timeZoneLabel, timeZoneInput, "Time offset:", 9);
  this->errorLabel = new LabelWidget(0, yOff, lcd.width(), 25, "", TFT_RED, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += this->errorLabel->height();

  // Load password / populate text
  if (ini.hasSection("time") && ini["time"].hasKey("zone")) {
    timeZoneInput->setText(ini["time"]["zone"]);
  } else {
    timeZoneInput->setText("");
  }

  // Focusables
  addFocusableWidget(timeZoneInput);
  setFocus(timeZoneInput);

  screenInited = false;
}

TimeConfigApp::~TimeConfigApp() {
  log_d("destroy TimeConfigApp");

  ini.backup();

  delete timeZoneLabel;
  delete timeZoneInput;
  delete errorLabel;
}

appEventResult TimeConfigApp::processEvent(EventType event) {
  log_v("<-- enter function");

  bool quit = false;

  FocusableWidget* focusedWidget = getFocused();

  if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    // Change focus
    nextFocus(event == WIPHONE_KEY_DOWN);

  } else if (event == WIPHONE_KEY_END) {

    quit = true;

  } else if (LOGIC_BUTTON_OK(event)) {

    log_d("Save button pressed");

    // Convert/Check user input
    const char* tzText = timeZoneInput->getText();
    const char* error = NULL;
    float tz = -25;
    if (Clock::parseTimeZone(tzText, tz, error)) {

      // Set time offset in the GUI
      ntpClock.setTimeZone(tz);

      // Save user input
      ini["time"]["zone"] = tz;
      ini.store();

      quit = true;

    } else {
      // Error message
      errorLabel->setText(error);
    }

  } else {

    // Pass button to whatever is focused

    if (focusedWidget != NULL) {
      focusedWidget->processEvent(event);
    }

  }

  return quit ? EXIT_APP : REDRAW_SCREEN;
}

void TimeConfigApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen TimeConfigApp");

  if (!screenInited || redrawAll) {
    ((GUIWidget*) clearRect)->redraw(lcd);
    ((GUIWidget*) timeZoneLabel)->redraw(lcd);
  }
  ((GUIWidget*) timeZoneInput)->redraw(lcd);
  ((GUIWidget*) errorLabel)->redraw(lcd);

  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  ScreenConfig app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ScreenConfigApp::ScreenConfigApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), FocusableApp(5), ini(Storage::ConfigsFile) {
  log_d("ScreenConfigApp");

  if (ini.load() || ini.restore()) {
    if (ini.isEmpty() || !ini[0].hasKey("v") || strcmp(ini[0]["v"], "1")) {
      log_d("unknown version or corrupt \"%s\" file", ini.filename());
    }
  } else {
    ini[0]["desc"] = "WiPhone general configs";
    ini[0]["v"] = "1";
  }
  IF_LOG(VERBOSE)
  ini.show();

  if (!ini.hasSection("screen")) {
    log_e("adding section `screen`");
    ini.addSection("screen");
    ini["screen"]["bright_level"] = "100";

    ini["screen"]["dimming"] = "1";
    ini["screen"]["dim_level"] = "15";
    ini["screen"]["dim_after_s"] = "20";

    ini["screen"]["sleeping"] = "1";
    ini["screen"]["sleep_after_s"] = "30";
  }
  if (!ini.hasSection("lock")) {
    log_e("adding section `lock`");
    ini.addSection("lock");
    ini["lock"]["lock_keyboard"] = "1";
  }

  // Create and arrange general widgets
  header->setTitle("Screen settings");
  footer->setButtons("Save", "Clear");

  clearRect = new RectWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), WP_COLOR_1);

  uint16_t yOff = header->height() + 5, rulerOff = 5;

  // Add form
  const uint16_t labelWidth = 110;
  // - brightness
  this->addInlineLabelSlider(yOff, labelWidth, brightLevelLabel, brightLevelSlider, "Brightness", 5, 100, "%", 19);
  this->addRuler(yOff, ruler1, rulerOff);

  // - dimming
  this->addInlineLabelYesNo(yOff, labelWidth, dimmingLabel, dimmingChoice, "Dim screen");
  this->addInlineLabelSlider(yOff, labelWidth, dimLevelLabel, dimLevelSlider, "Dim level", 5, 100, "%", 19);
  this->addInlineLabelInput(yOff, labelWidth, dimAfterLabel, dimAfterInput, "Dim after, s", 6, InputType::Numeric);
  this->addRuler(yOff, ruler2, rulerOff);

  // - screen sleeping & lock
  this->addInlineLabelYesNo(yOff, labelWidth, sleepingLabel, sleepingChoice, "Sleep screen");
  yOff += 1;
  this->addInlineLabelInput(yOff, labelWidth, sleepAfterLabel, sleepAfterInput, "Sleep after, s", 6, InputType::Numeric);
  yOff += 1;
  this->addInlineLabelYesNo(yOff, labelWidth, lockingLabel, lockingChoice, "Lock screen");
  this->addRuler(yOff, ruler3, rulerOff);

  this->errorLabel = new LabelWidget(0, yOff, lcd.width(), 25, "", TFT_RED, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += this->errorLabel->height();

  // Populate form
  lockingChoice->setValue((bool) ini["lock"].getIntValueSafe("lock_keyboard", 0));
  dimmingChoice->setValue((bool) ini["screen"].getIntValueSafe("dimming", 0));
  sleepingChoice->setValue((bool) ini["screen"].getIntValueSafe("sleeping", 0));

  brightLevelSlider->setValue(ini["screen"].getIntValueSafe("bright_level", 100));
  dimLevelSlider->setValue(ini["screen"].getIntValueSafe("dim_level", 100));

  dimAfterInput->setText(ini["screen"].getValueSafe("dim_after_s", ""));
  sleepAfterInput->setText(ini["screen"].getValueSafe("sleep_after_s", ""));

  // Preserve old values
  this->oldDimAfter = ini["screen"].getIntValueSafe("dim_after_s", 20);
  this->oldSleepAfter = ini["screen"].getIntValueSafe("sleep_after_s", 30);

  // Check form
  int32_t dimAfter, sleepAfter;
  this->checkForm(dimAfter, sleepAfter);

  // Focusables
  this->addFocusableWidget(brightLevelSlider);

  this->addFocusableWidget(dimmingChoice);
  this->addFocusableWidget(dimLevelSlider);
  this->addFocusableWidget(dimAfterInput);

  this->addFocusableWidget(sleepingChoice);
  this->addFocusableWidget(sleepAfterInput);
  this->addFocusableWidget(lockingChoice);

  this->setFocus(brightLevelSlider);

  this->screenInited = false;
}

ScreenConfigApp::~ScreenConfigApp() {
  log_d("destroy ScreenConfigApp");

  ini.backup();

  delete lockingLabel;
  delete lockingChoice;
  delete dimmingLabel;
  delete dimmingChoice;
  delete brightLevelLabel;
  delete brightLevelSlider;
  delete dimLevelLabel;
  delete dimLevelSlider;
  delete dimAfterLabel;
  delete dimAfterInput;
  delete sleepAfterLabel;
  delete sleepAfterInput;
  delete errorLabel;
  delete ruler1;
  delete ruler2;
  delete ruler3;
}

bool ScreenConfigApp::checkForm(int32_t &dimAfter, int32_t &sleepAfter, bool autocorrect) {
  bool correct = true;

  // - check dimAfter value
  if (dimAfterInput->getInt(dimAfter)) {
    if (dimAfter < 5) {
      errorLabel->setText("Dimming delay too small");
      correct = false;
    }
  } else {
    errorLabel->setText("Incorrect dimming delay");
    correct = false;
  }
  if (!correct && autocorrect) {
    dimAfter = oldDimAfter;
    dimAfterInput->setInt(dimAfter);
    correct = true;
  }

  // - check sleepAfter value
  if (sleepAfterInput->getInt(sleepAfter)) {
    if (sleepAfter < 5) {
      errorLabel->setText("Sleep delay too small");
      correct = false;
    } else if (correct && dimAfter > sleepAfter) {
      errorLabel->setText("Error: sleep before dimming");
      correct = false;
    }
  } else {
    errorLabel->setText("Incorrect sleep delay");
    correct = false;
  }
  if (!correct && autocorrect) {
    sleepAfter = oldSleepAfter;
    sleepAfterInput->setInt(sleepAfter);
    correct = true;
  }
  if (correct) {
    errorLabel->setText("");
  }
  return correct;
}

appEventResult ScreenConfigApp::processEvent(EventType event) {
  log_v("ScreenConfigApp::processEvent");

  bool quit = false;

  FocusableWidget* focusedWidget = this->getFocused();

  if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    if (focusedWidget == sleepAfterInput || focusedWidget == dimAfterInput) {
      // Enact changes of delays
      int32_t dimAfter, sleepAfter;
      bool correct = this->checkForm(dimAfter, sleepAfter);
      if (correct) {
        if (focusedWidget == dimAfterInput) {
          controlState.dimAfterMs = dimAfter*1000;
        } else if (focusedWidget == sleepAfterInput) {
          controlState.sleepAfterMs = sleepAfter*1000;
        }
      }
    }

    // Change focus
    this->nextFocus(event == WIPHONE_KEY_DOWN);

  } else if (event == WIPHONE_KEY_END) {

    // Restore values

    // - restore brightness
    int32_t brightness = ini["screen"].getIntValueSafe("bright_level", 100);
    controlState.screenBrightness = controlState.brightLevel = brightness;

#if GPIO_EXTENDER == 1509
    log_d("WIPHONE_KEY_END");
    lcdLedOnOff(true, conv100to255(brightness));
#endif // GPIO_EXTENDER == 1509

    // - restore other variables
    controlState.dimLevel = ini["screen"].getIntValueSafe("dim_level", 100);
    controlState.dimAfterMs = ini["screen"].getIntValueSafe("dim_after_s", 20)*1000;
    controlState.sleepAfterMs = ini["screen"].getIntValueSafe("sleep_after_s", 30)*1000;
    controlState.dimming = (bool)ini["screen"].getIntValueSafe("dimming", 0);
    controlState.sleeping = (bool)ini["screen"].getIntValueSafe("sleeping", 0);

    quit = true;

  } else if (LOGIC_BUTTON_OK(event)) {

    // Verify the form
    int32_t dimAfter = -1;
    int32_t sleepAfter = -1;
    bool correct = this->checkForm(dimAfter, sleepAfter, true);

    // If correct -> save
    if (correct) {
      log_v("Save button pressed");
      ini["screen"]["sleeping"] = (int32_t)sleepingChoice->getValue();
      ini["screen"]["dimming"] = (int32_t)dimmingChoice->getValue();
      ini["lock"]["lock_keyboard"] = (int32_t)lockingChoice->getValue();
      ini["screen"]["dim_level"] = (int32_t)dimLevelSlider->getValue();
      ini["screen"]["bright_level"] = (int32_t)brightLevelSlider->getValue();
      ini["screen"]["dim_after_s"] = dimAfter;
      ini["screen"]["sleep_after_s"] = sleepAfter;
      ini.store();
      quit = true;
    }

  } else {

    // Pass button to whatever is focused

    if (focusedWidget != NULL) {
      bool relevant = focusedWidget->processEvent(event);

      // Enact changes instantly

      if (relevant) {
        if (focusedWidget == brightLevelSlider) {

          // Change current screen brightness
          int32_t brightness = (int32_t)brightLevelSlider->getValue();
          controlState.screenBrightness = controlState.brightLevel = brightness;

#if GPIO_EXTENDER == 1509
          log_d("@ focusedWidget == brightLevelSlider");
          lcdLedOnOff(true, conv100to255(brightness));
#endif // GPIO_EXTENDER == 1509

        } else if (focusedWidget == dimLevelSlider) {

          // Change dim level
          controlState.dimLevel = (int32_t)dimLevelSlider->getValue();

        } else if (focusedWidget == dimmingChoice) {

          controlState.dimming = (int32_t)dimmingChoice->getValue();
          if (controlState.dimming) {
            controlState.scheduleEvent(SCREEN_DIM_EVENT, millis() + controlState.dimAfterMs);
          }

        } else if (focusedWidget == sleepingChoice) {

          controlState.sleeping = (int32_t)sleepingChoice->getValue();
          if (controlState.sleeping) {
            controlState.scheduleEvent(SCREEN_SLEEP_EVENT, millis() + controlState.sleepAfterMs);
          }

        } else if (focusedWidget == lockingChoice) {

          controlState.locking = (int32_t)lockingChoice->getValue();

        }

      }
    }

  }

  return quit ? EXIT_APP : REDRAW_SCREEN;
}

void ScreenConfigApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen ScreenConfigApp");

  if (!screenInited) {
    redrawAll = true;
  }
  if (redrawAll) {
    ((GUIWidget*) clearRect)->redraw(lcd);
    ((GUIWidget*) ruler1)->redraw(lcd);
    ((GUIWidget*) ruler2)->redraw(lcd);
    ((GUIWidget*) ruler3)->redraw(lcd);
    ((GUIWidget*) lockingLabel)->redraw(lcd);
    ((GUIWidget*) dimmingLabel)->redraw(lcd);
    ((GUIWidget*) brightLevelLabel)->redraw(lcd);
    ((GUIWidget*) dimLevelLabel)->redraw(lcd);
    ((GUIWidget*) sleepingLabel)->redraw(lcd);
    ((GUIWidget*) dimAfterLabel)->redraw(lcd);
    ((GUIWidget*) sleepAfterLabel)->redraw(lcd);
  }
  ((GUIWidget*) lockingChoice)->refresh(lcd, redrawAll);
  ((GUIWidget*) dimmingChoice)->refresh(lcd, redrawAll);
  ((GUIWidget*) sleepingChoice)->refresh(lcd, redrawAll);
  ((GUIWidget*) brightLevelSlider)->refresh(lcd, redrawAll);
  ((GUIWidget*) dimLevelSlider)->refresh(lcd, redrawAll);
  ((GUIWidget*) dimAfterInput)->refresh(lcd, redrawAll);
  ((GUIWidget*) sleepAfterInput)->refresh(lcd, redrawAll);
  ((GUIWidget*) errorLabel)->refresh(lcd, redrawAll);

  this->screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Networks app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

NetworksApp::NetworksApp(LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), ini(Networks::filename) {
  log_i("create NetworksApp");

  this->loadIni();

  // Create and arrange general widgets
  menu = NULL;
  editNetwork = NULL;

  // Subscribe to app timer event
  controlState.msAppTimerEventLast = millis();
  controlState.msAppTimerEventPeriod = 1000;

  // Start ASYNC scan
  log_v("scanning");
  wifiState.disconnect();   // disconnect and do not reconnect        TODO: after connecting to a network, don't go back to scanning view
  delay(100);

  screenInited = false;

  setHeaderFooter();
}

NetworksApp::~NetworksApp() {
  log_v("destroy NetworksApp");
  if (menu) {
    delete menu;
  }
  if (editNetwork) {
    delete editNetwork;
  }
}

void NetworksApp::setHeaderFooter() {
  header->setTitle("Networks");
  footer->setButtons("Select", "Back");
}

void NetworksApp::loadIni() {
  ini.unload();
  if (ini.load() || ini.restore()) {
    if (ini.isEmpty() || !ini[0].hasKey("v") || strcmp(ini[0]["v"], "1")) {
      log_e("unknown version or corrupt \"%s\" file", ini.filename());
    }
  } else {
    ini[0]["desc"] = "WiPhone WiFi networks";
    ini[0]["v"] = "1";
  }
  IF_LOG(VERBOSE)
  ini.show();
}

appEventResult NetworksApp::processEvent(EventType event) {
  log_v("processEvent NetworksApp");

  appEventResult res = DO_NOTHING;
  if (editNetwork != NULL) {

    // Editing a network

    if (event == APP_TIMER_EVENT) {

      int16_t rn = WiFi.scanComplete();
      if (rn != WIFI_SCAN_RUNNING) {
        log_v("scan complete: %d", rn);
        WiFi.scanDelete();
        controlState.msAppTimerEventPeriod = 0;     // unsubscribe from periodic "anyevent"
      }

    } else if ((res |= editNetwork->processEvent(event)) & EXIT_APP) {      // passing event to EditNetwork
      log_v("exited from EditNetwork");
      delete editNetwork;
      editNetwork = NULL;
      screenInited = false;
      delete menu;
      menu = NULL;
      controlState.msAppTimerEventPeriod = 1000;    // resume periodic checks
      this->loadIni();
      setHeaderFooter();
      res = (res | REDRAW_ALL) & ~(appEventResult)EXIT_APP;
    }

  } else if (LOGIC_BUTTON_BACK(event)) {

    res |= EXIT_APP;

  } else if (event == APP_TIMER_EVENT) {

    log_v("processing timer event");
    int16_t rn = WiFi.scanComplete();
    log_v("networks: ", rn, DEC);
    if (rn>=0) {
      // Remember currently selected network
      char* selected = NULL;
      if (menu!=NULL) {
        selected = (char *) menu->getSelectedTitle();
        if (selected!=NULL) {
          log_v("selected: ", selected);
          selected += strcspn(selected, ")") + 2;       // TRICKY: skip the RSSI prefix
          selected = strdup(selected);
        } else {
          log_v("nothing selected");
        }
      }

      // Create new menu
      if (menu!=NULL) {
        delete menu;
      }
      menu = new MenuWidget(0, header->height()+menuTopPadding, lcd.width(), lcd.height() - header->height() - footer->height() - menuTopPadding,
                            "No networks", fonts[OPENSANS_COND_BOLD_20], N_MAX_ITEMS, 1);
      menu->setStyle(MenuWidget::ALTERNATE_STYLE, WP_ACCENT_1, WP_COLOR_1, WP_COLOR_0, WP_ACCENT_0);
      char text[50];

      // Populate menu with new data
      for (uint16_t i=0; i<rn; i++) {
        String ssid = WiFi.SSID(i);
        bool openNetwork = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);        // network doesn't require a password
        bool knownNetwork = (ini.query("s", ssid.c_str()) >= 0);              // find a section with field "s" (SSID) equal to the current network SSID
        uint16_t len = snprintf(text, sizeof(text), "%c (%d) ", (knownNetwork || openNetwork) ? '+' : ' ', WiFi.RSSI(i));
        if (sizeof(text)>len+1)
          // Add SSID if fits
        {
          snprintf(text + len, sizeof(text) - len, "%s", ssid.c_str());
        }
        log_v("adding option: %s", text);
        bool connectedNetwork = false;
        if (wifiState.isConnected() && wifiState.ssid() != NULL && !strcmp(wifiState.ssid(), ssid.c_str())) {
          connectedNetwork = true;
        }
        menu->addOption(text, i+1, connectedNetwork ? 2 : 1);

        // Restore selection
        if (selected!=NULL && !strcmp(ssid.c_str(), selected)) {
          menu->selectLastOption();
        }
      }
      log_v("deleting selected");
      freeNull((void **) &selected);
      res |= REDRAW_SCREEN;
    } else if (rn == WIFI_SCAN_FAILED) {
      log_e("ERROR: scanning networks failed");
    }

    // Rescan
    log_v("rescanning");
    WiFi.scanNetworks(true, false, false, 750);

  } else if (IS_KEYBOARD(event) && menu!=NULL) {

    log_v("menu process");
    menu->processEvent(event);
    const char* chosen = menu->readChosenTitle();
    res |= REDRAW_SCREEN;
    if (chosen != NULL) {
      log_v("chosen title: %s", chosen);
      editNetwork = new EditNetworkApp(lcd, controlState, chosen+strcspn(chosen, ")")+2, header, footer);    // TRICKY: skipping RSSI prefix
      res |= REDRAW_ALL;
    }
  }

  return res;
}

void NetworksApp::redrawScreen(bool redrawAll) {
  log_v("redrawScreen NetworksApp");

  if (editNetwork != NULL) {

    editNetwork->redrawScreen(redrawAll);

  } else {

    if (!screenInited || redrawAll) {
      GUIWidget::corrRect(lcd, 0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), BLACK);
      lcd.setTextColor(THEME_TEXT_COLOR, THEME_BG);
      lcd.setTextFont(fonts[OPENSANS_COND_BOLD_20]);
      lcd.setTextDatum(TL_DATUM);
      lcd.drawString("Scanning...", 1, header->height() + menuTopPadding);
    }
    if (menu != NULL) {
      ((GUIWidget*) menu)->redraw(lcd);
    }

    screenInited = true;

  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Circle app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

CircleApp::CircleApp(LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state) {
  log_d("create CircleApp");

  anyPressed = false;

  SmoothFont* font = fonts[OPENSANS_COND_BOLD_20];
  lcd.fillScreen(BLACK);
  lcd.setTextColor(WHITE, BLACK);
  lcd.setTextFont(font);
  lcd.setTextDatum(TC_DATUM);
  lcd.drawString("Circles App", lcd.width()/2, (lcd.height()-font->height())/2);
}

CircleApp::~CircleApp() {
  log_d("destroy CircleApp");
}

appEventResult CircleApp::processEvent(EventType event) {
  log_d("processEvent CircleApp");
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  anyPressed = IS_KEYBOARD(event) ? true : false;
  return REDRAW_SCREEN;
}

void CircleApp::redrawScreen(bool redrawAll) {
  log_i("redraw CircleApp");

  // Draw random circles
  if (anyPressed) {
    anyPressed = false;

    uint32_t r = Random.random();     // global random number generator

    uint16_t x = r % lcd.width();
    uint16_t y = (r>>8) % lcd.height();
    uint16_t rad = 1 + ((r>>16) % 64);
    uint16_t col = r >> 3;

    // Make sure the colors are bright
    if (GETRED(col) < 4 && GETGREEN(col) < 4 && GETBLUE(col) < 4) {
      col = BLACK;
    } else if (GETRED(col) < GETGREEN(col)) {
      if (GETRED(col) < GETBLUE(col)) {
        col |= RED;
      } else {
        col |= BLUE;
      }
    } else {
      if (GETGREEN(col) < GETBLUE(col)) {
        col |= GREEN;
      } else {
        col |= BLUE;
      }
    }

    log_d("x = ", x);
    log_d("y = ", y);
    log_d("r = ", rad);
    log_d("color = ", col);

    lcd.fillCircle(x, y, rad, col);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Widgets Demo app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

WidgetDemoApp::WidgetDemoApp(LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state) {
  log_d("create WidgetDemoApp");

  widgets[0] = new RectWidget(0, 0, lcd.width(), THEME_HEADER_SIZE, GREEN);
  widgets[1] = new RectWidget(0, THEME_HEADER_SIZE, lcd.width(), lcd.height() - THEME_HEADER_SIZE - THEME_FOOTER_SIZE, BLUE);
  widgets[2] = new RectWidget(0, lcd.height() - THEME_FOOTER_SIZE, lcd.width(), THEME_FOOTER_SIZE, RED);

  label = new LabelWidget(0, (lcd.height()-fonts[AKROBAT_EXTRABOLD_22]->height())/2, lcd.width(), fonts[AKROBAT_EXTRABOLD_22]->height(),
                          "Demo", WHITE, BLACK, fonts[AKROBAT_EXTRABOLD_22], LabelWidget::CENTER);
}

WidgetDemoApp::~WidgetDemoApp() {
  log_d("destroy WidgetDemoApp");

  delete widgets[0];
  delete widgets[1];
  delete widgets[2];
  delete label;
}

appEventResult WidgetDemoApp::processEvent(EventType event) {
  log_d("processEvent WidgetDemoApp");
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  return REDRAW_SCREEN;
}

void WidgetDemoApp::redrawScreen(bool redrawAll) {
  log_i("redraw WidgetDemoApp");
  widgets[0]->redraw(lcd);
  widgets[1]->redraw(lcd);
  widgets[2]->redraw(lcd);

  ((GUIWidget *) label)->redraw(lcd);

  lcd.setTextDatum(TL_DATUM);

  lcd.setSmoothTransparency(true);
  lcd.setTextColor(WHITE, BLUE);
  lcd.setTextFont(fonts[AKROBAT_BOLD_16]);
  // WARNING: the following might produce deformed text due to hardware problems while drawing horizontal 1px lines
  // SOLUTIONS: 1) use only non-transparent font so that window is never set to 1px height (as in the following)
  //            2) use sprites
  //            3) use SPI_MODE3
  lcd.drawString("Quick lazy", 20, 43);
  lcd.setSmoothTransparency(false);
  lcd.drawString("Quick lazy", 20, 43 + lcd.fontHeight() + 5);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Pictures Demo app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PicturesDemoApp::PicturesDemoApp(LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state) {
  log_d("create PicturesDemoApp");
  pic = 1;
}

PicturesDemoApp::~PicturesDemoApp() {
  log_d("destroy PicturesDemoApp");
}

appEventResult PicturesDemoApp::processEvent(EventType event) {
  log_d("processEvent PicturesDemoApp");
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  pic++;
  if (pic>3) {
    pic = 1;
  }
  return REDRAW_SCREEN;
}

void PicturesDemoApp::redrawScreen(bool redrawAll) {
  log_i("redraw PicturesDemoApp");
  int t = micros();
  if (pic==1) {
    lcd.drawImage(rle3_image, sizeof(rle3_image), 0, 0);
  } else if (pic==2) {
    lcd.drawImage(image_i256, sizeof(image_i256), 0, 0);
  } else if (pic>2) {
    lcd.drawImage(image_jpg, sizeof(image_jpg));
  } else {
    lcd.fillScreen(BLACK);
    lcd.setTextColor(RED, BLACK);
    lcd.setTextSize(2);
    lcd.drawString("Error", lcd.width()/2, lcd.height()/2, 2);
  }
  log_d("time: %lu", micros()-t);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Font Demo app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

FontDemoApp::FontDemoApp(LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state) {
  log_d("create FontDemoApp");
  log_d("font index: ", curFontIndex);
  lcd.setTextFont(fonts[curFontIndex]);
}

FontDemoApp::~FontDemoApp() {
  log_d("destroy FontDemoApp");
}

appEventResult FontDemoApp::processEvent(EventType event) {
  log_d("processEvent FontDemoApp");
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  if (!smooth) {
    smooth = true;
  } else {
    curFontIndex = (curFontIndex + 1) % fonts.length();
    smooth = false;
  }
  log_d("font Index: %d", curFontIndex);
  return REDRAW_SCREEN;
}

void FontDemoApp::redrawScreen(bool redrawAll) {
  log_i("redraw FontDemoApp");
  lcd.fillScreen(BLACK);
  uint32_t t = micros();
  if (smooth) {
    lcd.setTextFont(fonts[curFontIndex]);
  } else {
    lcd.setTextFont(curFontIndex);
  }
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(0,40);

  char str[2] = "\0";
  for(uint8_t c=0x21; c<0x7E; c++) {
    str[0] = c;
    lcd.print(str);
  }
  t = micros()-t;
  lcd.setCursor(0,200);
  lcd.print("Time: ");
  lcd.println(t);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Clock app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ClockApp::ClockApp(LCD& lcd, TFT_eSprite& bgImg, ControlState& state) : WiPhoneApp(lcd, state), bgImg(bgImg) {
  log_d("create ClockApp");
}

ClockApp::~ClockApp() {
  log_d("destroy ClockApp");
}


appEventResult ClockApp::processEvent(EventType event) {
  //log_d("processEvent ClockApp");
  //LOG_MEM_STATUS;
  if (LOGIC_BUTTON_BACK(event) || LOGIC_BUTTON_OK(event) || event==WIPHONE_KEY_DOWN || event==WIPHONE_KEY_UP) {
    return EXIT_APP;
  }
  if (controlState.locked) {
    return DO_NOTHING;
  }
  if (NONKEY_EVENT_ONE_OF(event, TIME_UPDATE_EVENT | WIFI_ICON_UPDATE_EVENT | BATTERY_UPDATE_EVENT | REGISTRATION_UPDATE_EVENT | BATTERY_BLINK_EVENT | USB_UPDATE_EVENT)) {
    // TODO: optimize this app to only redraw small portion on events like WIFI_ICON_UPDATE_EVENT, BATTERY_UPDATE_EVENT, REGISTRATION_UPDATE_EVENT, BATTERY_BLINK_EVENT
    return REDRAW_SCREEN;
  } else if (event == NEW_MESSAGE_EVENT && !messageIconShown) {
    // Redraw screen only if message icon is not shown yet, but a new message was received
    return REDRAW_SCREEN;
  }
  if (event >= '0' && event <='9' || event == '*' || event == '#') {
    return EXIT_APP | ENTER_DIAL_APP;
  }
  return DO_NOTHING;

}

void ClockApp::redrawScreen(bool redrawAll) {
  //log_i("redraw ClockApp");

  // Paste background image
  if (bgImg.isCreated() && lcd.isSprite()) {
    bgImg.cloneDataInto((TFT_eSprite*) &lcd);
  } else {
    // Clear screen
    lcd.fillScreen(THEME_BG);
  }

  // Draw icons
  GUI::drawWifiIcon(lcd, controlState, 3, 5);
  auto w = GUI::drawSipIcon(lcd, controlState, 24, 5);
  messageIconShown = GUI::drawMessageIcon(lcd, controlState, 26 + w, 5) > 0;
  GUI::drawBatteryIcon(lcd, controlState, -1, lcd.width()-3, 7);

  // Print clock and date
  uint16_t yOff = 158;
  uint16_t cx = lcd.width()>>1;
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(WHITE, BLACK);
  lcd.setSmoothTransparency(true);

  if (ntpClock.isTimeKnown()) {
    // - clock
    lcd.setTextFont(fonts[AKROBAT_BOLD_90]);
    char buff[20];
    sprintf(buff, "%02d:%02d", ntpClock.getHour(), ntpClock.getMinute());
    lcd.drawString(buff, cx, yOff);
    // - date
    lcd.setTextFont(fonts[AKROBAT_BOLD_24]);
    sprintf(buff, "%d %s %d", ntpClock.getDay(), ntpClock.getMonth3(), ntpClock.getYear());
    lcd.drawString(buff, cx, yOff+lcd.fontHeight()+3);
  } else {
    // - time not set message
    lcd.setTextFont(fonts[AKROBAT_BOLD_90]);
    lcd.drawString("00:00", cx, yOff);
    lcd.setTextFont(fonts[AKROBAT_BOLD_24]);
    lcd.drawString("Network: waiting NTP", cx, yOff+lcd.fontHeight()+3);
  }
  yOff += lcd.fontHeight()+21;

  // Draw lock icon
  if (controlState.locked) {
    IconRle3 *iconObj = new IconRle3(icon_lock, sizeof(icon_lock));
    lcd.drawImage(*iconObj, cx-iconObj->width()/2, yOff);
    delete iconObj;
  }

  // Print center button text
  const char* msg = "Menu";
  if (controlState.locked) {
    // Locked text
    msg = (controlState.unlockButton1 == WIPHONE_KEY_OK) ? "Press * to unlock" : "Locked. Press OK";
  }
  lcd.drawString(msg, cx, lcd.height()-7);
  lcd.setSmoothTransparency(false);
  //log_v("- done");

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Splash screen app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SplashApp::SplashApp(LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state) {
  // Subscribe to app timer event
  controlState.msAppTimerEventLast = millis();
  controlState.msAppTimerEventPeriod = 300;
}

SplashApp::~SplashApp() {}

appEventResult SplashApp::processEvent(EventType event) {
  log_d("processEvent SplashApp");
  if (LOGIC_BUTTON_BACK(event) || LOGIC_BUTTON_OK(event)) {
    return EXIT_APP;
  }
  if (event==APP_TIMER_EVENT) {
    return ++screenNo>=8 ? EXIT_APP : REDRAW_SCREEN;
  }
  return DO_NOTHING;
}

void SplashApp::redrawScreen(bool redrawAll) {
  // TODO: blink the LED from here
  IconRle3 *icon;
  uint8_t screen = screenNo % 4;

  // Draw base icon
  if (screen == 0) {
    icon = new IconRle3(icon_splash_base, sizeof(icon_splash_base));
    lcd.drawImage(*icon, 0, 0);
    // Version
    lcd.setTextColor(TFT_BLUE, TFT_WHITE);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextSize(1);
    lcd.setTextFont(1);
    lcd.drawString("Ver. " FIRMWARE_VERSION, 5, 310);
    delete icon;
  }

  // Draw wifi icon
  if (screen == 0) {
    icon = new IconRle3(icon_splash_1, sizeof(icon_splash_1));
  } else if (screen == 1) {
    icon = new IconRle3(icon_splash_2, sizeof(icon_splash_2));
  } else if (screen == 2) {
    icon = new IconRle3(icon_splash_3, sizeof(icon_splash_3));
  } else {
    icon = new IconRle3(icon_splash_4, sizeof(icon_splash_4));
  }
  lcd.drawImage(*icon, (lcd.width()-icon->width())>>1, 98);
  delete icon;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Messages app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MessagesApp::MessagesApp(LCD& lcd, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), flash(flash) {
  log_d("create MessagesApp");

  // Load messages database
  if (!flash.messages.isLoaded()) {
    flash.messages.load(ntpClock.isTimeKnown() ? ntpClock.getExactUnixTime() : 0);
  }

  // Generate main menu
  this->createMainMenu();

  inboxMenu = NULL;
  sentMenu = NULL;

  subApp = NULL;

  enterState(MAIN);
}

MessagesApp::~MessagesApp() {
  log_d("destroy MessagesApp");

  if(mainMenu) {
    delete mainMenu;
  }
  if(inboxMenu) {
    delete inboxMenu;
  }
  if(sentMenu) {
    delete sentMenu;
  }

  if(subApp) {
    delete subApp;
  }
}

void MessagesApp::enterState(MessagesState_t state) {
  log_i("enterState %d", (int)state);
  if (state == MAIN) {
    header->setTitle("Messages");
  } else if (state == INBOX) {
    header->setTitle("Inbox");
  } else if (state == OUTBOX) {
    header->setTitle("Outbox");
  }
  footer->setButtons("Select", "Back");
  appState = state;
}

appEventResult MessagesApp::processEvent(EventType event) {
  log_i("processEvent MessagesApp %d", event);

  appEventResult res = REDRAW_SCREEN;

  if (event == NEW_MESSAGE_EVENT) {

    // Update messages count in the main menu (we destroy and create entire menu for that)
    this->createMainMenu();
    if (appState == MAIN) {
      res |= REDRAW_SCREEN;
    }

    // Update INBOX screen if it is the current screen
    if (appState == INBOX) {
      // New message arrived: redraw inbox
      this->createLoadMessageMenu(INCOMING, inboxOffset, 0);
      res |= REDRAW_SCREEN;
    }

    // We assume that header will be redrawn by GUI class for this event, so we don't do res |= REDRAW_HEADER here

  } else if (subApp) {
    if ((res = subApp->processEvent(event)) & EXIT_APP) {
      if (appState == COMPOSING) {
        this->createMainMenu();       // update main menu to show new sent messages count
        enterState(MAIN);
      } else {
        // If message was deleted, reload menus
        if ((res & REDRAW_ALL) == REDRAW_ALL) {       // special signal to reload the messages
          // Clear message cache
          flash.messages.clearPreloaded();
          // Create menus
          int32_t offset = ((ViewMessageApp*)subApp)->messageOffset;        // TODO: use it for preserving the visible offset
          this->createLoadMessageMenu(appState==INBOX ? INCOMING : SENT, -1, 0);
          this->createMainMenu();
        }
        // After viewing message `appState` stays the same, just need to change Title and Header widgets
        enterState(appState);
      }

      delete subApp;
      subApp = NULL;

      res = REDRAW_ALL;
    }

  } else if (appState == MAIN) {

    if (LOGIC_BUTTON_BACK(event)) {
      return EXIT_APP;
    }

    mainMenu->processEvent(event);

    if (LOGIC_BUTTON_OK(event)) {
      MenuOption::keyType sel = mainMenu->readChosen();
      switch (sel) {
      case 1:
        enterState(INBOX);
        break;
      case 2:
        enterState(OUTBOX);
        break;
      case 3:
        enterState(COMPOSING);
        break;
      default:
        log_e("unknown key");
        break;
      }
      res |= REDRAW_ALL;
      if (appState == INBOX) {
        // Initialize
        this->createLoadMessageMenu(INCOMING, inboxOffset, 0);
      } else if (appState == OUTBOX) {
        this->createLoadMessageMenu(SENT, sentOffset, 0);
      } else if (appState == COMPOSING) {
        subApp = new CreateMessageApp(lcd, controlState, flash, header, footer);
      }
    }

  } else if ((appState == INBOX || appState == OUTBOX) && (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN)) {

    // This is a bit hackish way to allow displaying potentially unlimited number of messages without lags. The idea is simple:
    //   More messages get preloaded from the files when user attempts to go past currently displayed N_MENU_ITEMS (5) messages.

    MenuWidget* box = (appState == INBOX) ? inboxMenu : sentMenu;

    if (event == WIPHONE_KEY_DOWN && box->isSelectedLast()) {

      // Load next messages

      if (box->size() == N_MENU_ITEMS) {
        MenuOption::keyType selectedKey = box->currentKey();
        int32_t messageOffset = this->decodeMessageOffset(selectedKey);
        if (-messageOffset >= N_MENU_ITEMS) {      // negative messageOffset expected here
          int32_t newMessageOffset = messageOffset + N_MENU_ITEMS - 2;
          this->createLoadMessageMenu(appState == INBOX, newMessageOffset, selectedKey);
          box = (appState == INBOX) ? inboxMenu : sentMenu;
          if (!box->isSelectedLast()) {
            box->processEvent(event);
          }
          res |= REDRAW_ALL;
        }
      }

    } else if (event == WIPHONE_KEY_UP && box->isSelectedFirst()) {

      // Load previous messages

      MenuOption::keyType selectedKey = box->currentKey();
      int32_t messageOffset = this->decodeMessageOffset(selectedKey);
      if (messageOffset < -1) {      // negative messageOffset expected here
        int32_t newMessageOffset = messageOffset + 1;
        this->createLoadMessageMenu(appState == INBOX, newMessageOffset, selectedKey);
        box = (appState == INBOX) ? inboxMenu : sentMenu;
        box->processEvent(event);
        res |= REDRAW_ALL;
      }

    } else {

      box->processEvent(event);
      res |= REDRAW_ALL;

    }

  } else if (appState == INBOX) {

    if (LOGIC_BUTTON_BACK(event)) {
      enterState(MAIN);
      res |= REDRAW_ALL;
    } else if (LOGIC_BUTTON_OK(event)) {
      // View message
      inboxMenu->processEvent(event);
      MenuOption::keyType selectedKey = inboxMenu->readChosen();
      int32_t messageOffset = this->decodeMessageOffset(selectedKey);
      subApp = new ViewMessageApp(messageOffset, lcd, controlState, flash, header, footer);
      // Reload this menu   TODO: fix offset, selected message will be on top of now, that's not intuitive
      this->createLoadMessageMenu(appState == INBOX, messageOffset, selectedKey);
      res |= REDRAW_ALL;
    }

  } else if (appState == OUTBOX) {

    if (LOGIC_BUTTON_BACK(event)) {
      enterState(MAIN);
      res |= REDRAW_ALL;
    } else if (LOGIC_BUTTON_OK(event)) {
      // View message
      sentMenu->processEvent(event);
      MenuOption::keyType selectedKey = sentMenu->readChosen();
      int32_t messageOffset = this->decodeMessageOffset(selectedKey);
      subApp = new ViewMessageApp(messageOffset, lcd, controlState, flash, header, footer);
      // Reload this menu   TODO: fix offset, selected message will be on top of now, that's not intuitive
      this->createLoadMessageMenu(appState == INBOX, messageOffset, selectedKey);
      res |= REDRAW_ALL;
    }

  }

  return res;
}

MenuOption::keyType MessagesApp::encodeMessageOffset(int32_t offset) {
  MenuOption::keyType key = abs(offset);    // clear normal minus bit
  if (key >= 0x40000000) {
    log_e("message offset too big");
  }
  if (offset < 0) {
    key |= 0x40000000;  // set unsigned minus bit
  } else {
    offset += 1;  // offset 0 -> key 1 (for non-negative offsets)
  }
  return key;
}

/* Description:
 *     this is a mess.
 */
int32_t MessagesApp::decodeMessageOffset(MenuOption::keyType key) {
  int32_t offset = key & 0xBFFFFFFF;        // clear "unsigned minus" bit (bit 30)
  if (key & 0x40000000) {
    offset = -offset;  // set normal minus bit (bit 31) if "unsigned minus" (bit 30) was set
  } else {
    offset -= 1;  // key 1 -> offset 0 (for non-negative offsets)
  }
  return offset;
}

void MessagesApp::createMainMenu() {
  MenuOption::keyType selectedKey = 3;
  if (mainMenu) {
    MenuOption::keyType curKey = mainMenu->currentKey();
    if (curKey) {
      selectedKey = curKey;
    }
    delete mainMenu;
  }

  mainMenu = new MenuWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), NULL, fonts[AKROBAT_EXTRABOLD_22], N_MENU_ITEMS, 8);
  mainMenu->setStyle(MenuWidget::DEFAULT_STYLE, BLACK, WHITE, WHITE, WP_ACCENT_1);

  mainMenu->addOption("New Message", NULL, 3, 1, icon_Write_b, sizeof(icon_Write_b), icon_Write_w, sizeof(icon_Write_w));
  char str[25];
  str[0] = '\0';
  if (flash.messages.isLoaded()) {
    int32_t n = flash.messages.inboxTotalSize();
    if (n>0) {
      snprintf(str, sizeof(str), "%d Messages", n);
    } else {
      snprintf(str, sizeof(str), "No messages");
    }
  }
  mainMenu->addOption("Inbox", str, 1, 1, icon_Inbox_b, sizeof(icon_Inbox_b), icon_Inbox_w, sizeof(icon_Inbox_w));
  if (flash.messages.isLoaded()) {
    int32_t n = flash.messages.sentTotalSize();
    if (n>0) {
      snprintf(str, sizeof(str), "%d Messages", n);
    } else {
      snprintf(str, sizeof(str), "No messages");
    }
  }
  mainMenu->addOption("Sent", str, 2, 1, icon_Outbox_b, sizeof(icon_Outbox_b), icon_Outbox_w, sizeof(icon_Outbox_w));

  if (selectedKey) {
    mainMenu->select(selectedKey);
  }
}

void MessagesApp::createLoadMessageMenu(bool incoming, int32_t offset, MenuOption::keyType selectKey) {
  log_i("createLoadMessageMenu: %d %d", offset, N_MENU_ITEMS);

  // Create new menu widget
  MenuWidget* menu = new MenuWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(),
                                    incoming ? "Inbox is empty" : "No sent messages",
                                    fonts[AKROBAT_EXTRABOLD_22], N_MENU_ITEMS, 8);
  menu->setStyle(MenuWidget::DEFAULT_STYLE,   BLACK, GRAY_85, GRAY_95, WP_ACCENT_1);    // Read messages & sent
  menu->setStyle(MenuWidget::ALTERNATE_STYLE, BLACK, WHITE, WHITE, WP_ACCENT_S);        // Unread messages

  if (incoming) {
    if (inboxMenu) {
      delete inboxMenu;
    }
    inboxMenu = menu;
  } else {
    if (sentMenu) {
      delete sentMenu;
    }
    sentMenu = menu;
  }

  // Pre-load messages in the database
  flash.messages.preload(incoming, offset, N_MENU_ITEMS);

  // Display the messages
  MenuOptionIconnedTimed* option;
  for (auto it = flash.messages.iteratorCount(offset, N_MENU_ITEMS); it.valid(); ++it) {
    log_i("Looping over messages");
    MenuOption::keyType key = this->encodeMessageOffset((int32_t)it);
    option = new MenuOptionIconnedTimed(key, it->isRead() ? MenuWidget::DEFAULT_STYLE : MenuWidget::ALTERNATE_STYLE, it->getOtherUri(), it->getMessageText(), it->getTime());
    if (option) {
      menu->addOption(option);
    }
  }
  if (selectKey) {
    menu->select(selectKey);
  }
}

void MessagesApp::redrawScreen(bool redrawAll) {
  log_i("redraw MessagesApp");

  if (subApp) {
    subApp->redrawScreen(redrawAll);
    return;
  }

  if (appState == MAIN) {
    ((GUIWidget*)mainMenu)->redraw(lcd);
  } else if (appState == INBOX) {
    ((GUIWidget*)inboxMenu)->redraw(lcd);
  } else if (appState == OUTBOX) {
    ((GUIWidget*)sentMenu)->redraw(lcd);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Create Message app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

CreateMessageApp::CreateMessageApp(LCD& lcd, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer, const char* sipUri)
  : WindowedApp(lcd, state, header, footer), FocusableApp(2), flash(flash) {
  log_d("create CreateMessageApp");

  label1 = NULL;
  addr = NULL;
  sendMessageAs = NULL;
  label2 = NULL;
  text = NULL;

  this->setupUI(sipUri, hasSipAndLora(sipUri));
  this->setHeaderFooter();

  subApp = NULL;
}

void CreateMessageApp::setupUI(const char* sipUri, bool showMessageType) {
  log_d("CreateMessageApp::createUI");

  this->deleteUI();
  focusableWidgets.clear();

  uint16_t yOff = header->height();

  label1 = new LabelWidget(0, yOff, lcd.width(), 25, "To:", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += label1->height();

  addr = new TextInputWidget(0, yOff, lcd.width(), 35, controlState, 100, fonts[AKROBAT_BOLD_20], InputType::AlphaNum, 8);
  addr->setText(sipUri ? sipUri : "");
  yOff += addr->height();

  if (showMessageType) {
    sendMessageAs = new ChoiceWidget(0, yOff, lcd.width(), 35);
    sendMessageAs->addChoice("sip");
    sendMessageAs->addChoice("LoRa");
    yOff += sendMessageAs->height();
  } else {
    sendMessageAs = NULL;
  }

  label2 = new LabelWidget(0, yOff, lcd.width(), 25, "Message:", WP_ACCENT_1, WP_COLOR_1, fonts[AKROBAT_BOLD_18], LabelWidget::LEFT_TO_RIGHT, 8);
  yOff += label1->height();

  text = new MultilineTextWidget(0, yOff, lcd.width(), lcd.height()-yOff-footer->height(),
                                 "type your message", controlState, 1000, fonts[AKROBAT_BOLD_20], InputType::AlphaNum, 8, 5);
  text->setText("");    // TODO: allow a template like "Hello!\nHow are you doing?"


  addFocusableWidget(addr);
  if (showMessageType) {
    addFocusableWidget(sendMessageAs);
  }
  addFocusableWidget(text);

  setFocus(addr);
}

void CreateMessageApp::deleteUI() {
  if(label1) {
    delete label1;
  }
  if(label2) {
    delete label2;
  }
  if(addr) {
    delete addr;
  }
  if(text) {
    delete text;
  }
  if(sendMessageAs) {
    delete sendMessageAs;
  }
}

CreateMessageApp::~CreateMessageApp() {
  log_d("destroy CreateMessageApp");

  this->deleteUI();

  if(subApp) {
    delete subApp;
  }
}

void CreateMessageApp::setHeaderFooter() {
  header->setTitle("New Message");
  footer->setButtons(this->getFocused()==addr ? "Choose" : "Send", "Clear");
}

bool CreateMessageApp::isSipAddress(const char* address) {
  log_d("#### checking address type: %s", address);
  if (strncmp(address, "LORA:", 5) == 0) {
    return false;
  }

  if (strlen(address) == 6) {
    bool valid = true;
    for (int i = 0; i < strlen(address); ++i) {
      char c = address[i];

      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        valid = false;
        break;
      }
    }

    if (valid) {
      return false;
    }
  }

  return true;
}

bool CreateMessageApp::hasSipAndLora(const char* address) {
  if (address == NULL) {
    return false;
  }

  if (strncmp(address, "LORA:", 5) != 0) {
    return false;
  }

  for (int i = 0; i < strlen(address); ++i) {
    if (address[i] == '!') {
      return true;
    }
  }

  return false;
}

const char* CreateMessageApp::extractAddress(const char* address, MessageType_t type) {
  static char ra[400] = {0};
  memset(ra, 0x00, sizeof(ra));
  switch (type) {
  case SIP:
    if (strncmp(address, "LORA:", 5) == 0) {
      int start = 0;
      for (start = 0; start < strlen(address); ++start) {
        if (address[start] == '!') {
          break;
        }
      }
      memcpy(ra, address+start+1, strlen(address)-start-1);
    } else {
      strlcpy(ra, address, sizeof(ra));
    }
    break;
  case LORA: {
    if (strncmp(address, "LORA:", 5) == 0) {
      int end = 0;
      for (end = 0; end < strlen(address); ++end) {
        if (address[end] == '!') {
          break;
        }
      }

      if (end > 0 && sizeof(ra) > end-5) {
        memcpy(ra, address+5, end-5);
      }
    }
  }
  break;
  }

  return ra;
}

extern uint32_t chipId;

appEventResult CreateMessageApp::processEvent(EventType event) {
  log_i("processEvent CreateMessageApp");

  appEventResult res = DO_NOTHING;
  if (subApp) {
    if ((res |= subApp->processEvent(event)) & EXIT_APP) {
      const char* sipUri = ((PhonebookApp*)subApp)->getSelectedSipUri();
      const char* loraAddress = ((PhonebookApp*)subApp)->getSelectedLoraAddress();
      delete subApp;
      subApp = NULL;

      if (sipUri && *sipUri && loraAddress && *loraAddress) {
        char tmp[800] = {0};
        snprintf(tmp, sizeof(tmp), "LORA:%s!%s", loraAddress, sipUri);
        setupUI(tmp, true);
      } else if (sipUri && *sipUri) {
        setupUI(sipUri, false);
      } else if (loraAddress && *loraAddress) {
        char tmp[300] = {0};
        snprintf(tmp, sizeof(tmp), "LORA:%s", loraAddress);
        setupUI(tmp, false);
      } else {
        log_e("empty SIP URI");
      }

      // Restore Title and Header widgets
      this->setHeaderFooter();

      res = REDRAW_ALL;
    }
  } else if (event == WIPHONE_KEY_END) {     // the button below BACK, which, it turn, is used as backspace
    return EXIT_APP;
  } else if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN) {

    bool cursorMoved = false;
    if (this->getFocused() == text) {
      int32_t cursRow = text->getCursorRow();
      text->processEvent(event);
      if (cursRow != text->getCursorRow()) {
        cursorMoved = true;
        res |= REDRAW_SCREEN;
      }
    }
    if (!cursorMoved) {
      nextFocus(event == WIPHONE_KEY_DOWN);
      this->setHeaderFooter();
      res |= REDRAW_ALL;
    }

  } else if (LOGIC_BUTTON_OK(event)) {

    if (this->getFocused() == addr) {       // TextInputWidget has focus

      // Select the receiver address

      subApp = new PhonebookApp(NULL, lcd, lcd, controlState, flash, header, footer, true);
      res |= REDRAW_ALL;

    } else {

      // Send message

      // Collect message data
      const char* fromUri  = controlState.fromUriDyn;
      const char* toUri    = addr->getText();
      const char* message  = text->getText();
      bool        incoming = false;
      char loraAddress[100] = {0};
      bool sipMessage = isSipAddress(toUri);

      if (sendMessageAs != NULL) {
        switch (sendMessageAs->getValue()) {
        case 0: // Sip
          sipMessage = true;
          toUri = extractAddress(toUri, MessageType_t::SIP);
          break;
        case 1: // LoRa
          sipMessage = false;
          toUri = extractAddress(toUri, MessageType_t::LORA);
          break;
        default:
          log_e("Unknown message type: %d", sendMessageAs->getValue());
        }
      }

      if (!sipMessage) {
        // LoRa message
        snprintf(loraAddress, sizeof(loraAddress), "%X", chipId);
        fromUri = loraAddress;

        if (strncmp(toUri, "LORA:", 5) == 0) {
          toUri = extractAddress(toUri, MessageType_t::LORA);
        }
      }

      log_d("To address: %s", toUri);

      uint32_t time        = 0;    // 0 - indicates unknown time
      if (ntpClock.isTimeKnown()) {
        time = ntpClock.getExactUtcTime();
        if (!time) {
          time++;  // avoid 0 if time is known (will almost NEVER hapen)
        }
      }

      // Save to message database
      flash.messages.saveMessage(message, fromUri, toUri, incoming, time);

      // Queue for sending
      log_d("adding message to send queue: %d %s %s", sipMessage, fromUri, toUri);
      MessageData* msg = new MessageData(fromUri, toUri, message, time, incoming);

      if (sipMessage) {
        controlState.outgoingMessages.add(msg);
      } else {
        controlState.outgoingLoraMessages.add(msg);
      }

      return EXIT_APP | REDRAW_ALL;       // special signal for the parent that a message was sent
    }

  } else {
    FocusableWidget *widg = this->getFocused();
    if (widg!=NULL) {
      widg->processEvent(event);
    }
    res |= REDRAW_SCREEN;
  }

  return res;
}

void CreateMessageApp::redrawScreen(bool redrawAll) {
  log_i("redraw CreateMessageApp");

  if (subApp) {
    subApp->redrawScreen(redrawAll);
    return;
  }

  ((GUIWidget*)addr)->redraw(lcd);
  ((GUIWidget*)text)->redraw(lcd);

  ((GUIWidget*)label1)->redraw(lcd);
  ((GUIWidget*)label2)->redraw(lcd);

  if (sendMessageAs != NULL) {
    ((GUIWidget*)sendMessageAs)->redraw(lcd);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  View Message app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ViewMessageApp::ViewMessageApp(int32_t messageOffset, LCD& lcd, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), flash(flash), messageOffset(messageOffset) {
  log_i("create ViewMessageApp: messageOffset = %d", messageOffset);

  const int16_t padding =  4;
  textArea = new MultilineTextWidget(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(),
                                     "Empty message", state, 10000, fonts[OPENSANS_COND_BOLD_20], InputType::AlphaNum, padding, padding);
  textArea->setColors(WP_COLOR_0, WP_COLOR_1);

  // Loop over a single element in preloaded messages array: a little trick to use MessageData interface to access the message
  for (auto it = flash.messages.iteratorCount(messageOffset, 1); it.valid(); ++it) {
    textArea->setText(it->getMessageText());
    const char* format = "%s\n\n--\nFrom:\n%s\nTo:\n%s\nTime:\n%s\n";
    char msgTime[30];
    Clock::unixToHuman(it->getTime(), msgTime);
    char msg[strlen(it->getMessageText()) + strlen(it->getOtherUri()) + strlen(it->getOwnUri()) + strlen(format) + strlen(msgTime)];
    sprintf(msg, format, it->getMessageText(), it->getOwnUri(), it->getOtherUri(), msgTime);
    textArea->setText(msg);
    textArea->cursorToStart();

    IF_LOG(VERBOSE)
    it->show();

    // Remove "unread" status
    if (!it->isRead()) {
      flash.messages.setRead(*it);
      state.unreadMessages = flash.messages.hasUnread();
    }
  }
  textArea->setFocus(true);     // to reveal the cursor
  this->changeState(MAIN);
}

ViewMessageApp::~ViewMessageApp() {
  if (textArea) {
    delete textArea;
  }
  if (options) {
    delete options;
  }
  if (subApp) {
    delete subApp;
  }
}

appEventResult ViewMessageApp::processEvent(EventType event) {
  log_i("processEvent ViewMessageApp: %d", (int)event);

  appEventResult res = DO_NOTHING;
  if (subApp) {

    if ((res = subApp->processEvent(event)) & EXIT_APP) {
      this->messageSent = ((res & REDRAW_ALL) == REDRAW_ALL);

      // After creating a message `appState` stays the same, just need to change Title and Header widgets
      changeState(appState);

      delete subApp;
      subApp = NULL;

      res = REDRAW_ALL;
    }

  } else if (appState == MAIN) {
    if (LOGIC_BUTTON_BACK(event)) {
      return EXIT_APP | (this->messageSent ? REDRAW_ALL : 0);  // special signal for the MessagesApp to reload the menus if a message was sent (or deleted)
    }

    if (event == WIPHONE_KEY_SELECT) {
      res |= this->changeState(OPTIONS);
    } else if (IS_KEYBOARD(event)) {
      textArea->processEvent(event);
      res |= REDRAW_SCREEN;
    }
  } else if (appState == OPTIONS) {

    res |= REDRAW_SCREEN;
    if (LOGIC_BUTTON_BACK(event)) {
      res |= changeState(MAIN);
    } else {
      options->processEvent(event);
      MenuOption::keyType sel = options->readChosen();
      if (sel>0) {
        if (sel==111) {
          // "Reply" option selected

          // Loop over a single element in preloaded messages array: a little trick to use MessageData interface to access the message's otherUri
          const char* otherUri = "";
          for (auto it = flash.messages.iteratorCount(messageOffset, 1); it.valid(); ++it) {
            otherUri = it->getOtherUri();
          }

          // Create message app
          subApp = new CreateMessageApp(lcd, controlState, flash, header, footer, otherUri);
          res |= REDRAW_ALL;

        } else if (sel==222) {
          // "Delete" option selected
          flash.messages.deleteMessage(this->messageOffset);
          res = EXIT_APP | REDRAW_ALL;       // special signal for the MessagesApp to reload the menus if a message was deleted (or sent)
        }
      }
    }

  }

  return res;
}

appEventResult ViewMessageApp::changeState(ViewMessageState_t newState) {
  if (newState == OPTIONS) {
    header->setTitle("Options");
    footer->setButtons("Select", "Back");
    if (options == NULL) {
      options = new OptionsMenuWidget(0, header->height(), lcd.width(), lcd.height()-header->height()-footer->height());
      if (options) {
        options->addOption("Reply",  111);
        options->addOption("Delete", 222);
      }
    }
  } else if (newState == MAIN) {
    header->setTitle("Message");
    footer->setButtons("Options", "Back");
  }
  appState = newState;
  return REDRAW_ALL;
}

void ViewMessageApp::redrawScreen(bool redrawAll) {
  log_i("redraw ViewMessageApp");
  if (subApp) {
    subApp->redrawScreen(redrawAll);
    return;
  } else if (appState == MAIN) {
    ((GUIWidget*) textArea)->redraw(lcd);
  } else if (appState == OPTIONS) {
    ((GUIWidget*) options)->redraw(lcd);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Design Demo app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

DesignDemoApp::DesignDemoApp(LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state) {
  log_d("create DesignDemoApp");
}

DesignDemoApp::~DesignDemoApp() {
  log_d("destroy DesignDemoApp");
}

appEventResult DesignDemoApp::processEvent(EventType event) {
  log_d("processEvent DesignDemoApp");
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  screenNo = (screenNo + 1) % 2;
  return REDRAW_SCREEN;
}

void DesignDemoApp::redrawScreen(bool redrawAll) {
  log_i("redraw DesignDemoApp");

  uint32_t t = micros();

  // Screen sprite
  TFT_eSprite screen = TFT_eSprite(&lcd);
  screen.setColorDepth(16);
  screen.createSprite(lcd.width(), lcd.height());
  if (!screen.isCreated()) {
    log_d("screen sprite not created");
  }

  // Test 256-color image
  if (!screenNo) {

    screen.drawImage(image_i256, sizeof(image_i256));
    //screen.drawImage(icon_skull_5, sizeof(icon_skull_5), 10, 10);
    //screen.drawImage(icon_skull_10, sizeof(icon_skull_10), 10, 60);
    //screen.drawImage(icon_skull_15, sizeof(icon_skull_15), 10, 150);
    screen.drawImage(icon_batt_w_5, sizeof(icon_batt_w_5), 120, 18);
    screen.drawImage(icon_wifi_w_3, sizeof(icon_wifi_w_3), 100, 18);
    IconRle3* icon = new IconRle3(icon_Games_w, sizeof(icon_Games_w));
    screen.drawImage(*icon, 160, 10);
    delete icon;
    icon = new IconRle3(icon_Games_b, sizeof(icon_Games_b));
    screen.drawImage(*icon, 160, 60);
    delete icon;
    screen.pushSprite(0, 0);

  } else {

    screen.fillSprite(TFT_BLACK);

    // Header sprite
    TFT_eSprite sprite = TFT_eSprite(&screen);

    sprite.setColorDepth(16);
    sprite.createSprite(lcd.width(), 30);
    sprite.fillSprite(TFT_BLACK);

    // - Title
    sprite.setTextColor(sprite.color565(0xFF, 0x6F, 0x00), TFT_BLACK);
    sprite.setTextFont(fonts[AKROBAT_BOLD_18]);
    sprite.setCursor(8, 3);
    sprite.printToSprite("Messages", 8);

    // - Icons
    sprite.drawImage(icon_batt_w_5, sizeof(icon_batt_w_5), 213, 7);
    sprite.drawImage(icon_wifi_w_3, sizeof(icon_wifi_w_3), 189, 5);

    // - Time
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    sprite.setTextFont(fonts[AKROBAT_BOLD_16]);
    sprite.setTextDatum(TR_DATUM);
    //sprite.setCursor(179, 5);
    sprite.drawString("12:55", 180, 6);

    sprite.pushSprite(0, 0);
    sprite.deleteSprite();

    // Footer sprite
    sprite.createSprite(lcd.width(), 40);
    sprite.fillSprite(TFT_BLACK);

    sprite.setCursor(92, 4+7);
    sprite.setTextFont(fonts[AKROBAT_BOLD_24]);
    sprite.printToSprite("Select", 6);

    sprite.setCursor(192, 7+7);
    sprite.setTextFont(fonts[AKROBAT_SEMIBOLD_22]);
    sprite.printToSprite("Back", 4);

    sprite.setCursor(8, 7+7);
    sprite.setTextFont(fonts[AKROBAT_SEMIBOLD_22]);
    sprite.printToSprite("Options", 7);

    sprite.pushSprite(0, lcd.height()-40);
    sprite.deleteSprite();

    // Direct to screen
    screen.fillRect(0, 30, 240, 8, TFT_WHITE);
    screen.fillRect(0, 38, 240, 35, sprite.color565(0x63, 0xD9, 0x67));
    screen.fillRect(0, 73, 240, 8, TFT_WHITE);
    screen.setTextFont(fonts[AKROBAT_BOLD_20]);
    screen.setTextColor(TFT_WHITE, sprite.color565(0x63, 0xD9, 0x67));
    //screen.setCursor(8, 42+4);
    //screen.printToSprite("+ Create New Message", 20);
    screen.drawString("+ Create New Message", 8, 42+4);
    screen.drawImage(icon_batt_w_4, sizeof(icon_batt_w_4), 200, 48);

    // Menu sprite
    sprite.createSprite(lcd.width(), 199);    // menu sprite
    sprite.fillSprite(TFT_WHITE);

    // Menu item sprites
    uint16_t yOff = 0;
    const char* names[4] = { "Yemi Ajibade", "John Doe", "Skynet Logistics", "Zuck-dawg" };
    const char* dates[4] = { "05-09-2020", "27-08-2018", "01-07-2029", "04-08-2018" };
    const char* texts[4] = { "Temporarily out of funds...", "Lorem Ipsum", "Did you get the package? ", "Need to poo, can't reach your timeline!" };
    TFT_eSprite spr = TFT_eSprite(&sprite);

    for (uint8_t x=0; x<4; x++) {
      spr.createSprite(lcd.width(), 51);
      spr.drawFastHLine(0, 0, 240, TFT_BLACK);
      spr.fillRect(0, 1, 240, 50, TFT_WHITE);

      spr.setTextFont(fonts[AKROBAT_EXTRABOLD_22]);
      spr.setCursor(8, 5);
      spr.setTextColor(TFT_BLACK, TFT_WHITE);
      spr.printToSprite(names[x], strlen(names[x]));

      spr.setTextFont(fonts[AKROBAT_BOLD_18]);
      //spr.setCursor(156, 6);
      //spr.printToSprite(dates[x], strlen(dates[x]));
      spr.setTextDatum(TR_DATUM);
      spr.drawString(dates[x], 232, 7);

      spr.setTextFont(fonts[AKROBAT_BOLD_16]);
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(spr.color565(0x64, 0x64, 0x64), TFT_WHITE);
      spr.setCursor(8, 32);
      spr.printToSprite(texts[x], strlen(texts[x]));

      spr.pushSprite(0, yOff);
      spr.deleteSprite();

      yOff += 51;
    }

    sprite.pushSprite(0, 81);
    sprite.deleteSprite();

    // Overlay image
    screen.drawImage(rle3_image, sizeof(rle3_image));

    // Draw entire screen
    screen.pushSprite(0, 0);
    screen.deleteSprite();
  }
  t = micros()-t;

  log_d("Drawing & pushing sprite: %d us\r\n", t);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Mic Test app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//MicTestApp::MicTestApp(Audio* audio, LCD& lcd, ControlState& state, FooterWidget* footer) : WiPhoneApp(lcd, state), audio(audio) {
MicTestApp::MicTestApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer) : WindowedApp(lcd, state, header, footer), audio(audio) {
  state.msAppTimerEventLast = millis();
  state.msAppTimerEventPeriod = 33;    // 30 fps

  audio->start();
  audio->turnMicOn();

  redrawScreen(true);
}

MicTestApp::~MicTestApp() {
  audio->shutdown();
}

appEventResult MicTestApp::processEvent(EventType event) {
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  if (event == APP_TIMER_EVENT) {
    return REDRAW_SCREEN;
  }
  return DO_NOTHING;
}

void MicTestApp::redrawScreen(bool redrawAll) {
  uint32_t val = audio->getMicAvg();

  if ( redrawAll ) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setTextFont(2);
    lcd.drawString("Front Mic", 10, 40);
    lcd.drawString("Rear Mic", 10, 110);
    lcd.setTextSize(1);

    header->setTitle("Mic Test");
    footer->setButtons("", "Back");

  }

  uint8_t stp = 0;
  uint16_t xOff = 0;
  uint16_t colorStep = 17;
  uint16_t xStep = 16;

  // draw the front mic line
  do {
    uint8_t R = colorStep*stp;
    lcd.drawFastHLine(xOff, 80, xStep, lcd.color565(R, 255-R, 0));
    xOff += xStep;
    val >>= 1;
    stp++;
  } while (val && xOff < lcd.width()-xStep);
  lcd.drawFastHLine(xOff, 80, lcd.width()-xOff, TFT_BLACK);

  // draw the rear mic line
  xOff = 0;
  stp = 0;
  //val = audio->getRearMicAvg();
  val = audio->getMicAvg();
  do {
    uint8_t R = colorStep*stp;
    lcd.drawFastHLine(xOff, 100, xStep, lcd.color565(R, 255-R, 0));
    xOff += xStep;
    val >>= 1;
    stp++;
  } while (val && xOff < lcd.width()-xStep);
  lcd.drawFastHLine(xOff, 100, lcd.width()-xOff, TFT_BLACK);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Audio recorder app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RecorderApp::RecorderApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), audio(audio), sprite(&lcd) {
  state.msAppTimerEventLast = millis();
  state.msAppTimerEventPeriod = 0;    // 30 fps

  //sprite.setColorDepth(3);        // it's possible to use 3-bit depth here, but it is too slow
  sprite.setColorDepth(16);
  sprite.createSprite(160, 100);
  if (sprite.isCreated()) {
    sprite.fillSprite(TFT_RED);
  } else {
    log_d("error: screen sprite not created");
  }

  header->setTitle("Recoder");
  footer->setButtons("Record", "Back");

  label = new LabelWidget(0, 195, lcd.width(), 35, "Not recording", WP_COLOR_1, WP_COLOR_0, fonts[AKROBAT_BOLD_22], LabelWidget::CENTER, 8);

  audio->start();
  audio->turnMicOn();
}

RecorderApp::~RecorderApp() {
  audio->shutdown();
}

appEventResult RecorderApp::processEvent(EventType event) {
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }

  appEventResult res = DO_NOTHING;

  // Update sprite
  if (event == WIPHONE_KEY_SELECT && this->recorded && !this->recording) {

    audio->playRecord();

  } else if (event == WIPHONE_KEY_SELECT || event == WIPHONE_KEY_OK || recording && event == APP_TIMER_EVENT && audio->isRecordingFinished()) {

    recording = !recording;
    if (recording) {
      sprintf(this->filename, "/audio_%02d%02d%02d_%02d%02d%02d.pcm", ntpClock.getYear()-2000, ntpClock.getMonth(), ntpClock.getDay(), ntpClock.getHour(), ntpClock.getMinute(), ntpClock.getSecond());
      audio->setBitsPerSample(16);
      audio->setSampleRate(16000);
      audio->setMonoOutput(true);
      if (audio->recordFromMic()) {
        for (int i = 0; i < sizeof(microphoneValues)/sizeof(microphoneValues[0]); i++) {
          microphoneValues[i] = 1;
        }
        label->setText("Recording...");
        label->setColors(TFT_RED, WP_COLOR_0);
        controlState.msAppTimerEventPeriod = 33;    // 30 fps
        footer->setButtons("Stop", "Back");
        res |= REDRAW_FOOTER;
      } else {
        label->setColors(WP_COLOR_1, WP_COLOR_0);
        label->setText("ERROR: not enough RAM?");
        recording = !recording;
      }
    } else {
      footer->setButtons(NULL, "Wait");
      label->setText("Writing file...");
      label->setColors(WP_COLOR_1, WP_COLOR_0);
      ((GUIWidget*) label)->redraw(lcd);
      ((GUIWidget*) footer)->redraw(lcd);

      if (audio->saveWavRecord(&SD, filename)) {
        label->setText(filename+1);
      } else {
        label->setText("Couldn't save the file");
      }
      this->recorded = true;
      footer->setButtons("Play", "Back");
      res |= REDRAW_FOOTER;
      controlState.msAppTimerEventPeriod = 0;
    }
    spriteUpdated = true;
    res |= REDRAW_SCREEN;

  } else if (event == APP_TIMER_EVENT) {

    // Get microphone (averaged) value
    int32_t val = audio->getMicAvg();

    // Scale
    const float scaleDown = 80;
    val = log(val/scaleDown)/log(1.04);
    if (val > 100) {
      val = 100;
    }
    if (val <= 0) {
      val = 1;
    }

    // Remember
    int prevVal = curVal == 0 ? 159 : curVal - 1;
    microphoneValues[prevVal] = val;

    // Draw to sprite
    int x = curVal;
    for (int i=0; i<160; i++) {
      sprite.drawFastVLine(i, 0, 100-microphoneValues[x], TFT_BLACK);
      sprite.drawFastVLine(i, 100-microphoneValues[x], 100, TFT_RED);
      x = (x + 1) % 160;
    };
    spriteUpdated = true;
    curVal = (curVal + 1) % 160;
    res |= REDRAW_SCREEN;
  }

  return res;
}

void RecorderApp::redrawScreen(bool redrawAll) {
  uint32_t val = audio->getMicAvg();

  if (!screenInited || redrawAll) {
    lcd.fillRect(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), TFT_BLACK);
  }

  if (spriteUpdated || !screenInited || redrawAll) {
    if (recording) {
      sprite.pushSprite(40, 50);
      spriteUpdated = false;
    } else {
      lcd.fillRect(40, 50, 160, 100, TFT_BLACK);
      lcd.fillCircle(120, 114, 35, TFT_RED);
    }
  }

  if (label->isUpdated() || !screenInited || redrawAll) {
    ((GUIWidget*) label)->redraw(lcd);
  }

  screenInited = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Diagnostics app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// The diagnostcs app draws directly to the screen to avoid issues if the external RAM is not available.
// If you want to avoid flickering you'll need to only redraw if the element changed.

//DiagnosticsApp::DiagnosticsApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
//  : WindowedApp(lcd, state, header, footer), FocusableApp(8), audio(audio), lastVoltages(3), lastSocs(3) {
DiagnosticsApp::DiagnosticsApp(Audio* audio, LCD& lcd, ControlState& state)
  : WiPhoneApp(lcd, state), audio(audio), lastVoltages(3), lastSocs(3) {
  lastVoltages.zero();
  lastSocs.zero();

  //header->setTitle("Diagnostics");
  //footer->setButtons(NULL, "Back");

  // Create all the widgets
  const uint16_t spacing = 1;
  uint16_t xOff = spacing;
  uint16_t yOff = 15; //header->height();

  // Row
  bVersion = new ButtonWidget(xOff, yOff, "ver. " FIRMWARE_VERSION, 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bVersion);
  xOff += bVersion->width() + spacing;
  //printf("Firmware Version: %s\r\n", FIRMWARE_VERSION);

  bUptime = new ButtonWidget(xOff, yOff, "Up:00d00:00", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bUptime);
  xOff = spacing;
  yOff += bUptime->height() + spacing;

  // Row
  bVoltage = new ButtonWidget(xOff, yOff, "0.00V", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bVoltage);
  xOff += bVoltage->width() + spacing;

  bStateOfCharge = new ButtonWidget(xOff, yOff, "000%", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bStateOfCharge);
  xOff += bStateOfCharge->width() + spacing;

  bCardPresence = new ButtonWidget(xOff, yOff, "SD", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bCardPresence);
  xOff += bCardPresence->width() + spacing;

  bUsbPresence = new ButtonWidget(xOff, yOff, "USB", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bUsbPresence);
  xOff = spacing;
  yOff += bUsbPresence->height() + spacing;

  // Row
  bCharging = new ButtonWidget(xOff, yOff, "Discharging?", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bCharging);
  xOff += bCharging->width() + spacing;

  lastAutonomous = !controlState.usbConnected;
  bAutonomous = new ButtonWidget(xOff, yOff, "Autonomous", 0, 30, TFT_BLACK, lastAutonomous ? greenBg : greyBg, lastAutonomous ? greenBorder : greyBorder);
  this->registerWidget(bAutonomous);
  xOff = spacing;
  yOff += bAutonomous->height() + spacing;
  //printf("USB State: %s\r\n", lastAutonomous ? "Unplugged" : "Connected");

  // Row
  bool inited = controlState.gaugeInited;
  //printf("Battery Gauge Inited: %s\r\n", inited? "yes" : "no");
  bBatteryGauge = new ButtonWidget(xOff, yOff, "Gauge", 0, 30, TFT_BLACK, inited ? greenBg : redBg, inited ? greenBorder : redBorder);
  this->registerWidget(bBatteryGauge);
  xOff += bBatteryGauge->width() + spacing;

  inited = controlState.extenderInited;
  //printf("GPIO Extender Inited: %s\r\n", inited? "yes" : "no");
  bGpioExtender = new ButtonWidget(xOff, yOff, "Extender", 0, 30, TFT_BLACK, inited ? greenBg : redBg, inited ? greenBorder : redBorder);
  this->registerWidget(bGpioExtender);
  xOff = spacing;
  yOff += bGpioExtender->height() + spacing;

  // Row
  inited = controlState.scannerInited;
  //printf("Key Scanner Inited: %s\r\n", inited? "yes" : "no");
  bKeyScanner = new ButtonWidget(xOff, yOff, "Scanner", 0, 30, TFT_BLACK, inited ? greenBg : redBg, inited ? greenBorder : redBorder);
  this->registerWidget(bKeyScanner);
  xOff += bKeyScanner->width() + spacing;

  inited = controlState.codecInited;
  //printf("Audio Codec Inited: %s\r\n", inited? "yes" : "no");
  bCodec = new ButtonWidget(xOff, yOff, "Codec", 0, 30, TFT_BLACK, inited ? greenBg : redBg, inited ? greenBorder : redBorder);
  this->registerWidget(bCodec);
  xOff += bCodec->width() + spacing;

  inited = controlState.psramInited;
  //printf("PSRAM Inited: %s\r\n", inited? "yes" : "no");
  bSpiRam = new ButtonWidget(xOff, yOff, "PSRAM", 0, 30, TFT_BLACK, inited? greenBg : redBg, inited ? greenBorder : redBorder);
  this->registerWidget(bSpiRam);
  xOff = spacing;
  yOff += bSpiRam->height() + spacing;

  // Row
  char buff[25];
  uint8_t mac[6];
  wifiState.getMac(mac);
  //printf("Chip ID: %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  snprintf(buff, sizeof(buff), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  bMacAddress = new ButtonWidget(xOff, yOff, buff, 0, 30, TFT_BLACK, greenBg, greenBorder);
  this->registerWidget(bMacAddress);
  xOff = spacing;
  yOff += bMacAddress->height() + spacing;

  // Row
  IPAddress ipAddr = WiFi.localIP();
  lastIpAddr = WiFi.localIP();
  //printf("IP Address: %d.%d.%d.%d\r\n", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  snprintf(buff, sizeof(buff), "%d.%d.%d.%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  bIpAddress = new ButtonWidget(xOff, yOff, "000.000.000.000", 0, 30, TFT_BLACK, (uint32_t) ipAddr ? greenBg : greyBg, (uint32_t) ipAddr ? greenBorder : greyBorder);
  bIpAddress->setText(buff);
  this->registerWidget(bIpAddress);
  xOff = spacing;
  yOff += bIpAddress->height() + spacing;

  // Row
  bRSSI = new ButtonWidget(xOff, yOff, "RSSI: -100", 0, 30, TFT_BLACK, greyBg, greyBorder);
  this->registerWidget(bRSSI);
  xOff = spacing;
  yOff += bUptime->height() + spacing;

  // NETWORKS
  xOff = spacing;
  yOff = 15; //header->height();
  for (int i=0; i<sizeof(bbPings)/sizeof(bbPings[0]); i++) {
    bbPings[i] = new ButtonWidget(xOff, yOff, "Pinging...", lcd.width()-spacing, 30, TFT_BLACK, greyBg, greyBorder);
    this->registerWidget(bbPings[i]);
    yOff += bbPings[i]->height() + spacing;
  }

  // FILESYSTEMS
  // currently the SD card is tested in main

  // AUDIO
  /*
  yOff = header->height()+75;
  testEarSpeaker = new ButtonWidget(1, yOff, "Front", 115, 30, TFT_BLACK, greyBg, greyBorder);
  testLoudSpeaker = new ButtonWidget(81, yOff, "Back", 115, 30, TFT_BLACK, greyBg, greyBorder);
  testHeadphone = new ButtonWidget(161, yOff, "Jack", 115, 30, TFT_BLACK, greyBg, greyBorder);

  this->registerWidget(testEarSpeaker);
  this->registerWidget(testLoudSpeaker);
  this->registerWidget(testHeadphone);
  */

  // SCREEN

  // KEYPAD
  // - row 1
  yOff = 75; //header->height()+15;
  bbKeys[0] = new ButtonWidget(100, yOff, "U", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[0]->height() + spacing;

  // - row 2
  bbKeys[1] = new ButtonWidget(1, yOff, "S", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[2] = new ButtonWidget(59, yOff, "L", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[3] = new ButtonWidget(100, yOff, "K", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[4] = new ButtonWidget(141, yOff, "R", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[5] = new ButtonWidget(199, yOff, "B", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[1]->height() + spacing;

  // - row 3
  bbKeys[6] = new ButtonWidget(1, yOff, "C", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[7] = new ButtonWidget(100, yOff, "D", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[8] = new ButtonWidget(199, yOff, "E", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[1]->height() + spacing;

  // - row 4
  bbKeys[9] = new ButtonWidget(1, yOff, "1", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[10] = new ButtonWidget(67, yOff, "2", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[11] = new ButtonWidget(133, yOff, "3", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[12] = new ButtonWidget(199, yOff, "F1", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[1]->height() + spacing;

  // - row 5
  bbKeys[13] = new ButtonWidget(1, yOff, "4", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[14] = new ButtonWidget(67, yOff, "5", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[15] = new ButtonWidget(133, yOff, "6", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[16] = new ButtonWidget(199, yOff, "F2", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[1]->height() + spacing;

  // - row 6
  bbKeys[17] = new ButtonWidget(1, yOff, "7", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[18] = new ButtonWidget(67, yOff, "8", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[19] = new ButtonWidget(133, yOff, "9", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[20] = new ButtonWidget(199, yOff, "F3", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[1]->height() + spacing;

  // - row 7
  bbKeys[21] = new ButtonWidget(1, yOff, "*", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[22] = new ButtonWidget(67, yOff, "0", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[23] = new ButtonWidget(133, yOff, "#", 40, 30, TFT_BLACK, greyBg, greyBorder);
  bbKeys[24] = new ButtonWidget(199, yOff, "F4", 40, 30, TFT_BLACK, greyBg, greyBorder);
  yOff += bbKeys[1]->height() + spacing;

  for (int i=0; i<sizeof(bbKeys)/sizeof(bbKeys[0]); i++) {
    this->registerWidget(bbKeys[i]);
  }

  // Initialize widget text
  this->updateVoltage();
  this->updateUsb();
  this->updateIP();
  this->updateRSSI();
  this->updateScannerAndCodec();
  this->changeState(MAIN);
}

DiagnosticsApp::~DiagnosticsApp() {
  // all widgets must be registered and are deleted by WiPhoneApp destructor
  audio->shutdown();
  allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
}

void DiagnosticsApp::updateVoltage() {
  char buff[10];

  // Update battery gauge info
  auto& volt = controlState.battVoltage;
  auto& soc = controlState.battSoc;
  snprintf(buff, sizeof(buff), "%.2fV", controlState.battVoltage);
  bVoltage->setText(buff);
  snprintf(buff, sizeof(buff), "%.0f%%", controlState.battSoc);
  bStateOfCharge->setText(buff);
  if (controlState.battVoltage >= 3.7 || controlState.battSoc >= 80) {
    bVoltage->setColors(TFT_BLACK, greenBg, greenBorder);
    bStateOfCharge->setColors(TFT_BLACK, greenBg, greenBorder);
  } else if (controlState.battVoltage <= 3.3 || controlState.battSoc < 20) {
    bVoltage->setColors(TFT_BLACK, greenBg, greenBorder);
    bStateOfCharge->setColors(TFT_BLACK, greenBg, greenBorder);
  } else {
    bVoltage->setColors(TFT_BLACK, yellowBg, yellowBorder);
    bStateOfCharge->setColors(TFT_BLACK, yellowBg, yellowBorder);
  }

  //log_d("Voltages: %.3f -> %.3f -> %.3f (%.3f)", lastVoltages[-3], lastVoltages[-2], lastVoltages[-1], volt);
  //log_d("SOCs: %.2f -> %.2f -> %.2f (%.2f)", lastSocs[-3], lastSocs[-2], lastSocs[-1], soc);
  if (abs(volt-lastVoltages[-1])>=0.005) {    // voltage needs to change by at least 5 mv at a time
    lastVoltages.forcePut(volt);
    log_d("Voltage: %.2fV", volt);
  }
  if (abs(soc-lastSocs[-1])>=0.05) {          // SOC needs to change by at least 0.05% at a time (this is especially sensitive towards the end)
    lastSocs.forcePut(soc);
    log_d("State of Charge: %s", buff);
  }
  //log_d("Voltages: %.3f -> %.3f -> %.3f (%.3f)", lastVoltages[-3], lastVoltages[-2], lastVoltages[-1], volt);
  //log_d("SOCs: %.2f -> %.2f -> %.2f (%.2f)", lastSocs[-3], lastSocs[-2], lastSocs[-1], soc);

  bool vRising  = lastVoltages[-3] != 0.0 && lastVoltages[-1] > lastVoltages[-2] && lastVoltages[-2] > lastVoltages[-3];
  bool vFalling = lastVoltages[-1] < lastVoltages[-2] && lastVoltages[-2] < lastVoltages[-3];
  bool sRising  = lastSocs[-3] != 0.0 && lastSocs[-1] > lastSocs[-2] && lastSocs[-2] > lastSocs[-3];
  bool sFalling = lastSocs[-1] < lastSocs[-2] && lastSocs[-2] < lastSocs[-3];
  char usbState[16] = "Discharging";
  if ((vRising || sRising) && !(vFalling || sFalling)) {
    if (lastCharging != 1) {
      strcpy(usbState, "Charging");
      log_d("USB State: %s", usbState);
      bCharging->setText("Charging");
      bCharging->setColors(TFT_BLACK, greenBg, greenBorder);
      lastCharging = 1;
    }
  } else if (!(vRising || sRising) && (vFalling || sFalling)) {
    if (lastCharging != -1) {
      strcpy(usbState, "Discharging");
      log_d("USB State: %s", usbState);
      bCharging->setText("Discharging");
      bCharging->setColors(TFT_BLACK, redBg, redBorder);
      lastCharging = -1;
    }
  } else {
    if (lastCharging != 0) {
      strcpy(usbState, (lastSocs[-1] > lastSocs[-2] && lastSocs[-2] != 0) ? "Charging?" : "Discharging?");
      log_d("USB State: %s", usbState);
      bCharging->setText(usbState);
      bCharging->setColors(TFT_BLACK, yellowBg, yellowBorder);
      lastCharging = 0;
    }
  }

  // note: the SD card holder in the first batches of boards has a permanantly grounded card detect pin
  //       those boards will always report a card present
  //       additionally, this test only checks the detect pin state.
  //       better to test by using an actual write and read (see test_sd_card() below)
  //if (lastSd != (int8_t) controlState.cardPresent) {
  //  bool &sd = controlState.cardPresent;
  //  log_d("SD card: %s", sd? "present" : "none");
  //  bCardPresence->setColors(TFT_BLACK, sd ? greenBg : greyBg, sd ? greenBorder : greyBorder);
  //  lastSd = (int8_t) sd;
  //}

  if (lastSd != true) {
    bool sd = test_sd_card();
    bCardPresence->setColors(TFT_BLACK, sd ? greenBg : greyBg, sd ? greenBorder : greyBorder);
    lastSd = (int8_t) sd;
  }
}

void DiagnosticsApp::updateUsb() {
  if (lastUsb != (int8_t) controlState.usbConnected) {
    bool &usb = controlState.usbConnected;
    log_d("USB State: %s", usb ? "Unplugged" : "Connected");
    bUsbPresence->setColors(TFT_BLACK, usb ? greenBg : greyBg, usb ? greenBorder : greyBorder);
    lastUsb = (int8_t) usb;
  }
  if (lastAutonomous != !controlState.usbConnected) {
    bAutonomous->setColors(TFT_BLACK, greenBg, greenBorder);
    lastAutonomous = true;
  }
}

void DiagnosticsApp::updateIP() {
  IPAddress ipAddr = WiFi.localIP();
  if (lastIpAddr != ipAddr) {
    log_d("IP Address: %d.%d.%d.%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
    char buff[16];
    snprintf(buff, sizeof(buff), "%d.%d.%d.%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
    bIpAddress->setColors(TFT_BLACK, (uint32_t) ipAddr ? greenBg : greyBg, (uint32_t) ipAddr ? greenBorder : greyBorder);
    bIpAddress->setText(buff);
    lastIpAddr = ipAddr;
  }
}

void DiagnosticsApp::updateRSSI() {
  if (controlState.wifiRssi != lastRssi) {
    log_d("RSSI: %d", controlState.wifiRssi);
    char buff[10];
    snprintf(buff, sizeof(buff), "RSSI: %d", controlState.wifiRssi);
    bRSSI->setText(buff);
    if (controlState.wifiRssi <= -70) {
      bRSSI->setColors(TFT_BLACK, yellowBg, yellowBorder);
    } else if (controlState.wifiRssi == 0) {
      bRSSI->setColors(TFT_BLACK, greyBg, greyBorder);
    } else {
      bRSSI->setColors(TFT_BLACK, greenBg, greenBorder);
    }
    lastRssi = controlState.wifiRssi;
  }
}

void DiagnosticsApp::updateScannerAndCodec() {
  if (controlState.scannerInited != lastScannerInited) {
    log_d("Key Scanner Inited: yes");
    bKeyScanner->setColors(TFT_BLACK, controlState.scannerInited ? greenBg : redBg, controlState.scannerInited ? greenBorder : redBorder);
    lastScannerInited = controlState.scannerInited;
  }
  if (controlState.codecInited != lastCodecInited) {
    log_d("Audio Codec Inited: yes");
    bCodec->setColors(TFT_BLACK, controlState.codecInited ? greenBg : redBg, controlState.scannerInited ? greenBorder : redBorder);
    lastCodecInited = controlState.codecInited;
  }
}

void DiagnosticsApp::updateUptime() {
  uint32_t uptime = millis()/1000, closing;
  bool change = false;
  char buff[30];
  if (uptime < 3600) {
    closing = uptime % 60;    // seconds
    if (closing != lastUptimeClosing) {
      snprintf(buff, sizeof(buff), "Up: %02d:%02d", (uptime%3600)/60, closing);
      lastUptimeClosing = closing;
      change = true;
    }
  } else if (uptime < 3600*24) {
    closing = uptime % 60;    // seconds
    if (closing != lastUptimeClosing) {
      snprintf(buff, sizeof(buff), "Up:%2d:%02d:%02d", uptime / 3600, (uptime%3600)/60, closing);
      lastUptimeClosing = closing;
      change = true;
    }
  } else {
    closing = (uptime%3600)/60;   // minutes
    if (closing != lastUptimeClosing) {
      snprintf(buff, sizeof(buff), "Up:%2dd%02d:%02d", uptime/(24*3600), (uptime%(24*3600) / 3600), closing);
      lastUptimeClosing = closing;
      change = true;
    }
  }
  if (change) {
    bUptime->setText(buff);
  }
}

void DiagnosticsApp::updateDB() {
  // we should probably do something reasonable about making sure we aren't going to conflict
  // with something else already using these differently, but for now ignore that possibility
  allPinMode(38, INPUT);

  allPinMode(EXTENDER_PIN(10), OUTPUT);
  allPinMode(EXTENDER_PIN(11), OUTPUT);
  allPinMode(EXTENDER_PIN(12), OUTPUT);
  allPinMode(EXTENDER_PIN(13), OUTPUT);
  allPinMode(EXTENDER_PIN(14), OUTPUT);
  allPinMode(EXTENDER_PIN(15), OUTPUT);

  //allPinMode(25, OUTPUT);
  //allPinMode(15, OUTPUT);
  allPinMode(12, OUTPUT);
  allPinMode(27, OUTPUT);
  allPinMode(32, OUTPUT);
  allPinMode(13, OUTPUT);
  allPinMode(14, OUTPUT);

  if (dbCounter < 1 || allDigitalRead(38)) {
    allDigitalWrite(EXTENDER_PIN(10), HIGH);
    allDigitalWrite(EXTENDER_PIN(11), HIGH);
    allDigitalWrite(EXTENDER_PIN(12), HIGH);
    allDigitalWrite(EXTENDER_PIN(13), HIGH);
    allDigitalWrite(EXTENDER_PIN(14), HIGH);
    allDigitalWrite(EXTENDER_PIN(15), HIGH);
    //allDigitalWrite(25, HIGH); used by I2C, can't easily override
    //allDigitalWrite(15, HIGH);
    allDigitalWrite(12, HIGH);
    allDigitalWrite(27, HIGH);
    allDigitalWrite(32, HIGH);
    allDigitalWrite(13, HIGH);
    allDigitalWrite(14, HIGH);
    dbCounter++;
  } else {
    uint32_t val = audio->getMicAvg();
    val = val>>7;
    printf("mic: %u\r\n", val);

    allDigitalWrite(EXTENDER_PIN(10), LOW);
    allDigitalWrite(EXTENDER_PIN(11), LOW);
    allDigitalWrite(EXTENDER_PIN(12), LOW);
    allDigitalWrite(EXTENDER_PIN(13), LOW);
    allDigitalWrite(EXTENDER_PIN(14), LOW);
    allDigitalWrite(EXTENDER_PIN(15), LOW);


    //allDigitalWrite(25, LOW);
    //allDigitalWrite(15, LOW);

    allDigitalWrite(12, LOW);
    allDigitalWrite(27, LOW);
    allDigitalWrite(32, LOW);
    allDigitalWrite(13, LOW);
    allDigitalWrite(14, LOW);
    if (dbCounter > 0) {
      dbCounter = 0;
    }
  }
}

void DiagnosticsApp::updatePing() {
  if (this->pingedAll) {
    log_d("pinged all");
    return; // false;
  }
  const char* host = NULL;
  IPAddress addr((uint32_t)0);
  int i = this->nextToPing++;
  if (controlState.wifiRssi != 0) {
    switch(i) {
    case 0:
      addr = WiFi.localIP();
      if ((uint32_t)addr != 0) {
        addr[3] = 1;
      }
      break;

    case 1:
      host = "bing.com";

    //  break;
    //case 2:
    //  host = "sanjose2.voip.ms";
    //  break;
    //case 3:
    //  host = "amsterdam1.voip.ms";
    //  break;
    //case 4:
    //  host = "sip.antisip.com";
    //  break;
    //case 5:
    //  host = "sip.lax.didlogic.net";
    //  break;
    //case 6:
    //  host = "sip.nyc.didlogic.net";
    //  break;
    //case 7:
    //  host = "voip.ms";


    // fallthrough for last
    default:
      this->nextToPing = 0;
      this->pingedAll = true;
      break;
    }
  }

  log_d("PINGING...");
  char buff[100];
  bool res = false;
  int received = 0;
  if (addr || host && addr.fromString(host)) {
    snprintf(buff, sizeof(buff), "Pinging %d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
    log_d("%s", buff);
    ping_start(addr, 3, 1, 332, 1);
    res = (received = ping_get_received()) == 3;
  } else if (host) {
    snprintf(buff, sizeof(buff), "Pinging %s", host);
    log_d("%s", buff);
    IPAddress addr = resolveDomain(host);
    ping_start(addr, 3, 1, 332, 1);
    res = (received = ping_get_received()) == 3;
  }
  log_d(" - DONE");

  if (addr) {
    snprintf(buff, sizeof(buff), "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
  } else if (host) {
    snprintf(buff, sizeof(buff), "%s", host);
  }
  if (received) {
    if (!res) {
      snprintf(buff+strlen(buff), sizeof(buff), " %d/3", received);
    }
    int avgTime = ping_get_mean();
    snprintf(buff+strlen(buff), sizeof(buff), " %dms", avgTime);
  }
  bbPings[i]->setText(buff);
  if (received & res) {
    bbPings[i]->setColors(TFT_BLACK, greenBg, greenBorder);
  } else {
    bbPings[i]->setColors(TFT_BLACK, redBg, redBorder);
  }

  // Mark as having no address
  if (nextToPing == 0 && i!=sizeof(bbPings)/sizeof(bbPings[0])-1) {
    for (int j=i+1; j<sizeof(bbPings)/sizeof(bbPings[0]); j++) {
      if (bbPings[j]) {
        bbPings[j]->setText("No address to ping");
      }
    }
    if (this->pingedAll) {
      log_d("pinged all (2)");
      return; // true;
    }
  }

  return; // true;
}

void DiagnosticsApp::updateMic(void) {
  // flash the keypad LEDs based on the mic level
  // this is intended to let us test if the mic is soldered
  // correctly even before the screen or speakers are installed
  uint32_t val = audio->getMicAvg();
  val = val>>7;
  //allAnalogWrite(KEYBOARD_LED, val);// Why doesn't this work? Supposedly each pin on the GPIO extender supports PWM.
  allDigitalWrite(KEYBOARD_LED, val > 40 ? LOW : HIGH);
}

void DiagnosticsApp::toggleSpeaker(void) {
  static bool toggle = true;
  audio->chooseSpeaker(toggle);
  //allDigitalWrite(KEYBOARD_LED, LOW);

  //allDigitalWrite(VIBRO_MOTOR_CONTROL, toggle);

  //allDigitalWrite(KEYBOARD_LED, HIGH);
  //allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
  toggle = !toggle;
  printf("toggled speaker\r\n");
}

bool DiagnosticsApp::selfTest(void) {
  bool ip_ok = false;
  bool gauge_ok = false;
  bool extender_ok = false;
  bool scanner_ok = false;
  bool codec_ok = false;
  bool psram_ok = false;
  bool sd_ok = false;
  bool memtest_ok = false;
  bool self_check_passed = false;

  printf("\r\n\r\n SELF TEST BEGIN\r\n\r\n");

  char buff[25];
  uint8_t mac[6];
  wifiState.getMac(mac);
  printf("Chip ID: %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  IPAddress ipAddr = WiFi.localIP();
  ip_ok = (ipAddr[0] != 0)? true : false;
  printf("IP Address: %d.%d.%d.%d\r\n", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  if (ip_ok) {
    printf("Has IP: ok\r\n");
  } else {
    printf("No IP: failed\r\n");
  }
  printf("USB State: %s\r\n", controlState.usbConnected ? "Connected" : "Unplugged");
  gauge_ok = controlState.gaugeInited;
  printf("Battery Gauge Inited: %s\r\n", gauge_ok? "yes" : "no");
  extender_ok = controlState.extenderInited;
  printf("GPIO Extender Inited: %s\r\n", extender_ok? "yes" : "no");
  scanner_ok = controlState.scannerInited;
  printf("Key Scanner Inited: %s\r\n", scanner_ok? "yes" : "no");
  codec_ok = controlState.codecInited;
  printf("Audio Codec Inited: %s\r\n", codec_ok? "yes" : "no");
  psram_ok = controlState.psramInited;
  printf("PSRAM Inited: %s\r\n", psram_ok? "yes" : "no");

  sd_ok = test_sd_card();
  printf("SD OK: %s\r\n", sd_ok? "yes" : "no");

  memtest_ok = test_memory();

  if(ip_ok && gauge_ok && extender_ok && scanner_ok && codec_ok && psram_ok && sd_ok && memtest_ok) {
    self_check_passed = true;
    printf("\r\n SELF TEST PASSED\r\n\r\n");
    return true;
  }
  printf("\r\n SELF TEST FAILED\r\n\r\n");
  return false;
}

void DiagnosticsApp::changeState(DiagnosticsView_t newState) {
  log_d("DiagnosticsApp::changeState %d", (int) newState);
  if (newState == MAIN) {

    controlState.msAppTimerEventLast = millis();
    controlState.msAppTimerEventPeriod = 100;    // 10 updates per second, but this will be cut down to a display update rate of 1 fps elsewhere

    this->updateVoltage();
    this->updateIP();
    this->updateRSSI();
    this->updateUsb();
    this->updateScannerAndCodec();
    this->updateDB();
    this->selfTest();

  } else if (newState == NETWORKS) {
    audio->ceasePlayback();

    controlState.msAppTimerEventLast = millis();
    controlState.msAppTimerEventPeriod = 1000;    // 1 second delay before doing next ping

    this->nextToPing = 0;
    this->pingedAll = false;

    for (int i=0; i<sizeof(bbPings)/sizeof(bbPings[0]); i++) {
      if (bbPings[i]) {
        bbPings[i]->setText("Pinging...");
        bbPings[i]->setColors(TFT_BLACK, greyBg, greyBorder);
      }
    }

  } else if (newState == KEYPAD) {

    controlState.msAppTimerEventPeriod = 0;
    for (int i=0; i<sizeof(bbKeys)/sizeof(bbKeys[0]); i++) {
      bbKeys[i]->setColors(TFT_BLACK, greyBg, greyBorder);
    }
    memset(keyPressed, 0, sizeof(keyPressed));
    anyKeyPressed = false;

  } else if (newState == AUDIO) {
    controlState.msAppTimerEventLast = millis();
    controlState.msAppTimerEventPeriod = 33;    // 30 fps

  } else if (newState == SCREEN) {
    audio->ceasePlayback();
    printf("testing screen\r\n");
    controlState.msAppTimerEventLast = millis();
    controlState.msAppTimerEventPeriod = 500;    // 2 fps
  }
  appState = newState;
  screenInited = false;
}

appEventResult DiagnosticsApp::processEvent(EventType event) {

  if (!controlState.booted) {
    return DO_NOTHING;
  }

  static bool testPassed = false;
  static bool audioOn = false;
  static int splitter = 0;
  if (splitter > 29) {
    splitter = 0;
  }

  if (audioOn == true) {
    audio->loop();
    this->updateMic();
  }

#ifndef DIAGNOSTICS_ONLY
  if (LOGIC_BUTTON_BACK(event) && !(appState == KEYPAD && anyKeyPressed && keyPressed[5]<EXIT_CNT && keyPressed[8]<EXIT_CNT)) {
    return EXIT_APP;
  }
#endif
  appEventResult res = DO_NOTHING;
  if (event == WIPHONE_KEY_DOWN && !(appState == KEYPAD && anyKeyPressed && keyPressed[7]<EXIT_CNT)) {
    // Change current state
    DiagnosticsView_t newState = MAIN;
    allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
    switch(appState) {
    case MAIN:
      newState = NETWORKS;
      break;
    case NETWORKS:
      newState = AUDIO;
      break;
    case AUDIO:
      newState = SCREEN;
      screenStep = 0;
      break;
    case SCREEN:
      newState = KEYPAD;
      break;
    default:
      break;  // just loop back to MAIN
    }
    this->changeState(newState);
    res |= REDRAW_SCREEN;
  } else if (appState == MAIN) {
    if (event == BATTERY_UPDATE_EVENT) {
      this->updateVoltage();
      res |= REDRAW_SCREEN;
    } else if (event == APP_TIMER_EVENT || event == WIFI_ICON_UPDATE_EVENT) {      // NOTE: rssi is update once every two seconds, app timer event is once every second
      //if (controlState.codecInited && splitter > 28 ) {
      if (controlState.booted && controlState.codecInited) {
        //if (!audio->isOn()) {
        if ( !audioOn ) {
          // if we are in DIAGNOSTICS_ONLY mode, the audio object doesn't get set up/get passed in/function correctly and we need to create it again. it's unclear why
          static Audio audio_local(true, I2S_BCK_PIN, I2S_WS_PIN, I2S_MOSI_PIN, I2S_MISO_PIN);
          audio = &audio_local;

          //audio->setVolumes(speakerVol, headphonesVol, loudspeakerVol);
          //audio->setVolumes(0, 0, 0);
          //audio->chooseSpeaker(true);
          //gui.setAudio(audio);

          audio->shutdown();

          if (audio->start()) {
            audio->turnMicOn();
            audio->playRingtone(&SPIFFS);
            audioOn = true;
          } else {
            printf("audio: failed\r\n");
          }
        }

        if (splitter == 0) {
          this->updateIP();
          this->updateRSSI();
          this->updateScannerAndCodec();
          this->updateUptime();
          testPassed = this->selfTest();
        }
        if (splitter % 5 == 0) {
          this->updateDB();
        }
        if (splitter == 15 && audioOn) {
          this->toggleSpeaker();
        }

        // turn the motor on once every second if we fail
        // or once each 3 seconds if we pass
        int count = splitter;
        if ( !testPassed ) {
          count = splitter%10;
        }
        if ( count == 3 ) {
          printf("motor on\r\n");
          allDigitalWrite(VIBRO_MOTOR_CONTROL, HIGH);
        }
        if ( count == 7 ) {
          printf("motor off\r\n");
          allDigitalWrite(VIBRO_MOTOR_CONTROL, LOW);
        }

        if (splitter%10 == 0) { // redraw the screen only once each 10 updates (once per second)
          res |= REDRAW_SCREEN;
        }
      }
    } else if (event == USB_UPDATE_EVENT) {
      this->updateUsb();
      res |= REDRAW_SCREEN;
    }
  } else if (appState == NETWORKS) {
    if (event == APP_TIMER_EVENT) {
      this->updatePing();
      res |= REDRAW_SCREEN;
    }
  } else if (appState == AUDIO) {
    if (event == APP_TIMER_EVENT) {
      res |= REDRAW_SCREEN;
    }
    if (event == '1') {
      if (!audio->getHeadphones()) {
        audio->chooseSpeaker(false);
        audio->playRingtone(&SPIFFS);
        printf("playing from ear speaker\r\n");
      } else {
        printf("headphones connected, ignoring\r\n");
      }
    }
    if (event == '2') {
      if (!audio->getHeadphones()) {
        audio->chooseSpeaker(true);
        audio->playRingtone(&SPIFFS);
        printf("playing from loudspeaker\r\n");
      } else {
        printf("headphones connected, ignoring\r\n");
      }
    }
    if (event == '3') {
      if (audio->getHeadphones()) {
        audio->chooseSpeaker(false);
        audio->playRingtone(&SPIFFS);
        printf("playing from headphone jack\r\n");
      } else {
        printf("no headphones connected, ignoring\r\n");
      }
    }
    if (event == '4') {
      audio->ceasePlayback();
    }
  } else if (appState == SCREEN) {
    if (event == APP_TIMER_EVENT) {
      res |= REDRAW_SCREEN;
    }
  } else if (appState == KEYPAD) {
    if (IS_KEYBOARD(event)) {
      anyKeyPressed = true;
      unsigned int i = 0xeeee;
      log_d("Keypad Test, Pressed: %d", event);
      switch(event) {
      case WIPHONE_KEY_UP:
        i=0;
        break;
      case WIPHONE_KEY_SELECT:
        i=1;
        break;
      case WIPHONE_KEY_LEFT:
        i=2;
        break;
      case WIPHONE_KEY_OK:
        i=3;
        break;
      case WIPHONE_KEY_RIGHT:
        i=4;
        break;
      case WIPHONE_KEY_BACK:
        i=5;
        break;
      case WIPHONE_KEY_CALL:
        i=6;
        break;
      case WIPHONE_KEY_DOWN:
        i=7;
        break;
      case WIPHONE_KEY_END:
        i=8;
        break;
      case '1':
        i=9;
        break;
      case '2':
        i=10;
        break;
      case '3':
        i=11;
        break;
      case WIPHONE_KEY_F1:
        i=12;
        break;
      case '4':
        i=13;
        break;
      case '5':
        i=14;
        break;
      case '6':
        i=15;
        break;
      case WIPHONE_KEY_F2:
        i=16;
        break;
      case '7':
        i=17;
        break;
      case '8':
        i=18;
        break;
      case '9':
        i=19;
        break;
      case WIPHONE_KEY_F3:
        i=20;
        break;
      case '*':
        i=21;
        break;
      case '0':
        i=22;
        break;
      case '#':
        i=23;
        break;
      case WIPHONE_KEY_F4:
        i=24;
        break;
      default:
        break;
      }
      if (i<sizeof(keyPressed)/sizeof(keyPressed[0])) {
        keyPressed[i]++;
        if (keyPressed[i]==1) {
          bbKeys[i]->setColors(TFT_BLACK, blueBg, blueBorder);
        } else if (keyPressed[i]==2) {
          bbKeys[i]->setColors(TFT_BLACK, yellowBg, yellowBorder);
        } else if (keyPressed[i]==3) {
          bbKeys[i]->setColors(TFT_BLACK, greenBg, greenBorder);
        }
        res |= REDRAW_SCREEN;
      }
    }
  }
  splitter++;
  return res;
}

void DiagnosticsApp::redrawScreen(bool redrawAll) {
  if ((!screenInited || redrawAll) && (appState != SCREEN)) {
    //lcd.fillRect(0, header->height(), lcd.width(), lcd.height() - header->height() - footer->height(), TFT_BLACK);
    lcd.fillRect(0, 0, lcd.width(), lcd.height(), TFT_BLACK);
  }

  if (appState == MAIN) {

    ((GUIWidget*) bVoltage)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bStateOfCharge)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bUsbPresence)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bCardPresence)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bCharging)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bAutonomous)->refresh(lcd, redrawAll || !screenInited);

    ((GUIWidget*) bBatteryGauge)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bGpioExtender)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bKeyScanner)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bSpiRam)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bCodec)->refresh(lcd, redrawAll || !screenInited);

    ((GUIWidget*) bRSSI)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bUptime)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bVersion)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bMacAddress)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) bIpAddress)->refresh(lcd, redrawAll || !screenInited);

    if (!screenInited) {
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      lcd.setTextSize(1.5);
      lcd.setTextFont(2);
      lcd.drawString("Press down for more tests", 75, 280);
    }

  } else if (appState == NETWORKS) {

    for (int i=0; i<sizeof(bbPings)/sizeof(bbPings[0]); i++) {
      if (bbPings[i]) {
        ((GUIWidget*) bbPings[i])->refresh(lcd, redrawAll || !screenInited);
      }
    }
  } else if (appState == AUDIO) {
    if (!screenInited || redrawAll) {
      //lcd.fillScreen(TFT_BLACK);
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      lcd.setTextSize(2);
      lcd.setTextFont(2);
      lcd.drawString("Front Mic", 10, 40);
      lcd.drawString("Audio Out", 10, 90);

      lcd.setTextSize(1.3);
      //lcd.setTextFont(2);
      lcd.drawString("Press this key to test:", 10, 110);
      lcd.drawString("#1: Front Speaker", 25, 130);
      lcd.drawString("#2: Rear Speaker", 25, 150);
      lcd.drawString("#3: Headphones", 25, 170);
      lcd.drawString("#4: Stop", 25, 190);
    }
    uint32_t val = audio->getMicAvg();
    uint8_t stp = 0;
    uint16_t xOff = 0;
    uint16_t colorStep = 17;
    uint16_t xStep = 16;
    do {
      uint8_t R = colorStep*stp;
      lcd.drawFastHLine(xOff, 60, xStep, lcd.color565(R, 255-R, 0));
      xOff += xStep;
      val >>= 1;
      stp++;
    } while (val && xOff < lcd.width()-xStep);
    lcd.drawFastHLine(xOff, 60, lcd.width()-xOff, TFT_BLACK);

    /*bool hp = audio->getHeadphones();
    testEarSpeaker->setColors(TFT_BLACK, greyBg, greyBorder);

    ((GUIWidget*) testEarSpeaker)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) testLoudSpeaker)->refresh(lcd, redrawAll || !screenInited);
    ((GUIWidget*) testHeadphone)->refresh(lcd, redrawAll || !screenInited);
    */

  } else if (appState == SCREEN) {
    switch (screenStep) {
    case 0:
      lcd.fillRect(0, 0, lcd.width(), lcd.height(), TFT_BLACK);
      screenStep++;
      break;
    case 1:
      allAnalogWrite(LCD_LED_PIN, 0);
      screenStep++;
      break;
    case 2:
      lcd.fillRect(0, 0, lcd.width(), lcd.height(), TFT_WHITE);
      screenStep++;
      break;
    case 3:
      allAnalogWrite(LCD_LED_PIN, 255);
      screenStep++;
      break;
    case 4:
      lcd.drawFastHLine(5, 5, lcd.width()-10, TFT_BLACK);
      lcd.drawFastHLine(5, lcd.height()-5, lcd.width()-10, TFT_BLACK);
      lcd.drawFastVLine(5, 5, lcd.height()-10, TFT_BLACK);
      lcd.drawFastVLine(lcd.width()-5, 5, lcd.height()-10, TFT_BLACK);
      screenStep++;
      break;
    case 5:
      screenStep++;
      break;
    case 6:
      screenStep++;
      break;
    case 7:
      lcd.setTextColor(TFT_BLACK, TFT_WHITE);
      lcd.setTextSize(2);
      lcd.setTextFont(2);
      lcd.drawString("LCD TEST", 120, 150);
      screenStep = 2;
      break;
    default:
      screenStep = 0;
      break;
    }

  } else if (appState == KEYPAD) {
    for (int i=0; i<sizeof(bbKeys)/sizeof(bbKeys[0]); i++) {
      ((GUIWidget*) bbKeys[i])->refresh(lcd, redrawAll || !screenInited);
    }
  }
  screenInited = true;
}
#ifdef BUILD_GAMES
// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Chess app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ChessApp::ChessApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer, ChessVariant_t variant)
  : WindowedApp(lcd, state, header, footer), audio(audio), variant(variant) {
  state.msAppTimerEventLast = millis();
  state.msAppTimerEventPeriod = 250;    // 4 fps

  const char* p;
  switch (variant) {
  case Normal:
    p = "Chess";
    break;
  case KingOfTheHill:
    p = "King of the Hill";
    break;
  case Chess960:
    p = "Fischer Random";
    break;
  default:
    p = "WiPhone Chess";
    break;
  }
  header->setTitle(p);
  footer->setButtons("Black", "Back");    // TODO: implement black button

  // Prepare icons
  cell_black = new IconRle3(icon_cell_0, sizeof(icon_cell_0));
  cell_white = new IconRle3(icon_cell_1, sizeof(icon_cell_1));
  sel_black = new IconRle3(icon_sel_0, sizeof(icon_sel_0));
  sel_white = new IconRle3(icon_sel_1, sizeof(icon_sel_1));
  cursor_frame = new IconRle3(icon_select_piece, sizeof(icon_select_piece));

  pieces_b[PAWN] = new IconRle3(icon_pawn_0, sizeof(icon_pawn_0));
  pieces_b[KNGT] = new IconRle3(icon_knight_0, sizeof(icon_knight_0));
  pieces_b[BISH] = new IconRle3(icon_bishop_0, sizeof(icon_bishop_0));
  pieces_b[ROOK] = new IconRle3(icon_rook_0, sizeof(icon_rook_0));
  pieces_b[QUEN] = new IconRle3(icon_queen_0, sizeof(icon_queen_0));
  pieces_b[KING] = new IconRle3(icon_king_0, sizeof(icon_king_0));

  pieces_w[PAWN] = new IconRle3(icon_pawn_1, sizeof(icon_pawn_1));
  pieces_w[KNGT] = new IconRle3(icon_knight_1, sizeof(icon_knight_1));
  pieces_w[BISH] = new IconRle3(icon_bishop_1, sizeof(icon_bishop_1));
  pieces_w[ROOK] = new IconRle3(icon_rook_1, sizeof(icon_rook_1));
  pieces_w[QUEN] = new IconRle3(icon_queen_1, sizeof(icon_queen_1));
  pieces_w[KING] = new IconRle3(icon_king_1, sizeof(icon_king_1));

  engine = new FairyMax::FairyMax(&this->post);
  if (engine != NULL) {
    log_d("xboard");
    engine->exchange("xboard");
    log_d("protover");
    engine->exchange("protover");
    log_d("post");
    engine->exchange("post");
    log_d("st 5");
    engine->exchange("st 5");        // maximum search time (seconds)
    log_d("sd 4");
    engine->exchange("sd 4");         // maximum search depth
    log_d("new");
    engine->exchange("new");

    // NOTE: "variant" should be called after "new" (the latter command loads normal variant)
    if (variant == Chess960) {
      log_d("variant");
      engine->exchange("variant fischerandom");     // TODO: currently not implemented in the Fairy-Max engine
    } else if (variant == KingOfTheHill) {
      log_d("variant");
      engine->exchange("variant king-of-the-hill");
    }

    log_d("board");
    engine->exchange("board");

    log_d("running");
    engineRunning = true;
  }
}

ChessApp::~ChessApp() {
  log_d("deleting engine");
  if (engine) {
    delete engine;
  }

  log_d("deleting icons");
  for (int i=0; i<6; i++) {
    delete pieces_w[i];
    delete pieces_b[i];
  }
  delete cell_black;
  delete cell_white;
  delete sel_black;
  delete sel_white;
  delete cursor_frame;
}

void ChessApp::post(const char* feedback) {
  if (static_lcd && feedback[0]!='\0') {
    int yOff = 270;   // hardcoded
    static_lcd->fillRect(0, yOff, static_lcd->width(), 10, BLACK);      // hardcoded height
    // Draw info message
    static_lcd->setTextColor(GREEN, TFT_BLACK);
    static_lcd->setTextDatum(TL_DATUM);
    static_lcd->setTextSize(1);
    static_lcd->setTextFont(1);
    static_lcd->drawString(feedback, 5, yOff + 1);
    //if (feedback[0]=='M') delay(400);     // delay for MAX.STACK events for debugging
  }
}

appEventResult ChessApp::processEvent(EventType event) {
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }
  if (engineRunning && event == APP_TIMER_EVENT) {
    log_d("- exchange");
    if (processEngine("")) {
      return REDRAW_SCREEN;
    }
  } else if (IS_KEYBOARD(event)) {
    if (!(event == WIPHONE_KEY_OK || event == WIPHONE_KEY_CALL)) {
      info = "";  // reset user message
    }
    bool unknown = false;
    if (event == WIPHONE_KEY_UP) {
      if (cursor >= 8) {
        cursor -= 8;
      }
    } else if (event == WIPHONE_KEY_DOWN) {
      if (cursor < 56) {
        cursor += 8;
      }
    } else if (event == WIPHONE_KEY_LEFT) {
      if ((cursor % 8) > 0) {
        cursor--;
      }
    } else if (event == WIPHONE_KEY_RIGHT) {
      if ((cursor % 8) < 7) {
        cursor++;
      }
    } else if (event == WIPHONE_KEY_SELECT) {
      footer->setButtons("Go", "Back");         // TODO: pressing Go a few times crashes the system
      if (processEngine("go")) {
        return REDRAW_SCREEN;
      }
    } else if (event == WIPHONE_KEY_OK || event == WIPHONE_KEY_CALL) {
      if (src < 0) {
        if (board[cursor] != EMPTY) {
          src = cursor;
        }
      } else if (src >= 0) {
        if (cursor == src) {
          src = -1;
        } else {
          makeMove(src, cursor);
        }
      }
    } else {
      unknown = true;
    }
    if (!unknown) {
      return REDRAW_SCREEN | REDRAW_FOOTER;
    }
  }
  return DO_NOTHING;
}

bool ChessApp::processEngine(const char* msg) {
  switch(engine->exchange(msg)) {
  case FairyMax::XboardChessEngine::Quit:
    engineRunning = false;
    log_d("- engine: quit");
    break;
  case FairyMax::XboardChessEngine::EmptyInput:
    log_d("- engine: empty");
    break;
  case FairyMax::XboardChessEngine::Continue:
    log_d("- engine: cont");
    break;
  default:
    break;
  }
  info = "";
  bool redraw = false;
  if (engine->output.length()) {
    const char* cstr = engine->output.c_str();
    log_d("Engine output: %s", cstr);
    while (*cstr) {
      if (!strncmp(cstr, "move ", 5)) {
        // Implement computer move
        cstr += 4;                          // skip "move"
        cstr += strspn(cstr, " \t");        // skip whitespace
        int len = strcspn(cstr, " \t\n");   // length of the move token
        char mov[7];
        strncpy(mov, cstr, len);
        mov[len] = '\0';
        log_d("computer move: %s", mov);

        // Show message to the user
        info = "Computer move: ";
        info += mov;

        // Execute computer move
        char promotion = (len>4) ? mov[4] : '\0';
        int8_t src, dst;
        decodeMove(mov, src);
        decodeMove(mov+2, dst);
        makeMove(src, dst, true, promotion);

        redraw = true;
        cstr += len;
      } else if (!strncmp(cstr, "Illegal move:", 13)) {
        // Show message to the user
        cstr += 13;
        int len = strcspn(cstr, "\n");
        char str[len+1];
        strncpy(str, cstr, len);
        str[len] = '\0';
        info = "Illegal move: ";
        info += str;
        cstr += len;
        // Restore state
        memcpy(board, board_backup, sizeof(board));
        redraw = true;
      } else if (!strncmp(cstr, "0-1", 3) || !strncmp(cstr, "1-0", 3) || !strncmp(cstr, "1/2-1/2", 7) || !strncmp(cstr, "resign", 6)) {       // TODO: doesn't show the "resign" message
        // Show message to the user
        int len = strcspn(cstr, "\n");
        char str[len+1];
        strncpy(str, cstr, len);
        str[len] = '\0';
        info = str;
        cstr += len;
      } else {
        int len = strcspn(cstr, "\n");
        if (*cstr != '#' && !info.length()) {
          char str[len + 1];
          strncpy(str, cstr, len);
          str[len] = '\0';
          info = str;
        }
        // skip this line
        cstr += len;
      }
      if (*cstr=='\n') {
        cstr++;
      }
    }
    engine->output = "";
  }
  if (info.length()>0 && !redraw) {
    this->post(info.c_str());  // special case for when there is only message, but the board is not to be redrawn
  }
  return redraw;
}

void ChessApp::encodeMove(int8_t lin, char& file, char& rank) {
  file = 'a' + (lin % 8);
  rank = '1' + (7 - (lin / 8));
}

void ChessApp::decodeMove(const char* mov, int8_t& lin) {
  lin = (mov[0]-'a') + (7-(mov[1]-'1'))*8;
}

void ChessApp::makeMove(uint8_t frm, uint8_t to, bool engineMove, char promotion) {
  if (!engineMove)
    // Preserve state for unmove
  {
    memcpy(board_backup, board, sizeof(board));
  }

  // Convert into coordinate notation
  char mov[13];
  char r1, f1, r2, f2;
  encodeMove(frm, f1, r1);
  encodeMove(to, f2, r2);
  sprintf(mov, "%c%c%c%c\n", f1, r1, f2, r2);
  log_d("move: %s", mov);

  // Check for special cases
  if ((board[frm] & 0xF) == PAWN && (r2 == '1' || r2 == '8')) {
    //  - moving a pawn to last ranks -> promotion
    // TODO: allow underpromotions for the user
    uint8_t typ;
    char prom;
    switch(tolower(promotion)) {
    case 'n':
      typ = KNGT;
      prom = 'n';
      break;
    case 'b':
      typ = BISH;
      prom = 'b';
      break;
    case 'r':
      typ = ROOK;
      prom = 'r';
      break;
    default:
      typ = QUEN;
      prom = 'q';
      break;
    }
    board[frm] = (board[frm] & 0xF0) | typ;       // update pawn to a higher piece (typically queen)
    sprintf(mov, "%c%c%c%c%c\n", f1, r1, f2, r2, prom);
    if (engineMove) {
      info += "; promotion";
    } else {
      info == "Promotion";
    }
  } else if (*mov=='e' && (board[frm] & 0xF) == KING) {
    // - castling: check for one of four possible moves and move the rook accordingly
    bool castling = false;
    if (!strncmp(mov, "e1g1", 4)) {
      board[61] = board[63];
      board[63] = EMPTY;
      castling = true;
    } else if (!strncmp(mov, "e1c1", 4)) {
      board[59] = board[56];
      board[56] = EMPTY;
      castling = true;
    } else if (!strncmp(mov, "e8g8", 4)) {
      board[5] = board[7];
      board[7] = EMPTY;
      castling = true;
    } else if (!strncmp(mov, "e8c8", 4)) {
      board[3] = board[0];
      board[0] = EMPTY;
      castling = true;
    }
    if (castling) {
      if (engineMove) {
        info += "; castling";
      } else {
        info = "Casting";
      }
    }
  } else if ( (board[frm] & 0xF) == PAWN && board[to]==EMPTY &&
              ( ((to - frm == 7 || to - frm == 9) && (board[frm] & BL)) || ((to - frm == -7 || to - frm == -9) && !(board[frm] & BL)) ) )   {
    // - moving a pawn to an empty square diagonally -> en-passant, kill the passing pawn
    if (to - frm == -7 || to - frm == 9) {
      board[frm + 1] = EMPTY;
    } else {
      board[frm - 1] = EMPTY;
    }
    if (engineMove) {
      info += "; en passant";
    } else {
      info = "En passant";
    }
  }

  // Show move on board;
  board[to] = board[frm];
  board[frm] = EMPTY;
  src = -1;
  cursor = to;

  if (!engineMove) {
    log_d("sending move to the engine: %s", mov);
    processEngine(mov);
  }
}

void ChessApp::redrawScreen(bool redrawAll) {
  log_d("redrawScreen ChessApp");

  // Draw chess board
  int cellSize = ((lcd.width() < lcd.height()) ? lcd.width() : lcd.height()) / 8;
  int yOff = header->height();
  bool white = true;
  for (int i=0; i<64; i+=8) {
    int xOff = 0;
    for (int j=0; j<8; j++) {
      int8_t lin = i + j;
      if (lin != src) {
        lcd.drawImage(white ? *cell_white : *cell_black, xOff, yOff);
      } else {
        lcd.drawImage(white ? *sel_white : *sel_black, xOff, yOff);
      }
      uint8_t piece = board[lin];
      if (!(piece & EMPTY)) {
        lcd.drawImage( **(((piece & BL) ? pieces_b : pieces_w) + (piece & typeMask)), xOff+2, yOff+2);
      }
      if (lin == cursor) {
        lcd.drawImage(*cursor_frame, xOff, yOff);
      }
      xOff += cellSize;
      white = !white;
    }
    yOff += cellSize;
    white = !white;
  }
  if (variant == KingOfTheHill) {
    lcd.drawRect(3*cellSize, header->height()+3*cellSize, 2*cellSize, 2*cellSize, TFT_BLACK);
  }

  // Draw black padding below
  if (lcd.height() - yOff - footer->height() > 0) {
    lcd.fillRect(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), BLACK);
  }

  // Draw info message
  if (info.length() > 0) {
    lcd.setTextColor(GREEN, TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextSize(1);
    lcd.setTextFont(1);
    lcd.drawString(info.c_str(), 5, yOff+1);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Ackman game  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

AckmanApp::AckmanApp(Audio* audio, LCD& lcd, ControlState& state) : WiPhoneApp(lcd, state), audio(audio), sprite(&lcd) {

  assert(AckmanApp::cellSize*AckmanApp::width  <= lcd.width());
  assert(AckmanApp::cellSize*AckmanApp::height <= lcd.height() - 3);
  this->gridXOff = (lcd.width()  - AckmanApp::cellSize*AckmanApp::width)  / 2;
  this->gridYOff = (lcd.height() - AckmanApp::cellSize*AckmanApp::height) - 5;

  // Restore high score from flash
  IniFile ini(AckmanApp::filename);
  if (ini.load() && !ini.isEmpty()) {
    this->highScore = ini[0].getIntValueSafe(AckmanApp::highField, 0);
  }

  sprite.setColorDepth(16);
  sprite.createSprite(AckmanApp::agentSize, AckmanApp::agentSize);
  if (!sprite.isCreated()) {
    log_e("sprite not created");
  }

  this->resetGame();
  this->parseLevel(ackmanLevels[this->level]);
  this->startGame();
  //audio->start();
}

AckmanApp::~AckmanApp() {
  //audio->shutdown();
  if (this->score > this->highScore) {
    this->saveHighScore(this->score);
  }
}

void AckmanApp::parseLevel(const char* level) {
  this->foodCnt = 0;
  this->warpCnt = 0;
  this->screenInited = false;
  this->nextAckmanDir = None;
  this->chewingTime = 0;
  this->moveCnt = 0;
  this->scaredTimer = 0;

  for (auto i = 0; i < sizeof(agents)/sizeof(agents[0]); i++) {
    agents[i].moving = false;
    agents[i].state = Absent;
    agents[i].outside = i <= 1;     // only ackman and the first enemy is outside
    agents[i].dirOffset = 0.0;
    agents[i].x = agents[i].origX = 0;
    agents[i].y = agents[i].origY = 0;
  }

  // Parse textual level representation
  memset(grid, 0, sizeof(grid));
  for (int j=0; j<AckmanApp::height; j++) {
    for (int i=0; i<AckmanApp::width; i++) {
      switch (*level) {
      case '.':
        grid[j][i] = crumbFlag;
        this->foodCnt++;
        break;
      case 'o':
        grid[j][i] = breadFlag;
        this->foodCnt++;
        break;
      case '-':
        grid[j][i] = doorFlag;
        break;
      case 'X':
        grid[j][i] = wallFlag;
        break;
      case '<': // if you want to have two sets of warping tunnels, they have to be opposite of one another for this to work right
      case '{':
        grid[j][i] = warpLeftFlag;
        warps[warpCnt].x = i;
        warps[warpCnt++].y = j;
        break;
      case '>':
      case '}':
        grid[j][i] = warpRightFlag;
        warps[warpCnt].x = i;
        warps[warpCnt++].y = j;
        break;
      case 'P':
        agents[0].origX = i;
        agents[0].origY = j;
        agents[0].state = Normal;
        break;
      case 'B':
        agents[1].origX = i;
        agents[1].origY = j;
        agents[1].state = Normal;
        break; // TODO: hardcoded positions in the array
      case 'R':
        agents[2].origX = i;
        agents[2].origY = j;
        agents[2].state = Normal;
        break;
      case 'M':
        agents[3].origX = i;
        agents[3].origY = j;
        agents[3].state = Normal;
        break;
      case 'S':
        agents[4].origX = i;
        agents[4].origY = j;
        agents[4].state = Normal;
        break;
      case ' ':
      default:
        break;
      }
      level++;
    }
  }

  // Calculate positions of "nodes" (crossings between different paths)
  for (int j=0; j<AckmanApp::height; j++) {
    for (int i=0; i<AckmanApp::width; i++) {
      if (grid[j][i] & (wallFlag | doorFlag)) {
        continue;
      }
      bool horiz = (i && !(grid[j][i-1] & wallFlag)) || (i+1 < AckmanApp::width  && !(grid[j][i+1] & wallFlag));
      bool verti = (j && !(grid[j-1][i] & wallFlag)) || (j+1 < AckmanApp::height && !(grid[j+1][i] & wallFlag));
      if (horiz && verti) {
        grid[j][i] |= nodeFlag;
      }
    }
  }

  // Reset enemy states
  for (auto i = 0; i < sizeof(agents)/sizeof(agents[0]); i++) {
    if (agents[i].state == Absent) {
      continue;
    }
    agents[i].moving = true;
    this->respawn(&agents[i]);
  }

}

void AckmanApp::respawn(struct Agent* agent) {
  agent->x = agent->origX;
  agent->y = agent->origY;
  agent->state = Normal;
  agent->outside = agent->typ == Bloody;      // only Bloody's original position is outside (by convention)

  // Pick a random direction which makes sense
  auto off = Random.random() % 4;
  for (auto d = 0; d < 4; d++) {
    AgentDirection_t dir = (AgentDirection_t) ((d + off) % 4);
    if (this->isRelevantDir(agent, dir)) {
      agent->dir = dir;
      break;
    }
  }
}

void AckmanApp::updateAgentPosition(struct Agent* agent) {

  // Get position of the agent on screen
  auto& x = agent->screenX;
  auto& y = agent->screenY;

  // - calculate center of the "cell" (that's where a crumb would be drawn)
  x = this->gridXOff + AckmanApp::cellSize/2 + AckmanApp::cellSize * agent->x;
  y = this->gridYOff + AckmanApp::cellSize/2 + AckmanApp::cellSize * agent->y;

  // - calculate offset of the agent in pixels
  float delta = (agent->dirOffset < 1.0 ? agent->dirOffset : 1.0) * AckmanApp::cellSize;
  switch (agent->dir) {
  case North:
    y = round(y - delta);
    break;
  case East:
    x = round(x + delta);
    break;
  case South:
    y = round(y + delta);
    break;
  case West:
    x = round(x - delta);
    break;
  }

}

void AckmanApp::drawAgent(struct Agent* agent, bool draw) {

  this->updateAgentPosition(agent);

  auto& x = agent->screenX;
  auto& y = agent->screenY;
  const auto R = AckmanApp::agentSize / 2;

  // Do the actual screen modifications
  if (draw) {

    sprite.fillSprite(transparent);

    if (agent->typ == Ackman) {

      // Draw ackman
      // NOTE: sprite for ackman is needed to avoid blinking while standing
      sprite.fillCircle(R, R, R, TFT_YELLOW);

      // Draw ackman's mouth
      int32_t t = abs(this->chewingTime - AckmanApp::chewingPeriod / 2);    // how open the mouth is? (3 - fully open, 0 - closed)

      if (t>=1) {
        t++;  // skip t==1 as it is ugly (looks like a thick line)
      }

      // - default calculation: mouth facing westward
      int32_t dx1 = (t != 0 ? (AckmanApp::cellSize - 4)/2 : 0) - (t == AckmanApp::chewingPeriod / 2 ? 1 : 0);
      int32_t dy1 = 0;
      int32_t dx2 = -R;
      int32_t dy2 = t;
      int32_t dx3 = -R;
      int32_t dy3 = -t;

      // - mirroring and rotation based on direction
      switch (agent->dir) {
      case East:
        dx1 = -dx1;
        dx2 = -dx2;
        dx3 = -dx3;
        break;
      case North:
      case South:
        t = dx1;
        dx1 = dy1;
        dy1 = t;
        t = dx2;
        dx2 = dy2;
        dy2 = t;
        t = dx3;
        dx3 = dy3;
        dy3 = t;
        break;
      }
      if (agent->dir == South) {
        dy1 = -dy1;
        dy2 = -dy2;
        dy3 = -dy3;
      }

      // - actually draw the mouth
      sprite.fillTriangle(R + dx1, R + dy1,
                          R + dx2, R + dy2,
                          R + dx3, R + dy3,
                          transparent);

    } else {

      // Draw enemy body
      if (agent->state != Eaten) {
        colorType color;
        if (agent->state != Scared) {
          switch (agent->typ) {
          case Bloody:
            color = 0xF800;
            break;     // Red
          case Rosy:
            color = 0xFB56;
            break;     // Hot Pink
          case Moody:
            color = 0x64BD;
            break;     // Cornflower Blue
          case Sunny:
            color = 0xFD20;
            break;     // Orange
          }
        } else {
          color = 0x0011;   // Dark Blue
        }
        // NOTE: sprite is used to reduce blinking in scared enemies' eyes
        sprite.fillRoundRect(0, 0, AckmanApp::agentSize, AckmanApp::agentSize, 3, color);
      }

      // Draw enemy eyes
      const auto d = 3;         // distance between eyes
      int dx = 0;               // eye offset
      int ddx = 0, ddy = 0;     // iris offset
      switch (agent->dir) {
      case North:
        ddy = -1;
        break;
      case East:
        ddx =  1;
        break;
      case South:
        ddy =  1;
        break;
      case West:
        ddx = dx = -1;
        break;      //  for shifting entire eyes to the left when moving west
      }
      if (agent->state != Scared) {

        // Eyes look normal
        sprite.fillCircle(R - d + dx, R - d, 2, TFT_WHITE);
        sprite.fillCircle(R + d + dx, R - d, 2, TFT_WHITE);
        sprite.fillCircle(R - d + dx + ddx, R - d + ddy, 1, TFT_BLACK);
        sprite.fillCircle(R + d + dx + ddx, R - d + ddy, 1, TFT_BLACK);

      } else {

        // Eyes look like crosses
        const auto er = 2;
        sprite.drawLine(R - d + dx - er, R - d - er, R - d + dx + er, R - d + er,  GRAY_75);
        sprite.drawLine(R - d + dx - er, R - d + er, R - d + dx + er, R - d - er,  GRAY_75);
        sprite.drawLine(R + d + dx - er, R - d - er, R + d + dx + er, R - d + er,  GRAY_75);
        sprite.drawLine(R + d + dx - er, R - d + er, R + d + dx + er, R - d - er,  GRAY_75);

      }

      // Draw mouth
      // TODO
    }

    // Push agent sprite
    sprite.pushSprite(x - R, y - R, transparent);

  } else {
    // Clear: draw black rectangle over it
    int32_t x0 = x - R;
    lcd.fillRect(x0 >= 0 ? x0 : 0, y - R,
                 AckmanApp::agentSize + (x0 >= 0 ? 0 : x0),
                 AckmanApp::agentSize,
                 TFT_BLACK);
  }
}

appEventResult AckmanApp::processEvent(EventType event) {
  if (LOGIC_BUTTON_BACK(event)) {
    return EXIT_APP;
  }

  if (event == APP_TIMER_EVENT) {

    if (this->gameState == Playing) {

      // New move
      this->moveCnt++;

      // Update timers
      if (this->scaredTimer) {
        this->scaredTimer--;
        if (!this->scaredTimer) {
          for (auto i = 1; i < sizeof(agents)/sizeof(agents[0]); i++)
            if (agents[i].state == Scared) {
              agents[i].state = Normal;
            }
        }
      }

      // Process each agent individually
      bool highScore = false;
      struct Agent *agent = agents;
      for (int i = 0; i < sizeof(agents)/sizeof(agents[0]); i++, agent++) {
        if (!agent->moving) {
          this->drawAgent(agent, true);     // redraw ackman in case it was cleared by an eaten enemy
          continue;
        } else if (!i) {
          this->chewingTime = (this->chewingTime + 1) % AckmanApp::chewingPeriod;
        }

        // Clear on screen
        this->drawAgent(agent, false);

        // Redraw objects
        if (grid[agent->y][agent->x] & (crumbFlag | breadFlag | doorFlag)) {
          if (grid[agent->y][agent->x] & (crumbFlag | breadFlag)) {
            this->drawFood(agent->x, agent->y);
          } else {
            this->drawDoors();
          }
        }

        // Move
        switch (agent->state) {
        case Normal:
          agent->dirOffset += AckmanApp::normalSpeed;
          break;
        case Scared:
          agent->dirOffset += AckmanApp::normalSpeed*0.4;
          break;
        case Eaten:
          agent->dirOffset += AckmanApp::normalSpeed*2;
          break;
        }

        // Check food collisions
        if (agent->typ == Ackman) {
          uint8_t tx, ty;
          this->getDest(agent, tx, ty);
          if ((grid[ty][tx] & crumbFlag) && agent->dirOffset >= 0.4) {
            this->drawFood(tx, ty, true);   // clear food
            this->foodCnt--;
            grid[ty][tx] &= ~crumbFlag;
            this->score += 1;
            this->drawScore(false);
          } else if ((grid[ty][tx] & breadFlag) && agent->dirOffset >= 0.3) {
            this->drawFood(tx, ty, true);   // clear food
            this->foodCnt--;
            grid[ty][tx] &= ~breadFlag;
            this->score += 5;
            this->drawScore(false);
            this->scaredTimer = AckmanApp::scaredPeriod;
            for (auto i = 1; i < sizeof(agents)/sizeof(agents[0]); i++)
              if (agents[i].state == Normal) {
                agents[i].state = Scared;
              }
          }
          if (!this->foodCnt) {
            // Proceed to next level
            this->freezeGame();
            this->level = (this->level + 1) % (sizeof(ackmanLevels)/sizeof(ackmanLevels[0]));
            this->setState(LevelOver);
          }
        }

        // Check if direction of movement needs to be changed
        while (agent->dirOffset >= 1.0) {

          // reached new cell -> update state
          agent->dirOffset -= 1.0;
          this->nextCell(agent);

          if (agent->typ == Ackman) {

            // Set new direction if relevant
            if (this->nextAckmanDir != None && this->isRelevantDir(agent, this->nextAckmanDir)) {
              agent->dir = this->nextAckmanDir;
              this->nextAckmanDir = None;
            }

            // Check if still can move in current direction
            if (!this->isRelevantDir(agent, agent->dir)) {
              agent->moving = false;
              agent->dirOffset = 0.0;
              this->nextAckmanDir = None;       // previous direction was irrelevant
            }

          } else {

            // Calculate new direction for an enemy
            if (!this->isRelevantDir(agent, agent->dir) || grid[agent->y][agent->x] & nodeFlag) {
              this->newEnemyDirection(agent);
            }

          }
        }

        // Calculate screen position
        this->updateAgentPosition(agent);

        // Check enemy collision
        if (agent->typ != Ackman) {
          auto dist = abs(agent->x - agents[0].x) + abs(agent->y - agents[0].y);    // distance to ackman
          if (dist <= 2) {
            // TODO: better collision detection (by drawing in a sprite and seeing if figures are connected)
            //          float screenDist = this->agentDistance(0, i);
            //          bool caught = false;
            //          if (agent->x == agents[0].x || agent->y == agents[0].y)
            //            caught = screenDist <= 0.9*AckmanApp::agentSize;
            //          else
            //            caught = screenDist <= 1.2*AckmanApp::agentSize;
            if (this->agentDistance(0, i) <= 0.75*AckmanApp::agentSize) {     // Ms. Pac-Man allows significant overlap before actual collision

              if (agent->state == Normal) {

                // Game over
                this->freezeGame();
                this->setState(GameOver);

                if (this->score > this->highScore) {
                  this->highScore = this->score;
                  this->drawScore(true, true);
                  highScore = true;
                }

              } else if (agent->state == Scared) {

                // Enemy is eaten
                this->score += 10;
                agent->state = Eaten;

              }

            }
          }
        }

        // Draw on screen
        this->drawAgent(agent, true);

      }

      // Save high score to flash only after all agents are redrawn (to avoid visible lag)
      if (highScore) {
        this->saveHighScore(this->highScore);
      }

      return DO_NOTHING;

    } else if (this->gameState == Ready) {

      this->setState(Playing);

    } else if (this->gameState == LevelOver || this->gameState == GameOver) {

      // Restart level
      if (this->gameState == GameOver) {
        this->resetGame();
      }
      this->parseLevel(ackmanLevels[this->level]);
      this->startGame();
      return REDRAW_SCREEN;

    }
  } else if (IS_KEYBOARD(event)) {

    if (this->gameState != GameOver) {

      // Process ackman movement

      if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_RIGHT || event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_LEFT) {
        switch (event) {
        case WIPHONE_KEY_UP:
          this->nextAckmanDir = North;
          break;
        case WIPHONE_KEY_RIGHT:
          this->nextAckmanDir = East;
          break;
        case WIPHONE_KEY_DOWN:
          this->nextAckmanDir = South;
          break;
        case WIPHONE_KEY_LEFT:
          this->nextAckmanDir = West;
          break;
        }

        if (agents[0].moving) {
          // Check if can change direction immediately, otherwise: wait till move is finished (arrived to the center of the next cell)
          bool opposing = this->nextAckmanDir == North && agents[0].dir == South ||
                          this->nextAckmanDir == South && agents[0].dir == North ||
                          this->nextAckmanDir == West && agents[0].dir == East ||
                          this->nextAckmanDir == East && agents[0].dir == West;
          if (opposing) {
            this->nextCell(&agents[0]);
            agents[0].dir = this->nextAckmanDir;
            agents[0].dirOffset = 1.0 - agents[0].dirOffset;
            this->nextAckmanDir = None;
          }
        } else {
          // If Ackman is not moving, check if the selected movement makes sense, if it does - start moving
          if (this->isRelevantDir(agents, this->nextAckmanDir)) {
            agents[0].dir = this->nextAckmanDir;
            agents[0].moving = true;
          }
          this->nextAckmanDir = None;
        }
      }

    }
  }
  return DO_NOTHING;
}

void AckmanApp::saveHighScore(int32_t highScore) {
  // Save high score to flash
  IniFile ini(AckmanApp::filename);
  ini[0][AckmanApp::highField] = highScore;
  ini.store();
}

float AckmanApp::agentDistance(uint8_t i, uint8_t j) {
  int a = agents[i].screenX - agents[j].screenX;
  int b = agents[i].screenY - agents[j].screenY;
  return sqrt(a*a + b*b);
}

// Move agent to the given direction (or to his own direction)
void AckmanApp::nextCell(struct Agent* agent, AgentDirection_t dir) {
  if (dir == None) {
    dir = agent->dir;
  }
  this->getDest(agent, dir, agent->x, agent->y);
  if (grid[agent->y][agent->x] & doorFlag) {
    // Passing a door
    if (agent->outside && agent->state == Eaten) {
      agent->state = Normal;
    }
    agent->outside = !agent->outside;
  }
}

// Where is this agent going?
void AckmanApp::getDest(struct Agent* agent, uint8_t& x, uint8_t& y) {
  this->getDest(agent, agent->dir, x, y);
}

void AckmanApp::getDest(struct Agent* agent, AgentDirection_t dir, uint8_t& x, uint8_t& y) {
  x = agent->x;
  y = agent->y;
  switch (dir) {
  case North:
    y--;
    break;
  case South:
    y++;
    break;
  case East:
    if (grid[y][x] & warpRightFlag) {
      this->moveToWarp(x, y);
    } else {
      x++;
    }
    break;
  case West:
    if (grid[y][x] & warpLeftFlag) {
      this->moveToWarp(x, y);
    } else {
      x--;
    }
    break;
  }
}

void AckmanApp::startGame() {
  this->setState(Ready);
}

void AckmanApp::freezeGame() {
  for (auto i = 0; i < sizeof(agents)/sizeof(agents[0]); i++) {
    agents[i].moving = false;
  }
}

void AckmanApp::resetGame() {
  // Reset variables to mark beginning of the game (score, lives, level, etc.)
  this->score = 0;
  this->level = 0;
}

void AckmanApp::setState(GameState_t state) {
  this->gameState = state;
  switch (state) {
  case Ready:
    controlState.msAppTimerEventPeriod = 2000;
    break;
  case LevelOver:
    controlState.msAppTimerEventPeriod = 2500;
    break;
  case GameOver:
    this->drawMessage(true, false);
    controlState.msAppTimerEventPeriod = 3500;
    break;
  case Playing:
    this->drawMessage(false, true);   // clear Ready! message
    controlState.msAppTimerEventPeriod = 33;
    break;     // 30 fps
  }
  controlState.msAppTimerEventLast = millis();
  this->gameState = state;
}

void AckmanApp::moveToWarp(uint8_t& x, uint8_t& y) {
  for (auto i = 0; i < warpCnt; i++) {
    if (warps[i].x == x && warps[i].y == y) {
      i = (i & 1) ? i - 1 : i + 1;
      x = warps[i].x;
      y = warps[i].y;
      return;
    }
  }
  log_e("warp not found");
}

void AckmanApp::newEnemyDirection(struct Agent* agent) {

  // Choose current movement type for this agent
  AgentType_t movementType = agent->typ;
  if (!agent->outside) {
    movementType = Sunny;       // enemies move randomly "inside" their home
  } else if (agent->state == Scared || agent->state == Eaten) {
    movementType = Bloody;      // scared enemies act the same trying to move away from ackman semirandomly
  } else if (agent->state == Normal && this->moveCnt <= AckmanApp::confusedPeriod && agent->typ != Moody) {
    movementType = Sunny;       // Bloody and Rosy act randomly the first few seconds
  } else if (agent->typ == Moody) {
    switch (Random.random() % 4) {
    case 0:
      movementType = Bloody;
      break;  // chase
    case 1:
      movementType = Rosy;
      break;  // chase
    default:
      movementType = Sunny;
      break;  // wander
    }
  }

  // Don't go back inside, unless Eaten
  uint8_t obstr = wallFlag;
  if (agent->outside && agent->state != Eaten) {
    obstr |= doorFlag;
  }

  // See what directions are possible
  AgentDirection_t moves[4];
  uint32_t chance[4];
  for (auto i = 0; i < 4; i++) {
    uint8_t ncell = 0;
    chance[i] = 0;
    moves[i] = (AgentDirection_t) i;
    if ((AgentDirection_t) i == North) {
      if (!agent->y) {
        continue;
      }
      ncell = grid[agent->y - 1][agent->x];
    } else if ((AgentDirection_t) i == East) {
      if (agent->x + 1 >= AckmanApp::width) {
        continue;
      }
      ncell = grid[agent->y][agent->x + 1];
    } else if ((AgentDirection_t) i == South) {
      if (agent->y + 1 >= AckmanApp::height) {
        continue;
      }
      ncell = grid[agent->y + 1][agent->x];
    } else if ((AgentDirection_t) i == West) {
      if (!agent->x) {
        continue;
      }
      ncell = grid[agent->y][agent->x - 1];
    }
    if (!(ncell & obstr)) {
      chance[i] = (ncell & doorFlag) ? 10 : 1;  // strongly prefer to cross the door if possible
    }
  }

  // Calculate next move chances based on movement type

  if (movementType == Bloody || movementType == Rosy) {

    // Bloody chases Ackman, Rosy tries to get in front of Ackman

    // What is the target cell?
    uint8_t tx, ty;
    if (movementType == Bloody) {
      if (agent->state == Normal || agent->state == Scared) {
        // for Bloody movement type in Normal state -> "target" is the current ackman position
        tx = agents[0].x;
        ty = agents[0].y;
      } else {
        // for Bloody movement type in Eaten state -> target is the original position
        tx = agent->origX;
        ty = agent->origY;
      }
    } else {
      // for Rosy -> projected ackman position
      this->projectAgent(&agents[0], agents[0].dir, tx, ty);
    }

    // Modify chances of each move based on the target cell
    const uint32_t factor = 1000;     // the bigger it is, the more best move will be preferred, the more it will be predictable
    uint32_t mn = factor;
    for (auto i = 0; i < 4; i++) {
      if (chance[i]) {
        uint8_t nx, ny;
        this->projectAgent(agent, (AgentDirection_t) i, nx, ny);
        chance[i] *= factor / (abs(nx - tx) + abs(ny - ty) + 1);
        if (!chance[i]) {
          chance[i] = 1;
        }
        if (chance[i] < mn) {
          mn = chance[i];
        }
      }
    }
    // - cutoff (makes the worst moves less possible)
    mn -= mn/4;
    for (auto i = 0; i < 4; i++)
      if (chance[i]) {
        chance[i] -= mn;
      }

  } else if (movementType == Sunny) {

    // Random movement while preferring to move in same direction, avoiding moving backwards
    uint32_t curDir = (uint32_t) agent->dir;
    for (auto i = 0; i < 4; i++) {
      if (chance[i]) {
        chance[i] *= (i != (curDir + 2) % 4) ? (curDir == i ? 200 : 100) : 16;
      }
    }

  }

  // "Reverse" chances, if the enemy is scared
  if (agent->state == Scared) {
    uint32_t sum = chance[0] + chance[1] + chance[2] + chance[3];
    for (auto i = 0; i < 4; i++) {
      if (chance[i]) {
        chance[i] = sum / chance[i];
        if (!chance[i]) {
          chance[i] = 1;
        }
      }
    }
  }

  // Select move randomly
//  switch (agent->typ) {
//    case Bloody: DEBUG_PRINTF("BLOODY"); break;
//    case Rosy:   DEBUG_PRINTF("ROSY"); break;
//    case Moody:  DEBUG_PRINTF("MOODY"); break;
//    case Sunny:  DEBUG_PRINTF("SUNNY"); break;
//  }
//  switch (movementType) {
//    case Bloody: DEBUG_PRINTF("/BLOODY"); break;
//    case Rosy:   DEBUG_PRINTF("/ROSY"); break;
//    case Sunny:  DEBUG_PRINTF("/SUNNY"); break;
//  }
  auto draw = Random.random() % (chance[0] + chance[1] + chance[2] + chance[3]);
//  DEBUG_PRINTF(" %d,%d,%d,%d", chance[0], chance[1], chance[2], chance[3]);
//  DEBUG_PRINTF(", rnd=%d", draw);
  for (auto i = 0; i < 4; i++) {
    if (chance[i] && draw <= chance[i]) {
      agent->dir = moves[i];
//      log_d(", m=%d", i);
      break;
    } else {
      draw -= chance[i];
    }
  }
}

bool AckmanApp::isRelevantDir(struct Agent* agent, AgentDirection_t dir) {
  uint8_t obstr = wallFlag;
  if (agent->typ == Ackman || agent->outside && agent->state != Eaten) {
    obstr |= doorFlag;
  }
  switch (dir) {
  case North:
    if (!agent->y) {
      return false;
    }
    if (grid[agent->y-1][agent->x] & obstr) {
      return false;
    }
    break;
  case East:
    if (grid[agent->y][agent->x] & warpRightFlag) {
      return true;
    }
    if (agent->x + 1 >= AckmanApp::width) {
      return false;
    }
    if (grid[agent->y][agent->x+1] & obstr) {
      return false;
    }
    break;
  case South:
    if (agent->y + 1 >= AckmanApp::height) {
      return false;
    }
    if (grid[agent->y+1][agent->x] & obstr) {
      return false;
    }
    break;
  case West:
    if (grid[agent->y][agent->x] & warpLeftFlag) {
      return true;
    }
    if (!agent->x) {
      return false;
    }
    if (grid[agent->y][agent->x-1] & obstr) {
      return false;
    }
    break;
  }
  return true;
}

void AckmanApp::projectAgent(struct Agent* agent, AgentDirection_t dir, uint8_t& x, uint8_t& y) {
  struct Agent futureAgent = *agent;
  while (this->isRelevantDir(&futureAgent, dir)) {
    this->nextCell(&futureAgent, dir);
    if (grid[futureAgent.y][futureAgent.x] & nodeFlag) {
      break;
    }
  }
  x = futureAgent.x;
  y = futureAgent.y;
}

void AckmanApp::redrawScreen(bool redrawAll) {
  if (!this->screenInited) {
    redrawAll = true;
  }
  if (redrawAll) {

    // Clear screen
    lcd.fillScreen(TFT_BLACK);

    // Draw text
    this->drawScore(true);
    lcd.setTextSize(2);
    lcd.drawString("Ackman", 10, 1);
    if (this->gameState == GameOver || this->gameState == Ready) {
      this->drawMessage(true, this->gameState == Ready);
    }

    // Draw border
    auto k = this->level % (sizeof(ackmanLevels)/sizeof(ackmanLevels[0]));
    colorType mazeColor = wallColors[k % (sizeof(wallColors)/sizeof(wallColors[0]))];
    lcd.drawRoundRect(this->gridXOff - AckmanApp::cellSize/2,
                      this->gridYOff - AckmanApp::cellSize/2,
                      (AckmanApp::width + 1) * AckmanApp::cellSize,
                      (AckmanApp::height + 1) * AckmanApp::cellSize,
                      4, mazeColor);

    // Draw food & maze
    for (int j=0; j<AckmanApp::height; j++) {
      for (int i=0; i<AckmanApp::width; i++) {
        if (grid[j][i] & (crumbFlag | breadFlag)) {
          this->drawFood(i, j);
        } else if (grid[j][i] & wallFlag) {
          lcd.fillCircle(getX(i), getY(j), 1, mazeColor);
          //lcd.fillRoundRect(getX(i) - 2, getY(j) - 2, 4, 4, 2, mazeColor);
          if (i && grid[j][i-1] & wallFlag || !i) {
            this->drawLine(i-1, j, i, j, mazeColor);  // connect to left
          }
          if (j && grid[j-1][i] & wallFlag || !j) {
            this->drawLine(i, j-1, i, j, mazeColor);  // connect up
          }
          if (i+1 == AckmanApp::width) {
            this->drawLine(i, j, i+1, j, mazeColor);  // connect to right
          }
          if (j+1 == AckmanApp::height) {
            this->drawLine(i, j, i, j+1, mazeColor);  // connect down
          }
        } else if (grid[j][i] & (warpLeftFlag | warpRightFlag)) {
          if (grid[j][i] & warpLeftFlag)
            lcd.drawFastVLine(this->getX(i) - AckmanApp::cellSize,
                              this->getY(j) - AckmanApp::cellSize + 2,
                              2*AckmanApp::cellSize - 3, TFT_BLACK);
          else
            lcd.drawFastVLine(this->getX(i) + AckmanApp::cellSize - 1,
                              this->getY(j) - AckmanApp::cellSize + 2,
                              2*AckmanApp::cellSize - 3, TFT_BLACK);
        }
      }
    }
    this->drawDoors();

    // Draw agents
    struct Agent* agent = agents;
    for (int i = 0; i < sizeof(agents)/sizeof(agents[0]); i++, agent++) {
      this->drawAgent(agent, true);
    }

    this->screenInited = true;
  }
}

void AckmanApp::drawFood(uint8_t i, uint8_t j, bool clear) {
  lcd.fillCircle(getX(i), getY(j), grid[j][i] & crumbFlag ? 1 : 3, clear ? TFT_BLACK : AckmanApp::foodColor);
}

void AckmanApp::drawDoors() {
  // Draw doors
  for (int j=0; j<AckmanApp::height; j++) {
    for (int i=0; i<AckmanApp::width; i++) {
      if (grid[j][i] & doorFlag) {
        lcd.fillCircle(getX(i), getY(j), 1, AckmanApp::doorColor);
        if (i && grid[j][i-1] & (doorFlag | wallFlag)) {
          this->drawLine(i-1, j, i, j, AckmanApp::doorColor);  // connect to left
        }
        if (j && grid[j-1][i] & (doorFlag | wallFlag)) {
          this->drawLine(i, j-1, i, j, AckmanApp::doorColor);  // connect up
        }
        if (i+1 < AckmanApp::width && grid[j][i+1] & (doorFlag | wallFlag)) {
          this->drawLine(i, j, i+1, j, AckmanApp::doorColor);  // connect to right
        }
        if (j+1 < AckmanApp::height && grid[j+1][i] & (doorFlag | wallFlag)) {
          this->drawLine(i, j, i, j+1, AckmanApp::doorColor);  // connect down
        }
      }
    }
  }
}

void AckmanApp::drawScore(bool redrawAll, bool newHigh) {
  lcd.setTextColor(newHigh ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  lcd.setTextFont(2);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  char str[9];
  snprintf(str, sizeof(str), "%u", this->score);
  lcd.drawString(str, 56, 31);

  if (redrawAll) {
    lcd.setTextDatum(TR_DATUM);
    lcd.drawString("Score:", 49, 31);
    if (this->highScore) {
      lcd.drawString("High:", 153, 31);
      lcd.setTextDatum(TL_DATUM);
      snprintf(str, sizeof(str), "%u", this->highScore);
      lcd.drawString(str, 160, 31);
    }
  }
}

void AckmanApp::drawMessage(bool draw, bool ready) {
  if (draw) {
    lcd.setTextColor(ready ? TFT_YELLOW : TFT_RED);
    lcd.setTextFont(2);
    lcd.setTextSize(1);
    lcd.setTextDatum(TR_DATUM);
    lcd.drawString(ready ? "Ready!" : "Game Over!", 230, 10);
  } else {
    lcd.fillRect(162, 10, 68, 16, TFT_BLACK);
  }
}

inline uint16_t AckmanApp::getX(int8_t i) {
  return this->gridXOff + AckmanApp::cellSize/2 + AckmanApp::cellSize*i;
}

inline uint16_t AckmanApp::getY(int8_t j) {
  return this->gridYOff + AckmanApp::cellSize/2 + AckmanApp::cellSize*j;
}

void AckmanApp::drawLine(uint8_t i1, uint8_t j1, uint8_t i2, uint8_t j2, colorType color) {
  lcd.drawLine(getX(i1), getY(j1), getX(i2), getY(j2), color);
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  LED board app  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifdef LED_BOARD
APA102<LED_BOARD_DATA, LED_BOARD_CLOCK> ledBoard;

LedMicApp::LedMicApp(Audio* audio, LCD& lcd, ControlState& state, HeaderWidget* header, FooterWidget* footer)
  : WindowedApp(lcd, state, header, footer), audio(audio), FocusableApp(2) {
  log_d("create LedMicApp");

  // Set timer
  state.msAppTimerEventLast = millis();
  state.msAppTimerEventPeriod = 33;     // 30 fps

  // Set titles
  header->setTitle("LED microphone");
  footer->setButtons(NULL, "Clear");

  // Background
  uint16_t yOff = header->height();
  bgRect = new RectWidget(0, yOff, lcd.width(), lcd.height() - yOff - footer->height(), WP_COLOR_1);

  // Create and arrange widgets
  addLabelInput(yOff, labels[0], inputs[0], "Scale down:", 16);
  addLabelInput(yOff, labels[1], inputs[1], "Step:", 6);

  // Set text
  inputs[0]->setText("100");
  inputs[1]->setText("1.5");
  takeInputs();

  // Initialize variables
  this->time = Random.random();

  // Precompute: colors
  for (uint16_t hue = 0; hue<360; hue++) {
    hue2rgb[hue] = LedMicApp::hsvToRgb(hue, 255, 255);
  }

  // Turn on microphone
  audio->setSampleRate(16000);
  audio->start();
  audio->turnMicOn();

  allDigitalWrite(LED_BOARD_ENABLE, HIGH);

  // Set focusables
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    addFocusableWidget(inputs[i]);
  }

  setFocus(inputs[0]);
}

LedMicApp::~LedMicApp() {
  log_d("destroy LedMicApp");

  allDigitalWrite(LED_BOARD_ENABLE, LOW);

  for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
    delete labels[i];
  }
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    delete inputs[i];
  }

  audio->shutdown();
}

void LedMicApp::takeInputs() {
  const char* sScaleDown = inputs[0]->getText();
  const char* sStep = inputs[1]->getText();

  scaleDown = sScaleDown && *sScaleDown ? atof(sScaleDown) : 0.0;
  step = sStep && *sStep ? atof(sStep) : 0.0;

  log_d("scale down: %f", scaleDown);
  log_d("step: %f", step);

  scale[0] = 1;
  for(int i=1; i<12; i++) {
    scale[i] = step*scale[i-1];
  }
}

/* Converts a color from HSV to RGB.
 * h is hue, as a number between 0 and 360.
 * s is the saturation, as a number between 0 and 255.
 * v is the value, as a number between 0 and 255. */
rgb_color LedMicApp::hsvToRgb(uint16_t h, uint8_t s, uint8_t v) {
  uint8_t f = (h % 60) * 255 / 60;
  uint8_t p = (255 - s) * (uint16_t)v / 255;
  uint8_t q = (255 - f * (uint16_t)s / 255) * (uint16_t)v / 255;
  uint8_t t = (255 - (255 - f) * (uint16_t)s / 255) * (uint16_t)v / 255;
  uint8_t r = 0, g = 0, b = 0;
  switch((h / 60) % 6) {
  case 0:
    r = v;
    g = t;
    b = p;
    break;
  case 1:
    r = q;
    g = v;
    b = p;
    break;
  case 2:
    r = p;
    g = v;
    b = t;
    break;
  case 3:
    r = p;
    g = q;
    b = v;
    break;
  case 4:
    r = t;
    g = p;
    b = v;
    break;
  case 5:
    r = v;
    g = p;
    b = q;
    break;
  }
  return rgb_color(r, g, b);
}

appEventResult LedMicApp::processEvent(EventType event) {
  if (event == APP_TIMER_EVENT) {

    // Get average microprohone volume
    int32_t val = audio->getMicAvg();

    // Make it close to zero
    val /= this->scaleDown>1 ? this->scaleDown : 1;
    if (!val) {
      val++;
    }

    // Update LEDs
    uint16_t hue = time % 360;
    for(uint16_t i = 0; i < LED_BOARD_COUNT; i++) {
      uint16_t j = i % 12;
      if (((i / 12) & 1)) {
        j = 11 - j;
      }

      if (val>=scale[j]) {
        colors[i] = hue2rgb[hue];
      } else {
        colors[i] = rgb_color(0, 0, 0);
      }

      hue += 360 / LED_BOARD_COUNT;
      if (hue>=360) {
        hue -= 360;
      }
    }
    ledBoard.write(colors, LED_BOARD_COUNT, LED_BOARD_BRIGHTNESS);

    // Update
    time += 2;
    return DO_NOTHING;

  } else if (event == WIPHONE_KEY_END) {

    return EXIT_APP;

  } else if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {

    nextFocus(event == WIPHONE_KEY_DOWN);
    return REDRAW_SCREEN;

  } else if (IS_KEYBOARD(event)) {

    // Pass event to a focused widget
    ((GUIWidget*) getFocused())->processEvent(event);
    takeInputs();
    return REDRAW_SCREEN;

  }

  return DO_NOTHING;
}

void LedMicApp::redrawScreen(bool redrawAll) {
  if (!screenInited || redrawAll) {
    ((GUIWidget*) bgRect)->redraw(lcd);
    // Draw labels
    for(uint16_t i=0; i<sizeof(labels)/sizeof(LabelWidget*); i++) {
      ((GUIWidget*) labels[i])->redraw(lcd);
    }
  }

  // Redraw input widgets
  for(uint16_t i=0; i<sizeof(inputs)/sizeof(TextInputWidget*); i++) {
    ((GUIWidget*) inputs[i])->redraw(lcd);
  }

  screenInited = true;
}

#endif // LED_BOARD

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  GUI WIDGETS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

void GUIWidget::clear(LCD &lcd, uint16_t col) {
  corrRect(lcd, parentOffX, parentOffY, widgetWidth, widgetHeight, col);
}

void GUIWidget::corrRect(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight, uint16_t color) {
  lcd.fillRect(screenOffX, screenOffY,
               windowWidth + (lcd.width() == windowWidth ? 1 : 0),
               windowHeight + (lcd.height() == windowHeight ? 1 : 0),
               color);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  RectWidget - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RectWidget::RectWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, uint16_t color)
  : NonFocusableWidget(posX, posY, width, height), color(color) {
}

void RectWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  this->corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, color);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  RectIconWidget - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RectIconWidget::RectIconWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, uint16_t color, const uint8_t* iconData, const size_t iconSize)
  : RectWidget(posX, posY, width, height, color) {
  if (iconData) {
    icon = new IconRle3(iconData, iconSize);
  }
}

RectIconWidget::~RectIconWidget() {
  if (icon) {
    delete icon;
  }
}

void RectIconWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  this->corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, color);
  if (icon) {
    lcd.drawImage(*icon, screenOffX + (windowWidth - icon->width())/2, screenOffY + (windowHeight - icon->height())/2);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  RulerWidget - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RulerWidget::RulerWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t color)
  : NonFocusableWidget(posX, posY, width, 1), color(color) {
}

void RulerWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  this->corrRect(lcd, screenOffX, screenOffY, windowWidth, 1, color);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  LabelWidget - - - - - - - - - - - - - - - - - - - - - - - - - - - -

LabelWidget::LabelWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, const char* str,
                         uint16_t col, uint16_t bg, SmoothFont* font, TextDirection_t orient, uint16_t xPadding)
  : NonFocusableWidget(posX, posY, width, height), widgetFont(font), textColor(col), bgColor(bg), textDirection(orient), xPadding(xPadding) {
  //log_d("create LabelWidget");

  textDyn = strdup(str);
  updated = true;
  if (font==NULL) {
    widgetFont = fonts[OPENSANS_COND_BOLD_20];
  }
};

LabelWidget::~LabelWidget() {
  freeNull((void **) &textDyn);
}

void LabelWidget::setText(const char* str) {
  freeNull((void **) &textDyn);
  if (str!=NULL) {
    textDyn = strdup(str);
  } else {
    textDyn = strdup("");
  }
  updated = true;
}

void LabelWidget::setColors(colorType text, colorType bg) {
  textColor = text;
  bgColor = bg;
  updated = true;
}

void LabelWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  uint16_t off = 0;

  // Redraw background
  this->clear(lcd, bgColor);      // TODO: can be optimized to avoid double redrawing

  // Draw actual text
  lcd.setTextColor(textColor, bgColor);
  lcd.setTextFont(widgetFont);
  // TODO: change text direction to datum
  if (textDirection==0) {   // left to right
    lcd.setTextDatum(ML_DATUM);
    lcd.drawFitString(textDyn, windowWidth-(xPadding*2), screenOffX+xPadding, screenOffY+windowHeight/2);
  } else if (textDirection==1) {      // right to left
    lcd.setTextDatum(MR_DATUM);
    lcd.drawFitString(textDyn, windowWidth-(xPadding*2), screenOffX+windowWidth-xPadding, screenOffY+windowHeight/2);
  } else if (textDirection==2) {      // center
    lcd.setTextDatum(MC_DATUM);
    lcd.drawFitString(textDyn, windowWidth-(xPadding*2), screenOffX + windowWidth/2, screenOffY+windowHeight/2);
  }
  updated = false;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  ChoiceWidget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ChoiceWidget::ChoiceWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, SmoothFont* font,
                           colorType textColor, colorType bgColor, colorType regColor, colorType selColor)
  : FocusableWidget(posX, posY, width, height), textColor(textColor), bgColor(bgColor), regColor(regColor), selColor(selColor) {
  widgetFont = font ? font : fonts[OPENSANS_COND_BOLD_20];
}

ChoiceWidget::~ChoiceWidget() {
  for (auto it = choices.iterator(); it.valid(); ++it) {
    free(*it);
  }
  choices.clear();
}

void ChoiceWidget::addChoice(const char* name) {
  bool succ = false;
  char* copy = extStrdup(name);
  if (copy != NULL && choices.add(copy)) {
    this->setValue(choices.size()-1);
    succ = true;
  }
  if (!succ) {
    log_e("failed to add a choice");
  }
}

void ChoiceWidget::setValue(ChoiceValue val) {
  if (this->curChoice != val) {
    this->curChoice = val;
    this->updated = true;
  }
}

bool ChoiceWidget::processEvent(EventType event) {
  log_e("choicewidget pointer this: %p", this);
  if (!this->choices.size()) {
    log_e("MESUT %d", __LINE__);
    return false;  // no choices
  }
  if (event == WIPHONE_KEY_LEFT || event == WIPHONE_KEY_RIGHT) {
    if (event == WIPHONE_KEY_LEFT) {
      if (this->curChoice) {
        this->curChoice--;
      } else {
        this->curChoice = this->choices.size()-1;
      }
      log_e("MESUT %d choice: %d", __LINE__, this->curChoice);
    } else {
      this->curChoice++;
      if (this->curChoice >= this->choices.size()) {
        this->curChoice = 0;
      }
      log_e("MESUT %d choice: %d", __LINE__, this->curChoice);
    }
    return (this->updated = true);
  }
  return false;
}

void ChoiceWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  //log_d("redraw: at %d,%d, focuced = %d, color = %x", screenOffX, screenOffY, this->getFocus(), this->getFocus() ? selColor : regColor);

  this->corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, bgColor);
  auto th = widgetFont->height();
  auto cy = screenOffY + windowHeight/2;

  // Left arrow
  lcd.fillTriangle(screenOffX, cy,
                   screenOffX + arrowWidth, cy - th/2,
                   screenOffX + arrowWidth, cy + th/2 + 1,
                   this->getFocus() ? selColor : regColor);

  // Right arrow
  lcd.fillTriangle(screenOffX + windowWidth - 1, cy,
                   screenOffX + windowWidth - 1 - arrowWidth, cy - th/2,
                   screenOffX + windowWidth - 1 - arrowWidth, cy + th/2 + 1,
                   this->getFocus() ? selColor : regColor);

  // Draw text for the current choice
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextFont(this->widgetFont);
  lcd.setTextColor(this->textColor, this->bgColor);
  lcd.drawFitString(this->choices[this->curChoice], windowWidth - 2*arrowWidth - 2, screenOffX + windowWidth/2, cy);

}

YesNoWidget::YesNoWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, SmoothFont* font,
                         colorType textColor, colorType bgColor, colorType regularColor, colorType selectedColor)
  : ChoiceWidget(posX, posY, width, height, font, textColor, bgColor, regularColor, selectedColor) {
  this->addChoice("No");
  this->addChoice("Yes");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  TextInputAbstract  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

TextInputAbstract::TextInputAbstract(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                                     ControlState& state,
                                     SmoothFont* font, uint32_t maxInputSize, InputType typ)
  : FocusableWidget(xPos, yPos, width, height), maxInputSize(maxInputSize), controlState(state), inputType(typ) {
  widgetFont = font ? font : fonts[OPENSANS_COND_BOLD_20];
}

void TextInputAbstract::setFocus(bool focus) {
  controlState.setInputState(inputType);
  this->focused = focus;
  this->updated = true;
};

void TextInputAbstract::drawCursor(LCD &lcd, uint16_t posX, uint16_t posY, uint16_t charHeight, uint16_t color) {
  lcd.drawLine(posX, posY, posX, posY+charHeight-1, color);
}

void TextInputAbstract::setColors(colorType fg, colorType bg) {
  fgColor = fg;
  bgColor = bg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  TextInputBase - - - - - - - - - - - - - - - - - - - - - - - - - - - -

TextInputBase::TextInputBase(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                             ControlState& state,
                             SmoothFont* font, uint32_t maxInputSize, InputType typ)
  : TextInputAbstract(xPos, yPos, width, height, state, font, maxInputSize, typ) {
  inputStringDyn = NULL;
  inputStringSize = 0;      // allocated size (bytes)
  textOffset = 0;
  cursorOffset = 0;
}

// Return: true if space was allocated in FULL extent
bool TextInputBase::allocateMore(uint32_t minSize) {    // bytes
  log_v("allocMore TextInputBase: %d", minSize);
  uint32_t sz = 2*inputStringSize;        // double the space by default
  if (!sz || minSize>sz) {
    sz = minSize;  // increase if still not enough
  }
  if (!sz) {
    sz = 8;  // minimum number of bytes to allocate for any TextInput
  }
  if (sz > maxInputSize) {
    sz = maxInputSize;  // do no go over the limit
  }
  if (sz > inputStringSize) {
    //log_d("realloc %d", sz);
    char *p = (char*) realloc(inputStringDyn, sz);
    if (p!=NULL) {
      //log_d("realloc done %d", sz);
      inputStringDyn = p;
      inputStringSize = sz;
      if (inputStringSize>=minSize) {
        return true;  // enough space allocated to cover the request (and at least one byte more)
      }
    }
  }
  return false;
}

void TextInputBase::setText(const char* str) {
  log_v("setText TextInputBase");
  if (str != NULL) {
    uint32_t len = strlen(str);
    if (len+1<=inputStringSize || allocateMore(len+1)) {
      // string can fit into inputStringDyn
      memcpy(inputStringDyn, str, len+1);
      cursorOffset = len;
      this->updated = true;
    } else if (inputStringSize>0) {
      // string is too big for inputStringDyn
      memcpy(inputStringDyn, str, inputStringSize-1);
      inputStringDyn[inputStringSize-1] = '\0';
      cursorOffset = inputStringSize-1;
      this->updated = true;
    }
  }
}

TextInputBase::~TextInputBase() {
  freeNull((void **) &inputStringDyn);
}

bool TextInputBase::getInt(int32_t &i) {
  const char* text = this->getText();
  char *endptr = NULL;
  errno = 0;
  i = strtol(text, &endptr, 10);
  if (!*text || !endptr || *endptr || errno) {
    return false;
  }
  return true;
}

void TextInputBase::setInt(int32_t i) {
  char s[12];
  sprintf(s, "%d", i);
  this->setText(s);
}
// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Multiline text input widget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MultilineTextWidget::MultilineTextWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
    const char* emptyText, ControlState& state,
    uint32_t maxInputSize, SmoothFont* font, InputType typ, uint16_t xPadding, uint16_t yPadding)
  : TextInputAbstract(xPos, yPos, width, height, state, font, maxInputSize, typ),
    xPadding(xPadding), yPadding(yPadding) {
  rowsDyn = NULL;
  retTextDyn = NULL;
  emptyTextDyn = emptyText ? strdup(emptyText) : NULL;

  firstVisibleRow = 0;
  visibleRows = (height-(yPadding*2)) / font->height();

  cursRow = 0;
  maxRows = 0;
  cursOffset = 0;

  allocateMore(10);
}

MultilineTextWidget::~MultilineTextWidget() {
  freeNull((void **) &rowsDyn);
  freeNull((void **) &retTextDyn);
  freeNull((void **) &emptyTextDyn);
}

// Return: true if space was allocated in *full* extent
bool MultilineTextWidget::allocateMore(int minSize) {    // items
  log_i("allocMore MultilineTextWidget: cur rows=%d, min rows=%d", maxRows, minSize);

  // Determine the size to be allocated
  uint32_t rows = 2*maxRows;                      // double the current space by default
  if (!rows || minSize>rows) {
    rows = minSize;  // increase if still not enough
  }
  if (!rows) {
    rows = 1;  // minimum number of rows is 1
  }
  if (rows > maxInputSize) {
    rows = maxInputSize;  // no need for more rows than maximum amount of characters
  }

  // Reallocate (if still makes sense)
  if (rows > maxRows) {
    log_v("realloc, rows=%d", rows);
    char **p = (char**) realloc(rowsDyn, rows*sizeof(char*));
    if (p!=NULL) {
      memset(p + maxRows, 0, sizeof(char*)*(rows - maxRows));
      log_v("inited, new rows=%d", rows - maxRows);
      rowsDyn = p;
      maxRows = rows;
      if (maxRows>=minSize) {
        return true;  // enough space allocated to cover the request (and at least one byte more)
      }
    }
  } else if (rows == maxInputSize) {
    log_i("failed to allocate: max size reached");
  }
  return false;
}

void MultilineTextWidget::appendText(const char* str) {
  const char* t = getText();
  int ntextLen = strlen(str);
  int otextLen = strlen(t);
  int bufferLen = ntextLen + otextLen + 1;

  char* buffer = (char*)malloc(bufferLen);
  memset(buffer, 0x00, bufferLen);

  strlcpy(buffer, t, bufferLen);
  strlcat(buffer, str, bufferLen);
  setText(buffer);
  free(buffer);
}

void MultilineTextWidget::setText(const char* str) {
  log_i("MultilineTextWidget::setText");
  log_v("Text: \"%s\"", str);
  const char* p = str;
  const char* e = str + strlen(str);

  // Reset memory of all the rows
  for (int i=0; i<maxRows; i++) {
    freeNull((void **) &rowsDyn[i]);
  }

  // Fitting text
  cursRow = -1;
  const uint16_t horizontalSpace = widgetWidth - 2*xPadding;
  uint16_t fit = 0;
  while (p < e) {
    cursRow++;
    if (cursRow>=maxRows) {
      allocateMore();
    }
    fit = widgetFont->fitWordsLength(p, horizontalSpace);
    if (fit) {
      rowsDyn[cursRow] = strndup(p, fit);
      //log_i("fitted: %s (len=%d) on line %d", rowsDyn[cursRow], fit, cursRow);
      p += fit;
    } else {
      log_e("could not fit text");      // TODO: break a word into head and tail
      break;
    }
  }
  // Was the last character a new line? (special case)
  if (cursRow>=0 && fit>0 && rowsDyn[cursRow][fit-1]=='\n') {
    // Set cursor to beginning of the next row
    cursRow++;
    cursOffset = 0;
  } else if (cursRow>=0) {
    // Set cursor to end of the string
    cursOffset = strlen(rowsDyn[cursRow]);
  } else if (cursRow<0) {
    // Special case for no text
    cursRow = 0;
    if (cursRow>=maxRows) {
      allocateMore();
    }
    rowsDyn[cursRow] = strdup("");
  }
  revealCursor();
  this->updated = true;
}

void MultilineTextWidget::cursorToStart() {
  cursRow = 0;
  cursOffset = 0;
  revealCursor();
}

void MultilineTextWidget::revealCursor() {
  if (cursRow >=0 && cursRow < firstVisibleRow) {
    firstVisibleRow = cursRow;
  } else if (cursRow >= firstVisibleRow + visibleRows) {
    firstVisibleRow = cursRow - visibleRows + 1;
  }
}

const char* MultilineTextWidget::getText() {
  // Merge all lines into a single dynamic variable
  // - calculate final length
  int len = 0, i;
  for (i=0; i<maxRows; i++) {
    if (rowsDyn[i] && rowsDyn[i][0]) {
      len += strlen(rowsDyn[i]);
    }
  }
  // - reallocate memory
  freeNull((void **) &retTextDyn);
  retTextDyn = (char*) malloc(len+1);
  if (retTextDyn) {
    // - merge the contents
    int l = 0;
    if (len) {
      for (i=0; i<maxRows; i++) {
        if (rowsDyn[i] && rowsDyn[i][0]) {
          uint16_t lr = strlen(rowsDyn[i]);
          memcpy(retTextDyn + l, rowsDyn[i], lr);
          l += lr;
        }
      }
    }
    retTextDyn[l] = '\0';
    return retTextDyn;
  } else {
    // - memory allocation failed
    return NULL;
  }
}

bool MultilineTextWidget::processEvent(EventType event) {
  // TODO: add this->updated = true; where relevant (for efficient redrawing)
  log_i("processEvent MultilineTextWidget");

  if (event>=32 && event<=126) {      // printable ASCII character (TODO: allow newline and maybe tab)

    // Check maximum length
    int len = 0;
    for (int i=0; i<this->maxRows; i++) {
      if (this->rowsDyn[i] && this->rowsDyn[i][0]) {
        len += strlen(rowsDyn[i]);
      }
    }
    if (len>=maxInputSize) {
      log_d("character limit reached");
      return false;
    }

    // Insert character
    log_d("insert character: %d, row: %d", event, cursRow);

    // Insert character in current row
    len = rowsDyn[cursRow] && rowsDyn[cursRow][0] ? strlen(rowsDyn[cursRow]) : 0;
    char* p = (char*) realloc(rowsDyn[cursRow], len+2);
    if (p) {
      rowsDyn[cursRow] = p;
      for (int i=len; i>cursOffset; i--) {
        p[i] = p[i-1];
      }
      p[len+1] = '\0';
      p[cursOffset] = event;     // new character
      cursOffset++;
      len++;
    }

    // Split if needed
    int row = cursRow;
    uint16_t horizontalSpace = widgetWidth - 2*xPadding;
    do {
      uint16_t fit = widgetFont->fitWordsLength(rowsDyn[row], horizontalSpace);
      if (fit<len) {

        // Next row size
        uint16_t len2 = (row+1 < maxRows && rowsDyn[row+1] && rowsDyn[row+1][0]) ? strlen(rowsDyn[row+1]) : 0;

        log_d("added: %s", rowsDyn[row]);
        if (len2) {
          log_d("next:  %s", rowsDyn[row+1]);
        }

        // Allocate memory for new row if needed
        if (!len2 && row+1 >= maxRows)
          if (!allocateMore())
            if (!allocateMore(maxRows+1)) {
              break;  // could not allocate more memory for one more row
            }

        // Allocate memory for new strings
        char* s1 = (char*) malloc(fit + 1);
        char* s2 = (char*) malloc(len-fit + len2 + 1);
        if (s1 && s2) {
          // String with removed characters from the end
          memcpy(s1, rowsDyn[row], fit);
          s1[fit] = '\0';
          log_d("s1: %s (%d)", s1, fit);

          // Added characters to beginning
          memcpy(s2, rowsDyn[row]+fit, len-fit);
          if (len2) {
            memcpy(s2+len-fit, rowsDyn[row+1], len2);
          }
          s2[len-fit+len2] = '\0';
          log_d("s2: %s (%d)", s2, len-fit+len2);
          log_d("offset = %d", cursOffset);

          // Forget old strings
          free(rowsDyn[row]);
          freeNull((void **) &rowsDyn[row+1]);

          // New strings
          rowsDyn[row] = s1;
          rowsDyn[row+1] = s2;

          // Fix cursor position
          if (row==cursRow && cursOffset>fit) {
            cursRow++;
            cursOffset -= fit;
          }

          // Proceed to next row
          row++;
          len = len-fit+len2;

        } else {
          // Error: could not allocate memory for string manipulation
          freeNull((void **) &s1);
          freeNull((void **) &s2);
          break;
        }
      } else {
        break;  // all characters fit
      }
    } while (1);

    revealCursor();
    return true;

  } else if (event == WIPHONE_KEY_BACK) {

    // Backspace ("Clear")

    // Remove one character
    if (cursOffset>0) {
      // Remove character from this row
      if (rowsDyn[cursRow]) {
        int len = strlen(rowsDyn[cursRow]);
        for (int i=cursOffset; i<=len; i++) {
          rowsDyn[cursRow][i-1] = rowsDyn[cursRow][i];
        }
      }
      cursOffset--;
    } else if (cursRow>0) {
      // Remove character from previous row
      uint16_t l = strlen(rowsDyn[cursRow-1]);
      if (l) {
        rowsDyn[cursRow-1][l-1] = '\0';
      }
      log_d("cursor = %d, %d, len = %d, string = \"%s\"", cursRow, cursOffset, l, rowsDyn[cursRow-1]);
      cursRow -= 1;
      cursOffset = l-1;
      log_d("cursor = %d, %d", cursRow, cursOffset);
    }

    // Realign: try to fit more characters into current row
    int row = cursRow;
    uint16_t horizontalSpace = widgetWidth - 2*xPadding;
    while (row+1 < maxRows && rowsDyn[row+1] && rowsDyn[row+1][0]) {
      // Merge current row with next one, then split it in two until no changes needed

      // - merge two rows into one
      uint16_t l1 = strlen(rowsDyn[row]);
      uint16_t l2 = strlen(rowsDyn[row+1]);
      char* dyn = (char*) malloc(l1 + l2 + 1);
      if (dyn) {
        memcpy(dyn, rowsDyn[row], l1);
        memcpy(dyn+l1, rowsDyn[row+1], l2+1);
      } else {
        break;      // merging failed: quit silently
      }
      log_d("merged string: %s", dyn);

      // - break the merged string in two
      bool quit = false;
      uint16_t fit = widgetFont->fitWordsLength(dyn, horizontalSpace);
      if (fit != l1) {
        char* s2 = strdup(dyn+fit);
        dyn[fit] = '\0';
        char* s1 = strdup(dyn);
        if (s1 && s2) {
          // - replace original two rows with new ones
          free(rowsDyn[row]);
          free(rowsDyn[row+1]);
          rowsDyn[row] = s1;
          rowsDyn[row+1] = s2;
          log_d("new break up: %s / %s", s1, s2);
        }
        // - fix offset
        if (row==cursRow && fit < cursOffset) {
          log_d("cursor = %d, %d", cursRow, cursOffset);
          cursRow++;
          cursOffset -= fit;
          log_d("cursor = %d, %d", cursRow, cursOffset);
        }
        // - proceed realignment to next row
        row++;
      } else {
        quit = true;
      }

      free(dyn);
      if (quit) {
        break;
      }
    }
    revealCursor();
    return true;

  } else if (event == WIPHONE_KEY_UP || event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_LEFT || event == WIPHONE_KEY_RIGHT) {

    // Cursor movements

    if (event == WIPHONE_KEY_UP) {
      if (cursRow>0) {
        cursRow--;
        if (cursOffset>strlen(rowsDyn[cursRow])) {
          cursOffset = strlen(rowsDyn[cursRow]);  // TODO: remember from what column the cursor started moving
        }
        revealCursor();
      }
    } else if (event == WIPHONE_KEY_DOWN) {
      if (cursRow < maxRows-1 && (notEmptyRow(cursRow+1) || newLineRow(cursRow))) {
        cursRow++;
        if (emptyRow(cursRow)) {
          cursOffset = 0;
        } else if (cursOffset>strlen(rowsDyn[cursRow])) {
          cursOffset = strlen(rowsDyn[cursRow]);
        }
        revealCursor();
      }
    } else if (event == WIPHONE_KEY_LEFT) {
      // TODO: cycle to the end
      if (cursOffset>0) {
        cursOffset--;
      } else if (cursRow>0) {
        cursRow--;
        cursOffset = strlen(rowsDyn[cursRow]);
        revealCursor();
      }
    } else if (event == WIPHONE_KEY_RIGHT) {
      // TODO: cycle to the beginning
      log_d("cursor right");
      if (notEmptyRow(cursRow)) {
        // TODO: this can be optimized
        if (cursOffset<strlen(rowsDyn[cursRow]) + (newLineRow(cursRow) ? -1 : 0)) {
          log_d("next character");
          // Next character in current row
          cursOffset++;
        } else if (notEmptyRow(cursRow+1) || newLineRow(cursRow)) {
          log_d("next line");
          // Proceed to next row
          cursRow++;
          cursOffset = 0;
          revealCursor();
        }
      }
    }

    // Always set cursor before the newline character at the end of row
    if (newLineRow(cursRow) && cursOffset==strlen(rowsDyn[cursRow])) {
      cursOffset--;
    }

    return true;
  }
  return false;
}

void MultilineTextWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  log_i("redraw MultilineTextWidget");

  if (windowWidth != widgetWidth || windowHeight != widgetHeight) {
    return;  // this widget can only be rendered in full
  }
  lcd.fillRect(screenOffX, screenOffY, windowWidth, windowHeight, bgColor);

  uint16_t yOff = yPadding;
  lcd.setTextFont(widgetFont);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextColor(fgColor, bgColor);

  // Vertical centering
  if (centering) {
    // Count actual rows present
    int actualRows = 0;
    for (int i=firstVisibleRow; i<firstVisibleRow + visibleRows; i++) {
      if (!rowsDyn[i]) {
        break;
      }
      actualRows++;
    }
    // Add offset
    uint16_t freeSpace = (windowHeight - (yPadding*2)) - widgetFont->height()*actualRows;
    yOff += (freeSpace/2);
  }

  // Draw actual lines
  bool anyText = false;
  for (int i=firstVisibleRow; i<firstVisibleRow + visibleRows; i++) {
    // Draw text of each line
    if (i<maxRows) {
      if (rowsDyn[i] && rowsDyn[i][0]) {
        anyText = true;

        // Draw string without trailing newline
        int len = strlen(rowsDyn[i]);
        if (len && rowsDyn[i][len-1] != '\n') {
          lcd.drawString(rowsDyn[i], screenOffX + xPadding, screenOffY + yOff);
        } else {
          char *dup = strdup(rowsDyn[i]);
          dup[len-1] = '\0';
          lcd.drawString(dup, screenOffX + xPadding, screenOffY + yOff);
          free(dup);
        }
      }
    }

    // Draw cursor
    if (this->focused && cursRow == i) {
      uint16_t  curPosX = 0;
      if (i < maxRows && rowsDyn[i] && rowsDyn[i][0]) {
        // If there is text in this row -> calculate cursor offset
        if (cursOffset < strlen(rowsDyn[i])) {
          char* dup = strdup(rowsDyn[i]);
          dup[cursOffset] = 0;
          curPosX = lcd.textWidth(dup);
          free(dup);
        } else {
          curPosX = lcd.textWidth(rowsDyn[i]);
        }
      }
      drawCursor(lcd, screenOffX + xPadding + curPosX, screenOffY + yOff, widgetFont->height(), WP_COLOR_0);
    }

    yOff += widgetFont->height();
  }

  // Empty?
  if (!anyText && emptyTextDyn != NULL) {
    yOff = yPadding;
    for (int i=0; i<maxRows; i++) {
      if (rowsDyn[i] && rowsDyn[i][0]) {
        anyText = true;
        break;
      }
    }
    if (!anyText) {
      lcd.setTextColor(GRAY_50, bgColor);
      lcd.drawString(emptyTextDyn, screenOffX + xPadding + 1, screenOffY + yOff);
    }
  }

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  TextInputWidget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

TextInputWidget::TextInputWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                                 ControlState& state,
                                 uint32_t maxInputSize, SmoothFont* font, InputType typ, uint16_t sidePadding)
  : TextInputBase(xPos, yPos, width, height, state, font,  maxInputSize, typ), xPad(sidePadding) {
  //log_i("create TextInputWidget");
  focused = false;
}

void TextInputWidget::shiftCursor(int16_t shift) {
  log_d("cursor offset: %d", cursorOffset);
  if (shift < 0) {
    if (cursorOffset >= -shift) {
      cursorOffset += shift;
      this->updated = true;
    }
    //else
    //  cursorOffset = (inputStringDyn!=NULL) ? strlen(inputStringDyn) : 0;     // cycle to the end
  } else if (shift > 0) {
    uint16_t len = (inputStringDyn!=NULL) ? strlen(inputStringDyn) : 0;
    if (cursorOffset+shift<=len) {
      cursorOffset += shift;
      this->updated = true;
    }
  }
}

bool TextInputWidget::insertCharacter(char c) {
  log_d("insertCharacter TextInputWidget");
  if (inputStringDyn==NULL) {
    allocateMore();
    inputStringDyn[0] = '\0';
  }
  uint32_t len = strlen(inputStringDyn);
  if (len+1>=inputStringSize) {
    allocateMore();
  }
  if (len+1<inputStringSize) {
    // Free space
    for (uint32_t i=len+1; i>0 && i>cursorOffset; i--) {
      inputStringDyn[i] = inputStringDyn[i-1];
    }

    // Insert character
    inputStringDyn[cursorOffset] = c;

    this->updated = true;
    return true;
  }
  return false;
}

bool TextInputWidget::processEvent(EventType event) {
  log_d("processEvent TextInputWidget");
  if (event>=32 && event<=126) {
    if (this->insertCharacter(event)) {
      cursorOffset++;
    }
    return true;
  } else if (event==WIPHONE_KEY_LEFT) {
    this->shiftCursor(-1);
    return true;
  } else if (event==WIPHONE_KEY_RIGHT) {
    this->shiftCursor(1);
    return true;
  } else if (event==WIPHONE_KEY_BACK) {
    // Backspace
    if (cursorOffset>0) {
      uint32_t len = strlen(inputStringDyn);
      for(int i=cursorOffset; i<=len; i++) {
        inputStringDyn[i-1] = inputStringDyn[i];
      }
      cursorOffset--;
      this->updated = true;
    }
    return true;
  }
  return false;
}

void TextInputWidget::revealCursor() {
  log_d("revealCursor TextInputWidget");
  uint16_t len = inputStringDyn != NULL ? strlen(inputStringDyn) : 0;
  if (len>0) {
    uint16_t padding = (xPad*2);

    // Ensure the text offset is not past the last character
    if (textOffset>=len) {
      textOffset = len>0 ? len-widgetFont->fitTextLength(inputStringDyn, widgetWidth-padding, -1) : 0;
    }

    if (cursorOffset<textOffset) {
      // Cursor is to the left from the visible text
      uint16_t shift = textOffset - cursorOffset;
      textOffset -= shift;
    } else {
      // Reveal cursor if it is to the right from the visible area
      if (cursorOffset > len) {
        cursorOffset = len;  // Sanity check
      }
      int visibleChars = widgetFont->fitTextLength(inputStringDyn + textOffset, widgetWidth-padding);
      if (cursorOffset > textOffset + visibleChars) {
        log_d("revealing cursor on the right");
        char* dup = strdup(inputStringDyn + textOffset);
        if (dup) {
          log_d("dup: %s", dup);
          dup[cursorOffset-textOffset] = '\0';
          log_d("dup: %s", dup);
          int fit = widgetFont->fitTextLength(dup, widgetWidth-padding, -1);
          textOffset += cursorOffset-textOffset-fit;
          free(dup);
        }
      }
    }
  } else {
    textOffset = cursorOffset = 0;
  }
}

void TextInputWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  log_d("redraw TextInputWidget ", (uint8_t) focused);
  if (windowWidth != widgetWidth || windowHeight != widgetHeight) {
    return;  // this widget can only be rendered in full
  }

  // Draw background
  this->corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, bgColor);      // TODO: enabled color?

  // Draw text
  revealCursor();
  int len=(inputStringDyn!=NULL) ? strlen(inputStringDyn) : 0;
  if (len>0) {
    lcd.setTextFont(widgetFont);
    lcd.setTextColor(fgColor, bgColor);
    lcd.setTextDatum(ML_DATUM);
    int fit = lcd.drawFitString(inputStringDyn + textOffset, windowWidth-(xPad*2), screenOffX + xPad, screenOffY + windowHeight/2);

    // Draw cursor
    if (this->focused) {
      uint16_t  curPosX = 0;
      char* dup = strdup(inputStringDyn + textOffset);
      if (fit<=strlen(dup)) {   // sanity check
        dup[fit] = '\0';
        if (cursorOffset - textOffset <=  fit) {
          dup[cursorOffset - textOffset] = '\0';
          curPosX = lcd.textWidth(dup);
          drawCursor(lcd, screenOffX + curPosX + xPad, screenOffY + (windowHeight-widgetFont->height())/2, widgetFont->height(), WP_COLOR_0);
        }
      }
      free(dup);
    }
  } else {
    if (focused) {
      drawCursor(lcd, screenOffX+xPad+1, screenOffY+(windowHeight-widgetFont->height())/2, widgetFont->height(), WP_COLOR_0);
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  PasswordInputWidget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PasswordInputWidget::PasswordInputWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
    ControlState& state,
    uint32_t maxInputSize, SmoothFont* font, InputType typ, uint16_t sidePadding)
  : TextInputBase(xPos, yPos, width, height, state, font,  maxInputSize, typ), xPad(sidePadding) {
  outputStringDyn = nullptr;
  focused = false;
}

PasswordInputWidget::~PasswordInputWidget() {
  freeNull((void **) &outputStringDyn);
}

// Return: true if space was allocated in FULL extent
bool PasswordInputWidget::allocateMore(uint32_t minSize) {    // bytes
  log_d("allocMore PasswordInputWidget: %d", minSize);
  uint32_t sz = 2*inputStringSize;        // double the space by default
  if (!sz || minSize>sz) {
    sz = minSize;  // increase if still not enough
  }
  if (!sz) {
    sz = 8;  // minimum number of bytes to allocate for any TextInput
  }
  if (sz > maxInputSize) {
    sz = maxInputSize;  // do no go over the limit
  }
  if (sz > inputStringSize) {
    //log_d("realloc %d", sz);
    char *p = (char*) realloc(inputStringDyn, sz);
    char *p2 = (char*) realloc(outputStringDyn, sz);
    if (p!=NULL && p2!=NULL) {
      //log_d("realloc done %d", sz);
      inputStringDyn = p;
      outputStringDyn = p2;
      inputStringSize = sz;
      if (inputStringSize>=minSize) {
        return true;  // enough space allocated to cover the request (and at least one byte more)
      }
    }
  }
  return false;
}

void PasswordInputWidget::setText(const char* str) {
  log_d("setText PasswordInputWidget");
  if (str != NULL) {
    uint32_t len = strlen(str);
    if (len+1<=inputStringSize || allocateMore(len+1)) {
      // string can fit into inputStringDyn
      memcpy(inputStringDyn, str, len+1);
      memset(outputStringDyn, '*', len);
      outputStringDyn[len] = '\0';
      cursorOffset = len;
      this->updated = true;
    } else if (inputStringSize>0) {
      // string is too big for inputStringDyn
      memcpy(inputStringDyn, str, inputStringSize-1);
      memset(outputStringDyn, '*', inputStringSize-1);
      inputStringDyn[inputStringSize-1] = '\0';
      outputStringDyn[inputStringSize-1] = '\0';
      cursorOffset = inputStringSize-1;
      this->updated = true;
    }
  }
}

void PasswordInputWidget::shiftCursor(int16_t shift) {
  if (shift < 0) {
    if (cursorOffset >= -shift) {
      cursorOffset += shift;
      this->updated = true;
    }
  } else if (shift > 0) {
    uint16_t len = (inputStringDyn!=NULL) ? strlen(inputStringDyn) : 0;
    if (cursorOffset+shift<=len) {
      cursorOffset += shift;
      this->updated = true;
    }
  }
}

bool PasswordInputWidget::insertCharacter(char c) {
  if (inputStringDyn==NULL) {
    allocateMore();
    inputStringDyn[0] = '\0';
  }
  uint32_t len = strlen(inputStringDyn);
  if (len+1>=inputStringSize) {
    allocateMore();
  }
  if (len+1<inputStringSize) {
    // Shift the characters to the right freeing space for one more character
    for (uint32_t i=len+1; i>0 && i>cursorOffset; i--) {
      inputStringDyn[i] = inputStringDyn[i-1];
    }

    // Insert character
    inputStringDyn[cursorOffset] = c;

    // Just add one more asterisk
    outputStringDyn[len] = '*';
    outputStringDyn[len+1] = '\0';

    this->updated = true;
    return true;
  }
  return false;
}

bool PasswordInputWidget::processEvent(EventType event) {
  if (event>=32 && event<=126) {
    if (insertCharacter(event)) {
      cursorOffset++;
    }
    return true;
  } else if (event==WIPHONE_KEY_LEFT) {
    shiftCursor(-1);
    return true;
  } else if (event==WIPHONE_KEY_RIGHT) {
    shiftCursor(1);
    return true;
  } else if (event==WIPHONE_KEY_BACK) {
    // Backspace
    if (cursorOffset>0) {
      // Remove one character, shifting the following ones to the left
      uint32_t len = strlen(inputStringDyn);
      for(int i=cursorOffset; i<=len; i++) {
        inputStringDyn[i-1] = inputStringDyn[i];
      }
      cursorOffset--;
      // Remove one asterisk
      if (len>0) {
        outputStringDyn[len-1] = '\0';
      }
      this->updated = true;
    }
    return true;
  }
  return false;
}

void PasswordInputWidget::revealCursor() {
  uint16_t len = outputStringDyn != NULL ? strlen(outputStringDyn) : 0;
  if (len>0) {
    uint16_t padding = (xPad*2);

    // Ensure the text offset is not past the last character
    if (textOffset>=len) {
      textOffset = len>0 ? len - widgetFont->fitTextLength(outputStringDyn, widgetWidth-padding, -1) : 0;
    }

    if (cursorOffset<textOffset) {
      // Cursor is to the left from the visible text
      uint16_t shift = textOffset - cursorOffset;
      textOffset -= shift;
    } else {
      // Reveal cursor if it is to the right from the visible area
      if (cursorOffset > len) {
        cursorOffset = len;  // Sanity check
      }
      int visibleChars = widgetFont->fitTextLength(outputStringDyn + textOffset, widgetWidth-padding);
      if (cursorOffset > textOffset + visibleChars) {
        log_d("revealing cursor on the right");
        char* dup = strdup(outputStringDyn + textOffset);
        if (dup) {
          log_d("dup: %s", dup);
          dup[cursorOffset-textOffset] = '\0';
          log_d("dup: %s", dup);
          int fit = widgetFont->fitTextLength(dup, widgetWidth-padding, -1);
          textOffset += cursorOffset-textOffset-fit;
          free(dup);
        }
      }
    }
  } else {
    textOffset = cursorOffset = 0;
  }
}

void PasswordInputWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  if (windowWidth != widgetWidth || windowHeight != widgetHeight) {
    return;  // this widget can only be rendered in full
  }

  // Draw background
  this->corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, bgColor);      // TODO: enabled color?

  // Draw text
  revealCursor();
  int len=(outputStringDyn!=NULL) ? strlen(outputStringDyn) : 0;
  if (len>0) {
    lcd.setTextFont(widgetFont);
    lcd.setTextColor(fgColor, bgColor);
    lcd.setTextDatum(ML_DATUM);
    int fit = lcd.drawFitString(outputStringDyn + textOffset, windowWidth-(xPad*2), screenOffX + xPad, screenOffY + windowHeight/2);

    // Draw cursor
    if (this->focused) {
      uint16_t  curPosX = 0;
      char* dup = strdup(outputStringDyn + textOffset);
      if (fit<=strlen(dup)) {   // sanity check
        dup[fit] = '\0';
        if (cursorOffset - textOffset <=  fit) {
          dup[cursorOffset - textOffset] = '\0';
          curPosX = lcd.textWidth(dup);
          drawCursor(lcd, screenOffX + curPosX + xPad, screenOffY+(windowHeight-widgetFont->height())/2, widgetFont->height(), WP_COLOR_0);
        }
      }
      free(dup);
    }
  } else {
    // Empty input -> draw only cursor if necessary
    if (focused) {
      drawCursor(lcd, screenOffX+xPad+1, screenOffY+(windowHeight-widgetFont->height())/2, widgetFont->height(), WP_COLOR_0);
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Header & Footer widgets  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

uint16_t GUI::drawBatteryIcon(TFT_eSPI &lcd, ControlState &controlState, int16_t xLeft, int16_t xRight, uint16_t y) {
  if (controlState.battVoltage>0) {

    uint8_t SOC = controlState.battSoc;
    bool showBlink = controlState.battBlinkOn;
    //bool fullyCharged = SOC>=100 || controlState.battCharged;   // NOTE: controlState.battCharged is more strict: it can be FALSE if WiFi is working and SOC is 100,
    //       but it can also be true when SOC is not 100
    bool fullyCharged = SOC>=100;

    if (!GUI::batteryExtraLength) {

      // Draw battery as a single icon

      // Determine which icon to draw right now
      const unsigned char *icon = NULL;
      int iconSize = 0;
      if (controlState.usbConnected && fullyCharged) {
        icon = icon_batt_w_full;
        iconSize = sizeof(icon_batt_w_full);
      } else if (SOC < 10 && !showBlink) {
        icon = icon_batt_w_0;
        iconSize = sizeof(icon_batt_w_0);
      } else if (SOC < 10 || (SOC < 30 && !showBlink) ) {
        icon = icon_batt_w_1;
        iconSize = sizeof(icon_batt_w_1);
      } else if (SOC < 30 || (SOC < 50 && !showBlink) ) {
        icon = icon_batt_w_2;
        iconSize = sizeof(icon_batt_w_2);
      } else if (SOC < 50 || (SOC < 70 && !showBlink) ) {
        icon = icon_batt_w_3;
        iconSize = sizeof(icon_batt_w_3);
      } else if (SOC < 70 || (!showBlink && (controlState.usbConnected || SOC < 90)) ) {
        icon = icon_batt_w_4;
        iconSize = sizeof(icon_batt_w_4);
      } else {
        icon = icon_batt_w_5;
        iconSize = sizeof(icon_batt_w_5);
      }

      // Draw icon
      if (icon && iconSize) {
        IconRle3 *iconObj = new IconRle3(icon, iconSize);
        if (xLeft<0) {
          // Draw with right anchor
          lcd.drawImage(*iconObj, xRight-iconObj->width(), y);
        } else {
          // Draw with left anchor
          lcd.drawImage(*iconObj, xLeft, y);
        }
        uint16_t w = iconObj->width();
        delete iconObj;
        return w;
      }

    } else {

      // Draw battery icon out of components (can be any length)

      uint8_t SOC = controlState.battSoc;
      bool showBlink = controlState.battBlinkOn;
      bool fullyCharged = SOC>=100;

      // Determine how many sections would be drawn normally?
      int sections = 0;
      if (controlState.usbConnected && fullyCharged) {
        sections = 5;
      } else if (SOC < 10 && !showBlink) {
        sections = 0;
      } else if (SOC < 10 || (SOC < 30 && !showBlink) ) {
        sections = 1;
      } else if (SOC < 30 || (SOC < 50 && !showBlink) ) {
        sections = 2;
      } else if (SOC < 50 || (SOC < 70 && !showBlink) ) {
        sections = 3;
      } else if (SOC < 70 || (!showBlink && (controlState.usbConnected || SOC < 90))  ) {
        sections = 4;
      } else {
        sections = 5;
      }

      IconRle3 *sectionIcon = new IconRle3(icon_batt_s, sizeof(icon_batt_s));
      sections += (GUI::batteryExtraLength+1) / (sectionIcon->width()+1);

      // Draw icon
      uint16_t w = 0;
      if (xLeft<0) {
        // Draw with right anchor

        // - right part of the battery
        IconRle3 *icon = new IconRle3(icon_batt_r, sizeof(icon_batt_r));
        lcd.drawImage(*icon, xRight-icon->width(), y);
        w = icon->width();
        delete icon;

        // - draw sections
        for (int sec=0; sec<sections; sec++) {
          lcd.drawImage(*sectionIcon, xRight - (sectionIcon->width() + 1) * (sec + 1) - w + 1, y + 2);
        }

        // - draw two lines
        lcd.drawLine(xRight-20-w-GUI::batteryExtraLength, y, xRight-w, y, TFT_WHITE);
        lcd.drawLine(xRight-20-w-GUI::batteryExtraLength, y+12, xRight-w, y+12, TFT_WHITE);

        // - left part of the battery
        icon = new IconRle3(icon_batt_l, sizeof(icon_batt_l));
        lcd.drawImage(*icon, xRight - icon->width()-20-w-GUI::batteryExtraLength, y);
        w += icon->width() + 20 + GUI::batteryExtraLength;
        delete icon;


      } else {
        // Not implemented
      }

      delete sectionIcon;

      return w;

    }
  }
  return 0;
}

uint8_t GUI::wifiSignalStrength(int rssi) {
  if (rssi > -60) {
    return 3;  // ..-59
  }
  if (rssi > -70) {
    return 2;  // -60..-69
  }
  if (rssi > -80) {
    return 1;  // -70..-79
  }
  return 0;
}

uint16_t GUI::drawWifiIcon(TFT_eSPI &lcd, ControlState &controlState, uint16_t x, uint16_t y) {
  if (wifiState.isConnected() && WiFi.status() == WL_CONNECTED) {
    int iconSize = 0;
    const unsigned char *icon;
    int wifiLevel = GUI::wifiSignalStrength(controlState.wifiRssi);
    if (wifiLevel >= 3)        {
      icon = icon_wifi_w_3;
      iconSize = sizeof(icon_wifi_w_3);
    } else if (wifiLevel >= 2) {
      icon = icon_wifi_w_2;
      iconSize = sizeof(icon_wifi_w_2);
    } else if (wifiLevel >= 1) {
      icon = icon_wifi_w_1;
      iconSize = sizeof(icon_wifi_w_1);
    } else                     {
      icon = icon_wifi_w_0;
      iconSize = sizeof(icon_wifi_w_0);
    }
    lcd.drawImage(icon, iconSize, x, y);
    return 17;    // TODO: hardcoded width
  }
  return 0;
}

uint16_t GUI::drawSipIcon(TFT_eSPI &lcd, ControlState &controlState, uint16_t x, uint16_t y) {
  if (wifiState.isConnected() && WiFi.status() == WL_CONNECTED && controlState.sipEnabled) {
    // Draw SIP icon only if WiFi is connected
    if (controlState.sipRegistered) {
      lcd.drawImage(icon_phone_small_w, sizeof(icon_phone_small_w), x, y);
      return 11;      // TODO: hardcoded width
    } else {
      lcd.drawImage(icon_phone_small_w_crossed, sizeof(icon_phone_small_w_crossed), x, y);
      return 17;      // TODO: hardcoded width
    }
  }
  return 0;
}

uint16_t GUI::drawMessageIcon(TFT_eSPI &lcd, ControlState &controlState, uint16_t x, uint16_t y) {
  if (controlState.unreadMessages) {
    lcd.drawImage(icon_incoming_message_w, sizeof(icon_incoming_message_w), x, y);
    return 19;      // TODO: hardcoded width
  }
  return 0;
}

void GUI::drawOtaUpdate() {
  lcd.fillRect(0, 0, lcd.width(), lcd.height(), THEME_BG);
  lcd.setTextColor(WP_ACCENT_0, WP_COLOR_0);
  lcd.setTextFont(fonts[AKROBAT_BOLD_18]);
  lcd.setTextDatum(ML_DATUM);
  lcd.drawString("Installing firmware update", 5, lcd.height()/2);
}

void GUI::drawPowerOff() {
  // Just a blank screen
  lcd.fillScreen(TFT_BLACK);
}

void HeaderWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  //log_i("redraw HeaderWidget");

  lcd.fillRect(screenOffX, screenOffY, windowWidth+1, windowHeight, WP_COLOR_0);

  // - Title
  if (title) {
    //log_d("header title: %s", title);
    lcd.setTextColor(WP_ACCENT_0, WP_COLOR_0);
    lcd.setTextFont(fonts[AKROBAT_BOLD_18]);
    lcd.setTextDatum(ML_DATUM);
    lcd.drawString(title, screenOffX+8, screenOffY+windowHeight/2);
  }

  // - Battery state & WiFi icons
  const uint16_t space = 3;
  uint16_t xOff = space, w;
  xOff += (w = GUI::drawBatteryIcon(lcd, controlState, -1, screenOffX + windowWidth - xOff, screenOffY + 7));
  if (w) {
    xOff += space;
  }
  xOff += (w = GUI::drawWifiIcon(lcd, controlState, screenOffX + windowWidth - xOff - 20, screenOffY + 5));
  if (w) {
    xOff += space + 3;
  }
  xOff += (w = GUI::drawMessageIcon(lcd, controlState, screenOffX + windowWidth - xOff - 20, screenOffY + 6));
  if (w) {
    xOff += space;
  }

  // - Time
  if (ntpClock.isTimeKnown()) {
    lcd.setTextColor(TFT_WHITE, WP_COLOR_0);
    lcd.setTextFont(fonts[AKROBAT_BOLD_18]);      // original desing: AKROBAT_BOLD_16
    lcd.setTextDatum(MR_DATUM);
    char buff[6];
    sprintf(buff, "%02d:%02d", ntpClock.getHour(), ntpClock.getMinute());
    lcd.drawString(buff, screenOffX+windowWidth-xOff-3, screenOffY+windowHeight/2);
  }
}

void FooterWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  log_d("redraw footer");
  /* Here's what we are drawing (in horizontal sense):
   *     +             +                 +                +                 +              +
   *     | Left button |                 | Input sequence |                 | Right button |
   *     +            x0              off0              off                x1
   *     <------------->
   *     <------------------------------->
   *     <------------------------------------------------>
   *     <------------------------------------------------------------------>
   */
  uint16_t xPad = 8;
  uint16_t x0 = screenOffX + xPad, x1 = screenOffX + windowWidth - xPad;   // button boundaries

  lcd.fillRect(screenOffX, screenOffY, windowWidth+1, windowWidth+1, WP_COLOR_0);

  if (!controlState.locked) {
    // Draw button text
    lcd.setTextFont(fonts[AKROBAT_SEMIBOLD_22]);
    lcd.setTextColor(WP_COLOR_1, WP_COLOR_0);
    if(leftButtonName != NULL) {
      //lcd.setTextDatum(TL_DATUM);
      //x0 += lcd.drawString(leftButtonName, screenOffX+1, screenOffY+1);
      lcd.setTextDatum(ML_DATUM);
      x0 += lcd.drawString(leftButtonName, x0, screenOffY+windowHeight/2);
    }
    if (rightButtonName != NULL) {
      //lcd.setTextDatum(TR_DATUM);
      //x1 -= lcd.drawString(rightButtonName, screenOffX+windowWidth-1, screenOffY+1);
      lcd.setTextDatum(MR_DATUM);
      x1 -= lcd.drawString(rightButtonName, x1, screenOffY+windowHeight/2);
    }

    // Display input characters
    bool inputSeq = false;
    if (controlState.inputCurKey) {
      uint8_t len = strlen(controlState.inputSeq);
      if (len) {
        inputSeq = true;
        uint16_t off0, off;
        // TODO: consider upper letters here if shift is pressed (controlState.inputShift); currently uppercase letters are not cetered
        off = off0 = (windowWidth-lcd.textWidth(controlState.inputSeq))/2;     // free pixels halved
        lcd.setTextDatum(TL_DATUM);
        int fontHeight = fonts[AKROBAT_SEMIBOLD_22]->height();

        // Draw each character separately
        bool currentCharacter;
        char s[2];
        s[1] = '\0';
        for (uint8_t i=0; i<len; i++) {

          // Shift pressed?
          s[0] = controlState.inputSeq[i];
          if (controlState.inputShift) {
            s[0] = toupper(s[0]);
          }

          // Character width
          currentCharacter = (i==controlState.inputCurSel);
          int charWidth = fonts[AKROBAT_SEMIBOLD_22]->textWidth(s);

          // Keep within visible window boundaries
          if (off>=0 && off<=windowWidth) {

            // Character background
            colorType bgColor = currentCharacter ? SALAD : WHITE;
            lcd.fillRect(screenOffX+off, screenOffY, charWidth, fontHeight, bgColor);

            // Actually draw character
            lcd.setTextColor(BLACK, bgColor);
            lcd.drawString(s, screenOffX + off, screenOffY+1);

          }

          off += charWidth;

        }

        // Draw background from x0 to off0
        if (x0<off0) {
          lcd.fillRect(x0, screenOffY, off0-x0, windowHeight, WP_COLOR_0);
        }

        // Draw background from off to x1
        if (off<x1) {
          lcd.fillRect(off, screenOffY, 1, windowHeight, currentCharacter ? SALAD : WHITE);      // add 1px to make "1" more visible
          lcd.fillRect(off+1, screenOffY, x1-off-1, windowHeight, WP_COLOR_0);
        }

        // Filler below characters (from x0 to x1)
        lcd.fillRect(x0, screenOffY+fontHeight, x1-x0, windowHeight-fontHeight, WP_COLOR_0);
      }
    }
    if (!inputSeq) {
      // Theme filler between left and right button texts (from x0 to x1)
      lcd.fillRect(x0, screenOffY+1, x1-x0, windowHeight, WP_COLOR_0);
    }
  } else {
    // Display only unlock text
    lcd.setTextFont(fonts[AKROBAT_SEMIBOLD_22]);
    lcd.setTextColor(WP_COLOR_1, WP_COLOR_0);
    lcd.setTextDatum(MC_DATUM);
    const char* msg = (controlState.unlockButton1 == WIPHONE_KEY_OK) ? "Press * to unlock" : "Locked. Press OK";
    lcd.drawString(msg, windowWidth/2, screenOffY + windowHeight/2);
    log_d("footer draw: %d", controlState.unlockButton1);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Menu widget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MenuWidget::MenuWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height, const char* empty,
                       SmoothFont* font, uint8_t itemsPerScreen, uint16_t leftOffset, bool opaque)
  : FocusableWidget(xPos, yPos, width, height), options(), opaque(opaque), leftOffset(leftOffset), widgetFont(font) {
  if (font==NULL) {
    widgetFont = fonts[OPENSANS_COND_BOLD_20];
  }
  if (empty) {
    this->emptyMessageDyn = strdup(empty);
  }
  optionSelectedIndex = 0;
  optionOffsetIndex = 0;
  optionHeight = itemsPerScreen ? height / itemsPerScreen : widgetFont->height() + spacing;
  optionsVisible = optionHeight ? height / optionHeight : 0;
  chosenKey = 0;
  drawOnce = true;    // redraw entirely once
}

void MenuWidget::deleteAll() {
  options.clear();
  optionSelectedIndex = 0;
  optionOffsetIndex = 0;
  chosenKey = 0;
  drawOnce = true;
}

MenuWidget::~MenuWidget() {
  // Delete each of the options one-by-one
  for(uint16_t i=0; i<options.size(); i++) {
    delete options[i];
  }
  freeNull((void **) &this->emptyMessageDyn);
}

bool MenuWidget::processEvent(EventType event) {
  if (!options.size()) {
    return false;
  }
  if (event == WIPHONE_KEY_DOWN || event == WIPHONE_KEY_UP) {
    // Move selection up or down the list
    if (event == WIPHONE_KEY_DOWN) {
      // Move selection down
      optionSelectedIndex = (optionSelectedIndex + 1) % options.size();
    } else if (event == WIPHONE_KEY_UP) {
      // Move selection up
      optionSelectedIndex = (optionSelectedIndex > 0) ? optionSelectedIndex - 1 : options.size() - 1;
    }
    this->revealSelected();
    return true;
  } else if (LOGIC_BUTTON_OK(event) || event==WIPHONE_KEY_RIGHT) {
    if (options.size()) {
      chosenKey = options[optionSelectedIndex]->id;
      log_v("menu: chosen: %d", chosenKey);
    }
    return true;
  }
  return false;
}

void MenuWidget::revealSelected() {
  // Ensure selection is shown
  if (optionSelectedIndex >= optionOffsetIndex + optionsVisible) {
    optionOffsetIndex += optionSelectedIndex - optionOffsetIndex - optionsVisible + 1;
    drawScroll = true;
  } else if (optionOffsetIndex > optionSelectedIndex) {
    optionOffsetIndex = optionSelectedIndex;
    drawScroll = true;
  }
}

const char* MenuWidget::getSelectedTitle() {
  log_d("getSelectedTitle MenuWidget");
  if (options.size() && optionSelectedIndex < options.size()) {
    return (const char*) options[optionSelectedIndex]->titleDyn;
  }
  return NULL;
}

void MenuWidget::setStyle(uint8_t styleNum, colorType textCol, colorType bgCol, colorType selTextCol, colorType selBgCol) {
  if (styleNum==1) {
    style1TextColor = textCol;
    style1BgColor = bgCol;
    style1SelTextColor = selTextCol;
    style1SelBgColor = selBgCol;
  } else if (styleNum==2) {
    style2TextColor = textCol;
    style2BgColor = bgCol;
    style2SelTextColor = selTextCol;
    style2SelBgColor = selBgCol;
  }
}

void MenuWidget::setStyle(uint8_t styleNum, colorType textCol, colorType bgCol, colorType selBgCol) {
  this->setStyle(styleNum, textCol, bgCol, textCol, selBgCol);
}

void MenuWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  // Fill background
  //corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, BLACK);

  // Print option names
  if (options.size() > 0) {
    lcd.setTextDatum(CL_DATUM);
    if (!widgetFont) {
      widgetFont = fonts[OPENSANS_COND_BOLD_20];
    }
    uint8_t maxChars = windowWidth / lcd.textWidth("M");      // TODO: remove maxChars
    uint16_t yOff = 0, xOff;
    for(uint16_t i=optionOffsetIndex; i<options.size() && i<optionOffsetIndex+optionsVisible; i++) {
      if (yOff+optionHeight > windowHeight) {
        if (yOff < windowHeight) {
          // TODO: there is extra place -> display an arrow
        }
        break;
      }

      // Choose colors (one of four options)
      uint16_t textColor, bgColor;
      if (i!=optionSelectedIndex) {
        textColor = options[i]->style==1 ? style1TextColor : style2TextColor;
        bgColor   = options[i]->style==1 ? style1BgColor : style2BgColor;
      } else {
        textColor = options[i]->style==1 ? style1SelTextColor : style2SelTextColor;
        bgColor   = options[i]->style==1 ? style1SelBgColor : style2SelBgColor;
      }

      options[i]->redraw(lcd, screenOffX, screenOffY + yOff, windowWidth, optionHeight, textColor, bgColor, opaque, i==optionSelectedIndex, widgetFont, leftOffset);

      yOff += optionHeight;
    }
    lcd.setSmoothTransparency(false);

    // Print blacks
    if (opaque && yOff < windowHeight) {
      if (drawOnce || drawItems) {
        corrRect(lcd, screenOffX, screenOffY + yOff, windowWidth, windowHeight - yOff, style1BgColor);
      }
    }
  } else if (this->emptyMessageDyn) {
    if (drawOnce || drawItems) {
      if (opaque) {
        corrRect(lcd, screenOffX, screenOffY, windowWidth, windowHeight, style1BgColor);
      }
      lcd.setTextFont(fonts[AKROBAT_BOLD_18]);
      lcd.setTextDatum(TL_DATUM);
      lcd.setTextColor(GRAY, style1BgColor);
      lcd.drawFitString(this->emptyMessageDyn, windowWidth-10, screenOffX + 5, screenOffY + 5);
    }
  }

  if (!lcd.isSprite() || !opaque) {
    drawOnce = drawItems = drawScroll = false;
  }
}

MenuOption::keyType MenuWidget::readChosen() {
  MenuOption::keyType r = chosenKey;
  chosenKey = 0;
  return r;
}

const char* MenuWidget::readChosenTitle() {
  MenuOption::keyType r = chosenKey;
  chosenKey = 0;

  // Extract title
  for (uint16_t i=0; i<options.size(); i++) {
    if (options[i]->id == r) {
      return options[i]->titleDyn;
    }
  }
  return NULL;
}

bool MenuWidget::addOption(MenuOption* option) {
  if (options.add(option)) {
    drawItems = true;
    return true;
  }
  return false;
}

void MenuWidget::addOption(const char* title) {
  this->addOption(title, options.size()+1, 1);
}

void MenuWidget::addOption(const char* title, MenuOption::keyType key, uint16_t style) {
  if (key) {
    MenuOption* option = new MenuOption(key, style, title);
    if (!this->addOption(option)) {
      delete option;
    }
  } else {
    log_e("menu option key is 0");
  }
}

void MenuWidget::addOption(const char* title, const char* subTitle, MenuOption::keyType key, uint16_t style,
                           const unsigned char* iconData, const uint16_t iconSize,
                           const unsigned char* selIconData, const uint16_t selIconSize) {
  MenuOptionIconned* option = new MenuOptionIconned(key, style, title, subTitle, iconData, iconSize, selIconData, selIconSize);
  if (!addOption(option)) {
    delete option;
  }
}

void MenuWidget::select(MenuOption::keyType key) {
  for (int i=0; i<options.size(); i++)
    if (options[i]->id == key) {
      optionSelectedIndex = i;
      this->revealSelected();
      break;
    }
}

void MenuWidget::selectLastOption() {
  optionSelectedIndex = options.size()-1;
}

//void MenuWidget::removeOption(uint16_t key) {
//  // TODO
//}
//
//void MenuWidget::sortOptions() {
//  // TODO
//}
//
//void MenuWidget::restoreOrderLast(uint16_t i) {
//  // TODO
//}

OptionsMenuWidget::OptionsMenuWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height)
  : MenuWidget(xPos, yPos, width, height, "No options available", fonts[AKROBAT_BOLD_20], N_OPTION_ITEMS, 8, true) {
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Menu option  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MenuOption::MenuOption() {
  id = 0;
  style = 0;
  titleDyn = NULL;
}

MenuOption::~MenuOption() {
  freeNull((void **) &titleDyn);
};

MenuOption::MenuOption(MenuOption::keyType pId, uint16_t pStyle, const char* title)
  : id(pId), style(pStyle) {
  titleDyn = title ? strdup(title) : NULL;
};

void MenuOption::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                        uint16_t textColor, uint16_t bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset) {
  // Draw background if needed
  if (opaque || selected) {
    lcd.fillRect(screenOffX, screenOffY, windowWidth, windowHeight, bgColor);
    lcd.setSmoothTransparency(false);
  } else {
    lcd.setSmoothTransparency(true);
  }

  // Print text
  lcd.setTextFont(font);
  lcd.setTextColor(textColor, bgColor);
  lcd.drawFitString(titleDyn, windowWidth - leftOffset, screenOffX + leftOffset, screenOffY + windowHeight/2);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Menu option with icon & subtitle - - - - - - - - - - - - - - - - - - - - - - - - - - - -

MenuOptionIconned::MenuOptionIconned(MenuOption::keyType pId, uint16_t pStyle, const char* title,
                                     const char* subTitle,
                                     const unsigned char* iconData, const uint16_t iconSize,
                                     const unsigned char* selIconData, const uint16_t selIconSize,
                                     uint8_t textOffset, colorType selBgColor)
  : MenuOption(pId, pStyle, title), selectedBgColor(selBgColor) {
  if (subTitle) {
    subTitleDyn = strdup(subTitle);
  } else {
    subTitleDyn = NULL;
  }
  if (iconData && iconSize) {
    icon = new IconRle3(iconData, iconSize);
  }
  if (selIconData && selIconSize) {
    iconSelected = new IconRle3(selIconData, selIconSize);
  }
  textLeftOffset = textOffset;
}

MenuOptionIconned::~MenuOptionIconned() {
  freeNull((void **) &subTitleDyn);
  if (icon) {
    delete icon;
  }
  if (iconSelected) {
    delete iconSelected;
  }
}

void MenuOptionIconned::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                               uint16_t textColor, uint16_t bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset) {
  // Draw background if needed
  if (opaque || selected) {
    lcd.fillRect(screenOffX, screenOffY, windowWidth, windowHeight, bgColor);
    lcd.setSmoothTransparency(false);
  } else {
    lcd.setSmoothTransparency(true);
  }

  // Draw icons
  uint16_t iconOffset = 0;
  if (selected && iconSelected) {
    if (selected && selectedBgColor!=IGNORED_COLOR) {
      lcd.fillRect(screenOffX, screenOffY, iconSelected->width(), iconSelected->height(), selectedBgColor);
    }
    lcd.drawImage(*iconSelected, screenOffX + leftOffset, screenOffY + (windowHeight - iconSelected->height())/2 );
    iconOffset = iconSelected->width();
  } else if (icon) {
    lcd.drawImage(*icon, screenOffX + leftOffset, screenOffY + (windowHeight - icon->height())/2 );
    iconOffset = icon->width();
  }
  if (iconOffset) {
    iconOffset += textLeftOffset;
  }

  // Print text
  lcd.setTextColor(textColor, bgColor);
  lcd.setTextDatum(ML_DATUM);
  uint16_t allotted = windowWidth - iconOffset - leftOffset;
  if (subTitleDyn == NULL) {
    lcd.setTextFont(font);
    lcd.drawFitString(titleDyn, allotted, screenOffX + leftOffset + iconOffset, screenOffY + windowHeight/2);
  } else {
    lcd.setTextFont(font);
    lcd.drawFitString(titleDyn, allotted, screenOffX + leftOffset + iconOffset, screenOffY + (windowHeight-fonts[AKROBAT_BOLD_16]->height())/2);
    lcd.setTextFont(fonts[AKROBAT_BOLD_16]);
    lcd.drawFitString(subTitleDyn, allotted, screenOffX + leftOffset + iconOffset, screenOffY + font->height() + (windowHeight-font->height())/2);
  }
}

MenuOptionIconnedTimed::MenuOptionIconnedTimed(MenuOption::keyType pId, uint16_t pStyle, const char* title, const char* subTitle, uint32_t zeit, uint16_t globalBg,
    const unsigned char* iconData, const uint16_t iconSize,
    const unsigned char* selIconData, const uint16_t selIconSize)
  : MenuOptionIconned(pId, pStyle, title, subTitle, iconData, iconSize, selIconData, selIconSize), zeit(zeit), globalBgColor(globalBg) {
};

void MenuOptionIconnedTimed::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                                    uint16_t textColor, uint16_t bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset) {
  // Draw background if needed
  if (opaque || selected) {
    lcd.fillRect(screenOffX, screenOffY, windowWidth, windowHeight, bgColor);
    lcd.setSmoothTransparency(false);
  } else {
    lcd.setSmoothTransparency(true);
  }

  // Draw icons
  uint16_t iconOffset = 0;
  if (selected && iconSelected) {
    lcd.drawImage(*iconSelected, screenOffX + leftOffset, screenOffY + (windowHeight - iconSelected->height())/2 );
    iconOffset = iconSelected->width();
  } else if (icon) {
    lcd.drawImage(*icon, screenOffX + leftOffset, screenOffY + (windowHeight - icon->height())/2 );
    iconOffset = icon->width();
  }
  if (iconOffset) {
    iconOffset += 12;  // hardcoded parameter (same effect as textLeftOffset)
  }

  // Print text
  lcd.setTextColor(textColor, bgColor);
  // - Date / Ago
  char ago[20];
  const uint16_t rightOffset = 8;
  ntpClock.dateTimeAgo(zeit, ago);
  lcd.setTextFont(fonts[AKROBAT_BOLD_18]);
  lcd.setTextDatum(MR_DATUM);
  uint16_t dateWidth = lcd.drawString(ago, windowWidth - rightOffset, screenOffY + (windowHeight-fonts[AKROBAT_BOLD_16]->height())/2);
  // - Name
  lcd.setTextFont(font);
  lcd.setTextDatum(ML_DATUM);
  lcd.drawFitString(titleDyn, windowWidth - leftOffset - iconOffset - rightOffset - dateWidth, screenOffX + leftOffset + iconOffset, screenOffY + (windowHeight-fonts[AKROBAT_BOLD_16]->height())/2);
  // - Subtitle (message text)
  if (subTitleDyn != NULL) {
    lcd.setTextFont(fonts[AKROBAT_BOLD_16]);
    lcd.setTextColor(selected ? textColor : WP_DISAB_0, bgColor);
    //lcd.drawString(subTitleDyn, screenOffX + leftOffset + iconOffset, screenOffY + font->height() + (windowHeight-font->height())/2);
    uint16_t allotted = windowWidth - iconOffset - leftOffset;
    lcd.drawFitString(subTitleDyn, allotted, screenOffX + iconOffset + leftOffset, screenOffY + font->height() + (windowHeight-font->height())/2);
  }
}

MenuOptionPhonebook::MenuOptionPhonebook(MenuOption::keyType pId, uint16_t pStyle, const char* title, const char* subTitle)
  : MenuOptionIconned(pId, pStyle, title, subTitle, icon_person_b, sizeof(icon_person_b), icon_person_w, sizeof(icon_person_w)) {
};

void MenuOptionPhonebook::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                                 uint16_t textColor, uint16_t bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset) {
  // Load icons
  uint16_t leftIconOff = 0;
  uint16_t rightIconOff = 0;
  IconRle3 *leftIcon;
  IconRle3 *rightIcon;
  if (selected) {
    leftIcon = iconSelected;
    rightIcon = new IconRle3(icon_phone_w, sizeof(icon_phone_w));
  } else {
    leftIcon = icon;
    rightIcon = new IconRle3(icon_phone_b, sizeof(icon_phone_b));
  }

  // Draw background if needed
  if (opaque || selected) {
    lcd.fillRect(screenOffX + (leftIcon && selected ? leftIcon->width() : 0), screenOffY, windowWidth, windowHeight, bgColor);
    lcd.setSmoothTransparency(false);
  } else {
    lcd.setSmoothTransparency(true);
  }

  // Draw icons
  if (leftIcon) {
    if (selected) {
      lcd.fillRect(screenOffX, screenOffY, leftIcon->width(), leftIcon->height(), WP_ACCENT_0);
    }
    lcd.drawImage(*leftIcon, screenOffX, screenOffY + (windowHeight - leftIcon->height())/2 );
    leftIconOff = leftIcon->width();
    // delete leftIcon; // you might think this should be deleted here, but you would be wrong.
  }
  if (rightIcon) {
    rightIconOff = rightIconOffset + rightIcon->width();
    lcd.drawImage(*rightIcon, screenOffX + windowWidth - rightIconOff, screenOffY + (windowHeight - rightIcon->height())/2 );
    delete rightIcon;
  }

  // Print text
  if (leftIconOff) {
    leftIconOff += 7;
  }
  uint16_t allotted = windowWidth - leftIconOff - rightIconOff;
  lcd.setTextColor(textColor, bgColor);
  lcd.setTextDatum(ML_DATUM);
  if (subTitleDyn == NULL) {
    lcd.setTextFont(font);
    lcd.drawFitString(titleDyn, allotted, screenOffX + leftIconOff, screenOffY + windowHeight/2);
  } else {
    lcd.setTextFont(font);
    lcd.drawFitString(titleDyn, allotted, screenOffX + leftIconOff, screenOffY + (windowHeight-fonts[AKROBAT_BOLD_16]->height())/2);
    lcd.setTextFont(fonts[AKROBAT_BOLD_16]);
    lcd.drawFitString(subTitleDyn, allotted, screenOffX + leftIconOff, screenOffY + font->height() + (windowHeight-font->height())/2);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Button widget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ButtonWidget::ButtonWidget(uint16_t xPos, uint16_t yPos, const char* title,
                           uint16_t width, uint16_t height,
                           uint16_t col, uint16_t bgCol, uint16_t border, uint16_t sel, uint16_t selBg)
  : FocusableWidget(xPos, yPos, width, height),
    textColor(col), bgColor(bgCol), borderColor(border), selTextColor(sel), selBgColor(selBg) {
  pressed = false;
  titleDyn = strdup(title);
  if (!widgetWidth) {
    widgetWidth = fonts[OPENSANS_COND_BOLD_20]->textWidth(title) + 18;
  }
  updated = true;
}

ButtonWidget::~ButtonWidget() {
  if (titleDyn) {
    freeNull((void **) &titleDyn);
  }
}

void ButtonWidget::setText(const char* str) {
  freeNull((void **) &titleDyn);
  titleDyn = strdup(str);
  updated = true;
}

void ButtonWidget::setColors(colorType fg, colorType bg, colorType border, colorType sel, colorType selBg) {
  textColor = fg;
  bgColor = bg;
  borderColor = border;
  selTextColor = sel;
  selBgColor = selBg;
  updated = true;
}

int32_t ButtonWidget::textWidth(const char* str) {
  return fonts[OPENSANS_COND_BOLD_20]->textWidth(str);
}

bool ButtonWidget::processEvent(EventType event) {
  if (LOGIC_BUTTON_OK(event)) {
    pressed = true;
    return true;
  }
  return false;
}

bool ButtonWidget::readPressed() {
  bool res = pressed;
  pressed = false;
  return res;
}

void ButtonWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  if (windowWidth != widgetWidth || windowHeight != widgetHeight) {
    return;  // this widget can only be rendered in full
  }

  // Draw border
  lcd.drawRect(screenOffX, screenOffY, windowWidth, windowHeight, borderColor);

  // Draw background
  lcd.fillRect(screenOffX+1, screenOffY+1, windowWidth-2, widgetHeight-2, focused ? selBgColor : bgColor);

  // Draw text
  if (!focused) {
    lcd.setTextColor(textColor, bgColor);
  } else {
    lcd.setTextColor(selTextColor, selBgColor);
  }
  lcd.setTextFont(fonts[OPENSANS_COND_BOLD_20]);
  lcd.setTextDatum(MC_DATUM);
  lcd.drawString(titleDyn, screenOffX + windowWidth/2, screenOffY + windowHeight/2);
  updated = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Slider widget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

SliderWidget::SliderWidget(uint16_t xPos, uint16_t yPos,
                           uint16_t width, uint16_t height,
                           colorType col, colorType selCol, colorType bgCol, colorType textCol)
  : FocusableWidget(xPos, yPos, width, height),
    mainColor(col), selectedColor(selCol), bgColor(bgCol), textColor(textCol) {
}

void SliderWidget::drawSlider(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight, colorType color, float pos) {
  //lcd.drawRect(screenOffX, screenOffY, windowWidth, windowHeight, TFT_BLACK);     // debug

  // Draw horizontal rounded rect
  const uint16_t lineRadius = lineHeight >= 3 ? lineHeight/2 : 1;
  const uint16_t mid = screenOffY + windowHeight/2;
  const uint16_t lineStartX = screenOffX + dotRadius;
  const uint16_t lineStartY = mid - lineHeight/2;
  const uint16_t lineEndX = screenOffX + windowWidth - dotRadius - 1;
  if (lineEndX > dotRadius*2) {
    lcd.fillRoundRect(lineStartX, lineStartY, lineEndX-lineStartX, lineHeight, lineRadius, color);
  }

  // Draw circle
  const uint16_t posX = lineStartX + ((lineEndX > lineStartX) ? (int)((lineEndX-lineStartX)*pos) : 0);
  log_d("%.2f%% -> %d, max = %d", pos*100, ((lineEndX > lineStartX) ? (int)((lineEndX-lineStartX)*pos) : 0),  lineEndX-lineStartX);
  lcd.fillCircle(posX, mid, dotRadius, color);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Integer slider widget  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IntegerSliderWidget::IntegerSliderWidget(uint16_t posX, uint16_t posY,
    uint16_t width, uint16_t height,
    int minValue, int maxValue, int step, bool showText, const char* unit,
    colorType color, colorType selectedColor, colorType bgColor, colorType textColor)
  : SliderWidget(posX, posY, width, height, color, selectedColor, bgColor, textColor), minVal(minValue), maxVal(maxValue), val(minValue), step(step), unit(unit) {
  log_d("creating IntegerSliderWidget");
  if (showText) {
    log_d("showText");
    bool minus = minValue < 0;
    uint8_t minExp, maxExp;
    for (minExp=1; minValue/=10; minExp++);
    for (maxExp=1; maxValue/=10; maxExp++);
    if (maxExp<minExp) {
      maxExp = minExp;
    }
    int unitLen = unit ? strlen(unit) : 0;
    char tmp[12+1+unitLen];
    memset(tmp, '6', sizeof(tmp));      // NOTE: we are assuming that "6" is the fattest digit in the font, which might not be true
    if (minus) {
      tmp[0] = '-';
      maxExp++;
    }
    if (unitLen) {
      sprintf(tmp + maxExp, " %s", unit);
    } else {
      tmp[maxExp] = '\0';
    }
    maxTextWidth = fonts[smoothFont]->textWidth(tmp);
    log_d("maxTextWidth = %d, maxExp = %d\n", maxTextWidth, maxExp);
  } else {
    maxTextWidth = 0;
  }
}

IntegerSliderWidget::~IntegerSliderWidget() {

}

bool IntegerSliderWidget::processEvent(EventType event) {
  if (event == WIPHONE_KEY_LEFT) {
    this->setValue(this->val - step);
    return true;
  } else if (event == WIPHONE_KEY_RIGHT) {
    this->setValue(this->val + step);
    return true;
  }
  return false;
}

void IntegerSliderWidget::redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
  log_d("drawing integer slider");

  // Clear background
  lcd.fillRect(screenOffX, screenOffY, windowWidth, windowHeight, bgColor);

  // Draw slider
  const uint16_t offset = 4;
  const float pos = ((float)val - minVal)/(maxVal - minVal);
  const colorType color = this->focused ? selectedColor : mainColor;
  drawSlider(lcd, screenOffX + offset, screenOffY, windowWidth - 2*offset - this->maxTextWidth - offset, windowHeight, color, pos);

  // Draw text
  if (this->maxTextWidth) {
    char buff[12 + 1 + (unit!=NULL ? strlen(unit) : 0)];
    if (unit!=NULL) {
      sprintf(buff, "%d %s", this->val, unit);
    } else {
      sprintf(buff, "%d", this->val);
    }
    lcd.setTextColor(textColor, bgColor);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextFont(fonts[smoothFont]);
    lcd.drawFitString(buff, this->maxTextWidth+2*offset, screenOffX + windowWidth - offset - this->maxTextWidth/2, screenOffY + windowHeight/2);     //
    //lcd.drawRect(windowWidth - offset - this->maxTextWidth, screenOffY, this->maxTextWidth, windowHeight, TFT_BLACK);   // debug
  }
}

void IntegerSliderWidget::setValue(int value) {
  if (value > this->maxVal) {
    value = this->maxVal;
  } else if (value < this->minVal) {
    value = this->minVal;
  }
  if (this->val != value) {
    this->val = value;
    this->updated = true;
  }
}
