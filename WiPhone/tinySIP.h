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

#ifndef TINY_SIP_h
#define TINY_SIP_h

#include "esp_log.h"

/* TODO:
 *  + send BYE method
 *  + receive BYE methods
 *  + send CANCEL method
 *  - 481 Call/Transaction Does Not Exist
 *      - implement it
 *      - handle it
 *  - 487 (Request Terminated)
 *  + send REGISTER method
 *  + persistent connection
 *  - strict routing: implement sending requests properly when lr parameter is absent in Route  (See. 16.12.1.2 Traversing a Strict-Routing Proxy)
 *  - not acceptable here (when codecs do not match)
 *  - implement persistent Route-Set for sending CANCEL/BYE requests correctly
 *  - apply methods from RFC 5626 "Client-Initiated Connections in SIP"
 *  - disposition types:
 *    - "session" (SDP, default)
 *    - "render" (message)
 *    - "icon" (headpic)
 *    - "alert" (ringtone)
 *  - multipart bodies, per RFC 5621
 *  - 493 (Undecipherable) per RFC 5621
 */

/* Related RFCs (see tinySIP.cpp also):
 *  - RFC 3261: SIP
 *    - RFC 2543: SIP (obsolete) - this defines strict routing behavior which is not supported by this implementation
 *  - RFC 5626: Improved SIP reliability
 *    - RFC 3327: Path header must be supported
 *    - RFC 5627: Globally Routable UA URI (GRUU)
 *  - RFC 4566: SDP: Session Description Protocol
 *    - RFC 3264: An Offer/Answer Model with the Session Description Protocol (SDP)
 *  - RFC 3840: Indicating SIP Agent Capabilities (callee capabilities)
 *  - RFC 3841: caller preferences
 *  - RFC 4122: Generating UUIDs
 *  - RFC 3263: Session Initiation Protocol (SIP): Locating SIP Servers
 *      = "DNS resolution on the next hop URI"
 *  - RFC 3428 "Session Initiation Protocol (SIP) Extension for Instant Messaging" (December 2002)
 *    - RFC 2779: "Instant Messaging / Presence Protocol Requirements" (requirement, 2000)    <-- SHALL
 *    - RFC 4483: "A Mechanism for Content Indirection in Session Initiation Protocol (SIP) Messages"
 *      - Section 3.2 specifies how to send "document" via MESSAGE request; shortly:
 *        do not include data into messages, if it makes messages too long
 *    - RFC 5621: "Message Body Handling in the Session Initiation Protocol (SIP)"
 *      - RFC 2392: "Content-ID and Message-ID Uniform Resource Locators"
 *    - RFC 3860: "Common Profile for Instant Messaging (CPIM)"
 *    - RFC 3862: "Common Presence and Instant Messaging (CPIM): Message Format" (message/cpim)
 *    - RFC 6533: "Internationalized Delivery Status and Disposition Notifications"
 *      - RFC 3464: "An Extensible Message Format for Delivery Status Notifications"
 *        - RFC 1894 (obsoleted): "An Extensible Message Format for Delivery Status Notifications" (message/delivery-status)
 *    - RFC 8591: "SIP-Based Messaging with S/MIME" (update, April 2019)
 */

// TODO:
// - "UA MUST support sets with at / least two outbound proxy URIs"      - RFC 5626, 4.2.1, page 14
// - "UAC MUST support the Path header [RFC3327] mechanism"     - RFC 5626, 4.2.1, page 15
// - "outbound option-tag"  - RFC 5626, 4.2.1, page 15
// - honor "Retry-After" on 503 response  - RFC 5626, 4.2.1, page 16
// - process 439 "First Hop Lacks Outbound Support"     - RFC 5626, 4.2.1, page 16
// - "select the next hop URI"     - RFC 5626, 4.3, page 17

#include <WiFi.h>
#include "src/digcalc.h"
#include "helpers.h"
#include "config.h"
#include "Networks.h"
#include "LinearArray.h"
#include <WiFiUdp.h>

#define TINY_SIP_DEBUG      // allow debugging (calling unitTest)

#define TINY_SIP_PORT   5060

// Logical constants
#define TINY_SIP_NONE   0
#define TINY_SIP_OK     1
#define TINY_SIP_ERR    2

#define TERMINATE_OK    0x02

#define IMPOSSIBLY_HIGH         0x3ff00000                // sometimes tcp->available() returns more than 1 billion - ignore those cases

// Method type constants (not flags); see respType
#define TINY_SIP_METHOD_NONE      0x00
#define TINY_SIP_METHOD_INVITE    0x01
#define TINY_SIP_METHOD_BYE       0x02
#define TINY_SIP_METHOD_ACK       0x04
#define TINY_SIP_METHOD_CANCEL    0x08
#define TINY_SIP_METHOD_REGISTER  0x10
#define TINY_SIP_METHOD_MESSAGE   0x20
#define TINY_SIP_METHOD_UNKNOWN   0xff

