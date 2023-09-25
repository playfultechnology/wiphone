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

#ifndef _WIPHONE_GUI_H_
#define _WIPHONE_GUI_H_

#include "src/TFT_eSPI/TFT_eSPI.h"
#include <Arduino.h>
#include "Storage.h"
#include <random>
#include "Networks.h"
#include "config.h"
#include "helpers.h"
#include "Hardware.h"
#include "src/assets/fonts.h"
#include "src/assets/icons.h"
#include "src/ringbuff.h"
#include "clock.h"
#include "Audio.h"
#include "FairyMax.h"
#include "ota.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"

using namespace std;

#define LCD TFT_eSPI
extern LCD* static_lcd;      // a hack for the LCD to be usable from static callbacks

// Pixel colors
#define WHITE       TFT_WHITE     // 0xFFFF
#define GRAY        TFT_DARKGREY  // 0x7BEF      // 50%
#define BLACK       TFT_BLACK     // 0x0000
#define BLUE        TFT_BLUE      // 0x001F
#define GREEN       TFT_GREEN     // 0x07E0
#define RED         TFT_RED       // 0xF800
#define YELLOW      TFT_YELLOW    // 0XFFE0      // red + green
#define MAGENTA     TFT_MAGENTA   // 0xF81F      // blue + red
#define CYAN        TFT_CYAN      // 0x7FFF      // blue + green
#define NONE        TFT_BLACK     // 0x0000

#define GETBLUE(x)  (x & BLUE)
#define GETRED(x)   ((x & RED)>>11)
#define GETGREEN(x) ((x & GREEN)>>6)    // only 5 highest bits

// 13 shades of gray
// (BLUE & (uint16_t)(BLUE*k)) | (GREEN & (uint16_t)(GREEN*k)) | (RED & (uint16_t)(RED*k))
// Python: (BLUE & int(BLUE*k)) | (GREEN & int(GREEN*k)) | (RED & int(RED*k))
#define GRAY_05     0x0861
#define GRAY_10     0x18C3
#define GRAY_15     0x2124
#define GRAY_20     0x3186
#define GRAY_25     0x39E7
#define GRAY_33     0x528A
#define GRAY_50     GRAY
#define GRAY_67     0xA554
#define GRAY_75     0xBDF7
#define GRAY_80     0xC658
#define GRAY_85     0xD6BA
#define GRAY_90     0xDF1B
#define GRAY_95     0xEF7D

typedef uint16_t colorType;

#ifdef WIPHONE_PRODUCTION
//#define GUI_DEBUG(fmt, ...)
//#define GUI_DEBUG0(a1, ...)
//#define GUI_DEBUG2(a1,a2, ...)
//#define DIAGNOSTICS_REPORT(fmt, ...)
#else
//#define GUI_DEBUG(fmt, ...)      DEBUG(fmt, ##__VA_ARGS__)
//#define GUI_DEBUG0(a1, ...)      Serial.print(a1, ##__VA_ARGS__)
//#define GUI_DEBUG2(a1,a2, ...)   do { Serial.print(a1); Serial.println(a2, ##__VA_ARGS__); } while(0)
//#define DIAGNOSTICS_REPORT(fmt, ...)      log_i("[Diagnostics] " fmt, ##__VA_ARGS__)
#endif // WIPHONE_PRODUCTION

#define BUTTON_PADDING  26

// Theme colors
// Python: rgb = lambda r,g,b: hex((((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))
#define RGB_COLOR(r,g,b)    (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define THEME_HEADER_SIZE   30
#define THEME_FOOTER_SIZE   40

#define THEME_COLOR       0x7BFF            // RGB = 50%, 50%, 100%
#define THEME_APP_COLOR   0xFBEF            // RGB = 100%, 50%, 50%
#define THEME_BG          BLACK
#define THEME_TEXT_COLOR  WHITE
#define THEME_CURSOR      WHITE
#define TOMATO            0xFBEF            // RGB = 100%, 50%, 50%
#define SALAD             0x57EA            // RGB = 33%, 100%, 33%
#define REDDISH           0xFBF5            // RGB = 100%, 50%, 70%

// New palette colors
#define WP_COLOR_0        0x0000            // Black
#define WP_ACCENT_0       0x4CDB            // Lighter blue     (#4C9ADD)
#define WP_COLOR_1        0xFFFF            // White
#define WP_ACCENT_1       0x0379            // Darker blue      (#006FCE)
#define WP_DISAB_0        0x632C            // Darker gray      (#646464)
#define WP_DISAB_1        0xB596            // Lighter gray     (#B2B2B2)
#define WP_ACCENT_S       0xFA40            // Light orange     (#FF4C00)
#define WP_ACCENT_G       TFT_GREEN         // Completely green (#0x07E0)

#define N_MAX_ITEMS       0                 // fit as many items in menu as possible
#define N_MENU_ITEMS      5                 // items per screen in regular menus
#define N_OPTION_ITEMS    7                 // items per screen in option menus

#define KEYBOARD_TIMEOUT_EVENT    0x7f
#define IS_KEYBOARD(event)     (event <= 0x7f)

// Non-keyboard events (>=0x80)
// TODO: use a struct with bit fields for the same purpose; this got a bit confusing
#define APP_TIMER_EVENT            0x80     // also: non-keyboard event flag
#define BATTERY_UPDATE_EVENT       0x81     // each event should have only two bits set
#define CALL_UPDATE_EVENT          0x82
#define WIFI_ICON_UPDATE_EVENT     0x84     // triggered when RSSI level changed SIGNIFICANTLY and icon has to be redrawn
#define TIME_UPDATE_EVENT          0x88
#define USER_SERIAL_EVENT         0x180
#define REGISTRATION_UPDATE_EVENT 0x280
#define BATTERY_BLINK_EVENT       0x480
#define USB_UPDATE_EVENT          0x880
#define POWER_OFF_EVENT          0x1080     // power off is about to happen (user has been pressing the POWER OFF/END button for a few seconds now)
#define POWER_NOT_OFF_EVENT      0x2080     // power off was expected to happen, but user released the OFF/END button 
#define NEW_MESSAGE_EVENT        0x4080
#define CUSTOM_EVENT             0x8080     // these are flags for a custom event. AppID must be stored in bits 0-6, bits 8-14 are for custom event identification
#define SCREEN_DIM_EVENT         0x8780     // AppID == 0 means GUI class itself
#define SCREEN_SLEEP_EVENT       0x8680     // AppID == 0 means GUI class itself
#define UNLOCK_CLEAR_EVENT       0x8880     // AppID == 0 means GUI class itself
typedef uint16_t EventType;

#define NONKEY_EVENT_ONE_OF(e, flags)    ((e & 0x80) && ((e & 0xFF7F) & (flags)))        // only for non-keyboard events

// Keypad
#define MAX_INPUT_SEQUENCE        18          // how many characters AT MOST can one button represent

// Process event results
typedef uint8_t appEventResult;
#define DO_NOTHING      0x00
#define REDRAW_SCREEN   0x01
#define REDRAW_HEADER   0x02
#define REDRAW_FOOTER   0x04
#define ENTER_DIAL_APP  0x08      // 0x88 - exit and enter DialApp (to be returned by ClockApp and MenuApp)
#define LOCK_UNLOCK     0x10      // one of two events: screen locked or screen unlocked
#define EXIT_APP        0x80
#define REDRAW_ALL    (REDRAW_SCREEN | REDRAW_HEADER | REDRAW_FOOTER)         // TODO: change these into more appropriate types

typedef enum {
  OPENSANS_COND_BOLD_20 = 0,
  AKROBAT_BOLD_16,
  AKROBAT_BOLD_18,
  AKROBAT_BOLD_20,
  AKROBAT_BOLD_22,
  AKROBAT_BOLD_24,
  AKROBAT_SEMIBOLD_20,
  AKROBAT_SEMIBOLD_22,
  AKROBAT_EXTRABOLD_22,
  AKROBAT_BOLD_32,
  AKROBAT_BOLD_90,
} FontIndex_t;

typedef enum {
  SIP = 0,
  LORA
} MessageType_t;

enum class InputType { Numeric, AlphaNum, IPv4 };
enum class CallState {
  NotInited = 0,    // connection with proxy not established yet
  Idle,
  InvitingCallee,   // INVITE needs to be sent
  InvitedCallee,    // UAC: INVITE(s) sent, waiting for any reply   /   UAS: 200 OK response sent, waiting for ACK
  RemoteRinging,    // callee's phone is ringing; TODO: produce beeps
  Call,             // audio session in progress
  HangUp,           // the user pressed HANG UP/REJECT button (assume that this event can be triggered from any other state)    /   the user has pressed REJECT button  TODO
  HangingUp,        // waiting for confirmation of BYE/CANCEL request, resending
  HungUp,           // the call has ended, display CALL ENDED to user for some short period of time

  // Callee states
  BeingInvited,     // notifying user of the incoming invite (the phone rings)
  Accept,           // the user has pressed ACCEPT button  -> send 200 OK
  Decline,          // user declined incoming call

  Error,
};

struct QueuedEvent {
  uint32_t  msTriggerAt;
  EventType event;
};

class ControlState {
public:
  ControlState();
  ~ControlState();

  // Keyboard input state

  char inputCurKey;   // current physical button active
  InputType inputType;
  uint8_t inputCurSel;
  bool inputShift;
  char inputSeq[MAX_INPUT_SEQUENCE+1];
  int32_t msAppTimerEventPeriod;
  uint32_t msAppTimerEventLast;

  void setInputState(InputType newInputType);

  // SIP account

  char* fromNameDyn;                // display name
  char* fromUriDyn;                 // SIP URI
  char* proxyPassDyn;               // proxy password
  char* global_UDP_TCP_SIP;         //UDP-SIP or TCP-SIP selected

  bool sipAccountChanged = false;   // does the SIP proxy requires (re)connecting? (this is set to true whenever user changes preferred account)
  bool sipEnabled = false;          // is the phone set to connect to the SIP proxy? (used for the SIP icon)
  bool sipRegistered = false;       // is the phone registered at the SIP proxy? (used for the SIP icon)

  bool loadSipAccount();            // load primary (preferred / default) SIP account from flash to RAM
  void setSipAccount(const char* dispName, const char* uri, const char* passwd, const char* UDP_TCP_SIP_Selection);            // use the supplied SIP account (store it in RAM)
  void removeSipAccount();          // remove account from RAM (and don't reconnect in future)
  bool isCallPossible() {
    return this->sipRegistered && ! this->sipAccountChanged;
  };
  bool hasSipAccount() {
    return fromUriDyn != NULL && *fromUriDyn;
  };

  // SIP/Call state

  CallState sipState = CallState::NotInited;
  char* calleeNameDyn;
  char* calleeUriDyn;
  char* lastReasonDyn;