// Character literals (RFC 3261, p. 221)
#define TINY_SIP_STAR   '*'
#define TINY_SIP_SLASH  '/'
#define TINY_SIP_EQUAL  '='
#define TINY_SIP_LPAREN '('
#define TINY_SIP_RPAREN ')'
#define TINY_SIP_RAQUOT '>'
#define TINY_SIP_LAQUOT '<'
#define TINY_SIP_COMMA  ','
#define TINY_SIP_SEMI   ';'
#define TINY_SIP_HCOLON ':'
#define TINY_SIP_COLON  ':'               // this literal is only used in "sent-by" grammar construct (RFC 3261, page 232); all other cases use HCOLON
#define TINY_SIP_DQUOT  '"'
#define TINY_SIP_LDQUOT '\x12'            // a double quote (") when used on the left
#define TINY_SIP_RDQUOT '\x13'            // a double quote (") when used on the right

#define TINY_SIP_CRLF   "\r\n"

#define TINYSIP_BRANCH_PREFIX     "z9hG4bKMZJ-"
#define TINYSIP_UUID_PREFIX       "b5fc7dec-40e2-11e9-b210-"                  // UUID variant-1, version-1 (time + MAC), RFC 4122; see RFC 5626, Section 4.1
#define TINYSIP_URN_UUID_PREFIX   "urn:uuid:" TINYSIP_UUID_PREFIX

#define TRYING_100                          100
#define RINGING_180                         180
#define OK_200                              200       // success
#define UNAUTHORIZED_401                    401       // unauthorized at UAS
#define PROXY_AUTHENTICATION_REQUIRED_407   407       // unauthorized at proxy
#define UNSUPPORTED_URI_SCHEME_416          416
#define TRANSACTION_DOES_NOT_EXIST_481      481       // TODO: implement
#define CALL_DOES_NOT_EXIST_481             481
#define BUSY_HERE_486                       486       // TODO: test
#define REQUEST_TERMINATED_487              487
#define NOT_ACCEPTABLE_HERE_488             488
#define SERVER_INTERNAL_ERROR_500           500
#define DECLINE_603                         603
#define REQUEST_PENDING                     491


/* Description:
 *      helper class for parsing SIP addresses (or, to be more precise, addr-spec) according to RFC 3261
 * Usage:
 *      feed it a C-string of the address through constructor and simply access the properties
 * Memory usage:
 *      it creates a dynamic copy of the C-string on creation and doesn't use more memory unless host or port are accessed; in that case more memory will be allocated for host string
 */
class AddrSpec {
public:
  AddrSpec(const char* str);

  // Properties
  char* scheme() const {
    return _scheme;
  };
  char* hostPort() const {
    return _hostport;
  };
  char* userinfo() const {
    return _userinfo;
  };
  char* uriParams() const {
    return _uriParams;
  };
  char* headers() const {
    return _headers;
  };

  // Properties (these use slightly more memory after being accessed)
  char* host();
  uint16_t port();
  bool hasParameter(const char* param);

  // Display through serial
  void show();

protected:
  std::unique_ptr<char[]> _copy;

  char* _scheme;
  char* _hostport;
  char* _userinfo;
  char* _uriParams;
  char* _headers;

  std::unique_ptr<char[]> _host;
  int _port = -1;                   // -1 - port & host are not parsed from hostport, 0 - port absent or zero

  void parseHostPort();
};

class TextMessage {
public:
  char* message = NULL;
  char* from = NULL;
  char* to = NULL;
  uint32_t millisTime;
  uint32_t utcTime;
  bool useTime = false;

  TextMessage(const char* msg, const char* from, const char* to, uint32_t msTime);
  ~TextMessage();

protected:
};

class Connection {
public:
  Connection() {
    msLastPing=0;
  }
  virtual ~Connection() {}
  virtual bool isUdp()=0;
  virtual bool isTcp()=0;
  virtual uint8_t connected()=0;
  virtual IPAddress remoteIP()=0;
  virtual uint16_t remotePort()=0;
  virtual uint16_t localPort()=0;
  virtual void stop()=0;
  virtual int available()=0;
  virtual int32_t read(uint8_t *buffer, uint32_t length)=0;
  virtual void write(uint8_t *buffer, uint32_t length)=0;
  virtual int connect(IPAddress &ip, uint16_t port, int32_t timeout)=0;
  virtual int beginPacket(IPAddress ip, uint16_t port)=0;
  virtual int endPacket()=0;
  virtual void flush()=0;
  bool stale();

  /*virtual int print(const char *format, ...)=0;
  virtual int print(uint8_t a, ...)=0;
  virtual int printf(const char *format, ...)=0;*/


  int print(const char *format, ...) {
    va_list aptr;
    char buffer[500] = {0};
    int ret;

    va_start(aptr, format);
    ret = vsprintf(buffer, format, aptr);
    va_end(aptr);
    write((uint8_t *)buffer, strlen(buffer));

    return(ret);
  }

  int print(uint8_t a, ...) {
    return print("%d", a);
  }

  int printf(const char *format, ...) {
    va_list aptr;
    char buffer[500] = {0};
    int ret;

    va_start(aptr, format);
    ret = vsprintf(buffer, format, aptr);
    va_end(aptr);
    write((uint8_t *)buffer, strlen(buffer));

    return(ret);
  }


protected:
  uint8_t _connected;
  //int _fd;
  uint32_t msLastConnected = 0;
  uint32_t msLastReceived = 0xffffffff - 3600000;

  // Everything below is only used for connections to proxy
  uint32_t msLastPing;                  // RFC 5626
  uint32_t msLastPong;

  bool pinged = false;
  bool rePinged = false;
  bool everPonged = false;              // did we ever receive a pong to a ping?

//  uint32_t msLastWrite;
//  uint32_t msLastRead;

  friend class TinySIP;
};

/* Description:
 *     Helper class that stores information regardig viability of the TCP connection (WiFiClient).
 *     Particularly, it helps to decide if a connection is alive or got stale and needs reconnecting.
 */
class UDP_SIPConnection : public WiFiUDP, public Connection {
public:

  //WiFiUDP udpSocket;

  UDP_SIPConnection() : Connection() {
    //_fd = -1;
    mRemotePort = 5060;
    _connected = false;
    lastUdpWriteTime = 0;
    endSent = false;
  }
  ~UDP_SIPConnection() {
    log_e("~UDP_SIPConnection called");
  }

  bool isUdp() {
    return true;
  }
  bool isTcp() {
    return false;
  }

  uint8_t connected() {
    //return true;
    //log_d("connected...");
    return _connected;
  }

  /*bool endUdpSending() {
    //long long int msNow = millis();
    uint32_t msNow = millis();
    if(elapsedMillis(msNow, lastUdpWriteTime, 50) && lastUdpWriteTime != 0 && !endSent) {
      endSent = true;
      log_d("udp write timeout\n");
      if (!WiFiUDP::endPacket()) {
        // TODO set an error
        // failed to send
        log_d("Connection(UDP)::write failed\n");
      }
    }

    return _connected;
  }*/


  int32_t read(uint8_t *buffer, uint32_t length) {
    return WiFiUDP::read(buffer, length);
  }
  void write(uint8_t *buffer, uint32_t length) {
    WiFiUDP::write(buffer, length);
  }
  int beginPacket(IPAddress ip, uint16_t port) {
    return WiFiUDP::beginPacket(ip, port);
  }
  int endPacket() {
    return WiFiUDP::endPacket();
  }

  void flush() {
    WiFiUDP::flush();
  }

  /*
    int print(const char *format, ...){return 0;}
    int print(uint8_t a, ...){return 0;}
    int printf(const char *format, ...){return 0;}
  */

private:
  IPAddress mRemoteIP;
  uint16_t mRemotePort;
  uint16_t mLocalPort;
  //long long int lastUdpWriteTime;
  uint32_t lastUdpWriteTime;
  bool endSent;

public:
  IPAddress remoteIP() {
    return mRemoteIP;
  }

  uint16_t remotePort() {
    return mRemotePort;
  }

  uint16_t localPort() {
    return mLocalPort;
  }

  void stop() {
    WiFiUDP::stop();
    _connected = false;
  }

  int available() {
    /*if(_fd < 0) {
      return 0;
    }*/
    //log_d("available...");
    WiFiUDP::parsePacket();
    int len = WiFiUDP::available();
    if(len <= 0) {
      return 0;
    }
    if(len > 0) {
      log_d("available %d\n", len);
    }
    return len;
  }

  int connect(IPAddress &ip, uint16_t port, int32_t timeout) {
    log_d("Connection(UDP)::connect\n");
    /*if(WiFiUDP::beginPacket(ip, port) <= 0) {
      return 0;
    }*/
    WiFiUDP::begin(mLocalPort);
    mRemotePort = 5060;//port;
    mRemoteIP = ip;
    //_fd = WiFiUDP::getUdpFd();
    _connected = true;
    log_d("Connection(UDP)::connect success\n");

    return 1;
  }

  bool stale();

};

class TCP_SIPConnection : public WiFiClient, public Connection {
public:
  TCP_SIPConnection() : Connection() { /*, WiFiClient()*/
    log_e("TCP_SIP_Connection constructor...");
  }
  ~TCP_SIPConnection() {
    log_e("~TCP_SIPConnection called");
  }