  void setRemoteNameUri(const char* dispName, const char* uri);
  void setSipState(CallState state);
  void setSipReason(const char* text);

  // Ringtone & ringtone vibration
  bool ringing = false;
  bool vibroOn = false;               // is vibration motor ON?
  uint32_t vibroToggledMs = 0;        // last time the vibration motor got toggled
  uint16_t vibroOnPeriodMs = 500;     // period of vibration for the vibration motor at a time (ON period)
  uint16_t vibroOffPeriodMs = 2500;   // amount of time the vibration motor rests (this should be longer than the ON period)
  uint16_t vibroDelayMs = 3000;       // time before the vibration motor starts vibrating for the first time (usually it's sum of ON and OFF periods)
  uint16_t vibroNextDelayMs;          // next time delay before toggling the vibration motor state

  // Messages
  bool unreadMessages = false;        // are there any unread messages?
  MessagesArray outgoingMessages;     // Messages to be sent via TinySIP class
  MessagesArray outgoingLoraMessages;

  // RSSI
  int16_t wifiRssi = 0;

  // Battery & power
  bool battUpdated;
  float battVoltage;
  float battSoc;        // state of charge
  bool battCharged;
  bool usbConnected = false;
  bool cardPresent = false;
  bool battBlinkOn = false;       // is the additional section of battery is ON? (while blinking the battery indicator)

  // ICs inited or not?
  bool psramInited = false;
  bool gaugeInited = false;
  bool codecInited = false;
  bool scannerInited = false;
  bool extenderInited = false;
  bool booted = false;

  // Keyboard lock
  bool locking = true;                // Is the keyboard locking is ON?
  bool locked = false;                // Is the keyboard currently locked?
  uint8_t unlockButton1 = 0;          // What first unlock button was pressed? (Determines what needs to be pressed next.)

  // Screen dimming & sleep
  bool screenWakeUp = false;          // event to signify that screen needs to be completely redrawn before restoring brightness
  uint8_t screenBrightness = 100;     // current brightness of the screen; if 0 - screen is sleeping
  bool dimming = true;                // is dimming enabled?
  uint8_t brightLevel = 100;          // maximum screen brightness (active)
  uint8_t dimLevel = 15;              // minimum sceeen brightness (dim)
  uint32_t dimAfterMs = 20000;        // after what time after key press to start dimming the screen
  bool sleeping = true;               // is sleeping enabled?
  uint32_t sleepAfterMs = 30000;      // after what time after key press to turn off the screen (should be higher than dimAfterMs)

  bool doDimming() {
    return this->dimming && this->dimAfterMs > 0 && this->dimAfterMs <= 86400000;
  }
  bool doSleeping() {
    return this->sleeping && this->sleepAfterMs > 0 && this->sleepAfterMs <= 86400000;
  }

  // Event queue
  LinearArray<QueuedEvent, LA_INTERNAL_RAM> eventQueue;
  bool scheduleEvent(EventType event, uint32_t msTriggerAt);
  void unscheduleEvent(EventType event);      // remove all events of this type
  EventType popEvent(uint32_t msNow);

public:
  static constexpr unsigned MAX_EVENTS = 128;

  // User serial
  RingBuffer<char> userSerialBuffer;      // size initialized in the constructor

  // LED app
  // TODO: make a simple and nice API for controlling pins
  bool ledPleaseTurnOn = false;
  bool ledPleaseTurnOff = false;

protected:
  void clearDynamicSip();
  void clearDynamicCallee();
};

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  MENUS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

// Menu actions
typedef enum ActionID : uint16_t {
  NO_ACTION = 0,
  GUI_ACTION_MAINMENU,
  GUI_ACTION_SUBMENU,
  GUI_ACTION_RESTART,

  // Specific applications
  GUI_BASE_APP = 0x4000,      // application flag

  // My Section
  GUI_APP_MYAPP,      // add your app like this
  GUI_APP_OTA,

  // Interface
  GUI_APP_MENU,
  GUI_APP_CLOCK,
  GUI_APP_SPLASH,

  // Calling
  GUI_APP_CALL,
  GUI_APP_DIALING,
  GUI_APP_PHONEBOOK,
  GUI_APP_SIP_ACCOUNTS,

  // Messages
  GUI_APP_MESSAGES,
  GUI_APP_VIEW_MESSAGE,
  GUI_APP_CREATE_MESSAGE,

  // Tools
  GUI_APP_NOTEPAD,
  GUI_APP_UDP,
  GUI_APP_MOTOR,
  GUI_APP_LED_MIC,
  GUI_APP_PARCEL,
  GUI_APP_PIN_CONTROL,
  GUI_APP_DIAGNOSTICS,
  GUI_APP_RECORDER,

  // Configs
  GUI_APP_EDITWIFI,
  GUI_APP_NETWORKS,
  GUI_APP_AUDIO_CONFIG,
  GUI_APP_WIFI_CONFIG,
  GUI_APP_TIME_CONFIG,
  GUI_APP_SCREEN_CONFIG,

  // Test apps
  GUI_APP_CIRCLES,
  GUI_APP_WIDGETS,
  GUI_APP_PICS_DEMO,
  GUI_APP_FONT_DEMO,
  GUI_APP_DESIGN_DEMO,
  GUI_APP_MIC_TEST,
  GUI_APP_DIGITAL_RAIN,
  GUI_APP_UART_PASS,

  // Games
  GUI_APP_FIDE_CHESS,
  GUI_APP_CHESS960,
  GUI_APP_HILL_CHESS,
  GUI_APP_ACKMAN,

} ActionID_t;

typedef struct GUIMenuItem {
  const int16_t ID;
  const int16_t parent;
  const char* const title;
  const char* const leftButton;
  const char* const rightButton;
  const ActionID_t action;
} GUIMenuItem;

typedef struct GUIMenuItemIcons {
  const int16_t ID;
  const unsigned char* icon1;       // icon in regular (not selected) state
  const uint16_t iconSize1;
  const unsigned char* icon2;       // icon in selected state
  const uint16_t iconSize2;
} GUIMenuItemIcons;

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  WIDGETS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

// Widget object bearing no data about its position and size
class AbstractWidget {
public:
  virtual bool processEvent(EventType event) = 0;         // return true if the event was relevant (processed); false - if ignored
  virtual void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) = 0;
};

// Base widget class with position and size data
class GUIWidget : public AbstractWidget {
protected:
  uint16_t parentOffX;          // position inside parent: screen or other widget (not supported yet)
  uint16_t parentOffY;
  uint16_t widgetWidth;         // widget size
  uint16_t widgetHeight;

  bool updated = true;          // TODO: is this used? remove?

public:
  GUIWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height) : parentOffX(posX), parentOffY(posY), widgetWidth(width), widgetHeight(height) {}
  virtual ~GUIWidget() {}

  // this widget has no parent -> draw directly on screen (visibility window exactly matches dimensions)
  void redraw(LCD &lcd) {
    redraw(lcd, parentOffX, parentOffY, widgetWidth, widgetHeight);
  };
  virtual void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) = 0;
  void refresh(LCD &lcd, bool redrawAll) {
    refresh(lcd, redrawAll, parentOffX, parentOffY, widgetWidth, widgetHeight);
  }
  void refresh(LCD &lcd, bool redrawAll, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight) {
    if (this->updated || redrawAll) {
      redraw(lcd, parentOffX, parentOffY, widgetWidth, widgetHeight);
      this->updated = false;
    }
  }
  bool isUpdated() {
    return this->updated;
  }

  virtual bool focusable() = 0;
  virtual void setFocus(bool focus) = 0;
  virtual bool getFocus() = 0;

  // Methods to get position (inside parent) and size
  void getPositionSize(uint16_t &px, uint16_t &py, uint16_t &ww, uint16_t &wh) {
    px = parentOffX;
    py = parentOffY;
    ww = widgetWidth;
    wh = widgetHeight;
  }
  inline uint16_t getParentOffX() {
    return parentOffX;
  }
  inline uint16_t getParentOffY() {
    return parentOffY;
  }
  inline uint16_t width()   {
    return widgetWidth;
  }
  inline uint16_t height()  {
    return widgetHeight;
  }

  // A helper function to handle the extra (321st or 241st) pixel on the screen when drawing rectangles
  static void corrRect(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight, uint16_t color);

protected:
  void clear(LCD &lcd, uint16_t col=BLACK);     // useful for updated labels
};

class FocusableWidget : public GUIWidget {
public:
  FocusableWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height) : GUIWidget(posX, posY, width, height) {
    this->focused = false;
    this->active = true;
  }
  bool focusable()  {
    return true;
  }
  void setFocus(bool focus) {
    this->focused = focus;
    this->updated = true;
  }
  bool getFocus()   {
    return this->focused;
  }
  bool getActive()  {
    return this->active;
  }
  void activate()   {
    this->active = true;
  }
  void deactivate() {
    this->active = false;
  }
protected:
  bool focused;
  bool active;      // can focus be set on this widget at this time?
};

class NonFocusableWidget : public GUIWidget {
public:
  NonFocusableWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height) : GUIWidget(posX, posY, width, height) {}
  bool focusable()  {
    return false;
  }
  void setFocus(bool focus) {}
  bool getFocus()   {
    return false;
  }
};

class RectWidget : public NonFocusableWidget {
public:
  RectWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, uint16_t color);

  /* virtuals from GUIWidget */
  bool processEvent(EventType event) {
    return false;
  }
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

protected:
  uint16_t color;
};

// Rectangle with an icon in the middle
class RectIconWidget : public RectWidget {
public:
  RectIconWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, uint16_t color, const uint8_t* iconData, const size_t iconSize);
  virtual ~RectIconWidget();

  /* virtuals from GUIWidget */
  bool processEvent(EventType event) {
    return false;
  }
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

protected:
  IconRle3* icon = NULL;
};

// Horizontal line to separate different groups of widgets visually (could be called LineWidget)
class RulerWidget : public NonFocusableWidget {
public:
  RulerWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t color=GRAY_75);

  /* virtuals from GUIWidget */
  bool processEvent(EventType event) {
    return false;
  }
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

protected:
  uint16_t color;
};

class LabelWidget : public NonFocusableWidget {
public:
  // Text is centered vertically. These are the settings for horizontal alignment.
  typedef enum TextDirection {
    LEFT_TO_RIGHT = 0,
    RIGHT_TO_LEFT,
    CENTER,
  } TextDirection_t;

  LabelWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height,
              const char* p,
              uint16_t col=WHITE, uint16_t bg=BLACK, SmoothFont* font=NULL, TextDirection_t orient=LEFT_TO_RIGHT, uint16_t xPadding=0);
  virtual ~LabelWidget();

  /* GUIWidget virtuals */
  bool processEvent(EventType event) {
    return false;
  }
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

  void setText(const char* p);
  void setColors(colorType textColor, colorType bgColor);