  bool isUdp() {
    return false;
  }
  bool isTcp() {
    return true;
  }

  uint8_t connected() {
    /*log_e("TCP_SIPConnection::connected()");*/return WiFiClient::connected();
  }
  IPAddress remoteIP() {
    log_e("TCP_SIPConnection::remoteIP()");
    return WiFiClient::remoteIP();
  }
  uint16_t remotePort() {
    log_e("TCP_SIPConnection::remotePort()");
    return WiFiClient::remotePort();
  }
  uint16_t localPort() {
    log_e("TCP_SIPConnection::localPort()");
    return WiFiClient::localPort();
  }
  void stop() {
    log_e("TCP_SIPConnection::stop()");
    WiFiClient::stop();
  }
  int available() {
    /*log_e("TCP_SIPConnection::available()");*/return WiFiClient::available();
  }
  int32_t read(uint8_t *buffer, uint32_t length) {
    return WiFiClient::read(buffer, length);
  }
  void write(uint8_t *buffer, uint32_t length) {
    WiFiClient::write(buffer, length);
  }
  int connect(IPAddress &ip, uint16_t port, int32_t timeout) {
    log_e("TCP_SIPConnection::connect()");
    return WiFiClient::connect(ip, port, timeout);
  }
  int beginPacket(IPAddress ip, uint16_t port) {
    log_e("tcp should not call beginPacket!\n");
    return 0;
  }
  int endPacket() {
    log_e("tcp should not call endPacket!\n");
    return 0;
  }

  void flush() {
    log_e("TCP_SIPConnection::flush()");
    WiFiClient::flush();
  }
  /*
  int print(const char *format, ...){return 0;}
  int print(uint8_t a, ...){return 0;}
  int printf(const char *format, ...){return 0;}*/

  /* this method is already implemented in the cpp.
  bool stale() {
    return 0;
  }*/

};



class TinySIP {

public:

  typedef uint16_t StateFlags_t;

  // Variables' sizes
  static const int MAX_MESSAGE_SIZE = 2000;           // we expect any incoming SIP messages to fit into 2000 bytes (Ethernet MTU 1500 bytes - 20 for IP - 20 for TCP at least)
  // (outgoing SIP messages should be within 1300 bytes, see RFC 3261)
  static const int MAX_HEADER_CNT = 100;              // we expect no more than 100 headers        // TODO: make it dynamic, use LinearArray
  static const int MAX_DIALOGS = 32;                  // how many dialogs to remember at most (see `dialogs`): single call can result in many dialogs (UAC-to-UAS)

  // 1-bit result flags for checkCall() method
  static const StateFlags_t EVENT_NONE = 0x00;                      // no events or blank event
  static const StateFlags_t EVENT_RINGING = 0x01;
  static const StateFlags_t EVENT_CALL_CONFIRMED = 0x02;
  static const StateFlags_t EVENT_CALL_TERMINATED = 0x04;
  static const StateFlags_t EVENT_SIP_ERROR = 0x08;                 // SIP error
  static const StateFlags_t EVENT_INCOMING_CALL = 0x10;
  static const StateFlags_t EVENT_CONNECTION_ERROR = 0x40;          // unable to establish connection error
  static const StateFlags_t EVENT_MORE_BUFFER = 0x80;
  static const StateFlags_t EVENT_REGISTERED = 0x100;
  static const StateFlags_t EVENT_RESPONSE_PARSED = 0x200;
  static const StateFlags_t EVENT_REQUEST_PARSED = 0x400;
  static const StateFlags_t EVENT_INVITE_TIMEOUT = 0x800;
  static const StateFlags_t EVENT_PONGED = 0x1000;
  static const StateFlags_t EVENT_INCOMING_MESSAGE = 0x2000;        // TODO

  /*bool endUdpSending() {
    if(tcpProxy) {
      //log_d("connected() tcpProxy NOT null");


      return tcpProxy->connected();

    } else {
      log_d("connected() tcpProxy null");
      return false;
    }
  }*/

  bool isBusy() {
    return currentCall!=nullptr && !currentCall->terminated && (currentCall->confirmed || currentCall->early);
  }

  bool registrationInvalid(uint32_t msNow) {
    return !this->registered || elapsedMillis(msNow, msLastRegistered, REGISTER_EXPIRATION_S*1000);
  }
  bool registrationValid(uint32_t msNow) {
    return !registrationInvalid(msNow);
  }

  // class constants
  static const uint8_t BRANCH_CONSTANT_LEN      = 11;       // length of TINYSIP_BRANCH_PREFIX     // TODO: check for matching lenth with this value
  static const uint8_t BRANCH_VARIABLE_LEN      = 9;
  static const uint8_t OWN_TAG_LENGTH           = 9;        // 'F'/'T' (for UAC/UAS) followed by 8 random bytes (more than 47 random bits (32 bits required))
  static const uint8_t CALL_ID_LENGTH           = 9;
  static const uint8_t CNONCE_LENGTH            = 6;        // more than 35 random bits

  // Payload types
  static const uint8_t G722_RTP_PAYLOAD         = 9;        // G.722, G722
  static const uint8_t ALAW_RTP_PAYLOAD         = 8;        // G.711, A-Law / PCMA
  static const uint8_t ULAW_RTP_PAYLOAD         = 0;        // G.711, u-Law / PCMU
  static const uint8_t NULL_RTP_PAYLOAD         = 255;      // payload type placeholder (0 is reserved for PCMU)

  const static uint8_t SUPPORTED_RTP_PAYLOADS[3];

  bool isAudioSupported(uint8_t rtpPayloadType) {
    for (int i=0; i<sizeof(SUPPORTED_RTP_PAYLOADS)/sizeof(uint8_t); i++)
      if (rtpPayloadType == SUPPORTED_RTP_PAYLOADS[i]) {
        return true;
      }
    return false;
  }

  // Timings
  static const uint32_t PING_PERIOD_MS = 58761u;        // 58.8 s     // TODO: use random, according to Page 20 of RFC 5626
  static const uint32_t PING_TIMEOUT_MS = 10000u;       // 10s; RFC 5626: "If a pong is not received within 10 seconds after sending a ping .. / .. then the client MUST treat the flow as / failed."
  static const uint32_t REGISTER_PERIOD_MS = 60000;   //modified to one minute // 3.29 m     // TODO: do registration retries (every minute if failed)
  static const uint32_t REGISTER_EXPIRATION_S = 60;    //modified to one minute // 15 min (in seconds)
  static const uint32_t STALE_CONNECTION_MS = 10000;    // 10 seconds
  static const uint32_t T1_MS = 500u;                   // 500 ms; RFC 3261, Section 17: "The default value for T1 is 500 ms"

  TinySIP();
  bool init(const char* name, const char* fromUri, const char* proxyPass, const uint8_t *mac);
  void clearRouteSet();
  void clearDynamicState();
  void clearDynamicParsed();
  void clearDynamicConnections();
  ~TinySIP();

  // High level flow control
  int startCall(const char* toUri, uint32_t now);
  int acceptCall();
  int declineCall();
  int terminateCall(uint32_t now);
  int wifiTerminateCall();
  void rtpSilent();
  StateFlags_t checkCall(uint32_t msNow);
  TextMessage* checkMessage(uint32_t msNow, uint32_t timeNow, bool useTime);
  int registration();
  int sendMessage(const char* toUri, const char* msg);

  // Where to send audio
  char*     getRemoteAudioAddr() {
    return remoteAudioAddrDyn;
  };
  uint16_t  getRemoteAudioPort() {
    return remoteAudioPort;
  };
  uint16_t  getLocalAudioPort()  {
    return 50000 + 2*(sdpSessionId % 4096);
  };     // we hope that the optimizer will replace this with (sdpSessionId & 0xFFF), which is equivalent to (sdpSessionId % 4096) :-P
  uint8_t   getAudioFormat()     {
    return audioFormat;
  };

  // UI
  const char* getReason();
  const char* getRemoteName();
  const char* getRemoteUri();

#ifdef TINY_SIP_DEBUG
  void unitTest();
  void xxd(char* b);
  void showParsed();
#endif // TINY_SIP_DEBUG 

protected:

  //logging SIP messages in a more readable way
  char TmpStringToSIPLogs[2048];

  // Messages
  LinearArray<TextMessage*, LA_EXTERNAL_RAM> textMessages;      // incoming messages in RAM waiting to be saved to flash

  class RouteSet {
  public:
    RouteSet();
    RouteSet(RouteSet& other);
    ~RouteSet();

    void copy(RouteSet& other);
    void clear(bool reverse=false);                           // sets `setReverse`
    bool add(const char *rrAddrSpec, const char *rrParams);
    const char* operator[](uint16_t index) const;             // automatically reverses for UAC

    // Properties
    bool isReverse() {
      return setReverse;
    };
    size_t size() const {
      return set.size();
    };

  protected:
    LinearArray<const char*, LA_INTERNAL_RAM> set;            // array of pointers to dynamic strings for SIP URIs
    bool setReverse;                                          // route set learned as client (UAC) -> route set needs to be sent in reverse order
  };