protected:
  SmoothFont* widgetFont;
  colorType textColor;
  colorType bgColor;
  uint8_t textDirection;
  uint16_t xPadding;
  char* textDyn;
};

class ButtonWidget : public FocusableWidget {
public:
  ButtonWidget(uint16_t posX, uint16_t posY, const char* title,
               uint16_t width = 0, uint16_t height = 30,            // if width is zero, the widget width will be inferred from text width
               colorType col=WP_COLOR_0, colorType bgCol=WP_ACCENT_0, colorType border=GRAY_95, colorType sel=WP_COLOR_1, colorType selBg=WP_COLOR_0);
  virtual ~ButtonWidget();
  void setText(const char* str);
  void setColors(colorType fg, colorType bg, colorType border=GRAY_95, colorType sel=WP_COLOR_1, colorType selBg=WP_COLOR_0);
  bool processEvent(EventType event);
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
  bool readPressed();
  static int32_t textWidth(const char* str);
protected:
  const char* titleDyn = NULL;
  uint8_t fontSize;
  bool pressed;

  // Colors
  colorType textColor;
  colorType bgColor;
  colorType borderColor;
  colorType selTextColor;
  colorType selBgColor;
};

class SliderWidget : public FocusableWidget {
public:
  SliderWidget(uint16_t posX, uint16_t posY,
               uint16_t width, uint16_t height,
               colorType color=WP_ACCENT_0, colorType selectedColor=WP_ACCENT_S, colorType bgColor=WP_COLOR_1, colorType textColor=WP_COLOR_0);
protected:
  static const uint16_t dotRadius = 6;
  static const uint16_t lineHeight = 2;
  void drawSlider(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight, colorType color, float pos);
  colorType mainColor;
  colorType selectedColor;
  colorType bgColor;
  colorType textColor;
};

class IntegerSliderWidget : public SliderWidget {
public:
  IntegerSliderWidget(uint16_t posX, uint16_t posY,
                      uint16_t width, uint16_t height,
                      int minValue, int maxValue, int step, bool showText, const char* unit = NULL,
                      colorType color=WP_ACCENT_0, colorType selectedColor=WP_ACCENT_S, colorType bgColor=WP_COLOR_1, colorType textColor=WP_COLOR_0);
  virtual ~IntegerSliderWidget();
  bool processEvent(EventType event);
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
  void setValue(int value);
  int getValue() {
    return val;
  };
protected:
  const uint16_t smoothFont = AKROBAT_BOLD_18;
  const char* unit = NULL;
  int minVal;
  int maxVal;
  int val;
  int step;
  uint16_t maxTextWidth = 0;     // if zero - don't show text
};

//class ScreenWidget : public NonFocusableWidget {
// public:
//  RectWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, uint16_t col) : NonFocusableWidget(posX, posY, width, height) {
//    bg = col;
//  };
//  bool processEvent(EventType event);
//  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
//  void append(GUIWidget* w);
// protected:
//  GUIWidget** widgets;
//  uint16_t bg;
//};

class HeaderWidget : public NonFocusableWidget {
public:
  HeaderWidget(const char* s, ControlState& state)
    : NonFocusableWidget(0, 0, TFT_WIDTH, THEME_HEADER_SIZE), title(s), controlState(state) {
  };       // TODO: remove TFT_WIDTH?
  void setTitle(const char* s) {
    title = s;
  }
  bool processEvent(EventType event) {
    return false;
  }
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
protected:
  const char* title;
  ControlState& controlState;
};

class FooterWidget : public NonFocusableWidget {
public:
  FooterWidget(const char* left, const char* right, ControlState& state) :
    NonFocusableWidget(0, TFT_HEIGHT - THEME_FOOTER_SIZE, TFT_WIDTH, THEME_FOOTER_SIZE),            // TODO: remove TFT_HEIGHT and TFT_WIDTH?
    leftButtonName(left), rightButtonName(right), controlState(state) {};
  void setButtons(const char* left, const char* right) {
    leftButtonName = left;
    rightButtonName = right;
  };
  bool processEvent(EventType event) {
    return false;
  }
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
protected:
  const char* leftButtonName;
  const char* rightButtonName;
  ControlState& controlState;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Choice widget logic  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class ChoiceWidget : public FocusableWidget {
public:
  ChoiceWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, SmoothFont* font=NULL,
               colorType textColor=WP_COLOR_0, colorType bgColor=WP_COLOR_1, colorType regColor=WP_ACCENT_1, colorType selectedColor=WP_ACCENT_S);
  ~ChoiceWidget();

  bool processEvent(EventType event);
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

  void addChoice(const char* name);
  void setFocus(bool focus) {
    this->focused = focus;
    this->updated = true;
  }

  typedef uint16_t ChoiceValue;       // type of indexes of the choices
  void setValue(ChoiceValue val);
  ChoiceValue getValue() {
    return this->curChoice;
  }

protected:
  ChoiceValue curChoice;      // index of the current choice
  LinearArray<char*, LA_EXTERNAL_RAM> choices;

  // Appearance
  const uint8_t arrowWidth = 6;
  const uint8_t arrowPad = 5;

  SmoothFont* widgetFont;
  colorType textColor;        // color of text when not selected
  colorType bgColor;          // background color
  colorType regColor;         // color of arrows when not selected
  colorType selColor;         // color of arrows and text when selected
};

class YesNoWidget : public ChoiceWidget {
public:
  YesNoWidget(uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, SmoothFont* font=NULL,
              colorType textColor=WP_COLOR_0, colorType bgColor=WP_COLOR_1, colorType regColor=WP_ACCENT_1, colorType selectedColor=WP_ACCENT_S);

  void setValue(bool val) {
    ((ChoiceWidget*)this)->setValue((ChoiceValue) val);
  }
  bool getValue() {
    return (bool)this->curChoice;
  }
};

class MessageBox : public FocusableWidget {
public:
  MessageBox(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
             const char* message, const char** buttons, size_t buttons_len);

  const char* show();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  Text input logic  - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class TextInputAbstract : public FocusableWidget {
public:
  TextInputAbstract(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                    ControlState& state,
                    SmoothFont* font=NULL, uint32_t maxInputSize=64000, InputType typ=InputType::Numeric);

  virtual void setText(const char* str) = 0;
  virtual const char* getText() = 0;

  void setFocus(bool focus);

  void setColors(colorType fg, colorType bg);

protected:
  uint32_t maxInputSize;

  // Appearance
  SmoothFont* widgetFont;
  colorType fgColor = WP_COLOR_0;
  colorType bgColor = WP_DISAB_1;

  // Input type control
  ControlState& controlState;
  InputType inputType;

  void drawCursor(LCD &lcd, uint16_t posX, uint16_t posY, uint16_t charHeight, uint16_t color);     // short vertical line
};

// Text input built around a single linear string of text
class TextInputBase : public TextInputAbstract {
public:
  TextInputBase(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                ControlState& state,
                SmoothFont* font=NULL, uint32_t maxInputSize=100, InputType typ=InputType::Numeric);
  virtual ~TextInputBase();

  bool getInt(int32_t &i);
  void setInt(int32_t i);

  /* TextInputAbstract virtuals */
  virtual void setText(const char* str);
  virtual const char* getText() {
    return inputStringDyn != NULL ? inputStringDyn : "";
  };

  /* Own virtuals */
  virtual bool insertCharacter(char c) = 0;

  virtual bool allocateMore(uint32_t minSize=0);

protected:
  // Text string
  char* inputStringDyn;
  uint32_t inputStringSize;

  // Text & cursor offsets
  uint32_t textOffset;        // inputStringDyn[textOffset]   - first visible character
  uint32_t cursorOffset;      // inputStringDyn[cursorOffset] - character next after cursor
};

// Widget similar to HTML <textarea>, can be use to display and input text
class MultilineTextWidget : public TextInputAbstract {
public:
  MultilineTextWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                      const char* emptyText, ControlState& state,
                      uint32_t maxInputSize, SmoothFont* font=NULL, InputType typ=InputType::AlphaNum, uint16_t xPadding=0, uint16_t yPadding=0);
  virtual ~MultilineTextWidget();

  int getCursorRow() {
    return this->cursRow;
  };
  void verticalCentering(bool p) {
    centering = p;
  };
  void cursorToStart();

  /* TextInputAbstract virtuals */
  void setText(const char* str);
  const char* getText();
  void appendText(const char* str);

  /* GUIWidget virtuals */
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
  bool processEvent(EventType event);

protected:
  char** rowsDyn;             // lines of text      TODO: maybe use LinearArray?
  int maxRows;                // maximum number of rows (by allocated size of the rowsDyn array)
  char* retTextDyn;           // resulting full text (for getText)
  char* emptyTextDyn = NULL;  // text to be shown if the widget is empty

  uint16_t visibleRows;       // number of visible rows per widget
  int firstVisibleRow;

  int cursRow;                // row where the cursor is
  uint16_t cursOffset;        // cursor offset inside a row

  uint16_t xPadding;          // appearance
  uint16_t yPadding;
  bool centering = false;

  bool allocateMore(int minSize=0);
  void revealCursor();

  bool emptyRow(int row) {
    return row < 0 || row >= maxRows || !rowsDyn[row] || !rowsDyn[row][0];
  };
  bool notEmptyRow(int row) {
    return !emptyRow(row);
  };
  bool newLineRow(int row) {
    return notEmptyRow(row) && rowsDyn[row][strlen(rowsDyn[row])-1]=='\n';
  };
};

// Widget similar to HTML <input>
class TextInputWidget : public TextInputBase {
public:
  TextInputWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                  ControlState& state,
                  uint32_t maxInputSize, SmoothFont* font=NULL, InputType typ=InputType::AlphaNum, uint16_t sidePadding=0);

  /* TextInputBase virtuals */
  bool insertCharacter(char c);

  /* GUIWidget virtuals */
  bool processEvent(EventType event);
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

protected:
  uint16_t xPad;    // side padding

  void revealCursor();
  void shiftCursor(int16_t shift);
};

// Widget similar to HTML <input type="password">
class PasswordInputWidget : public TextInputBase {
public:
  PasswordInputWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height,
                      ControlState& state,
                      uint32_t maxInputSize, SmoothFont* font=NULL, InputType typ=InputType::AlphaNum, uint16_t sidePadding=0);
  ~PasswordInputWidget();

  /* TextInputBase virtuals */
  bool insertCharacter(char c);
  void setText(const char* str);
  bool allocateMore(uint32_t minSize=0);

  /* GUIWidget virtuals */
  bool processEvent(EventType event);
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);

protected:
  uint16_t xPad;    // side padding
  char* outputStringDyn;

  void revealCursor();
  void shiftCursor(int16_t shift);
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -  MenuWidget logic  - - - - - - - - - - - - - - - - - - - - - - - - - - - -


// Menu option with text only. Also a base class for menu options.
class MenuOption {
public:
  typedef uint32_t keyType;

  MenuOption::keyType id;     // MUST be positive
  uint16_t style;     // this refers to one of two styles in MenuWidget     TODO: can make 8-bit
  char* titleDyn;

  MenuOption();
  MenuOption(MenuOption::keyType pId, uint16_t pStyle, const char* title);
  virtual ~MenuOption();

  virtual void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                      colorType fgColor, colorType bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset);
};

// Menu option with icons and subtitles (used for Main Menu, main menu of the message app; allows transparency)
class MenuOptionIconned : public MenuOption {
public:
  static const colorType IGNORED_COLOR = 0x0001;
  colorType selectedBgColor = IGNORED_COLOR;

  char* subTitleDyn;
  uint8_t textLeftOffset;

  IconRle3* icon = NULL;
  IconRle3* iconSelected = NULL;

  MenuOptionIconned(MenuOption::keyType pId, uint16_t pStyle, const char* title, const char* subTitle=NULL,
                    const unsigned char* iconData=NULL, const uint16_t iconSize=0,
                    const unsigned char* selIconData=NULL, const uint16_t selIconSize=0,
                    uint8_t textLeftOffset=12, colorType selBgColor = IGNORED_COLOR);
  virtual ~MenuOptionIconned();

  virtual void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                      colorType fgColor, colorType bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset);
};

// Menu option with date/time (used for message list)
class MenuOptionIconnedTimed : public MenuOptionIconned {
public:
  uint32_t zeit;                    // Unix time of the message received
  uint16_t globalBgColor;           // second style items have visually lower height, hence global "visual" background needs to be provided
  static const uint8_t style2Padding = 7;

  MenuOptionIconnedTimed(MenuOption::keyType ID, uint16_t style, const char* title, const char* subTitle=NULL, uint32_t zeit=0, uint16_t globalBg=WHITE,
                         const unsigned char* iconData=NULL, const uint16_t iconSize=0,
                         const unsigned char* selIconData=NULL, const uint16_t selIconSize=0);
  virtual ~MenuOptionIconnedTimed() {};

  virtual void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                      colorType fgColor, colorType bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset);
};

// Menu option with decorative phone icon (used for phonebook)
class MenuOptionPhonebook : public MenuOptionIconned {
public:
  MenuOptionPhonebook(MenuOption::keyType ID, uint16_t style, const char* title, const char* subTitle=NULL);
  virtual ~MenuOptionPhonebook() {};

  virtual void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight,
                      colorType fgColor, colorType bgColor, bool opaque, bool selected, SmoothFont* font, uint16_t leftOffset);
protected:
  static const uint8_t rightIconOffset = 8;
};

class MenuWidget : public FocusableWidget {
public:
  static const uint8_t DEFAULT_STYLE = 1;
  static const uint8_t ALTERNATE_STYLE = 2;

  MenuWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height, const char* emptyMessage,
             SmoothFont* font=NULL, uint8_t itemsPerScreen=0, uint16_t leftOffset=0, bool opaque=true);
  virtual ~MenuWidget();
  bool processEvent(EventType event);
  void redraw(LCD &lcd, uint16_t screenOffX, uint16_t screenOffY, uint16_t windowWidth, uint16_t windowHeight);
  void setDrawOnce() {
    drawOnce = true;
  };
  void setStyle(uint8_t styleNum, colorType textCol, colorType bgCol, colorType selCol);
  void setStyle(uint8_t styleNum, colorType textCol, colorType bgCol, colorType selTextCol, colorType selBgCol);

  // One option was chosen (OK pressed on selected)
  MenuOption::keyType readChosen();
  const char* readChosenTitle();

  MenuOption::keyType currentKey() {
    return options.size() ? options[optionSelectedIndex]->id : 0;
  }
  bool isSelectedLast() {
    return optionSelectedIndex + 1 == options.size();
  }
  bool isSelectedFirst() {
    return optionSelectedIndex == 0;
  }
  size_t size() {
    return options.size();
  }

  void deleteAll();
  void reset() {
    optionSelectedIndex = optionOffsetIndex = 0;
  };

  bool addOption(MenuOption* option);
  void addOption(const char* title);
  void addOption(const char* title, MenuOption::keyType key, uint16_t style=1);
  void addOption(const char* title, const char* subTitle, MenuOption::keyType key, uint16_t style,
                 const unsigned char* iconData, const uint16_t iconSize,
                 const unsigned char* iconSelData = NULL, const uint16_t iconSelSize = 0);

  //void removeOption(MenuOption::keyType key);
  //void sortOptions();

  void select(MenuOption::keyType key);
  void selectLastOption();
  void revealSelected();
  const char* getSelectedTitle();

protected:
  LinearArray<MenuOption*, LA_EXTERNAL_RAM>  options;  // array of pointers to MenuOption objects
  uint16_t optionSelectedIndex;
  uint16_t optionOffsetIndex;
  MenuOption::keyType chosenKey;

  // Appearance
  bool opaque;                              // false implies the widget must not overwrite its background (be transparent)
  uint16_t leftOffset;                      // offset (or padding) on the left in each menu option redrawing
  uint16_t optionHeight;                    // height of each MenuOption in this menu
  uint16_t optionsVisible;                  // options visible per screen
  static const uint8_t spacing = 2;         // spacing between items when N_MAX_ITEMS is used
  SmoothFont* widgetFont;
  char* emptyMessageDyn = NULL;             // text to be displayed if the widget is empty

  // Two styles (color schemes)
  colorType style1TextColor = BLACK;
  colorType style1BgColor = WHITE;
  colorType style1SelTextColor = WP_COLOR_1;
  colorType style1SelBgColor = WP_ACCENT_1;

  colorType style2TextColor = BLACK;
  colorType style2BgColor = GRAY_95;
  colorType style2SelTextColor = BLACK;
  colorType style2SelBgColor = MAGENTA;

  // Drawing optimization flags
  bool drawOnce;    // need to redraw entirely
  bool drawItems;   // items were changed (added) -> need to redraw items
  bool drawScroll;  // all items need to be redrawn

  bool allocateMore(uint16_t newMaxCount=0);
  //void restoreOrderLast(uint16_t i);
};

// A special recognizable style of MenuWidget used for options menu
class OptionsMenuWidget : public MenuWidget {
public:
  OptionsMenuWidget(uint16_t xPos, uint16_t yPos, uint16_t width, uint16_t height);
};

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  APPS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

class WiPhoneApp {
public:
  WiPhoneApp(LCD& disp, ControlState& state);
  virtual ~WiPhoneApp();
  virtual ActionID_t getId() {
    return NO_ACTION;
  };
  virtual bool isWindowed() {
    return false;
  };
  virtual appEventResult processEvent(EventType event) {
    return DO_NOTHING;
  };       // return "false" to trigger exit from the app (like "Back" button pressed)
  virtual void redrawScreen(bool redrawAll=false) {};
  void resetPush() {
    pushed = false;
  };
  void pushScreen();
  virtual LCD& getScreen() {
    return lcd;
  };

  void registerWidget(GUIWidget* w);

  // Standard design helpers
  void addLabelInput(uint16_t &yOff, LabelWidget*& label, TextInputWidget*& input, const char* labelText, const uint32_t inputSize, InputType inputType = InputType::AlphaNum);
  void addLabelPassword(uint16_t &yOff, LabelWidget*& label, PasswordInputWidget*& input, const char* labelText, const uint32_t inputSize, InputType inputType = InputType::AlphaNum);
  void addInlineLabelInput(uint16_t &yOff, uint16_t labelWidth, LabelWidget*& label, TextInputWidget*& input, const char* labelText, const uint32_t inputSize, InputType inputType = InputType::AlphaNum);
  void addDoubleLabelInput(uint16_t &yOff, LabelWidget*& label1, TextInputWidget*& input1, const char* labelText1, const uint32_t inputSize1,
                           LabelWidget*& label2, TextInputWidget*& input2, const char* labelText2, const uint32_t inputSize2, InputType inputType = InputType::AlphaNum);
  void addLabelSlider(uint16_t &yOff, LabelWidget*& label, IntegerSliderWidget*& input, const char* labelText, int minVal, int maxVal, const char* unit = NULL, int steps = 12);
  void addInlineLabelSlider(uint16_t &yOff, uint16_t labelWidth, LabelWidget*& label, IntegerSliderWidget*& input, const char* labelText, int minVal, int maxVal, const char* unit = NULL, int steps = 12);
  void addInlineLabelYesNo(uint16_t &yOff, uint16_t labelWidth, LabelWidget*& label, YesNoWidget*& input, const char* labelText);
  void addRuler(uint16_t &yOff, RulerWidget*& ruler, uint16_t addOffset = 3);

protected:
  LCD& lcd;
  ControlState& controlState;       // TODO: make this a global variable?
  int32_t anyEventPeriodStack;
  uint32_t anyEventLastStack;
  bool pushed = false;              // backwards compatibility: each app remembers if it was already pushed after redraw screen (so that app can push itself in redraw and can be pushed by GUI)
  LinearArray<GUIWidget*, LA_INTERNAL_RAM> registeredWidgets;
};

// App that manages focus for focusable widgets
class FocusableApp {
public:
  FocusableApp(int size) : focusableWidgets(size) {};
  void addFocusableWidget(FocusableWidget* w);
  void nextFocus(bool forward=true);
  FocusableWidget* getFocused();
  void setFocus(FocusableWidget* w);
  void deactivateFocusable();
protected:
  LinearArray<FocusableWidget*, LA_INTERNAL_RAM> focusableWidgets;
};

// App that has header and footer widgets
class WindowedApp : public WiPhoneApp {
public:
  WindowedApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~WindowedApp() {};
  bool isWindowed() {
    return true;
  }

protected:
  HeaderWidget* header;
  FooterWidget* footer;
};

class ThreadedApp : public WiPhoneApp {
public:
  ThreadedApp(LCD& disp, ControlState& state);
  ~ThreadedApp();
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false) {}
protected:
  //static virtual void thread(void *pvParam) {}
  TaskHandle_t xHandle;
};