  // Class to contain state of each individual dialog.
  // Dialogs represent relationship between two UACs (Note: REGISTER requests is outside of dialogs, so as  OPTIONS and first INVITE).
  // Dialogs are established via INVITE request.
  // A single call can result in multiple associated dialogs (e.g., when INVITE is forked and sent to multiple callee phones).
  // According to RFC 3261:
  //    "The dialog facilitates sequencing of messages between the user agents and proper routing of requests between both of them."
  //    "INVITE method is the only way defined in this specification to establish a dialog"
  //    "The dialog represents a context in which to interpret SIP messages."
  // Particularly, identifying dialogs is required to:
  //    - hang up properly via sending a BYE request (e.g., after a REGISTER request was sent between INVITE and attemted BYE)
  //    - ignoring ingenuine requests (so that random BYE request will not hang up any existing call)
  class Dialog {
  public:
    Dialog(bool isCaller);
    Dialog(bool isCaller, const char* callId, const char* localTag, const char* remoteTag);
    ~Dialog();

    // Access interfaces
    bool isTerminated() const {
      return this->terminated;
    }
    operator uint32_t() const {
      return this->dialogIdHash;
    }

    // Modification interface
    void setUseTime(uint32_t ms)  {
      usageTimeMs = ms;
    }
    void setConfirmed()  {
      this->early = false;
      this->confirmed = true;
    }

    uint32_t usageTimeMs;             // last time used; we are using this to clean up oldest dialogs

    // A hash of the dialog ID (below) used to find the correct dialog quickly
    uint32_t dialogIdHash;

    // RFC 3261: "A dialog is identified at each UA with a dialog ID, which consists of / a Call-ID value, a local tag and a remote tag." (dialog ID)
    char* callIdDyn;                  // Call-ID
    char* localTagDyn;                // To field tag for UAC, From tag field for UAS
    char* remoteTagDyn;               // To field tag for UAS, From tag field for UAC; can be NULL (RFC 3261, p.71, for backwards compatibility)

    // CSeq or Command Sequence: contains an integer and a method name (RFC 3261)
    // CSeq number is incremented for each new request within a dialog
    const int32_t EMPTY = -1;
    int32_t localCSeq   = EMPTY;      // local  CSeq must be "empty" for the UAS
    int32_t remoteCSeq  = EMPTY;      // remote CSeq must be "empty" for the UAC at first ("it is established when the remote UA sends a request within the dialog")

    char* localUriDyn;
    char* remoteUriDyn;
    char* localNameDyn;               // display name
    char* remoteNameDyn;
    char* remoteTargetDyn;            // for UAS: must be set to the URI from the Contact header field of the request.
    // Within a dialog, this can be modified via "target refresh requests": only re-INVITE, according to RFC 3261

    RouteSet routeSet;

    bool operator==(const Dialog& other) const;

    // TODO: session information
    // TODO: support for re-INVITE within a dialog

    // Call flags / dialog state flags (via bit fields)
    uint8_t  caller:1,                // are we the caller? (false - remote party is the caller, local is the invitee/callee); alternative name: isUAC, localCaller
             // this flag corresponds to CALLING in GUI.h

             // Dialog flags as speficified by RFC 3261
             early:1,                 // we already know the other side's tag (e.g. via 180 response), but it's not confirmed yet ()
             // RFC 3261: "A dialog can also be in the "early" / state, which occurs when it is created with a provisional response" (1xx)
             confirmed:1,             //   "...and then transition to the "confirmed" state when a 2xx final / response arrives"
             terminated:1,            //   "For other responses, or if no response arrives at / all on that dialog, the early dialog terminates"
             // or the dialog was terminated by a BYE (or CANCEL) request
             // TODO: 481/408 responses, or no response (TU) -> UAC should terminate a dialog
             // TODO: BYE request terminates a session and an associated dialog
             secure:1,                //   "boolean / flag called "secure"": when request is done via TLS and the Request-URI contains a SIPS URI.

             // Additional (internal) dialog flags
             accepted:1;              // TODO: might be unnecessary
  };

  LinearArray<Dialog*, LA_INTERNAL_RAM> dialogs;          // all known dialogs (array size is capped at a few dozen)

  Dialog* findDialog(const char* callID, const char* tagLocal, const char* tagRemote);
  Dialog* findCreateDialog(bool isCaller, const char* callID, const char* tagLocal, const char* tagRemote);
  void restoreDialogContext(Dialog& diag);

  // Connection variables
  Connection* tcpProxy;     // the server that we are calling through
  Connection* tcpRoute;     // the address of the route-set
  Connection* tcpCallee;    // "direct" connection to the callee (in real-world can be routed through a proxy)
  Connection*& tcpLast;     // reference to pointer to last connection from which data was read
  bool leftOver;

  // Local call credentials
  IPAddress proxyIpAddr;
  char* localUserDyn;
  char* localNameDyn;       // display name
  char* localUriDyn;
  char* proxyPasswDyn;
  char* remoteUriDyn;       // set when making (startCall) or accepting (sending 180 response) a call
  char* outgoingMsgDyn;
  uint8_t mac[6];
  char macHex[13];