class OtaApp : public WindowedApp, FocusableApp {
public:
  OtaApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~OtaApp();
  ActionID_t getId() {
    return GUI_APP_OTA;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:

  void setDataFromOtaFile(Ota &o,bool errorAsUpdate=false);

  bool screenInited = false;

  // WIDGETS
  RectWidget* clearRect;

  LabelWidget*  urlLabel;
  LabelWidget*  autoLabel;

  TextInputWidget* url;
  ChoiceWidget* autoUpdate;

  LabelWidget* deviceVersion;
  LabelWidget* lastInstall;

  ButtonWidget* checkForUpdates;
  ButtonWidget* reset;
  ButtonWidget* installUpdates;

  bool updateAvailable;
  bool manualUpdateRequested;
  bool manualCheckRequested;
  bool installBtnAdded;
};

class MyApp : public WindowedApp, FocusableApp {
public:
  MyApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~MyApp();
  ActionID_t getId() {
    return GUI_APP_MYAPP;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  Audio* audio;

  bool screenInited = false;

  // WIDGETS
  RectWidget* clearRect;
  RectIconWidget* iconRect;

  LabelWidget*  demoCaption;
  LabelWidget*  debugCaption;
};

class UartPassthroughApp :  public WindowedApp, FocusableApp {
public:
  UartPassthroughApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  ~UartPassthroughApp();
  ActionID_t getId() {
    return GUI_APP_UART_PASS;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  struct uartThreadParams {
    uart_port_t rxPort;
    uart_port_t txPort;
  };
  static void thread(void *pvParam);
  TaskHandle_t xHandle0, xHandle1;
  bool screenInited;
  bool startedSerial;

  uartThreadParams uart0Thread, uart1Thread;

  RectWidget* clearRect;
  LabelWidget*  baudLabel;
  LabelWidget*  echoLabel;
  TextInputWidget* baud;
  ButtonWidget* startStop;
  ChoiceWidget* echo;
};


class DigitalRainApp : public ThreadedApp {
public:
  DigitalRainApp(LCD& disp, ControlState& state);
  ~DigitalRainApp();
  ActionID_t getId() {
    return GUI_APP_DIGITAL_RAIN;
  };
protected:
  static void thread(void *pvParam);
  void clear();
  void draw();
  void drawMirroredChar(char c, uint16_t x, uint16_t y, colorType color);
  char randPrintable();

  char text[39][40];
  uint8_t brightness[39][40];
  TFT_eSprite sprite;
};

class CircleApp : public WiPhoneApp {
public:
  CircleApp(LCD& disp, ControlState& state);
  virtual ~CircleApp();
  ActionID_t getId() {
    return GUI_APP_CIRCLES;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  bool anyPressed;
};

class WidgetDemoApp : public WiPhoneApp {
public:
  WidgetDemoApp(LCD& disp, ControlState& state);
  virtual ~WidgetDemoApp();
  ActionID_t getId() {
    return GUI_APP_WIDGETS;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  GUIWidget* widgets[3];
  LabelWidget* label;
};

class PicturesDemoApp : public WiPhoneApp {
public:
  PicturesDemoApp(LCD& disp, ControlState& state);
  virtual ~PicturesDemoApp();
  ActionID_t getId() {
    return GUI_APP_PICS_DEMO;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  uint8_t pic = 0;
};

class FontDemoApp : public WiPhoneApp {
public:
  FontDemoApp(LCD& disp, ControlState& state);
  virtual ~FontDemoApp();
  ActionID_t getId() {
    return GUI_APP_FONT_DEMO;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  uint8_t curFontIndex = 0;
  bool smooth = true;
};

class DesignDemoApp : public WiPhoneApp {
public:
  DesignDemoApp(LCD& disp, ControlState& state);
  virtual ~DesignDemoApp();
  ActionID_t getId() {
    return GUI_APP_DESIGN_DEMO;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  uint8_t screenNo = 0;
};

class ClockApp : public WiPhoneApp {
public:
  ClockApp(LCD& disp, TFT_eSprite& bgImg, ControlState& state);
  virtual ~ClockApp();
  ActionID_t getId() {
    return GUI_APP_CLOCK;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  TFT_eSprite &bgImg;
  bool messageIconShown = false;
};

class SplashApp : public WiPhoneApp {
public:
  SplashApp(LCD& disp, ControlState& state);
  virtual ~SplashApp();
  ActionID_t getId() {
    return GUI_APP_SPLASH;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  int screenNo = 0;
};

class MessagesApp : public WindowedApp {
public:
  MessagesApp(LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer);
  virtual ~MessagesApp();
  ActionID_t getId() {
    return GUI_APP_MESSAGES;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static const bool INCOMING = true;
  static const bool SENT = false;

  typedef enum {
    MAIN,
    INBOX,
    OUTBOX,
    COMPOSING,
  } MessagesState_t;

  MenuWidget* mainMenu = NULL;
  MenuWidget* inboxMenu = NULL;
  MenuWidget* sentMenu = NULL;
  Storage& flash;
  WiPhoneApp* subApp = NULL;            // can be CreateMessageApp or ViewMessageApp

  MessagesState_t appState = MAIN;
  void enterState(MessagesState_t state);

  int32_t inboxOffset = -1;
  int32_t inboxSelected = -1;

  int32_t sentOffset = -1;
  int32_t sentSelected = -1;

  void createMainMenu();
  void createLoadMessageMenu(bool incoming, int32_t offset, MenuOption::keyType selectKey);

  MenuOption::keyType encodeMessageOffset(int32_t offset);
  int32_t decodeMessageOffset(MenuOption::keyType key);
};

class ViewMessageApp : public WindowedApp {
public:
  ViewMessageApp(int32_t messageOffset, LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer);
  ~ViewMessageApp();
  ActionID_t getId() {
    return GUI_APP_VIEW_MESSAGE;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

  friend class MessagesApp;

protected:
  Storage& flash;
  MultilineTextWidget* textArea;
  OptionsMenuWidget* options = NULL;

  typedef enum ViewMessageState { MAIN, OPTIONS } ViewMessageState_t;
  ViewMessageState_t appState = MAIN;
  appEventResult changeState(ViewMessageState_t newState);

  WiPhoneApp* subApp = NULL;        // can be CreateMessageApp (in responseMode)
  bool messageSent = false;

  int32_t messageOffset;            // stores the message offset in the `preloaded` array
};

class CreateMessageApp : public WindowedApp, FocusableApp {
public:
  CreateMessageApp(LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer, const char* sipUri=NULL);
  ~CreateMessageApp();
  ActionID_t getId() {
    return GUI_APP_CREATE_MESSAGE;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  LabelWidget* label1;
  TextInputWidget* addr;
  LabelWidget* label2;
  MultilineTextWidget* text;
  ChoiceWidget* sendMessageAs;

  Storage& flash;

  void setHeaderFooter();
  WiPhoneApp* subApp;       // can be PhonebookApp (in pick mode)

private:
  void setupUI(const char* sipUri, bool showMessageType=false);
  void deleteUI();
  bool isSipAddress(const char* address);
  bool hasSipAndLora(const char* address);
  const char* extractAddress(const char* address, MessageType_t type);
};

class MicTestApp : public WindowedApp {
public:
  MicTestApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~MicTestApp();
  ActionID_t getId() {
    return GUI_APP_MIC_TEST;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  Audio* audio;
};

class RecorderApp : public WindowedApp {
public:
  RecorderApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~RecorderApp();
  ActionID_t getId() {
    return GUI_APP_RECORDER;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  Audio* audio;
  TFT_eSprite sprite;
  LabelWidget* label;
  bool screenInited = false;
  bool spriteUpdated = false;
  bool recording = false;
  bool recorded = false;
  uint16_t microphoneValues[160];
  int curVal = 0;
  char filename[100];
};

//class DiagnosticsApp : public WindowedApp, FocusableApp {
class DiagnosticsApp : public WiPhoneApp {
public:
  //DiagnosticsApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  DiagnosticsApp(Audio* audio, LCD& disp, ControlState& state);
  virtual ~DiagnosticsApp();
  ActionID_t getId() {
    return GUI_APP_DIAGNOSTICS;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  // Variables initialized before constructor is called
  Audio* audio;
  RingBuffer<float> lastVoltages;     // size initialized in the constructor
  RingBuffer<float> lastSocs;         // size initialized in the constructor

  // App state
  typedef enum DiagnosticsView { MAIN, NETWORKS, FILESYSTEMS, AUDIO, CONTROL, SCREEN, KEYPAD, CORE, OPTIONS } DiagnosticsView_t;
  DiagnosticsView_t appState = MAIN;
  void changeState(DiagnosticsView_t newState);
//  typedef enum DiagnosticsState { PSRAM = 0, KEYPAD } DiagnosticsState_t;
//  DiagnosticsState_t appState;
//  typedef enum TestResult { FAILED = 0, SUCCESS, NOT_AVAILABLE } TestResult_t;
//  TestResult_t tests[25];

  // Widgets

  // - Main
  ButtonWidget* bVoltage = NULL;
  ButtonWidget* bStateOfCharge = NULL;
  ButtonWidget* bCardPresence = NULL;
  ButtonWidget* bUsbPresence = NULL;
  ButtonWidget* bAutonomous = NULL;
  ButtonWidget* bCharging = NULL;
  ButtonWidget* bVersion = NULL;
  ButtonWidget* bMacAddress = NULL;
  ButtonWidget* bIpAddress = NULL;
  ButtonWidget* bRSSI = NULL;
  ButtonWidget* bUptime = NULL;

  // -- ICs
  ButtonWidget* bBatteryGauge = NULL;
  ButtonWidget* bKeyScanner = NULL;
  ButtonWidget* bGpioExtender = NULL;
  ButtonWidget* bSpiRam = NULL;
  ButtonWidget* bCodec = NULL;

  // - Network
  ButtonWidget* bbPings[2]; //[8];

  // - Filesystems
  /*ButtonWidget* testEarSpeaker = NULL;
  ButtonWidget* testLoudSpeaker = NULL;
  ButtonWidget* testHeadphone = NULL;*/

  // - Screen
  int screenStep = 0;

  // - Keypad test
  static const int EXIT_CNT = 5;
  ButtonWidget* bbKeys[25];
  uint8_t keyPressed[25];
  bool anyKeyPressed;
  //MultilineTextWidget* details;

  bool screenInited = true;

  const colorType redBg = 0xf9a6;         // 255, 55, 55
  const colorType redBorder = TFT_RED;
  const colorType greenBg = TFT_GREEN;
  const colorType greenBorder = 0x5c0;    // 0, 185, 0
  const colorType yellowBg = TFT_YELLOW;
  const colorType yellowBorder = 0xb580;  // 177, 177, 0
  const colorType greyBg = GRAY_50;
  const colorType greyBorder = GRAY_33;
  const colorType blueBg = GRAY_50 | TFT_BLUE;
  const colorType blueBorder = GRAY_33 | (TFT_BLUE>>1);

  int8_t lastSd = 2;
  int8_t lastUsb = 2;
  int8_t lastAutonomous;
  int8_t lastCharging = 2;      // 1 - rising, 0 - not known, -1 - falling
  IPAddress lastIpAddr;
  int16_t lastRssi = -120;
  bool lastScannerInited = false;
  bool lastCodecInited = false;
  int8_t  nextToPing = 0;
  bool pingedAll = false;
  uint8_t lastUptimeClosing = 99;
  uint8_t dbCounter = 0;

  void updateVoltage();
  void updateUsb();
  void updateIP();
  void updateRSSI();
  void updateScannerAndCodec();
  void updateDB();
  void updateUptime();
  void updatePing();
  void updateMic();
  void toggleSpeaker();
  bool selfTest();
};

#ifdef BUILD_GAMES
class ChessApp : public WindowedApp {
public:
  typedef enum ChessVariant { Normal, KingOfTheHill, Chess960 } ChessVariant_t;

  ChessApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer, ChessVariant_t variant=ChessApp::Normal);
  virtual ~ChessApp();
  ActionID_t getId() {
    return GUI_APP_FIDE_CHESS;
  };
  static void post(const char *info);
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  // Variables initialized before constructor is called
  Audio* audio;
  ChessVariant_t variant;

  FairyMax::FairyMax* engine;
  bool engineRunning = false;

  const uint8_t typeMask = 0x0F;
  enum : uint8_t { PAWN = 0, KNGT, BISH, ROOK, QUEN, KING, EMPTY = 0x40 };
  enum : uint8_t { WH = 0, BL = 0x80 };
  uint8_t board[64] = {
    ROOK|BL, KNGT|BL, BISH|BL, QUEN|BL, KING|BL, BISH|BL, KNGT|BL, ROOK|BL,         // 8
    PAWN|BL, PAWN|BL, PAWN|BL, PAWN|BL, PAWN|BL, PAWN|BL, PAWN|BL, PAWN|BL,
    EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,
    EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,
    EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,
    EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,   EMPTY,
    PAWN|WH, PAWN|WH, PAWN|WH, PAWN|WH, PAWN|WH, PAWN|WH, PAWN|WH, PAWN|WH,
    ROOK|WH, KNGT|WH, BISH|WH, QUEN|WH, KING|WH, BISH|WH, KNGT|WH, ROOK|WH,         // 1
    // A                                                           // H
  };
  int8_t src = -1;      // selected source
  int8_t cursor = 56;   // A1
  String info;          // info from the engine

  // Unmove
  uint8_t board_backup[64];

  // Helpers
  void makeMove(uint8_t frm, uint8_t to, bool engineMove = false, char promotion = '\0');
  void encodeMove(int8_t lin, char& file, char& rank);
  void decodeMove(const char* mov, int8_t& lin);
  bool processEngine(const char* msg);