  // Dialog parameters
  // TODO: change to list of active dialogs and associated sessions
  char branch[80];        // change to dynamic: dialogBranchDyn, myBranch[BRANCH_VARIABLE_LENGTH+1]
  uint32_t phoneNumber;
  char localTag[OWN_TAG_LENGTH+1];      // local tag
  char* callIdDyn;                      // current call ID - this is set when 1) startCall(); 2) replying with 180
  uint16_t cseq;                        // "command sequence"
  uint32_t nonceCount;
  char cnonce[CNONCE_LENGTH+1];
  Dialog* currentCall = NULL;           // the dialog that has an active media session (person talking on the phone within this dialog)

  // MESSAGE method parameters
  char* msgCallIdDyn;                   // MESSAGE Call-ID is separate because messages exist outside of calls and outside of SIP dialogs;
  // it is generated only on sendMessage() and can be used to identify responses for MESSAGE request

  // REGISTER method parameters
  uint16_t regCSeq;                     // REGISTER method CSeq (starting at 1)
  char* regCallIdDyn;                   // REGISTER method Call-ID; RFC 3261: Call-ID "SHOULD be the same in each registration from a UA."
  char regBranch[BRANCH_CONSTANT_LEN+BRANCH_VARIABLE_LEN+1];
  bool registrationRequested = false;
  bool registered = false;              // was the last registration request successful? TODO: ensure that response matches the request
  bool everRegistered = false;          // did we ever receive a registration response?

  // BYE method parameters
  uint16_t byeCSeq;

  String thisIP;            // TODO: remove, use IPAddress tcp.localIP(), tcp.localIP().toString().c_str(); maybe store in C-string
  uint32_t sdpSessionId;

  // Response buffer
  uint16_t  buffLength;
  char      buff[MAX_MESSAGE_SIZE+1];
  char*     buffStart;

  // Parsed response/request
  // NOTE1: most of char* variables are just pointers to buffer, they are valid as long as the buffer is not reset
  // NOTE2: resp variables are used for parsing any incoming messages (not only responses, but also requests)
  bool isResponse;
  uint8_t   respType;           // this one comes from CSeq header for responses and method - for requests
  uint16_t  respCode;
  char      respClass;          // first digit of the response code (as ASCII)
  char*     respCallId;
  char*     respProtocol;
  char*     respReason;
  char*     respContentType;
  uint16_t  respContentLength;
  char*     respBody;
  char*     respMethod;         // this is non-empty only for requests
  char*     respUri;            // it is actually Request-URI, but we call respUri for consistency (NULL for requests)
  uint16_t  respHeaderCnt;      // number of headers
  char*     respHeaderName[MAX_HEADER_CNT];
  char*     respHeaderValue[MAX_HEADER_CNT];
  char*     remoteToFromDyn;        // exact copy of the remote From (for caller) or To (for callee) header value
  char*     respToDispName;         // display name from the To header
  char*     respToAddrSpec;         // addr-spec (SIP URI) from the To header
  char*     respToParams;           // parameters from the To header
  char*     respToTagDyn;           // tag parameter from the To header         TODO: replace with remoteTagDyn?
  char*     respFromDispName;       // display name from the From header
  char*     respFromAddrSpec;       // addr-spec (SIP URI) from the From header
  char*     respFromTagDyn;         // tag parameter from the From header
  char*     respContDispNameDyn;
  char*     respContAddrSpecDyn;    // SIP URI from Contact header

  RouteSet  respRouteSet;

  uint16_t  respCSeq;
  char*     respCSeqMethod;

  // Synonyms
  char*     remoteTag;      // points to either respToTagDyn or respFromTagDyn

  // Parsed challenge parameters (pointers to buffer)
  char*     respChallenge;
  char*     digestRealm;
  char*     digestDomain;
  char*     digestNonce;
  char*     digestCNonce;
  char*     digestOpaque;
  char*     digestStale;
  char*     digestAlgorithm;
  char*     digestQopOpt;
  char*     digestQopPref;        // pointer to first of "auth" or "auth-int" inside digestQopOpt

  // Digest response
  HASHHEX   digestResponse;

  // Parsed SDP
  char*     remoteAudioAddrDyn;   // IPv4 address where audio from local microphone needs to be sent after encoding
  uint16_t  remoteAudioPort;      // port where audio from local microphone needs to be sent after encoding
  uint8_t   audioFormat;          // chosen RTP payload type number

  // GUI
  char*     guiReasonDyn;

  // Timings & timers
  uint32_t  msLastKnownTime;
  uint32_t  msLastRegisterRequest;
  uint32_t  msLastRegistered;
  uint32_t  msTermination;
  uint8_t   nonFree;              // when call is in progress, ping and registration are checked less often; this is a counter of skipped checks

  // RFC 3261: "Timer A controls request retransmissions"
  uint32_t  msTimerAStart;
  uint32_t  msTimerADuration;