  IconRle3* pieces_w[6];
  IconRle3* pieces_b[6];
  IconRle3* cell_black;
  IconRle3* cell_white;
  IconRle3* sel_black;
  IconRle3* sel_white;
  IconRle3* cursor_frame;
};



class AckmanApp : public WiPhoneApp {
public:
  AckmanApp(Audio* audio, LCD& disp, ControlState& state);
  virtual ~AckmanApp();
  ActionID_t getId() {
    return GUI_APP_ACKMAN;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static const uint8_t width = 23;
  static const uint8_t height = 26;
  static const uint8_t cellSize = 10;
  static const uint8_t agentSize = 13;
  static const uint8_t chewingPeriod = 6;
  static constexpr const float normalSpeed = 0.2;   // 1/5, should be more than a pixel per frame
  static const uint16_t confusedPeriod = 120;       // Bloody and Rosy act like Sunny first 4s
  static const uint16_t scaredPeriod = 225;         // 7.5s

  // Grid flags
  static const uint8_t crumbFlag = 1<<0;
  static const uint8_t breadFlag = 1<<1;
  static const uint8_t wallFlag = 1<<2;
  static const uint8_t doorFlag = 1<<3;
  static const uint8_t nodeFlag = 1<<4;   // every crossing is a node: that is where enemies make decisions
  static const uint8_t warpLeftFlag = 1<<5;
  static const uint8_t warpRightFlag = 1<<6;

  static const colorType foodColor = 0xFBEA;   // Coral
  const colorType wallColors[2] PROGMEM = {
    0x0012,       // slightly lighter than Dark Blue
    0xE0B3,       // average of Medium Violet Red and Deep Pink
  };
  static const colorType doorColor = 0xF731;   // Khaki
  static const colorType transparent = 0x0001; // special transparent color

  static constexpr const char* filename = "/ackman.ini";
  static constexpr const char* highField = "high";

  // Variables initialized before constructor is called
  Audio* audio;

  typedef enum AgentType { Ackman, Bloody, Rosy, Moody, Sunny } AgentType_t;
  typedef enum AgentState { Normal, Scared, Eaten, Absent } AgentState_t;
  typedef enum AgentDirection { North = 0, East, South, West, None } AgentDirection_t;
  typedef enum GameState { Ready, Playing, LevelOver, GameOver } GameState_t;

  struct Agent {
    uint8_t x;
    uint8_t y;
    uint8_t origX;
    uint8_t origY;
    bool moving;
    bool outside;
    AgentType_t typ;
    AgentState_t state;
    AgentDirection_t dir;
    float dirOffset;
    uint16_t screenX;
    uint16_t screenY;
  };

  struct Agent agents[5] = {
    { 13, 20, 13, 20, true, true, Ackman, Absent, West, 0, 0, 0 },
    { 11, 10, 11, 10, true, true, Bloody, Absent, West, 0, 0, 0 },
    { 13, 10, 13, 10, true, false, Rosy, Absent, South, 0, 0, 0 },
    { 15, 10, 15, 10, true, false, Moody, Absent, North, 0, 0, 0 },
    { 17, 10, 17, 10, true, false, Sunny, Absent, North, 0, 0, 0 },
  };
  uint8_t grid[height][width];
  uint16_t gridXOff;
  uint16_t gridYOff;
  uint16_t foodCnt;
  uint16_t moveCnt;
  uint16_t scaredTimer;
  TFT_eSprite sprite;
  uint16_t level;

  struct Warp {
    uint8_t x;
    uint8_t y;
  };
  struct Warp warps[4];     // by convention, corresponding warps come in succeeding pairs (so if you enter warps[2*i], you will exit at warps[2*i+1] and vice versa)
  uint8_t warpCnt;

  GameState_t gameState;
  bool screenInited;
  AgentDirection_t nextAckmanDir;
  uint8_t chewingTime;
  uint32_t score;
  uint32_t highScore = 0;

  void startGame();
  void freezeGame();
  void resetGame();
  void setState(GameState_t state);

  void drawAgent(struct Agent* agent, bool draw);
  void drawFood(uint8_t i, uint8_t j, bool clear=false);
  void drawDoors();
  void drawScore(bool redrawAll = false, bool newHigh = false);
  void drawMessage(bool draw, bool ready);
  void drawLine(uint8_t i1, uint8_t j1, uint8_t i2, uint8_t j2, colorType color);

  void nextCell(struct Agent* agent, AgentDirection_t dir = None);
  void getDest(struct Agent* agent, uint8_t& x, uint8_t& y);
  void getDest(struct Agent* agent, AgentDirection_t dir, uint8_t& x, uint8_t& y);
  void moveToWarp(uint8_t& x, uint8_t& y);
  void newEnemyDirection(struct Agent* agent);
  bool isRelevantDir(struct Agent* agent, AgentDirection_t dir);
  void projectAgent(struct Agent* agent, AgentDirection_t dir, uint8_t& x, uint8_t& y);
  void parseLevel(const char* level);
  void updateAgentPosition(struct Agent* agent);
  void respawn(struct Agent* agent);
  float agentDistance(uint8_t i, uint8_t j);
  inline uint16_t getX(int8_t i);
  inline uint16_t getY(int8_t i);
  void saveHighScore(int32_t highScore);
};
#endif

#ifdef LED_BOARD
class LedMicApp : public WindowedApp, FocusableApp {
public:
  LedMicApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~LedMicApp();
  ActionID_t getId() {
    return GUI_APP_LED_MIC;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
protected:
  Audio* audio;
  rgb_color hue2rgb[360];
  rgb_color colors[LED_BOARD_COUNT];
  static rgb_color hsvToRgb(uint16_t h, uint8_t s, uint8_t v);
  uint32_t time;

  float scaleDown = 32;
  float step = 1.7;
  float scale[12];
  void takeInputs();

  // Widgets
  RectWidget* bgRect;         // background rectangle
  LabelWidget* labels[2];
  TextInputWidget* inputs[2];

  bool screenInited = false;
};
#endif

class NotepadApp : public WindowedApp {
public:
  NotepadApp(LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer);
  virtual ~NotepadApp();
  ActionID_t getId() {
    return GUI_APP_NOTEPAD;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static constexpr const char* cNotepadFlashPage = "notepad";
  static const uint16_t maxNotepadSize = 1984;      // TODO: test by setting it low
  Storage& flash;

  MultilineTextWidget* textArea;
};

class UdpSenderApp : public WindowedApp, FocusableApp {
// TODO: add About screen to the Options
public:
  UdpSenderApp(LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer);
  virtual ~UdpSenderApp();
  ActionID_t getId() {
    return GUI_APP_UDP;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  typedef enum UdpSenderState { MAIN, OPTIONS, SHORTCUTS } UdpSenderState_t;
  static const int UDP_CLIENT_PORT = 30895;   // random port

  // Widgets
  RectWidget* bgRect;         // background rectangle
  LabelWidget* labels[3];
  TextInputWidget* inputs[3];
  ButtonWidget* sendButton;
  LabelWidget* shortcutLabels[9];
  TextInputWidget* shortcutInputs[9];
  OptionsMenuWidget* options = NULL;

  WiFiUDP* udp = NULL;

  bool screenInited = false;
  Storage& flash;

  UdpSenderState_t appState;
  void changeState(UdpSenderState_t newState);
};

class AudioConfigApp : public WindowedApp, FocusableApp {
public:
  AudioConfigApp(Audio* audio, LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~AudioConfigApp();
  ActionID_t getId() {
    return GUI_APP_AUDIO_CONFIG;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static const constexpr char* headphonesVolField = "headphones_vol";
  static const constexpr char* earpieceVolField = "speaker_vol";
  static const constexpr char* loudspeakerVolField = "loudspeaker_vol";

  Audio* audio;
  CriticalFile ini;

  // Widgets
  LabelWidget* labels[3];
  IntegerSliderWidget* sliders[3];

  bool screenInited = false;
};

class ParcelApp : public WindowedApp, FocusableApp {
// TODO: add About screen to the Options
public:
  ParcelApp(LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer);
  virtual ~ParcelApp();
  ActionID_t getId() {
    return GUI_APP_PARCEL;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  typedef enum ParcelAppState { MAIN, OPTIONS, CONFIGURE } ParcelAppState_t;
  static const int TCP_CLIENT_PORT = 39946;   // random port
  const char* storagePage = "app_parcel";

  // Widgets
  RectWidget* bgRect;                   // background rectangle
  LabelWidget* labels[3];               // labels for Name, Parcel#, Status
  TextInputWidget* inputs[2];           // inputs for Name, Parcel# (to send to server in a TCP packet)
  ButtonWidget* sendButton;             // send report to a server
  LabelWidget* configsLabels[2];        // labels for IP and Port
  TextInputWidget* configsInputs[2];    // inputs for IP and Port (to send TCP report to)
  OptionsMenuWidget* options = NULL;

  WiFiClient* tcp = NULL;

  bool screenInited = false;
  Storage& flash;

  ParcelAppState_t appState = MAIN;
  void changeState(ParcelAppState_t newState);
};

#ifdef MOTOR_DRIVER
class MotorDriverApp : public WindowedApp {
// TODO: display received commands, add some kind of Help
public:
  MotorDriverApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~MotorDriverApp();
  ActionID_t getId() {
    return GUI_APP_MOTOR;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static const int UDP_SERVER_PORT = 10102;

  typedef enum {
    NEVER_MOVED,
    STOP,
    FORWARD,
    REVERSE,
    LEFT,
    RIGHT
  } Direction;
  Direction direction = NEVER_MOVED;
  void setDirection(Direction newDir);

  // Widgets
  RectWidget* bgRect;         // background rectangle
  MultilineTextWidget* text;
  RectIconWidget* sign = NULL;

  WiFiUDP* udp = NULL;

  bool moving = false;
  bool screenInited = false;
  uint32_t started;
};
#endif // MOTOR_DRIVER

class PinControlApp : public WindowedApp {
public:
  PinControlApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~PinControlApp();
  ActionID_t getId() {
    return GUI_APP_PIN_CONTROL;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static const int UDP_SERVER_PORT = 10104;
  const int marginY = 110;

  // Widgets
  RectWidget* bgRect;         // background rectangle
  LabelWidget* ledLabel;
  LabelWidget* onOffLabel;

  WiFiUDP* udp = NULL;

  bool isOn = false;
};

class CallApp : public WindowedApp, FocusableApp {
public:
  CallApp(Audio* audio, LCD& disp, ControlState& state, bool isCaller, HeaderWidget* header, FooterWidget* footer);
  virtual ~CallApp();
  ActionID_t getId() {
    return GUI_APP_CALL;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
  void setStateCaption(char* t) {
    stateCaption->setText(t);
  }

protected:
  Audio* audio;
  static const constexpr char* headphonesVolField = "headphones_vol";
  static const constexpr char* earpieceVolField = "speaker_vol";
  static const constexpr char* loudspeakerVolField = "loudspeaker_vol";

  CriticalFile ini;
  bool caller;
  bool screenInited = false;
  uint32_t reasonHash;

  // WIDGETS
  RectWidget* clearRect;
  RectIconWidget* iconRect;

  LabelWidget*  stateCaption;
  LabelWidget*  debugCaption;
  LabelWidget*  debugCaption_loudSpkr;
  LabelWidget*  nameCaption;
  LabelWidget*  uriCaption;
};

class DialingApp : public WindowedApp, FocusableApp {
public:
  DialingApp(Audio* audio, LCD& disp, LCD& hardDisp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~DialingApp();
  ActionID_t getId() {
    return GUI_APP_DIALING;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
  LCD& getScreen() {
    return callApp==NULL ? lcd : callApp->getScreen();
  };

protected:
  Audio* audio;
  LCD& hardDisp;      // hardware display (physical screen, rather than a memory page)

  CallApp*  callApp = NULL;
  bool screenInited = false;
  bool error = false;

  // Widgets
  MultilineTextWidget* textArea;
  LabelWidget* errorLabel;
};

class PhonebookApp : public WindowedApp, FocusableApp {

public:
  PhonebookApp(Audio* audio, LCD& disp, LCD& hardDisp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer, bool pick=false);
  virtual ~PhonebookApp();
  ActionID_t getId() {
    return GUI_APP_PHONEBOOK;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);
  const char* getSelectedSipUri();            // used in `pick` mode, when phonebook app is not a standalone app
  const char* getSelectedLoraAddress();
  const char* getCombinedAddress();
  LCD& getScreen() {
    return callApp==NULL ? lcd : callApp->getScreen();
  };

protected:
  static constexpr const char* cAddressFlashPage = "addr";
  static const uint8_t maxAddressRecordSize = 200;

  char* combinedAddress;

  Audio* audio;
  LCD& hardDisp;
  Storage& flash;

  // App state
  bool screenInited;
  typedef enum State {
    SELECTING = 1,
    ADDING,
    VIEWING,
    EDITING,
    CALLING,
    OPTIONS
  } PhonebookAppState_t;
  PhonebookAppState_t appState;
  MenuOption::keyType currentKey;                           // current address key/ID (editing. viewing)
  appEventResult changeState(PhonebookAppState_t newState);

  // WIDGETS

  // Selecting
  MenuWidget* menu;
  LabelWidget* emptyLabel = NULL;       // TODO: show "phonebook is empty" message

  void createLoadMenu();

  // VIEWING widgets
  RectWidget* rect;
  RectIconWidget* phonePic;
  RectIconWidget* headpic;
  MultilineTextWidget* contactName;
  MultilineTextWidget* addressView;
  MenuWidget* viewMenu;

  // OPTIONS
  OptionsMenuWidget* options;

  // Adding / Editing
  RectWidget* clearRect;
  LabelWidget* dispNameLabel;
  TextInputWidget* dispNameInput;
  LabelWidget* sipUriLabel;
  TextInputWidget* sipUriInput;
  LabelWidget* loraLabel;
  TextInputWidget* loraInput;

  // Calling
  CallApp*  callApp = NULL;
  void becomeCaller();
  void sendMessage();

  // Sending message
  CreateMessageApp* messageApp = NULL;

  bool standAloneApp;     // are we in PhonebookApp from main menu or via app like Messages?
};

class SipAccountsApp : public WindowedApp, FocusableApp {

public:
  SipAccountsApp(LCD& disp, ControlState& state, Storage& flash, HeaderWidget* header, FooterWidget* footer);
  virtual ~SipAccountsApp();
  ActionID_t getId() {
    return GUI_APP_SIP_ACCOUNTS;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

  static constexpr const char* filename = "/sip_accounts.ini";

protected:
  static const uint8_t maxAddressRecordSize = 200;

  CriticalFile ini;

  // App state
  bool screenInited;
  typedef enum State {
    SELECTING = 1,
    ADDING,
    VIEWING,
    EDITING,
  } SipAccountsAppState_t;
  SipAccountsAppState_t appState;
  MenuOption::keyType currentKey;                           // current address key/ID (editing. viewing)
  void changeState(SipAccountsAppState_t newState);

  // WIDGETS

  // Selecting
  MenuWidget* menu;
  LabelWidget* emptyLabel = NULL;       // TODO: show "no accounts" message

  void createLoadMenu();

  // VIEWING widgets
  RectWidget* rect;
  RectIconWidget* phonePic;
  RectIconWidget* headpic;
  MultilineTextWidget* contactName;
  MultilineTextWidget* addressView;
  MenuWidget* viewMenu;
  ChoiceWidget* udpTcpSipSelection;

  // Adding / Editing
  RectWidget* clearRect;
  LabelWidget* inputLabels[5];
  TextInputWidget* inputs[4];
  PasswordInputWidget* passwordInput;
};

class EditNetworkApp : public WindowedApp, FocusableApp {
public:
  EditNetworkApp(LCD& disp, ControlState& state, const char* SSID, HeaderWidget* header, FooterWidget* footer);
  virtual ~EditNetworkApp();

  ActionID_t getId() {
    return GUI_APP_EDITWIFI;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:

  CriticalFile ini;

  // Widgets
  RectWidget* clearRect;
  LabelWidget* ssidLabel;
  TextInputWidget* ssidInput;
  LabelWidget* passLabel;
  TextInputWidget* passInput;
  ButtonWidget* saveButton;
  ButtonWidget* forgetButton;
  ButtonWidget* connectionButton;   // Connect / Disconnect
  ChoiceWidget* wifiOnOff;

  bool screenInited;
  bool standAloneApp;

  bool knownNetwork;        // if yes -> show "Forget", "Connect/Disconnect"
  bool connectedNetwork;    // if yes -> show "Disconnect"
};

class NetworksApp : public WindowedApp {
public:
  NetworksApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~NetworksApp();

  ActionID_t getId() {
    return GUI_APP_NETWORKS;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:
  static const uint8_t menuTopPadding = 2;

  CriticalFile ini;
  EditNetworkApp* editNetwork;

  // Widget
  MenuWidget* menu;

  bool screenInited;

  void loadIni();
  void setHeaderFooter();
};

class TimeConfigApp : public WindowedApp, FocusableApp {
public:
  TimeConfigApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~TimeConfigApp();

  ActionID_t getId() {
    return GUI_APP_TIME_CONFIG;
  };
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:

  CriticalFile ini;

  // Widgets
  RectWidget* clearRect;
  LabelWidget* timeZoneLabel;
  TextInputWidget* timeZoneInput;
  LabelWidget* errorLabel;

  bool screenInited;
};

class ScreenConfigApp : public WindowedApp, FocusableApp {
public:
  ScreenConfigApp(LCD& disp, ControlState& state, HeaderWidget* header, FooterWidget* footer);
  virtual ~ScreenConfigApp();

  ActionID_t getId() {
    return GUI_APP_SCREEN_CONFIG;
  };
  bool checkForm(int32_t &dimAfter, int32_t &sleepAfter, bool autocorrect=false);
  appEventResult processEvent(EventType event);
  void redrawScreen(bool redrawAll=false);

protected:

  CriticalFile ini;

  // Widgets
  RectWidget* clearRect;
  RulerWidget* ruler1;
  RulerWidget* ruler2;
  RulerWidget* ruler3;
  LabelWidget* lockingLabel;
  YesNoWidget* lockingChoice;
  LabelWidget* dimmingLabel;
  YesNoWidget* dimmingChoice;
  LabelWidget* sleepingLabel;
  YesNoWidget* sleepingChoice;

  LabelWidget*          brightLevelLabel;
  IntegerSliderWidget*  brightLevelSlider;
  LabelWidget*          dimLevelLabel;
  IntegerSliderWidget*  dimLevelSlider;
  LabelWidget*          dimAfterLabel;
  TextInputWidget*      dimAfterInput;
  LabelWidget*          sleepAfterLabel;
  TextInputWidget*      sleepAfterInput;

  LabelWidget* errorLabel;

  uint32_t oldDimAfter;
  uint32_t oldSleepAfter;

  bool screenInited;
};

// # # # # # # # # # # # # # # # # # # # # # # # # # # # #  MAIN CLASS  # # # # # # # # # # # # # # # # # # # # # # # # # # # #

class GUI {
public:
  GUI();
  ~GUI();

  void init(void (*lcdOnOffCallback)(bool));
  void setDumpRegion() {
    lcd.setWindow(TFT_WIDTH, TFT_HEIGHT, TFT_WIDTH+1, TFT_HEIGHT+1);
  };
  void loadSettings();
  void reloadMessages();
  appEventResult processEvent(uint32_t now, EventType event);
  void redrawScreen(bool redrawHeader, bool redrawFooter, bool redrawScreen, bool redrawAll=false);
  void circle(uint16_t x, uint16_t y, uint16_t r, uint16_t col);    // TODO: remove
  void becomeCallee();
  void exitCall();
  bool inCall(); // are we in a call? used to determine when to disable entering lock screen or when to easily exit it
  void frameToSerial();     // "screenshot": print current frame to serial (requires exernal PSRAM)
  void toggleScreen();
  void setAudio(Audio* pAudio) {
    this->audio = pAudio;
  };

  void longBatteryAnimation();
  static uint16_t batteryExtraLength;

  static uint8_t wifiSignalStrength(int rssi);

  void pushScreen(TFT_eSPI* sprite);
  void pushScreenPart(TFT_eSPI* sprite, uint16_t yOff, uint16_t height);

  void drawOtaUpdate();

  static uint16_t drawBatteryIcon(TFT_eSPI &lcd, ControlState &controlState, int16_t xLeft, int16_t xRight, uint16_t y);
  static uint16_t drawWifiIcon(TFT_eSPI &lcd, ControlState &controlState, uint16_t x, uint16_t y);
  static uint16_t drawSipIcon(TFT_eSPI &lcd, ControlState &controlState, uint16_t x, uint16_t y);
  static uint16_t drawMessageIcon(TFT_eSPI &lcd, ControlState &controlState, uint16_t x, uint16_t y);
  void drawPowerOff();

  ControlState state;
  Storage flash;

  static const unsigned HUNGUP_TO_NORMAL_MS = 2650;     // 2.65 s


protected:
  GUIMenuItemIcons menuIcons[6] PROGMEM = {
    { 2,    icon_Phonebook_w, sizeof (icon_Phonebook_w), icon_Phonebook_b, sizeof (icon_Phonebook_b) },
    { 20,   icon_Messages_w, sizeof (icon_Messages_w), icon_Messages_b, sizeof (icon_Messages_b) },
    { 3,    icon_Tools_w, sizeof (icon_Tools_w), icon_Tools_b, sizeof (icon_Tools_b) },
    { 4,    icon_Games_w, sizeof (icon_Games_w), icon_Games_b, sizeof (icon_Games_b) },
    { 13,   icon_Reboot_w, sizeof (icon_Reboot_w), icon_Reboot_b, sizeof (icon_Reboot_b) },
    { 5,    icon_Settings_w, sizeof (icon_Settings_w), icon_Settings_b, sizeof (icon_Settings_b) },
  };

  GUIMenuItem menu[38] PROGMEM = {  // increment size by one to add a new app

    // TODO: button names can be removed

    { 0, -1, "Clock", "Menu", "", GUI_APP_CLOCK },

    { 1, 0, "WiPhone", "Select", "Back", GUI_ACTION_SUBMENU },

    // Main menu items
    // TODO: call log (icons: Call_log_b/Call_log_w)
    { 2, 1, "Phonebook", "", "", GUI_APP_PHONEBOOK },
    { 20, 1, "Messages", "", "", GUI_APP_MESSAGES },
    { 3, 1, "Tools", "Select", "Back", GUI_ACTION_SUBMENU },
    { 4, 1, "Games", "Select", "Back", GUI_ACTION_SUBMENU },
    { 5, 1, "Settings", "Select", "Back", GUI_ACTION_SUBMENU },
    { 13, 1, "Reboot", "", "", GUI_ACTION_RESTART },

    // Tools (3)
    { 31, 3, "Audio recorder", "", "", GUI_APP_RECORDER },
    { 14, 3, "Scan WiFi networks", "", "", GUI_APP_NETWORKS },    // duplicate from below
    { 7, 3, "Note page", "", "Back", GUI_APP_NOTEPAD },
    { 21, 3, "UDP sender", "", "", GUI_APP_UDP },
    { 28, 3, "Development", "Select", "Back", GUI_ACTION_SUBMENU },

    // Development (28)
    { 36, 28, "My App", "", "", GUI_APP_MYAPP },
    { 27, 28, "Diagnostics", "", "", GUI_APP_DIAGNOSTICS },
    { 19, 28, "Mic test", "", "", GUI_APP_MIC_TEST },
#ifdef MOTOR_DRIVER
    { 22, 28, "Motor driver", "", "", GUI_APP_MOTOR },
#endif // MOTOR_DRIVER
    { 10, 28, "Widgets demo", "", "", GUI_APP_WIDGETS },           // TODO: remove?
    { 16, 28, "Pictures demo", "", "", GUI_APP_PICS_DEMO },        // TODO: remove?
    { 17, 28, "Fonts demo", "", "", GUI_APP_FONT_DEMO },           // TODO: remove?
    { 18, 28, "Design demo", "", "", GUI_APP_DESIGN_DEMO },        // TODO: remove?
#ifdef LED_BOARD
    { 23, 28, "LED microphone", "", "", GUI_APP_LED_MIC },        // TODO: remove?
#endif
#ifdef USER_SERIAL
    { 24, 28, "Parcel delivery", "", "", GUI_APP_PARCEL },
#endif
    { 26, 28, "UDP pin control", "", "", GUI_APP_PIN_CONTROL },
    { 9, 28, "Circle app", "", "", GUI_APP_CIRCLES },
    { 35, 28, "Digital Rain demo", "", "", GUI_APP_DIGITAL_RAIN },
    { 38, 28, "UART Passthrough", "", "", GUI_APP_UART_PASS },
    // Games (4)
#ifdef BUILD_GAMES
    { 34, 4, "Ackman", "", "", GUI_APP_ACKMAN },
    { 6, 4, "FIDE Chess", "", "", GUI_APP_FIDE_CHESS },
    //{ 8, 4, "Chess960", "", "", GUI_APP_CHESS960 },
    { 29, 4, "King of the Hill", "", "", GUI_APP_HILL_CHESS },
#endif

    // Settings (5)
    { 11, 5, "SIP accounts", "", "", GUI_APP_SIP_ACCOUNTS },
    { 12, 5, "Edit current network", "", "", GUI_APP_EDITWIFI },
    { 15, 5, "Scan WiFi networks", "", "", GUI_APP_NETWORKS },
    { 30, 5, "Audio settings", "", "", GUI_APP_AUDIO_CONFIG },
    { 33, 5, "Screen config", "", "", GUI_APP_SCREEN_CONFIG },
    { 32, 5, "Time offset", "", "", GUI_APP_TIME_CONFIG },
    {37, 5, "Firmware settings", "", "", GUI_APP_OTA}
  };

  const char* alphNum[11] = {
    /* 0 */ " +0",
    /* 1 */ "1",
    /* 2 */ "abc2",
    /* 3 */ "def3",
    /* 4 */ "ghi4",
    /* 5 */ "jkl5",
    /* 6 */ "mno6",
    /* 7 */ "pqrs7",
    /* 8 */ "tuv8",
    /* 9 */ "wxyz9",
    /* # */ ".,!?@$/+-=%^ _:;'*#",   // see MAX_INPUT_SEQUENCE
  };

  TFT_eSPI     lcd;               // physical screen
  TFT_eSprite* page = NULL;       // full-screen sprite (if able to create) [abstraction over the `lcd`]
  TFT_eSprite* bgImage = NULL;    // full-screen sprite for backround image (if able to create)
  TFT_eSPI*    screen = NULL;     // the working screen object [one of the above]

  HeaderWidget* header;
  FooterWidget* footer;
  MenuWidget* mainMenu;

  CallApp* callApp = NULL;
  ClockApp* clockApp = NULL;
  WiPhoneApp* runningApp = NULL;
  Audio* audio = NULL;

  // Widgets
  GUIWidget** widgetsArray;       // widgets to be displayed in current application
  uint16_t numWidgets;
  uint16_t maxWidgets;

  // GUI state
  int16_t curApp;
  uint16_t curMenuId;
  uint16_t curMenuSel;
  bool powerOffScreen = false;    // the display is showing a "power off" screen
  bool menuDrawn;                 // menu appearance drawn on screen right now
  bool menuNewItems;              // items in the menu must be changed (redrawn)
  bool lcdOn = false;             // is the screen backlight supposed to be ON or OFF?
  void (*lcdOnOff)(bool);

  // Screen dimming events
  uint32_t msLastKeypadEvent = 0;     // track keypad events for screen dimming and locking

  // automatic
  uint16_t curMenuOffset;
  uint16_t curMenuSize;

  // Text cursor position
  uint8_t xPos;       // dependent on screen size
  uint8_t yPos;       // dependent on screen size

  // Timing
  uint32_t mil;       // last millis()

protected:
  // Input
  void alphanumericInputEvent(EventType key, EventType& r1, EventType &r2);

  // Helper functions
  int16_t findMenu(uint16_t ID);
  int16_t findMenuIcons(uint16_t ID);
  int16_t findSubMenu(uint16_t ID, uint16_t sel);
  void enterMenu(uint16_t ID);
  void exitMenu();
  void enterApp(ActionID_t app);

  void guiError(const char* s);
  void cleanAppDynamic();
  void del(void ** p);

  bool addWidget(GUIWidget* w);
  void deleteWidgets();

  static constexpr const char* backgroundFile = "/background.jpg";
  static constexpr int   backgroundFileMaxSize = 1<<20;
};

class FontCollection {
public:
  typedef struct FontIndex {
    const FontIndex_t ID;
    const unsigned char* fontData;
  } FontDict;

  FontDict fontDict[11] PROGMEM = {
    { OPENSANS_COND_BOLD_20, OpenSans_CondBold20 },
    { AKROBAT_BOLD_16, Akrobat_Bold16 },
    { AKROBAT_BOLD_18, Akrobat_Bold18 },
    { AKROBAT_BOLD_20, Akrobat_Bold20 },
    { AKROBAT_BOLD_22, Akrobat_Bold22 },
    { AKROBAT_BOLD_24, Akrobat_Bold24 },
    { AKROBAT_SEMIBOLD_20, Akrobat_SemiBold20 },
    { AKROBAT_SEMIBOLD_22, Akrobat_SemiBold22 },
    { AKROBAT_EXTRABOLD_22, Akrobat_ExtraBold22 },
    { AKROBAT_BOLD_32, Akrobat_Bold32 },
    { AKROBAT_BOLD_90, Akrobat_Bold90 },              // only numbers and colon; TODO: embed entire font? TODO: possibly allow using user font files in SPIFFS
  };

  FontCollection();
  ~FontCollection();
  SmoothFont* operator[](int index);
  SmoothFont* operator[](FontIndex_t index);
  int length() {
    return sizeof(fnt)/sizeof(SmoothFont*);
  };
protected:
  SmoothFont* fnt[sizeof(fontDict)/sizeof(FontDict)];
};

extern FontCollection fonts;


#endif // _WIPHONE_GUI_H_