  // RFC 3261: "Timer B controls transaction timeouts"
  //uint32_t  msTimerBStart;
  //uint32_t  msTimerBDuration;


  // - - - - - - - - - - - - - - - - - - - - - -  Protected methods  - - - - - - - - - - - - - - - - - - - - - -

  // SDP
  int sdpBody(Connection& tcp, const char* ip, bool onlyLen);

  // Methods
  int ping(uint32_t now);
  int requestInvite(uint32_t now, Connection& tcp, const char* toUri, const char* body=NULL);
  int sendAck(Connection& tcp, const char* toUri);
  int requestBye(Connection& tcp);
  int requestCancel(Connection& tcp);
  int requestRegister(Connection& tcp);        // register method
  int requestMessage(Connection& tcp);

  // Replies
  int sendResponse(Dialog* diag, Connection& tcp, uint16_t code, const char* reason, bool sendSdp=false);

  // Connections
  bool ensureIpConnection(Connection*& tcp, IPAddress &ip, uint16_t port, bool forceRenew=false, int32_t timeout=5000);
  IPAddress ensureConnection(Connection*& tcp, const char* addrSpec, bool forceRenew=false, int32_t timeout=5000);
  Connection* getConnection(bool isClient);

  // Parsing
  int parseResponse();
  int parseRequest();
  int parseAllHeaders(char* startOfHeaders);
  void parseHeader(uint16_t p);
  int parseSdp(const char* body);

public:
  // Grammar-related low level parsing
  static inline char* skipToken(const char* p);
  static char* nextParameter(const char* p, char sep, const char* terminateAt="");
  static char* skipLinearSpace(const char* p);
  static char* skipCharLiteral(const char* p, char c);
  static char* skipAlphanumAndSpecials(const char* p, const char* const specials);
  static char* parseContactParam(char* p, char** dispName, char** addrSpec, char** contactParams);
  static char* parseAddrSpec(char* p, char** scheme, char** hostport, char** userinfo, char** uriParams, char** headers);    // destructive parsing
  static void normalizeLinearSpaces(char* p);

  // - TODO: these are a bit ugly
  static char* quotedStringEnd(const char* p);
  static char* parseQuotedString(char* p);
  static char* parseQuotedStringValue(char** p, char sep);      // returns pointer to quoted string value (deescaped), the string is necessarily terminated by NUL character
  static bool retrieveGenericParam(const char* p, const char* const parName, const char sep, char** val);
  static uint8_t methodType(const char* methd);

  //this variable fixes restart problem on calling a not registered sip info.
  int triedToMakeCallCounter;

protected:
  // Helper routines
  // TODO: make some of the header methods `static`

  bool connectReturnedFalse;

  void freeNullConnectionProxyObject(bool isProxy);
  // - these headers are recommended to be appear towards the top (Via, Route, Record-Route, Proxy-Require, Max-Forwards, and Proxy-Authorization), p. 30
  static void sendHeaderVia(Connection& tcp, String& thisIp, uint16_t port, const char* branch);
  void sendHeadersVia(Connection& tcp);             // copy Via from request
  void sendRouteSetHeaders(Connection& tcp, bool isClient);
  void sendHeaderMaxForwards(Connection& tcp, uint8_t n);
  void sendHeaderAuthorization(Connection& tcp, const char* toUri);     // Authorization: or Proxy-Authorization:

  // - other headers:
  void sendHeaderToFromLocal(Connection& tcp, char TF, const Dialog* diag=NULL);           // send local credentials
  void sendHeaderToFromRemote(Connection& tcp, char TF, bool mirror, const char* toUri=NULL, const char* toTag=NULL);
  void sendHeaderAllow(Connection& tcp);
  void sendHeaderCallId(Connection& tcp, char* id=NULL);
  static void sendHeaderExpires(Connection& tcp, uint32_t seconds);
  void sendHeaderContact(Connection& tcp);
  void sendHeaderUserAgent(Connection& tcp);
  void sendHeaderCSeq(Connection& tcp, uint16_t seq=0, const char* method=NULL);

  static void sendRequestLine(Connection& tcp, const char* method, const char* reqUri);
  void sendBodyHeaders(Connection& tcp, int len, const char* type);
  void sendBodyHeaders(Connection& tcp) {
    sendBodyHeaders(tcp, 0, NULL);
  };
  void sendHeadersToFrom(Connection& tcp, const Dialog* diag=NULL);                        // used for REGISTER method and Dialogs
  void sendByeHeadersToFrom(Connection& tcp, const Dialog* diag);

  void newBranch(char* dStr);    // TODO: static
  void newLocalTag(bool caller);
  static void newCallId(char** dStr);
  void newCNonce(char* dStr);
  void resetBuffer();
  void resetBufferParsing();

  void randInit();
};

#endif // TINY_SIP_h
