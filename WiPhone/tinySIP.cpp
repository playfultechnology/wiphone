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

#include "tinySIP.h"
#include "helpers.h"

extern bool UDP_SIP;
// Handle disconnect timeout
bool    timeout_disconnect = false;
uint32_t timeout_disconnect_mls = 0;
uint16_t tmpRespSeq = 0;
/*
 * This is tiny implementation of the SIP protocol intended to be used in embedded designs.
 * The goal is to implement a minimalist SIP user agent (UA) while maintaining a compact
 *   RAM footprint and support sequential code execution. (The latter goal comes from the
 *   fact that LwIP sockets do no work well inside FreeRTOS threads on ESP32 platform.)
 *
 * tinySIP is mostly based on (see tinySIP.h also):
 *    RFC 3261 "SIP: Session Initiation Protocol"
 *    RFC 3263 "Session Initiation Protocol (SIP): Locating SIP Servers"
 *    RFC 4566 "SDP: Session Description Protocol"
 *    RFC 3428 "Session Initiation Protocol (SIP) Extension for Instant Messaging"
 */

// DEVELOPER NOTES:
// All dynamic variable names must contain "Dyn" suffix
// Don't forget to free memory after:
// - malloc
// - strdup
// - strndup
// Don't forget to `delete` objects after making `new` ones.
// Watch out for `strcspn` and especially `strsep` (latter one is destructive)

#ifdef WIPHONE_PRODUCTION

#define TCP(tcp, fmt, ...)              tcp.print(fmt, ##__VA_ARGS__)
#define TCP_PRINTF(tcp, fmt, ...)       tcp.printf(fmt, ##__VA_ARGS__)

// why isn't this enabled (disabled) in production?
//#define SIP_DEBUG_DELAY(n)
#else


#include <esp32-hal-log.h>

#define TCP(tcp, fmt, ...)           do{/*log_d(fmt, ##__VA_ARGS__);*/tcp.print(fmt, ##__VA_ARGS__);}while(0);
#define TCP_PRINTF(tcp, fmt, ...)    do{log_d(fmt, ##__VA_ARGS__);tcp.printf(fmt, ##__VA_ARGS__);}while(0);
#define SIP_DEBUG_DELAY(n)              delay(n)


#endif // WIPHONE_PRODUCTION

const uint8_t TinySIP::SUPPORTED_RTP_PAYLOADS[3] = {
  G722_RTP_PAYLOAD,
  ULAW_RTP_PAYLOAD,
  ALAW_RTP_PAYLOAD,
};

AddrSpec::AddrSpec(const char* str)
  : _copy(std::unique_ptr<char[]>(strdup(str))),
    _host(nullptr) {
  _scheme = nullptr;
  _hostport = nullptr;
  _userinfo = nullptr;
  _uriParams = nullptr;
  _headers = nullptr;

  char* const pStart = _copy.get();
  char* pEnd = TinySIP::parseAddrSpec(pStart, &_scheme, &_hostport, &_userinfo, &_uriParams, &_headers);
  if (pEnd) {
    _copy[pEnd - pStart] = '\0';

    // Try to free a little memory in the case that str has irrelevant extra characters
    char* ptr = (char*) realloc(pStart, pEnd - pStart + 1);
    if (ptr) {
      _copy.release();
      _copy.reset(ptr);
    }
  }
}

void AddrSpec::parseHostPort() {
  if (_hostport!=nullptr) {
    int colon = strcspn(_hostport, ":");
    if (_hostport[colon]==':') {
      _host = std::unique_ptr<char[]>(strndup(_hostport, colon));
      _port = atoi(_hostport+colon+1);
    } else {
      _host = std::unique_ptr<char[]>(strdup(_hostport));       // TODO: is there a way to do it without duplicating space?
      _port = 0;
    }
  }
}

char* AddrSpec::host() {
  if (_host==nullptr) {
    parseHostPort();
  }
  return _host.get();
}

uint16_t AddrSpec::port() {
  if (_port<0) {
    parseHostPort();
  }
  return _port<0 ? 0 : _port;
}

void AddrSpec::show() {
  if (_scheme) {
    log_d("scheme: %s", _scheme);
  }
  if (_hostport) {
    log_d("hostport: %s", _hostport);
  }
  if (this->host()) {
    log_d("host: %s", this->host());
  }
  if (this->port()) {
    log_d("port: %d", this->port());
  }
  if (_userinfo) {
    log_d("userinfo: %s", _userinfo);
  }
  if (_uriParams) {
    log_d("uriParams: %s", _uriParams);
  }
  if (_headers) {
    log_d("headers: %s", _headers);
  }
}

bool Connection::stale() {
  // This works only for proxy connections, which are regularly pinged
  // TODO: include other indicators
  return this->everPonged && this->pinged && this->rePinged && timeDiff(this->msLastPing, this->msLastPong) > TinySIP::STALE_CONNECTION_MS;
  // elapsedMillis(msNow, tcpProxy->msLastReceived, STALE_CONNECTION_MS) && elapsedMillis(msNow, tcpProxy->msLastConnected, STALE_CONNECTION_MS) ? true : false;
}

TextMessage::TextMessage(const char* msg, const char* src, const char* dst, uint32_t msTime) {
  if (msg) {
    message = extStrdup(msg);
  }
  if (src) {
    from = extStrdup(src);
  }
  if (dst) {
    to = extStrdup(dst);
  }
  millisTime = msTime;
}

TextMessage::~TextMessage() {
  freeNull((void **) &message);
  freeNull((void **) &from);
  freeNull((void **) &to);
}

TinySIP::RouteSet::RouteSet() : set(LinearArray<const char*, LA_INTERNAL_RAM>()) {
  setReverse = false;
}

TinySIP::RouteSet::RouteSet(RouteSet& other) : RouteSet() {
  this->copy(other);
}

TinySIP::RouteSet::~RouteSet() {
  this->clear();
}

void TinySIP::RouteSet::copy(RouteSet& other) {
  log_v("RouteSet::copy");
  this->clear();
  this->setReverse = other.setReverse;
  for (uint16_t i=0; i<other.set.size(); i++)
    if (other.set[i] != NULL) {
      this->set.add(strdup(other.set[i]));
    }
}

void TinySIP::RouteSet::clear(bool reverse) {
  log_v("RouteSet::clear");
  for (uint16_t i=0; i<set.size(); i++) {
    if (set[i]!=NULL) {
      freeNull((void **) &set[i]);
    }
  }
  set.clear();
  setReverse = reverse;
}

bool TinySIP::RouteSet::add(const char *rrAddrSpec, const char *rrParams) {
  // TODO: rrParams are ignored by this implementation for simplicity; preserve it (low priority, almost never used in practice)
  if (rrParams != NULL) {
    log_d("WARNING: non-empty route parameter (rr-param)");
  }
  const char* s = (const char*) strdup(rrAddrSpec);
  return (s != nullptr) ? set.add(s) : false;
}

const char* TinySIP::RouteSet::operator[](uint16_t index) const {
  return set[setReverse ? set.size()-1-index : index];
}

TinySIP::Dialog::Dialog(bool isCaller)
  : caller(isCaller), usageTimeMs(0),
    callIdDyn(nullptr), localTagDyn(nullptr), remoteTagDyn(nullptr),
    localUriDyn(nullptr), remoteUriDyn(nullptr),
    localNameDyn(nullptr), remoteNameDyn(nullptr),
    remoteTargetDyn(nullptr),
    early(0), confirmed(0), terminated(0), secure(0), accepted(0) {}

TinySIP::Dialog::Dialog(bool isCaller, const char* callId, const char* localTag, const char* remoteTag)
  : Dialog(isCaller) {
  this->dialogIdHash   = 0;

  // Remember the dialog ID and calculate its hash
  if (callId) {
    this->callIdDyn    = extStrdup(callId);
    this->dialogIdHash = rotate5(this->dialogIdHash) ^ hash_murmur(callId);
  }
  if (localTag) {
    this->localTagDyn  = extStrdup(localTag);
    this->dialogIdHash = rotate5(this->dialogIdHash) ^ hash_murmur(localTag);
  }
  if (remoteTag) {
    this->remoteTagDyn = extStrdup(remoteTag);
    this->dialogIdHash = rotate5(this->dialogIdHash) ^ hash_murmur(remoteTag);
  }

  // DEBUG
  IF_LOG(VERBOSE) {
    log_d("Dialog(%s, %s, %s) = 0x%x",
          callIdDyn ? callIdDyn : "(null)",
          localTagDyn ? localTagDyn : "(null)",
          remoteTagDyn ? remoteTagDyn : "(null)",
          dialogIdHash);
  }
}

TinySIP::Dialog::~Dialog() {
  if (this->callIdDyn) {
    freeNull((void **) &this->callIdDyn);
  }
  if (this->localTagDyn) {
    freeNull((void **) &this->localTagDyn);
  }
  if (this->remoteTagDyn) {
    freeNull((void **) &this->remoteTagDyn);
  }

  if (this->localUriDyn) {
    freeNull((void **) &this->localUriDyn);
  }
  if (this->remoteUriDyn) {
    freeNull((void **) &this->remoteUriDyn);
  }

  if (this->localNameDyn) {
    freeNull((void **) &this->localNameDyn);
  }
  if (this->remoteNameDyn) {
    freeNull((void **) &this->remoteNameDyn);
  }

  if (this->remoteTargetDyn) {
    freeNull((void **) &this->remoteTargetDyn);
  }
}

bool TinySIP::Dialog::operator==(const Dialog& other) const {
  // (Relatively) quick check for a mismatch
  if (this->dialogIdHash != other.dialogIdHash) {
    return false;
  }
  log_v("dialog ID hash matches");

  // Check whether all parts of dialog ID really match
  if ( !(this->callIdDyn && other.callIdDyn && !strcmp(this->callIdDyn, other.callIdDyn) &&
         this->localTagDyn && other.localTagDyn && !strcmp(this->localTagDyn, other.localTagDyn) &&
         (this->remoteTagDyn &&  other.remoteTagDyn && !strcmp(this->localTagDyn, other.localTagDyn) ||
          !this->remoteTagDyn && !other.remoteTagDyn)) ) {
    return false;
  }

//  // It matched -> update usage time
//  this->usageTimeMs = other.usageTimeMs = millis();
  return true;
}

TinySIP::Dialog* TinySIP::findDialog(const char* callId, const char* tagLocal, const char* tagRemote) {
  Dialog* res = nullptr;

  // Create the Dialog object
  Dialog diag = Dialog(false, callId, tagLocal, tagRemote);    // false or true -> doesn't matter here

  // Search for the same dialog in the array
  for (auto it = this->dialogs.iterator(); it.valid(); ++it) {
    if (diag == **it) {
      log_v("dialog 0x%x found", (uint32_t)diag);
      res = *it;
    }
  }

  if (!res) {
    log_e("dialog 0x%x not found", (uint32_t)diag);
  }

  return res;
}

/* Description:
 *     find a dialog in the `dialogs` array.
 *     if it is not found -> create one and add to the array.
 *     if the array is full while adding -> find the oldest terminated dialog and replace it.
 * Implicit parameters:
 *     on storing a dialog,
 */
TinySIP::Dialog* TinySIP::findCreateDialog(bool isCaller, const char* callId, const char* tagLocal, const char* tagRemote) {

  uint32_t now = millis();

  // Create the Dialog object
  Dialog* diag = new Dialog(isCaller, callId, tagLocal, tagRemote);

  // Search for the same dialog in the array
  for (auto it = this->dialogs.iterator(); it.valid(); ++it) {
    if (*diag == **it) {
      delete diag;

      // TODO: dialogs: update dialog with new information
      (*it)->setUseTime(now);
      if (!diag->remoteTargetDyn && respContAddrSpecDyn) {
        diag->remoteTargetDyn = extStrdup(respContAddrSpecDyn);
      }

      // TODO: dialogs: update CSeq

      return *it;
    }
  }

  // Dialog not found -> rememeber it

  // First: add more information about the dialog using the parsed fields

  {
    const char* localUri  = isCaller ? respFromAddrSpec : respToAddrSpec;
    const char* remoteUri = isCaller ? respToAddrSpec   : respFromAddrSpec;
    diag->localUriDyn  = localUri  ? extStrdup(localUri)  : NULL;
    diag->remoteUriDyn = remoteUri ? extStrdup(remoteUri) : NULL;
  }

  {
    log_d("NAME FROM:   %s", respFromDispName ? respFromDispName : "null");
    log_d("NAME TO:     %s", respToDispName ? respToDispName : "null");
    const char* localName  = isCaller ? respFromDispName : respToDispName;
    const char* remoteName = isCaller ? respToDispName   : respFromDispName;
    if (!localName  && !strcmp(tagLocal,  localTag)) {
      localName = localNameDyn;
    }
    if (!remoteName && !strcmp(tagRemote, localTag)) {
      remoteName = localNameDyn;
    }
    diag->localNameDyn  = localName  ? extStrdup(localName)  : NULL;
    diag->remoteNameDyn = remoteName ? extStrdup(remoteName) : NULL;
    log_d("NAME LOCAL:  %s", diag->localNameDyn ? diag->localNameDyn : "null");
    log_d("NAME REMOTE: %s", diag->remoteNameDyn ? diag->remoteNameDyn : "null");
  }

  diag->localCSeq  = isCaller ? cseq : respCSeq;
  diag->remoteCSeq = isCaller ? respCSeq : cseq;

  if (respContAddrSpecDyn) {
    diag->remoteTargetDyn = extStrdup(respContAddrSpecDyn);
  }

  if (respRouteSet.size()) {
    diag->routeSet.copy(respRouteSet);
  }

  // Second: add this dialog to the array
  diag->setUseTime(now);
  if (this->dialogs.size() < MAX_DIALOGS) {
    log_v("adding dialog 0x%08x to dialogs (size=%d)", (uint32_t)*diag, this->dialogs.size());
    this->dialogs.add(diag);
    return diag;
  }

  // If the array reached it's maximum size -> find a (prefereably terminated) dialog with the oldest usage time and replace it
  bool retry = false;
  int oldest = -1;
  uint32_t oldestTimeDiff = 0;      // time diff with the current time, maximum is 49.7 days, since we are looking only at past times
search:
  for (auto it = this->dialogs.iterator(); it.valid(); ++it) {
    uint32_t timeDiff = now - (*it)->usageTimeMs;
    if (((*it)->isTerminated() || retry) && timeDiff > oldestTimeDiff) {
      oldestTimeDiff = timeDiff;
      oldest = (int)it;
    }
  }
  if (oldest < 0) {
    // No terminated dialogs found -> drop terminated requirement
    log_e("dialogs array is full with non-terminated dialogs");
    retry = true;
    goto search;
  }
  if (oldest >= 0) {
    delete this->dialogs[oldest];
    this->dialogs[oldest] = diag;
    return diag;
  }

  // Should never be reached
  log_e("critical exception: dialog not added");
  return nullptr;
}

void TinySIP::restoreDialogContext(Dialog& diag) {
//  log_v("restoreDialogContext");
//  // TODO: dialogs: see sendResponse to what is needed to reply
//  // TODO: dialogs: see sendBye to what is needed to hangup
//  // TODO: dialogs: see sendAck to what is needed for ACK
//  freeNull((void**) &this->callIdDyn);
//
//  if (diag.callIdDyn) this->callIdDyn = strdup(diag.callIdDyn);
}

TinySIP::TinySIP()
  : tcpLast(tcpProxy), respRouteSet() {
  log_i("TinySIP construct");

  connectReturnedFalse = false;

  // Dynamic variables
  respToTagDyn = NULL;
  remoteToFromDyn = NULL;
  respFromTagDyn = NULL;
  remoteUriDyn = NULL;
  outgoingMsgDyn = NULL;
  localUserDyn = NULL;
  localNameDyn = NULL;
  localUriDyn = NULL;
  proxyPasswDyn = NULL;
  remoteAudioAddrDyn = NULL;
  remoteAudioPort = 0;
  //dialogsDyn = NULL;
  respContDispNameDyn = NULL;
  respContAddrSpecDyn = NULL;
  guiReasonDyn = NULL;
  callIdDyn = NULL;
  regCallIdDyn = NULL;
  msgCallIdDyn = NULL;

  sdpSessionId = 0;
  phoneNumber = 0;
  cseq = 0;
  regCSeq = 0;
  nonceCount = 0;
  nonFree = 0;

  tcpProxy = NULL;
  tcpRoute = NULL;
  tcpCallee = NULL;

  leftOver = false;

  // Reset buffer variables
  resetBuffer();

  // Timing
  this->msLastKnownTime = 0;
  this->msLastRegistered = 0xffffffff - TinySIP::REGISTER_EXPIRATION_S*1000;
  this->msLastRegisterRequest = 0xffffffff - TinySIP::REGISTER_PERIOD_MS + 4500;   // register 4.5 seconds after starting
}

/*
 * Description:
 *      (re)initialize dialog identifiers and connect to SIP proxy
 * return:
 *      whether connection was successfull
 */
bool TinySIP::init(const char* name, const char* fromUri, const char* proxyPass, const uint8_t* mac) {
  log_v("TinySIP::init");

  // re-init logic
  clearDynamicState();
  resetBuffer();

  // Reset bools
  this->registered = false;
  this->everRegistered = false;
  this->registrationRequested = false;

  // Caller parameters
  AddrSpec addrParsed(fromUri);
  localUserDyn = strdup(addrParsed.userinfo());
  localNameDyn = strdup(name);
  localUriDyn = strdup(fromUri);
  proxyPasswDyn = strdup(proxyPass);

  // MAC address
  memcpy(this->mac, mac, 6);
  sprintf(this->macHex, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Connect to the proxy server
  log_v("Connecting to proxy");
  // If the SYN from the connection attempt gets an immediate RST, such as:
  // connect(): socket error on fd 57, errno: 104, "Connection reset by peer"
  // it probably means the proxy doesn't support TCP. Better to warn the user instead of failing silently.
  // Something like: IP address X.X.X.X not accepting TCP connections on port XXXX.
  proxyIpAddr = ensureConnection(tcpProxy, fromUri, false, 500);
  if (tcpProxy && tcpProxy->connected()) {
    log_i("Connected to proxy!");
    log_i("  IP: %s", proxyIpAddr.toString().c_str());
    thisIP = WiFi.localIP().toString();
    return true;
  }
  // Else: connection failed
  log_e("Could NOT connect to proxy");
  return false;
}

/* Description:
 *     generate random phoneNumber and cseq.
 *     Important before making any request or sending a response. Should be called after randomness bits are collected.
 */
void TinySIP::randInit() {
  // Generate random "phone number" ONCE per existence of this object
  if (!phoneNumber) {
    phoneNumber = Random.random();

    // Ensure phoneNumber has 8 digits
    if (!phoneNumber) {
      phoneNumber = 12345678;
    }
    while (phoneNumber>99999999) {
      phoneNumber /= 10;
    }
    while (phoneNumber<10000000) {
      phoneNumber *= 3;
    }

    newLocalTag(true);

    // REGISTER Call-ID is should be the same for all registrations from the UA, therefore it's generated only once
    newCallId(&regCallIdDyn);
  }
  // cseq can potentially cycle past 65535 (almost impossible), so we initialize it separately
  if (!cseq) {
    cseq = (uint16_t) Random.random();              // CSeq must be less than 1<<31, but we use only 16-bit value
    if (cseq<1000) {
      cseq = 1000;  // to avoid confusion with other numbers
    }
    if (cseq>=64000) {
      cseq >>= 1;  // CSeq withing a dialog must be "strictly monotonically increasing" (RFC 3261, Section 12.2.1.1)
    }
  }
}

void TinySIP::freeNullConnectionProxyObject(bool isProxy) {
  // Clean up connections that are identical to tcpProxy
  if (isProxy) {
    if (tcpRoute==tcpProxy) {
      log_d("tcpRoute nulled");
      tcpRoute = NULL;
    }
    if (tcpCallee==tcpProxy) {
      log_d("tcpCallee nulled");
      tcpCallee = NULL;
    }
    tcpProxy = NULL;
  }
}

/*
 * Description:
 *     ensure tcp is connected to host specified by IP and port
 * Return:
 *
 */
bool TinySIP::ensureIpConnection(Connection*& tcp, IPAddress &ipAddr, uint16_t port, bool forceRenew, int32_t timeout) {

  // Check if ipAddr is valid
  if ((uint32_t) ipAddr == 0) {
    log_e("Cannot connect to 0.0.0.0");
    return false;
  }

  // Check if trying to connect to already connected proxy server
  bool isProxy = false;
  if (!forceRenew && tcpProxy!=NULL && tcp!=tcpProxy && tcpProxy->connected() && ipAddr==tcpProxy->remoteIP() && port==tcpProxy->remotePort() && !tcpProxy->stale()) {
    // A new connection is asked to be connected to proxy -> reuse proxy connection instead (useful when tcpRoute has the same address as the proxy)
    log_d("Reusing proxy connection");
    tcp = tcpProxy;
    isProxy = true;
  } else if (tcpProxy!=NULL && tcp==tcpProxy) {
    // Connection is the same as proxy, but it seems to be disconnected or stale
    log_d("Ensuring tcpProxy");
    isProxy = true;
  }

  // Check if already connected
  bool good = false;   // connection is good as is
  bool exist = false;
  if (tcp!=NULL) {
    exist = true;
    if (!forceRenew && tcp->connected() && tcp->remoteIP()==ipAddr && tcp->remotePort()==port && !tcp->stale()) {
      // Connection is good -> use existing connection
      good = true;
    } else {
      // Connection is not good -> clean it up
      log_d("TCP connection state: %s", forceRenew ? "FORCED RENEWAL" : tcp->stale() ? "stale" : (tcp->connected() ? "new destination" : "not connected"));
      tcp->stop();
      delete tcp;
      freeNullConnectionProxyObject(isProxy);//tcpProxy=null etc.
      tcp = NULL;
    }
  }
  uint32_t get_millis = millis();

  /*try to connect to tcp again by some seconds interval.*/
  if(timeout_disconnect && ( get_millis - timeout_disconnect_mls ) < 10000) {
    log_i("Still in disconnect mode");
    return good;
  }

  // Connect
  if (!good) {
    log_e("%s", exist ? "Reconnecting:" : "Connecting:");
    log_e("  IP:   %s", ipAddr.toString().c_str());
    log_e("  Port: %d", port);
    //UDP_SIP=1;
    if(UDP_SIP) {
      if(tcp && !tcp->isUdp() && !connectReturnedFalse || !tcp) {
        tcp = new UDP_SIPConnection;
      }
    } else {
      if(tcp && !tcp->isTcp() && !connectReturnedFalse || !tcp) {
        tcp = new TCP_SIPConnection;
      }
    }
    if (tcp->connect(ipAddr, port, timeout)) {                 // TODO: when there is no connection, this causes HANGING
      log_d("Connected!");
      //log_d("  Socket handle: %d", tcp->fd());
      log_d("  Local port: %d", tcp->localPort());
      good = tcp->connected();
      if (!good) {
        log_d("ERROR: DISCONNECTED");
        timeout_disconnect = true;
        timeout_disconnect_mls = get_millis;
        delete tcp;
        freeNullConnectionProxyObject(isProxy);//tcpProxy=null etc.
        tcp = NULL;
      } else {
        tcp->msLastConnected = msLastKnownTime;
        timeout_disconnect = false;
      }
      tcp->msLastConnected = msLastKnownTime;
      connectReturnedFalse = false;
    } else {
      timeout_disconnect = true;
      timeout_disconnect_mls = get_millis;
      connectReturnedFalse = true;
      log_d("Error: could not connect");
      if(tcp) {
        delete tcp;
        freeNullConnectionProxyObject(isProxy);//tcpProxy=null etc.
        tcp = NULL;
      }
    }
  } else {
    log_d("TCP connection is already good");
    
  }
  return good;
}

/*
 * Description:
 *     ensure `tcp` is connected to host specified by addrSpec.
 * Return:
 *     IP address which was resolved from addrSpec
 */
IPAddress TinySIP::ensureConnection(Connection*& tcp, const char* addrSpec, bool forceRenew, int32_t timeout) {
  log_d("Ensuring connection: %s", addrSpec);
  AddrSpec addrParsed(addrSpec);
  IPAddress ipAddr((uint32_t) 0);
  if (addrParsed.hostPort()) {
    int port = addrParsed.port() ? addrParsed.port() : TINY_SIP_PORT;
    log_d(" - host: %s", addrParsed.host());
    log_d(" - port: %d", port);

    // Connect to IP
    // TODO: correctly resolve NAPTR DNS records to IP addresses (this seems to be needed only for sip2sip.info)
    if (isdigit(addrParsed.host()[0]) && ipAddr.fromString(addrParsed.host())) {
      log_d("Proper IP address: %s", addrParsed.host());
    } else if (!strcasecmp(addrParsed.host(), "sip2sip.info")) {
      log_d("WARNING: hardcoded IP address");
      ipAddr.fromString("85.17.186.7");
//    } else if (!strcasecmp(addrParsed.host(), "sip.linphone.org")) {
//      log_d("WARNING: hardcoded IP address");
//      ipAddr.fromString("91.121.209.194");
//    } else if (!strcasecmp(addrParsed.host(), "iptel.org")) {
//      log_d("WARNING: hardcoded IP address");
//      ipAddr.fromString("212.79.111.155");    // A record of iptel.org, sip.iptel.org
//    } else if (!strcasecmp(addrParsed.host(), "antisip.org")) {
//      log_d("WARNING: hardcoded IP address");
//      ipAddr.fromString("91.121.30.149");    // A record of antisip.com, sip.antisip.com
//    } else if (!strcasecmp(addrParsed.host(), "opensips.org")) {
//      log_d("WARNING: hardcoded IP address");
//      ipAddr.fromString("136.243.23.236");    // A record of opensips.org
    } else {
      // TODO: this resolves domains only through A-record; need to use NAPTR- and SRV-records
      ipAddr = resolveDomain(addrParsed.host());
      if ((uint32_t) ipAddr != 0) {
        log_d("Resolved: %s -> %s", addrParsed.host(), ipAddr.toString().c_str());
      } else {
        log_d("Could not resolve: \"%s\"", addrParsed.host());
      }
    }
    ensureIpConnection(tcp, ipAddr, port, forceRenew, timeout);
  } else {
    log_d("ERROR: no hostport");
  }
  return ipAddr;
}

/*
 * Description:
 *      determine which IP need to be connected
 * Return:
 *      proper tcp conenction for communication
 */
Connection* TinySIP::getConnection(bool isClient) {
  log_d("--- Getting connection ---");
  log_d("TinySIP::getConnection as %s", isClient ? "client" : "server");
  log_d("TinySIP::getConnection respRouteSet.size() is : %d ", respRouteSet.size());
  // UAS found -> connect to UAS directly
  if (respRouteSet.size() > 0) {

    // Responses should be routed

    // The following route set always comes from Record-Route header, but order has different meaning for client and server
    log_d("Ensuring route");
    ensureConnection(tcpRoute, respRouteSet[0]);

    log_d("ensuring tcpRoute: ");
    if (tcpRoute!=NULL) {
      log_d("OK: port = %d", tcpRoute->localPort());
      return tcpRoute;
    } else {
      log_d("EMPTY");
    }
  } else if (respClass=='2') {
    if (respContAddrSpecDyn!=NULL) {

      // Response should be sent to UAS directly

      //ensureConnection(tcpCallee, respContAddrSpecDyn, true);       // TODO: why forced renewal?
      ensureConnection(tcpCallee, respContAddrSpecDyn);
      log_d("ensuring tcpCallee: ");
      if (tcpCallee!=NULL) {
        log_d("OK: port = %d", tcpCallee->localPort());
        return tcpCallee;
      } else {
        log_d("EMPTY");
      }
    } else {
      log_d("EMPTY respContAddrSpecDyn");
    }
  }
  // Fallback to proxy connection
  log_d("tcpProxy connection returned (no RouteSet, no Contact known)");
  return tcpProxy;     // TODO: ensure this one is connected
}

TinySIP::~TinySIP() {
  log_d("tinySIP: destruction");
  //SIP_DEBUG_DELAY(100);       // seems to improve stability
  clearDynamicState();

  // Clean up all Dialog objects
  for (auto it = dialogs.iterator(); it.valid(); ++it)
    if (*it) {
      delete *it;
    }

  // Free the linear array itself
  dialogs.clear();
  freeNull((void **) &regCallIdDyn);

  log_d("tinySIP: finishing destruction");
}

/*
 * Description:
 *      free all dynamic variables
 */
void TinySIP::clearDynamicState() {
  log_d("TinySIP::clearDynamicState");
  //SIP_DEBUG_DELAY(100);       // seems to improve stability?

  freeNull((void **) &remoteUriDyn);
  freeNull((void **) &localUserDyn);
  freeNull((void **) &localNameDyn);
  freeNull((void **) &localUriDyn);
  freeNull((void **) &proxyPasswDyn);
  freeNull((void **) &callIdDyn);
  freeNull((void **) &msgCallIdDyn);
  freeNull((void **) &outgoingMsgDyn);

  clearDynamicParsed();
  clearDynamicConnections();
}

/*
 * Description:
 *      Free only those dynamic variables that contain parsed values.
 *      In practice, this contains all the values that are supposed to be stable for a call.
 */
void TinySIP::clearDynamicParsed() {
  log_d("TinySIP::clearDynamicParsed");
  freeNull((void **) &respToTagDyn);
  freeNull((void **) &remoteToFromDyn);
  freeNull((void **) &respFromTagDyn);
  freeNull((void **) &remoteAudioAddrDyn);
  freeNull((void **) &respContDispNameDyn);
  freeNull((void **) &respContAddrSpecDyn);
  freeNull((void **) &guiReasonDyn);

  remoteAudioPort = 0;
  this->audioFormat = TinySIP::NULL_RTP_PAYLOAD;

  // Forget the route set
  respRouteSet.clear();
}

void TinySIP::clearDynamicConnections() {
  log_d("clearDynamicConnections");
  //SIP_DEBUG_DELAY(100);       // seems to improve stability

  if (tcpProxy!=NULL) {
    delete tcpProxy;
    //in order not to delete the same objects twice then cause crashes, we assign null to same pointer with the tcpProxy
    freeNullConnectionProxyObject(true);
    tcpProxy = NULL;
  }
  if (tcpRoute!=NULL) {
    delete tcpRoute;
    tcpRoute = NULL;
  }
  if (tcpCallee!=NULL) {
    delete tcpCallee;
    tcpCallee = NULL;
  }

  leftOver = false;
}

void TinySIP::resetBuffer() {
  log_d("reset SIP buffer");

  buff[0] = '\0';
  buffLength = 0;
  buffStart = buff;

  resetBufferParsing();
}

void TinySIP::resetBufferParsing() {
  log_d("reset SIP buffer parsing");

  //log_d(" - resetting digests");
  respChallenge = NULL;
  digestRealm = NULL;
  digestDomain = NULL;
  digestNonce = NULL;
  digestCNonce = NULL;
  digestOpaque = NULL;
  digestStale = NULL;
  digestAlgorithm = NULL;
  digestQopOpt = NULL;
  digestQopPref = NULL;

  //log_d(" - resetting links");
  respCode = 0;
  respClass = '0';
  respCallId = NULL;
  respProtocol = NULL;
  respReason = NULL;
  respContentLength = 0;
  respContentType = NULL;
  respBody = NULL;
  respMethod = NULL;
  respHeaderCnt = 0;
  //respToTag[0] = '\0';
  respToDispName = NULL;
  respToAddrSpec = NULL;
  respToParams = NULL;
  respFromDispName = NULL;
  respFromAddrSpec = NULL;

  //log_d(" - reset complete");
  // Resetting dynamically allocated variable
  //freeNull((void **) &respToTagDyn);       // TODO: do we really need to delete these here?
  //freeNull((void **) &respFromTagDyn);     // TODO: do we really need to delete these here?
}

// INVITE method
int TinySIP::requestInvite(uint32_t msNow, Connection& tcp, const char* toUri, const char* body) {
  if (!tcp.connected() || callIdDyn==NULL) {
    return TINY_SIP_ERR;
  }

  randInit();
  newBranch(branch);
  /*if (respCode==UNAUTHORIZED_401) {
    cseq++;
  }
  else {
    cseq;
  }*/
  cseq++;
  freeNull((void **) &respToTagDyn);

  // Set timer for next retransmission
  msTimerAStart = msNow;
  msTimerADuration = msTimerADuration>0 ? 2*msTimerADuration : TinySIP::T1_MS;
  if(UDP_SIP) {
    tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
  }
  // Send INVITE
  sendRequestLine(tcp, "INVITE", toUri);

  // Headers
  sendHeaderVia(tcp, thisIP, tcp.localPort(), branch);
  sendHeaderMaxForwards(tcp, 70);

  sendHeaderToFromLocal(tcp, 'F');                    // From:
  sendHeaderToFromRemote(tcp, 'T', false, toUri);     // To: we don't know the remote tag at this stage
  sendHeaderContact(tcp);
  sendHeaderCallId(tcp, callIdDyn);
  sendHeaderCSeq(tcp, cseq, "INVITE");
  sendHeaderAllow(tcp);
  sendHeaderUserAgent(tcp);
  sendHeaderAuthorization(tcp, toUri);    // Proxy-Authorization or Authorization

  // Content headers and body
  if (body==NULL) {
    const char* cstr = thisIP.c_str();
    int len = sdpBody(tcp, cstr, true);
    sendBodyHeaders(tcp, len, "application/sdp");
    sdpBody(tcp, cstr, false);
  } else {
    sendBodyHeaders(tcp, strlen(body), "application/sdp");
    TCP(tcp, body);
  }
  tcp.flush();
  if(UDP_SIP) {
    tcp.endPacket();
  }
  return TINY_SIP_OK;
}

/*
 * Description:
 *    send SDP body (if not onlyLen) or return length of the SDP body and exit
 * Parameters:
 *    tcp     - TCP connection (class Connection)
 *    ip      - IP of the phone as C-string
 *    onlyLen - whether to return length of the SDP body and exit
 * Return:
 *    of onlyLen is true -> return length of the SDP body to be sent
 *    otherwise -> return 0
 */
int TinySIP::sdpBody(Connection& tcp, const char* ip, bool onlyLen) {
  // SDP session ID has to be different for different sessions
  // So we form it randomly and add 1 for each new session.
  // Here we just ensure that it cycles withing 8 decimal digits
  sdpSessionId = 0x2000000 + (sdpSessionId % 0x2000000);        // ensure it has at least 8 digits, but no more: 33554432 .. 67108863

  const uint16_t localAudioPort = this->getLocalAudioPort();    // port to which audio should be sent through RTP
  const uint16_t localRtcpPort = localAudioPort + 1;

  // TODO: send a single format chosen from the invite or 200 OK
  // SDP body format for one audio stream
  const char format[] PROGMEM = "v=0\r\n"
                                "o=- 37%d 37%d IN IP4 %s\r\n"
                                "s=WiPhone\r\n"
                                "t=0 0\r\n"
                                "m=audio %d RTP/AVP%s\r\n"
                                "c=IN IP4 %s\r\n"
                                "a=r%s:%d\r\n"// %s is replaced by udp or tcp
                                "%s"
                                "a=sendrecv\r\n";

  char rtpPayloads[40];
  char rtpMaps[100];
  rtpPayloads[0] = rtpMaps[0] = '\0';
  char *p = rtpPayloads;
  char *m = rtpMaps;
  for (int i=0; i<sizeof(SUPPORTED_RTP_PAYLOADS)/sizeof(SUPPORTED_RTP_PAYLOADS[0]); i++) {
    if (this->audioFormat == TinySIP::NULL_RTP_PAYLOAD || SUPPORTED_RTP_PAYLOADS[i] == this->audioFormat) {
      p += snprintf(p, sizeof(rtpPayloads) - (p-rtpPayloads), " %d", SUPPORTED_RTP_PAYLOADS[i]);
      const char *s;
      switch (SUPPORTED_RTP_PAYLOADS[i]) {
      case TinySIP::ALAW_RTP_PAYLOAD:
        s = "a=rtpmap:8 PCMA/8000\r\n";
        break;
      case TinySIP::G722_RTP_PAYLOAD:
        s = "a=rtpmap:9 G722/8000\r\n";
        break;
      default:
      case TinySIP::ULAW_RTP_PAYLOAD:
        s = "a=rtpmap:0 PCMU/8000\r\n";
        break;
      }
      m += snprintf(m, sizeof(rtpMaps) - (m-rtpMaps), s);
    }
  }
  // Check for errors
  if (strlen(rtpPayloads)+1 >= sizeof(rtpPayloads)) {
    log_d("ERROR: rtpPayloads too short");
  }
  if (strlen(rtpMaps)+1 >= sizeof(rtpMaps)) {
    log_d("ERROR: rtpMaps too short");
  }

  // NOTE: assumes that final SDP message will be shorter than double the size of the format string
  char buff[2*strlen(format)];
  snprintf(buff, sizeof(buff), format, sdpSessionId, sdpSessionId, ip, localAudioPort, rtpPayloads, ip, (UDP_SIP ? "udp" : "tcp"), localRtcpPort, rtpMaps);
  auto sdpBodyLen = strlen(buff);
  if (sdpBodyLen == sizeof(buff)-1)
    // TODO: allocate more and retry
  {
    log_d("ERROR: SDP buff too short");  // assert
  }

  if (onlyLen) {
    // only return the SDP body size
    return sdpBodyLen;
  } else {
    // actually send SDP body
    TCP(tcp, buff);
    return 0;
  }
}

/* Description:
 *      send the first line of a request
 */
void TinySIP::sendRequestLine(Connection& tcp, const char* methd, const char* addr) {

  // TODO: account for strict routers (p. 74):
  // "If the route set is not empty, and the first URI in the route set
  //  contains the lr parameter (see Section 19.1.1), the UAC MUST place
  //  the remote target URI into the Request-URI"

  TCP(tcp, methd);
  TCP(tcp, " ");
//  if (1 || !strpos(addrSpec, ';')) {          // just send the address as is; parameters are allowed in Request-URI (p. 96, 156)
  // "An implementation MUST include any provided transport, maddr, ttl, or / user parameter in the Request-URI of the formed request"
  TCP(tcp, addr);
//  } else {
//    // Parse addrSpec and remove any URI params and headers       // TODO: not sure this is needed, the Request-Line might actually allow URI parameters and headers
//    char *p = strdup(addr);
//    if (p) {
//      // TODO: use AddrSpec
//      char *scheme, *hostport, *userinfo, *uriParams, *headers;
//      parseAddrSpec(p, &scheme, &hostport, &userinfo, &uriParams, &headers);
//      TCP(tcp, scheme);
//      TCP(tcp, ":");
//      TCP(tcp, userinfo);
//      TCP(tcp, "@");
//      TCP(tcp, hostport);
//      free(p);
//    }
//  }
  TCP(tcp, " SIP/2.0\r\n");
}

/* Description:
 *      ACK method is only sent in response to a response to INVITE
 */
int TinySIP::sendAck(Connection& tcp, const char* toUri) {
  if (!tcp.connected()) {
    return TINY_SIP_ERR;
  }
  log_d("---------------Sending ACK---------------");
  if(UDP_SIP) {
    tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
  }
  bool ackInvite200 = respClass=='2' ? true : false;

  if (ackInvite200) {

    // Acknowledging 200 OK (or 2xx) after INVITE

    if (respContAddrSpecDyn==NULL) {
      return TINY_SIP_ERR;  // TODO
    }

    newBranch(branch);

    // Answer to UAS directly
    sendRequestLine(tcp, "ACK", respContAddrSpecDyn);

  } else {

    // Acknowledging other responses (3xx-6xx)

    // Sanity checks
    if (respToAddrSpec!=NULL && strcmp(toUri, respToAddrSpec)) {
      log_e("To address different from response URI:");
      log_e(" -           toUri: %s", toUri ? toUri : "");
      log_e(" - respToAddrSpeci: %s", respToAddrSpec);
    }

    // Answer to proxy
    sendRequestLine(tcp, "ACK", toUri);
  }

  // Headers
  sendHeaderVia(tcp, thisIP, tcp.localPort(), branch);
  sendHeaderMaxForwards(tcp, 70);
  sendRouteSetHeaders(tcp, true);

  sendHeaderToFromLocal(tcp, 'F');                // From:
  sendHeaderToFromRemote(tcp, 'T', true);         // To: mirror the value
  sendHeaderCallId(tcp, callIdDyn);
//sendHeaderCSeq(tcp, cseq, "ACK");               // CSeq is equal to the requests being aknowledged, but CSeq method MUST be ACK
  sendHeaderCSeq(tcp, respCSeq, "ACK");
  sendHeaderUserAgent(tcp);
  sendBodyHeaders(tcp);

  if(UDP_SIP) {
    tcp.endPacket();
  }

  return TINY_SIP_OK;
}


// BYE method
int TinySIP::requestBye(Connection& tcp) {
  if (!tcp.connected()) {
    return TINY_SIP_ERR;
  }

  randInit();
  newBranch(branch);
  byeCSeq = ++cseq;     // remember bye CSeq to check against response  TODO
  if(UDP_SIP) {
    tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
  }
  // Send BYE
  sendRequestLine(tcp, "BYE", respContAddrSpecDyn!=NULL ? respContAddrSpecDyn : remoteUriDyn);

  // Headers
  sendHeaderVia(tcp, thisIP, tcp.localPort(), branch);      // TODO: dialogs: store Via per dialog?
  sendHeaderMaxForwards(tcp, 70);
  sendRouteSetHeaders(tcp, true);

  if (currentCall) {
    sendByeHeadersToFrom(tcp, currentCall);
    //sendHeadersToFrom(tcp, currentCall);
    sendHeaderCallId(tcp, currentCall->callIdDyn);
    sendHeaderCSeq(tcp, ++currentCall->localCSeq, "BYE");
  } else {
    // Old code to be removed
    log_e("no dialog to bye");
    sendHeaderToFromLocal(tcp, 'F');
    sendHeaderToFromRemote(tcp, 'T', false, remoteUriDyn, remoteTag);    // TODO: ensure that toUri is the same as remoteTag
    sendHeaderCallId(tcp, callIdDyn);
    sendHeaderCSeq(tcp, cseq, "BYE");
  }
  sendHeaderUserAgent(tcp);
  sendBodyHeaders(tcp);
  if(UDP_SIP) {
    tcp.endPacket();
  }
  return TINY_SIP_OK;
}


// CANCEL method
int TinySIP::requestCancel(Connection& tcp) {     // TODO: cancelling outgoing call still doesn't work for some reason
  if (!tcp.connected()) {
    return TINY_SIP_ERR;
  }
  if(!remoteUriDyn) {
    return TINY_SIP_ERR;
  }
  if(UDP_SIP) {
    tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
  }
  // Send CANCEL
  sendRequestLine(tcp, "CANCEL", remoteUriDyn);           // Request-URI must be identical to that in the INVITE request being cancelled

  // Headers
  sendHeaderVia(tcp, thisIP, tcp.localPort(), branch);     // "A CANCEL constructed by a  client MUST have only a single Via header field value matching the / top Via value in the request being cancelled."
  sendHeaderMaxForwards(tcp, 70);
  //sendRouteSetHeaders(tcp, true);                       // TODO: why no route-set?

  sendHeaderToFromLocal(tcp, 'F');                        // From must be identical to that in the INVITE request being cancelled (including tags)
  sendHeaderToFromRemote(tcp, 'T', false, remoteUriDyn);  // To must be identical to that in the INVITE request being cancelled (including tags)
  sendHeaderCallId(tcp, callIdDyn);                       // Call-ID must be identical to that in the INVITE request being cancelled
  sendHeaderCSeq(tcp, cseq, "CANCEL");                    // numeric part of CSeq must be identical to that in the INVITE request being cancelled (TODO: ensure)

  sendHeaderUserAgent(tcp);
  sendBodyHeaders(tcp);
  if(UDP_SIP) {
    tcp.endPacket();
  }
  return TINY_SIP_OK;
}

// REGISTER method
int TinySIP::requestRegister(Connection& tcp) {
  if (!tcp.connected()) {
    return TINY_SIP_ERR;
  }

  randInit();

  // TODO: store scheme:hostport (do not parse it each time)

  // Send first line of the request
  bool succ = false;
  if (localUriDyn != NULL) {
    char *p = strdup(localUriDyn);
    if (p!=NULL) {
      // TODO: use AddrSpec
      char *scheme, *hostport, *userinfo, *uriParams, *headers;
      parseAddrSpec(p, &scheme, &hostport, &userinfo, &uriParams, &headers);
      // TODO: store scheme:hostport for future use
      if(UDP_SIP) {
        tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
      }
      if (scheme!=NULL && hostport!=NULL) {
        // special case of a Request-Line
        TCP(tcp, "REGISTER ");
        TCP(tcp, scheme);
        TCP(tcp, ":");
        TCP(tcp, hostport);
        TCP(tcp, " SIP/2.0\r\n");
        succ = true;
      }
      free(p);
    }
  }
  if (!succ) {
    return TINY_SIP_ERR;
  }

  // New parameters
  newBranch(regBranch);
  if (++regCSeq > 60000) {
    regCSeq = 1;
  }

  // Send headers
  sendHeaderVia(tcp, thisIP, tcp.localPort(), regBranch);
  sendHeaderMaxForwards(tcp, 70);

  sendHeadersToFrom(tcp);
  sendHeaderCallId(tcp, regCallIdDyn);
  sendHeaderCSeq(tcp, regCSeq, "REGISTER");
  sendHeaderContact(tcp);
  sendHeaderExpires(tcp, REGISTER_EXPIRATION_S);
  sendHeaderAuthorization(tcp, localUriDyn);    // Proxy-Authorization or Authorization
  sendBodyHeaders(tcp);

  msLastRegisterRequest = msLastKnownTime;
  this->registrationRequested = true;
  this->registered = false;
  if(UDP_SIP) {
    tcp.endPacket();
  }
  return TINY_SIP_OK;
}

// MESSAGE method
int TinySIP::requestMessage(Connection& tcp) {
  if (!tcp.connected()) {
    return TINY_SIP_ERR;
  }
  if(UDP_SIP) {
    tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
  }
  randInit();
  newBranch(branch);
  byeCSeq = ++cseq;     // remember bye CSeq to check against response  TODO

  // Send MESSAGE
  sendRequestLine(tcp, "MESSAGE", remoteUriDyn);

  // Headers
  sendHeaderVia(tcp, thisIP, tcp.localPort(), branch);
  sendHeaderMaxForwards(tcp, 70);

  sendHeaderToFromLocal(tcp, 'F');
  sendHeaderToFromRemote(tcp, 'T', false, remoteUriDyn);
  sendHeaderCallId(tcp, msgCallIdDyn);
  sendHeaderCSeq(tcp, cseq, "MESSAGE");
  sendHeaderUserAgent(tcp);
  sendHeaderAuthorization(tcp, remoteUriDyn);

  // Body
  sendBodyHeaders(tcp, strlen(outgoingMsgDyn), "text/plain");
  TCP(tcp, outgoingMsgDyn);
  if(UDP_SIP) {
    tcp.endPacket();
  }
  return TINY_SIP_OK;
}

// Implicit parameters:
// - currentCall
// - via header contents - sendHeadersVia()
// - route set - sendRouteSetHeaders()
// - localNameDyn, localUriDyn, localTag - sendHeaderToFromLocal()
// - remoteToFromDyn, toUri, toTag - sendHeaderToFromRemote()
// - respCallId - sendHeaderCallId()
// - respCSeq, respCSeqMethod - sendHeaderCSeq()
// - sdpSessionId, sdpSessionId, localAudioPort, rtpPayloads, localRtcpPort, rtpMaps  - sdpBody()
int TinySIP::sendResponse(Dialog* diag, Connection& tcp, uint16_t code, const char* reason, bool sendSdp) {
  if (!tcp.connected()) {
    return TINY_SIP_ERR;
  }
  if(UDP_SIP) {
    tcp.beginPacket(tcp.remoteIP(), tcp.remotePort());
  }
  // First line
  TCP(tcp, "SIP/2.0 ");
  TCP_PRINTF(tcp, "%d", code);
  TCP(tcp, " ");
  TCP(tcp, reason);
  TCP(tcp, "\r\n");

  // Send headers
  bool caller = (!diag || !diag->caller) ? false : true;   // most requests are outside of a dialog, so if we are replying to them -> we are assuming that other party is the "caller"
  sendHeadersVia(tcp);
  sendRouteSetHeaders(tcp, false);
  sendHeaderToFromLocal(tcp, caller ? 'F' : 'T');          // To tag
  sendHeaderToFromRemote(tcp, caller ? 'T' : 'F', true);   // From tag mirror
  sendHeaderCallId(tcp);
  sendHeaderCSeq(tcp);
  sendHeaderContact(tcp);
  if (sendSdp) {
    // Send SDP body
    const char* cstr = thisIP.c_str();
    int len = sdpBody(tcp, cstr, true);
    sendBodyHeaders(tcp, len, "application/sdp");
    sdpBody(tcp, cstr, false);
  } else {
    sendBodyHeaders(tcp);
  }
  if(UDP_SIP) {
    tcp.endPacket();
  }
  return TINY_SIP_OK;
}

int TinySIP::startCall(const char* toUri, uint32_t msNow) {
  log_i("startCall with %s",toUri);

  // Reset state before making a call
  resetBuffer();
  clearDynamicParsed();
  msTimerAStart = msTimerADuration = 0;     // timer A is for request retransmissions

  // New callID
  newCallId(&callIdDyn);                     // create a new unique Call-ID for this call

  // New callee
  freeNull((void **) &remoteUriDyn);
  remoteUriDyn = strdup(toUri);
  if (remoteUriDyn==NULL) {
    log_i("NULL CALLEE ERROR ");
  }

  // New session ID
  randInit();
  sdpSessionId = sdpSessionId>0 ? sdpSessionId + 1 : phoneNumber;
  log_d("phoneNumber  = %d", phoneNumber);
  log_d("sdpSessionId = %d", sdpSessionId);

  // Send INVITE
//    log_d("FORCING PROXY");
//    if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT, true)) {     // TODO: why does this require renewing connection?
//      requestInvite(msNow, *tcpProxy, toUri, NULL);
//    }
  if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {
    requestInvite(msNow, *tcpProxy, toUri, NULL);
  }

  return TINY_SIP_OK;     // TODO: check for errors
}

int TinySIP::sendMessage(const char* toUri, const char* msg) {
  log_d("TinySIP::sendMessage");

  // Reset state before making a call
  resetBuffer();
  clearDynamicParsed();

  // New callID
  newCallId(&msgCallIdDyn);   // outside of a dialog, therefore creating a new unique Call-ID

  // New "callee"
  freeNull((void **) &remoteUriDyn);
  remoteUriDyn = strdup(toUri);

  // New message text
  freeNull((void **) &outgoingMsgDyn);
  outgoingMsgDyn = strdup(msg);

  // Send MESSAGE request
  if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {
    requestMessage(*tcpProxy);
    return TINY_SIP_OK;
  }

  return TINY_SIP_ERR;
}

/* Description:
 *      send 200 OK response for the incoming INVITE request
 */
int TinySIP::acceptCall() {
  log_i("TinySIP::acceptCall");
  if (!currentCall) {
    log_e("currentCall not set");
    return TINY_SIP_ERR+2;
  }

  // TODO: dialogs
  // TODO: merge this logic with Dialog
  // TODO: restore dialog parameters
//  this->restoreDialogContext(*currentCall);

  currentCall->accepted = true;   // we are being called
  if (isResponse || respType!=TINY_SIP_METHOD_INVITE) {
    log_e("error: isResonse = %d, respType = %d", isResponse, respType);
    log_d("terminated = 1");
    currentCall->terminated = 1;
    return TINY_SIP_ERR+1;
  }

  if (!ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {
    log_e("error: could not ensure proxy connection");
  }
  Connection* tcpReply = getConnection(false);
  //    if(UDP_SIP) {
// tcpProxy->beginPacket(tcpProxy->remoteIP(), tcpProxy->remotePort());
// #endif
  log_v("--- 200 OK for INVITE ---");
  int err = sendResponse(currentCall, *tcpReply, OK_200, "OK", true);
  //int err = sendResponse(currentCall, *tcpProxy, OK_200, "OK", true);
  if (err==TINY_SIP_OK) {
    // Change dialog state to confirmed
    currentCall->setConfirmed();
  } else {
    log_d("terminated = 1");
    currentCall->terminated = 1;
    log_e("response error: %d", err);
    //     if(UDP_SIP) {
    //tcpProxy->endPacket();
    //#endif
    return err;
  }

  return TINY_SIP_OK;
}

/* Description:
 *      send 603 Decline response for the incoming INVITE request
 */
int TinySIP::declineCall() {
  log_d("TinySIP::declineCall");

  log_d("terminated = 1");
  if (currentCall) {
    currentCall->terminated = 1;
  }

  if (isResponse || respType!=TINY_SIP_METHOD_INVITE) {        // TODO (1): use current call parameters, not last parsed message
    return TINY_SIP_ERR+1;
  }

  if (!ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {
    log_d("Error: could not ensure proxy connection");
  }
  Connection* tcpReply = getConnection(false);
  log_d("--- 603 Decline ---");
  int err = sendResponse(currentCall, *tcpReply, DECLINE_603, "Decline", true);
  if (err==TINY_SIP_OK) {
    freeNull((void **) &callIdDyn);
  } else {
    log_d("response error: %d", err);
    return err;
  }

  return TINY_SIP_OK;
}

/*
 * Description:
 *      send REGISTER request to the current server
 */
int TinySIP::registration() {
  log_d("TinySIP::register");
  if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {         // stale connection is checked against msLastReceived
    requestRegister(*tcpProxy);
  }
  return TINY_SIP_OK;     // TODO: check for errors in sending
}

int TinySIP::ping(uint32_t now) {
  log_d("TinySIP::ping");
  if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {         // stale connection is checked against msLastReceived
    if (tcpProxy->connected()) {
      TCP((*tcpProxy), TINY_SIP_CRLF TINY_SIP_CRLF);
      tcpProxy->msLastPing = now;
      tcpProxy->rePinged = tcpProxy->pinged;      // connection can be considered stale only after second ping sent without reply to the first one
      tcpProxy->pinged = true;
      return TINY_SIP_OK;
    }
  }
  return TINY_SIP_ERR;
}

/* Description:
 *     this method gets called when a user presses hangup button in the UI
 */
int TinySIP::terminateCall(uint32_t now) {
  log_i("TinySIP::terminateCall");
  if (!currentCall) {
    log_e("currentCall not set");
    if(tcpProxy) {
      int errCancel = requestCancel(*tcpProxy);
      if (errCancel!=TINY_SIP_OK) {
        log_v("CANCEL error: %d", errCancel);
      }
    }
    return TINY_SIP_ERR;
  }
  if (currentCall->terminated) {
    if(tcpProxy) {
      int errCancel = requestCancel(*tcpProxy);
    }
    log_e("currentCall is already terminated");
    return TINY_SIP_ERR+1;
  }

  log_v("terminated = 1");
  currentCall->terminated = 1;

  // Restore context from the current call before sending the request
  if (currentCall->caller) {
    // We are the caller: From == Local
    if (currentCall->localUriDyn) {
      respFromAddrSpec = currentCall->localUriDyn;
    }
    if (currentCall->remoteUriDyn) {
      respToAddrSpec   = currentCall->remoteUriDyn;
    }
    if (currentCall->localNameDyn) {
      respFromDispName = currentCall->localNameDyn;
    }
    if (currentCall->remoteNameDyn) {
      respToDispName   = currentCall->remoteNameDyn;
    }
    cseq  = currentCall->localCSeq;
  } else {
    // We are the callee: To == Local
    if (currentCall->localUriDyn) {
      respToAddrSpec   = currentCall->localUriDyn;
    }
    if (currentCall->remoteUriDyn) {
      respFromAddrSpec = currentCall->remoteUriDyn;
    }
    if (currentCall->localNameDyn) {
      respToDispName   = currentCall->localNameDyn;
    }
    if (currentCall->remoteNameDyn) {
      respFromDispName = currentCall->remoteNameDyn;
    }
    cseq  = currentCall->remoteCSeq;
  }

  freeNull((void **) &respContAddrSpecDyn);
  if (currentCall->remoteTargetDyn) {
    respContAddrSpecDyn = extStrdup(currentCall->remoteTargetDyn);
  }

  respRouteSet.copy(currentCall->routeSet);

  int err = TINY_SIP_ERR+1;
  if (0 && currentCall && currentCall->caller && !currentCall->confirmed) {     // TODO: sending CANCEL request doesn't work now; instead we are sending a regular BYE request
//    log_v("FORCING PROXY");
//    if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT, true)) {                 // TODO: why renewing the connection?
    if (ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {
      log_v("--- Cancelling ---");
      err = requestCancel(*tcpProxy);
      if (err!=TINY_SIP_OK) {
        log_v("CANCEL error: %d", err);
      }
    } else {
      log_e("error: could not ensure proxy connection");
      err = TINY_SIP_ERR+2;
    }
  } else {
    Connection* tcp = getConnection(true);   // we are making a request -> we are a client
    log_v("--- Byeing ---");
    err = requestBye(*tcp);
    if (err!=TINY_SIP_OK) {
      log_e("BYE error: %d", err);
    }
  }
  log_d("terminated = 1");
  currentCall->terminated = 1;
  msTermination = now;
  return err;
}

//ADDED: For terminating call when WiPhone is disconnected from wifi
int TinySIP::wifiTerminateCall() {

  ///////////////////////////////
  ///This part as same as TinySip destructor

  // Clean Dialog
  for (auto it = dialogs.iterator(); it.valid(); ++it)
    if (*it) {
      delete *it;
    }

  dialogs.clear();
  //freeNull((void **) &regCallIdDyn);
  ////////////////////////////////

  //TinySIP::StateFlags_t res;
  //res &= ~TinySIP::EVENT_REGISTERED;
  TinySIP::registered = false;

  //this->registrationRequested = false;
  currentCall->terminated = 1;
  return TINY_SIP_OK;
}

void TinySIP::rtpSilent() {
  // Clean Dialog
  for (auto it = dialogs.iterator(); it.valid(); ++it)
    if (*it) {
      delete *it;
    }

  dialogs.clear();
  //freeNull((void **) &regCallIdDyn);
  ////////////////////////////////

  TinySIP::registered = true;

  currentCall->terminated = 1;

}

void TinySIP::showParsed() {
  // Show parsing results
  log_d("%s", isResponse ? "Response parsed:" : "Request parsed:");
  log_d("  Protocol: %s", respProtocol);
  if (isResponse) {
    log_d("  Class: %c", respClass);
    log_d("  Code: %d", respCode);
    log_d("  Reason: %s", respReason!=NULL ? respReason : "NULL");
  } else {
    log_d("  Method: %s", respMethod!=NULL ? respMethod : "NULL");
  }
  log_d("  Call-ID: %s", respCallId!=NULL ? respCallId : "NULL");
  log_d("  CSeq: %d %s", respCSeq, respCSeqMethod!=NULL ? respCSeqMethod : "NULL");
  log_d("  Content-Length: %d", respContentLength);
  if (respContentType) {
    log_d("  Content-Type: %s", respContentType!=NULL ? respContentType : "NULL");
  }
  log_d("  Headers: %d", respHeaderCnt);
  for (int i=0; i<respHeaderCnt; i++) {
    log_d("    %s: %s", respHeaderName[i] ? respHeaderName[i] : "NULL", respHeaderValue[i] ? respHeaderValue[i] : "NULL");
  }
  if (respContentLength>0 && respBody!=NULL) {
    log_d("  Body: \r\n  --------------------------------\r\n%s  --------------------------------", respBody);
  }
}

/*
 * Description:
 *    process one incoming SIP request or reply
 * Return:
 *    one of the events (see StateFlags_t). It might look something like:
 *    (EVENT_NONE | EVENT_RINGING | ...) | (EVENT_RESPONSE_PARSED | EVENT_REQUEST_PARSED) | EVENT_MORE_BUFFER | EVENT_SIP_ERROR
 */
TinySIP::StateFlags_t TinySIP::checkCall(uint32_t msNow) {
  msLastKnownTime = msNow;

  // TODO: are we sure we want to create entirely new connection here?
  bool reconnected = false;
  if (tcpProxy==NULL || !tcpProxy->connected() || tcpProxy->stale()) {
    if(tcpProxy) {
      log_d("RENEWING: %s", tcpProxy->stale() ? "proxy connection is stale" : "proxy disconnected" );
    } else {
      log_d("RENEWING: %s", "proxy connection doesn't exist" );
      this->registered = false;
    }
    reconnected = ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT, true, 500);
    if (!reconnected) {
      return EVENT_CONNECTION_ERROR;
    }
  }

  StateFlags_t res = EVENT_NONE;

  // 1) Receive data

  // Pick the connection with incoming data
  int32_t avail;
  Connection* tcp = tcpLast;
  avail = tcp!=NULL ? tcp->available() : 0;
  if (!leftOver || avail <= 0 || avail >= IMPOSSIBLY_HIGH) {
    tcp = tcpProxy;
    avail = tcp!=NULL ? tcp->available() : 0;
    if (avail <= 0 || avail >= IMPOSSIBLY_HIGH) {
      tcp = tcpRoute;
      avail = tcp!=NULL ? tcp->available() : 0;
      if (avail <= 0 || avail >= IMPOSSIBLY_HIGH) {
        tcp = tcpCallee;
        avail = tcp!=NULL ? tcp->available() : 0;
        if (!(avail <= 0 || avail >= IMPOSSIBLY_HIGH)) {
          log_d("READING: tcpCallee %d", avail);
        }
      } else {
        log_d("READING: tcpRoute %d", avail);
      }
    } else {
      log_d("READING: tcpProxy %d", avail);
    }
  } else {
    log_d("READING: tcpLast %d", avail);
  }

  // Read from the connection with incoming data
  if (avail > 0 && avail < IMPOSSIBLY_HIGH) {
    auto totalReceived = 0;       // just for debugging
    log_v("avail: %d", avail);
    while (avail > 0) {
      // NOTE: could also read only message by message: can do this by deffered reading of body (parse header first, until CRLF met, then read body)
      log_v("len=%d, left=%d", buffLength, MAX_MESSAGE_SIZE - buffLength);
      int32_t justRead = tcp->read((uint8_t *)(buff+buffLength), MAX_MESSAGE_SIZE - buffLength);

      // Process one of the three options: 1) something read; 2) nothing read but buffer can be cleaned up; 3) neither

      if (justRead>0) {
        // Something read
        tcpLast = tcp;
        avail -= justRead;
        buffLength += justRead;
        totalReceived += justRead;
      } else if (buffStart > buff && MAX_MESSAGE_SIZE - buffLength <= 0) {
        // Nothing read and buffer is full -> try to shift it to the beginning to clear some space
        if (buffStart >= buff + buffLength) {
          // No information in the buffer
          log_d("BUFFER RESET");
          buffLength = 0;
          buffStart = buff;
        } else {
          // Buffer is not empty and starts not at the beginning -> shift it to the left
          log_d("BUFFER SHIFTED: %d", buffStart - buff);
          for (char* p=buffStart; p<buff+buffLength; p++) {
            buff[p-buffStart] = *p;
          }
          buffLength -= buffStart-buff;
          buffStart = buff;
        }
        buff[buffLength] = '\0';
      } else {
        // Stop condition: nothing read and buffer cannot be cleaned up
        break;
      }
    }
    leftOver = (avail > 0);
    buff[MAX_MESSAGE_SIZE] = '\0';   // extra safety termination byte

    // If something received -> parse response
    if (buffLength>0) {
      Random.feed(msNow);         // collect randomness bits for global Random object
      resetBufferParsing();
      //buffStart = buff;
      log_d("Received length: %d", totalReceived);
      buff[buffLength] = '\0';
      tcp->msLastReceived = msNow;
    }
  }

  // Process one message (response or request)
  // TODO: filter out garbage messages
  // TODO: how do we skip incorrect responses and/or requests?
  if (buffStart<buff+buffLength) {

    log_d("--- parsing ---");
    log_d("Length: %d", buffLength);
    log_d("Offset: %d", buffStart - buff);
    xxd(buffStart);
    log_d("---------------");


    // Parse one of the three options: 1) pong; 2) response; 3) request.

    uint16_t parsingErr = TINY_SIP_ERR;
    this->isResponse = strncmp(buffStart, "SIP/", 4) ? false : true;
    if (!strncmp(buffStart, TINY_SIP_CRLF, 2)) {

      // Received pong

      log_d("-----------------------> SIP pong received <-----------------------");
      if (tcp==tcpProxy && tcpProxy!=NULL && tcpProxy->pinged && !elapsedMillis(msNow, tcpProxy->msLastPing, TinySIP::PING_TIMEOUT_MS)) {
        tcpProxy->everPonged = true;
        tcpProxy->pinged = tcpProxy->rePinged = false;
        tcpProxy->msLastPong = msNow;
      } else {
        log_d("-----------------------> ERROR: wrong pong <-----------------------");
      }
      buffStart += 2;
      parsingErr = TINY_SIP_OK;      // mark that parsing was correct
      res |= EVENT_PONGED;

    } else if (isResponse) {

      // Received response

      parsingErr = parseResponse();
      if (parsingErr==TINY_SIP_OK) {
        showParsed();
        res |= EVENT_RESPONSE_PARSED;

        // Take actions:

        if (respType==TINY_SIP_METHOD_INVITE) {

          // Response to INVITE request

          // Create (or find) a dialog for the outgoing INVITE (that we've received a response to, supposedly)
          TinySIP::Dialog* dialog = nullptr;
          if (respToTagDyn) {
            // TODO: check that this response is legit: Maybe create another type of a dialog, MONOLOGUE with only our credentials; then destroy it on creating an "early" or "confirmed" dialog
            // TODO: save RouteSet here; (or where is RouteSet created? in 200 OK?)
            dialog = findCreateDialog(true, respCallId, respFromTagDyn, respToTagDyn);    // true - we are the caller, "From" is us
          }

          // TODO: dialogs: accept only one dialog into session

          if (respClass=='1') {
            if (dialog) {
              dialog->early = true;
            }
          }

          // - need to acknowledge any final response for INVITE responses
          else if (respClass>='2' && respClass<='6') {
            Connection* tcpAck = getConnection(true);
            log_d("--- Acking ---");
            //SIP_DEBUG_DELAY(100);       // seems to improve stability

            if (dialog != nullptr) {
              if (respClass=='2') {
                // RFC 3261: "transition to the "confirmed" state when a 2xx final response arrives"
                dialog->setConfirmed();
                currentCall = dialog;
              } else if (respClass!='2' && respClass!='1') {
                // RFC 3261: ""For other responses (>=3xx), or if no response arrives at all on that dialog, the early dialog terminates"
                log_d("terminated = 1");
                dialog->terminated = 1;
              }

              // ACK for a response to INVITE should be sent in any case
              int sendErr = sendAck(*tcpAck, remoteUriDyn);
              if (sendErr==TINY_SIP_OK) {
                if (respClass=='2') {
                  res |= EVENT_CALL_CONFIRMED;
                }
              } else {
                res |= EVENT_CALL_TERMINATED | EVENT_SIP_ERROR;
                log_d("terminated = 1");
                if (currentCall) {
                  currentCall->terminated = 1;
                }
                log_e("acking error: %d", sendErr);
              }
            } else {
              log_e("no dialog at INVITE response");
            }
          }
        }

        // - authenticate with the proxy
        // - authenticate with the registrar
        if ((respCode==PROXY_AUTHENTICATION_REQUIRED_407 || respCode==UNAUTHORIZED_401 || respCode == REQUEST_PENDING) &&
            (respType==TINY_SIP_METHOD_INVITE || respType==TINY_SIP_METHOD_REGISTER || respType==TINY_SIP_METHOD_MESSAGE)) {
          log_d("Authentication parameters");
          if (tmpRespSeq != respCSeq) {
            char empty[] = "";
            char* alg = (digestAlgorithm!=NULL) ? digestAlgorithm : empty;
            char* user = (localUserDyn!=NULL) ? localUserDyn : (char*) "anonymous";
            char* realm = (digestRealm!=NULL) ? digestRealm : empty;
            char* pass = (proxyPasswDyn!=NULL) ? proxyPasswDyn : empty;
            char* nonce = (digestNonce!=NULL) ? digestNonce : empty;
            char* qop = (digestQopPref!=NULL) ? digestQopPref : empty;
            *cnonce = '\0';
            char nonceCountStr[9] = "";
            // TODO: response-auth / rspauth
            if (*qop) {
              newCNonce((char *) cnonce);
              sprintf(nonceCountStr, "%08x", ++nonceCount);     // TODO: we always increment this, but maybe should check for nonce value first
            }
            char* toUri = empty;
            bool registerResponse = !strcasecmp(respCSeqMethod,"REGISTER");
            if (!registerResponse && remoteUriDyn!=NULL) {
              toUri = remoteUriDyn;
            } else if (registerResponse && localUriDyn != NULL) {
              toUri = localUriDyn;
            }
            char* meth = respCSeqMethod;

            HASHHEX HA1;
            HASHHEX HA2 = "";

            log_d("Digesting");
            DigestCalcHA1(alg, user, realm, pass,  nonce, cnonce, HA1);
            log_d("Digest HA1 = %s", HA1);
            DigestCalcResponse(HA1, nonce, nonceCountStr, cnonce, qop, meth, toUri, HA2, digestResponse);
            log_d("Digest reponse = %s", digestResponse);

            // Send updated request with digest response
            if (registerResponse) {
              requestRegister(*tcpProxy);
            } else if (respType==TINY_SIP_METHOD_INVITE) {
              if (!reconnected && !ensureIpConnection(tcpProxy, proxyIpAddr, TINY_SIP_PORT)) {    // <-- INVITE with authorization
                return EVENT_CONNECTION_ERROR;
              }

              /*
                The Arduino board may restart itself because of sending the invite and receiving 401 error message numerous times.
                This check fixes this restart problem.
              */
              triedToMakeCallCounter++;

              //if(triedToMakeCallCounter < 5) {
              //cseq--;
              requestInvite(msNow, *tcpProxy, remoteUriDyn, NULL);
              /*} else {
                res |= EVENT_CALL_TERMINATED;
                //std::cout << "call terminated due to not registered callee_____________________" << endl;
                log_d("call terminated due to not registered callee_____________________");
              }*/
            } else {
              requestMessage(*tcpProxy);
            }
            tmpRespSeq = respCSeq;
          }
        }

      } else {
        log_d("parseResponse ERROR: %d", parsingErr);
        res |= EVENT_SIP_ERROR;
      }

    } else {

      // Received request

      parsingErr = parseRequest();
      if (parsingErr==TINY_SIP_OK) {
        showParsed();
        res |= EVENT_REQUEST_PARSED;

        // Take actions:

        // - send 180 Ringing or 486 Busy Here for INVITE
        if (respType==TINY_SIP_METHOD_INVITE && respCallId && (!callIdDyn || strcmp(respCallId, callIdDyn))) {      // ignore one's own INVITE

          uint16_t localCode = RINGING_180;          // local response code
          const char* localReason = "Ringing";

          // Create a dialog for the incoming INVITE
          TinySIP::Dialog* dialog = findCreateDialog(false, respCallId, localTag, respFromTagDyn);    // false - we are the callee, "From" is them

          if (dialog && !isBusy()) {
            // Accept this INVITE and start ringing
            log_v("start ringing / 180 Ringing");

            dialog->early = true;
            currentCall = dialog;

            // Change call state
            freeNull((void **) &remoteUriDyn);
            remoteUriDyn = strdup(respFromAddrSpec);
            log_v("set remoteUriDyn: %s", remoteUriDyn);

            freeNull((void **) &callIdDyn);
            callIdDyn = strdup(respCallId);

          } else if (dialog && this->isBusy()) {
            // Reply that this phone is busy
            log_v("busy / 486 Busy Here");
            log_d("terminated = 1");
            dialog->terminated = 1;
            localCode = BUSY_HERE_486;
            localReason = "Busy Here";
          } else {
            log_e("critical error: failed to create dialog");
            log_d("terminated = 1");
            dialog->terminated = 1;
            localCode = SERVER_INTERNAL_ERROR_500;
            localReason = "Server Internal Error";
          }

          // Send actual response
          Connection* tcpReply = getConnection(false);
          log_v("--- %d %s ---", localCode, localReason);
          int sendErr = sendResponse(dialog, *tcpReply, localCode, localReason);
          if (sendErr!=TINY_SIP_OK) {
            log_e("responding error: %d", sendErr);
            res |= EVENT_SIP_ERROR;
          }

        } else

          // - send response for BYE request
          if (respType==TINY_SIP_METHOD_BYE) {

            // Check whether the dialog exists
            uint16_t localCode = OK_200;          // local response code
            const char* localReason = "OK";

            // Find the dialog: try both cases: we are the caller and the callee
            TinySIP::Dialog* dialog = findDialog(respCallId, respFromTagDyn, respToTagDyn);   // in case the caller is hanging up
            if (!dialog) {
              dialog = findDialog(respCallId, respToTagDyn, respFromTagDyn);  // in case the callee is hanging up (we are the caller)
            }

            if (dialog && !dialog->isTerminated()) {
              // Mark the dialog as terminated (if not already terminated)
              log_d("terminated = 1");
              dialog->terminated = 1;
            } else {
              // Dialog is not found or already terminated
              localCode = CALL_DOES_NOT_EXIST_481;
              localReason = "Call Does Not Exist";
            }
            // TODO: how do we ensure that the request came from the caller/callee?

            // Send the response
            Connection* tcpReply = getConnection(false);
            log_v("--- %d %s for BYE ---", localCode, localReason);
            int sendErr = sendResponse(dialog, *tcpReply, localCode, localReason);
            if (sendErr!=TINY_SIP_OK) {
              log_e("send response error: %d", sendErr);
              return sendErr;
            }

          } else

            // - send 200 OK for MESSAGE request (per RFC 3428, p. 5)
            if (respType==TINY_SIP_METHOD_MESSAGE) {
              Connection* tcpReply = getConnection(false);
              log_v("--- 200 OK for MESSAGE ---");
              int sendErr = sendResponse(NULL, *tcpReply, OK_200, "OK");
              if (sendErr!=TINY_SIP_OK) {
                log_e("send response error: %d", sendErr);
                return sendErr;
              }

              // Save message to be processed by checkMessage
              textMessages.add(new TextMessage(respBody, respFromAddrSpec, respToAddrSpec, msNow));
            }


        if (respType==TINY_SIP_METHOD_CANCEL) {
          if (currentCall && !currentCall->terminated) {
            // TODO: check validity according to the RFC
            Connection* tcpReply = getConnection(false);
            log_d("--- 200 OK ---");
            int sendErr = sendResponse(currentCall, *tcpReply, OK_200, "OK");
            if (sendErr!=TINY_SIP_OK) {
              log_d("response error: %d", sendErr);
              return sendErr;
            }
          }
        }

      } else {
        log_d("parseRequest ERROR: %d", parsingErr);
        res |= EVENT_SIP_ERROR;
      }

    }

    if (parsingErr == TINY_SIP_OK) {

      // No error encountered -> Final function result

      if (!(res & EVENT_PONGED)) {

        // The message is not a pong

        if (isResponse) {

          // Response

          if (respType==TINY_SIP_METHOD_INVITE) {
            if (respCode==180) {
              res |= EVENT_RINGING;
            }
            if (respClass>='3' && respCode!=407 && respCode!=401) {      // 401 - is a special treatment for VoIP.ms, which returns 401 on INVITE
              res |= EVENT_CALL_TERMINATED;
              log_d("terminated = 1");
              if (currentCall) {
                currentCall->terminated = 1;
              }
            }
          } else if (respType==TINY_SIP_METHOD_BYE) {
            if (respClass=='2') {
              res |= EVENT_CALL_TERMINATED;
              log_d("terminated = 1");
              if (currentCall) {
                currentCall->terminated = 1;
              }
            }
          } else if (respType==TINY_SIP_METHOD_REGISTER) {
            // TODO: ensure that headers match with the request
            if (respClass=='2') {
              this->registered = true;
              this->everRegistered = true;
              msLastRegistered = msNow;
              res |= EVENT_REGISTERED;
            }
          }
        } else {

          // Request

          if (respType==TINY_SIP_METHOD_BYE || respType==TINY_SIP_METHOD_CANCEL) {
            res |= EVENT_CALL_TERMINATED;
            log_d("terminated = 1");
            if (currentCall) {
              currentCall->terminated = 1;  // TODO: check if the request is legit
            }
          } else if (respType==TINY_SIP_METHOD_INVITE) {
            res |= EVENT_INCOMING_CALL;
          } else if (respType==TINY_SIP_METHOD_ACK && respCSeqMethod && respMethod && !strcmp(respMethod, respCSeqMethod)) {
            res |= EVENT_CALL_CONFIRMED;
            log_d("Received ACK for SDP request");
          } else if (respType==TINY_SIP_METHOD_MESSAGE) {
            res |= EVENT_INCOMING_MESSAGE;
          }
        }

      }

    } else {

      // Failed to parse -> drop erroneous buffer altogether

      respClass = '0';
      respCode = 99;
      res |= EVENT_SIP_ERROR;

      //buffStart++;    // to avoid parsing this again
      // TODO: pass wrong request/responses
      log_d("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! DROPPING BUFFER 0x%x !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", parsingErr);
      log_d("Length: %d", buffLength);
      log_d("Offset: %d", buffStart - buff);
      xxd(buffStart);
      log_d("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      resetBuffer();    // DEBUG

    }

    if (buffStart<buff+buffLength) {
      log_d("buffStart: %s", buffStart);
      res |= EVENT_MORE_BUFFER;
    }

    // Retransmit INVITE (this is needed only for unreliable transport - UDP; we are using TCP); hisorical code just for reference if UDP transport will ever be added
    //} else if (currentCall && currentCall->caller && !currentCall->proceeding && elapsedMillis(msNow, msTimerAStart, msTimerADuration)) {
    //  log_i("retransmitting INVITE");
    //  requestInvite(msNow, *tcpProxy, remoteUriDyn, NULL);

  } else if (!isBusy() || nonFree % 16 == 0) {       // do these less important checks every 16th time, not each time (minor optimization)

    if (tcpProxy!=NULL && this->everRegistered && elapsedMillis(msNow, tcpProxy->msLastPing, PING_PERIOD_MS)) {

      ping(msNow);

    } else if (!this->registrationRequested || elapsedMillis(msNow, msLastRegisterRequest, REGISTER_PERIOD_MS)) {

      // send REGISTER if nothing happened and the time has come
      registration();

      }

    } else {

    nonFree++;

  }

  return res;
}

/* Description:
 *      returns pointer to a text message, if one was received
 */
TextMessage* TinySIP::checkMessage(uint32_t msNow, uint32_t timeNow, bool useTime) {
  if (!textMessages.size()) {
    return nullptr;
  }
  TextMessage* res = textMessages[0];
  textMessages.remove(0);
  res->utcTime = timeNow + (msNow - res->millisTime)/1000;   // will be correct even if msNow (millis() time) overflows
  res->useTime = useTime;
  return res;
}

/*
 *  Description:
 *      1. Breaks message buffer into muliple strings saving pointers into appropriate variables
 *      2. Parses each header individually.
 *  implicit parameter and return value:  (TODO: very ugly and prone to mistakes)
 *      buffStart   (char*)
 *  other implicit parameters:
 *      buff        (char*)
 *      buffLength  (int)
 *  return:
 *      TINY_SIP_OK or TINY_SIP_ERR + N
 */
int TinySIP::parseResponse() {
  // Check correctness of response
  if (strncmp(buffStart, "SIP/", 4)) {
    return respCode = TINY_SIP_ERR;
  }

  char* s = buffStart;

  // Clear parsed
  respCode = TINY_SIP_ERR;
  respClass = '0';      // error
  respProtocol = respReason = respBody = NULL;
  respHeaderCnt = 0;
  respContentLength = 0;
  respContentType = NULL;
  respCSeq = 0;
  respCSeqMethod = NULL;
  respMethod = NULL;
  respUri = NULL;

  // First line:
  // - protocol and version
  respProtocol = strsep(&s, " ");

  // - status code
  char* code = strsep(&s, " ");
  if (code==NULL) {
    return TINY_SIP_ERR+1;
  }
  respClass = code[0];
  respCode = atoi(code);

  // - reason phrase
  respReason = strsep(&s, "\r\n");
  if (respReason==NULL) {
    return TINY_SIP_ERR+2;
  }
  if (s[0]=='\n') {
    s++;
  }

  // Parse all headers
  int err = parseAllHeaders(s);
  respType = methodType(respCSeqMethod);

  // Synonyms
  remoteTag = respToTagDyn;

  return err;
}

int TinySIP::parseRequest() {
  char* s = buffStart;

  // Clear parsed
  respCode = TINY_SIP_ERR;
  respClass = '0';      // error
  respProtocol = respReason = respBody = NULL;
  respHeaderCnt = 0;
  respContentLength = 0;
  respContentType = NULL;
  respCSeq = 0;
  respCSeqMethod = NULL;
  respMethod = NULL;
  respUri = NULL;
  respType = TINY_SIP_METHOD_NONE;

  // First line:
  // - request method
  respMethod = strsep(&s, " ");
  respType = methodType(respMethod);
  if (respType==TINY_SIP_METHOD_UNKNOWN || respType==TINY_SIP_METHOD_NONE) {
    return TINY_SIP_ERR+1;
  }

  // - Request-URI
  respUri = strsep(&s, " ");
  if (respUri==NULL) {
    return TINY_SIP_ERR+2;
  }

  // - protocol and version
  respProtocol = strsep(&s, "\r\n");
  if (respProtocol==NULL) {
    return TINY_SIP_ERR+2;
  }
  if (s[0]=='\n') {
    s++;
  }

  // Parse all headers
  int err = parseAllHeaders(s);

  // Synonyms
  remoteTag = respFromTagDyn;

  return err;
}

/*
 *  Description:
 *      used by parseRequest and parseResponse
 *  return:
 *      TINY_SIP_OK or TINY_SIP_ERR + N
 */
int TinySIP::parseAllHeaders(char* s) {
  char* buffEnd = buff + buffLength - 1;

  // Locate headers and body
  bool crlf = false;
  char* colon = NULL;
  while (s<=buffEnd && s[0]!='\0') {

    // Check for end of headers section, start of body section

    if (*s=='\n' && *(s-1)=='\r' && *(s-2)=='\n' && (*(s-3)=='\r' || *(s-3)=='\0')) {
      // Two consecutive CRLF found -> body found or end of message
      crlf = true;
      *(s-3) = '\0';    // replace CR with NUL for previous header
      s++;
      break;
    }


    if (*(s-1)=='\n' && (*(s-2)=='\r' || *(s-2)=='\0')) {
      if (*s>=33 && *s<=126 && *s!=':') {     // RFC 2822

        //log_d("Header found: %c ", *s);

        // Header found
        *(s-2) = '\0';   // replace CR with NUL for previous header
        respHeaderName[respHeaderCnt++] = s;

        // Find end of header name, simultaneously turning it into lowercase
        do {
          *s = tolower(*s);
          s++;
        } while (s<=buffEnd && *s>=33 && *s<=126 && *s!=':');    // find end of field-name

        if (s>buffEnd) {   // error: empty header          // TODO: just ignore any such headers
          respHeaderCnt--;
          return TINY_SIP_ERR+3;
        }

        // Skip spaces and a colon (see RFC 3261, Section 7.3.1 Header Field Format)
        char* e = skipCharLiteral(s, TINY_SIP_HCOLON);
        if (e==NULL) {     // invalid header detected    // TODO: just ignore any such headers
          respHeaderCnt--;
          return TINY_SIP_ERR+4;
        }
        *s = '\0';
        s = e;
        //log_d("%s", respHeaderName[respHeaderCnt-1]);

        // Header value
        if (s<=buffEnd) {
          respHeaderValue[respHeaderCnt-1] = s;
        }
      }
    }
    s++;
  }
  if (!crlf) {
    // ERROR: message is probably concatenated - CRLF not encountered
    //log_d("ERROR: CRLF missing from SIP message")
    return TINY_SIP_ERR+5;
  }

  // Parse each header individually
  {
    bool updateRouteSet = (respType!=TINY_SIP_METHOD_REGISTER && respType!=TINY_SIP_METHOD_ACK) ? true : false;
    if (updateRouteSet) {
      // RFC 3261:
      //   "If no Record-Route header field is present in the  request, the route set MUST be set to the empty set"
      //   "If no Record-Route header field is present in  the response, the route set MUST be set to the empty set"
      //   "the UAC MUST NOT create a new route set  based on the presence or absence of a Record-Route header field in  any response to a REGISTER request"
      //   "Requests within a dialog MAY contain Record-Route and Contact header  fields.  However, these requests do not cause the dialog's route set  to be modified" (p. 72)
      //   "Target refresh requests only update the dialog's remote target URI,  and not the route set formed from the Record-Route" (p. 73)
      respRouteSet.clear(isResponse);        // TODO: this should be set and preserved per dialog
    }
    for (int i=0; i<respHeaderCnt; i++) {
      if (strcmp(respHeaderName[i],"record-route")) {
        // Not Record-Route header -> parse
        parseHeader(i);
      } else if (updateRouteSet) {
        // Record-Route header -> Parse Record-Route header only if not REGISTER method response
        // RFC 3261: "the UAC MUST NOT create a new route set  based on the presence or absence of a Record-Route header field in  any response to a REGISTER request"
        parseHeader(i);
      }
    }
  }

  // Body present?
  if (respContentLength>0) {
    log_v("message body found");

    // body found
    int len = strlen(s);
    if (len >= respContentLength) {
      // Shift body 1 byte to the left (replacing the \n character that is part of CRLF) - this is needed to terminate the message body with a NUL character without spoiling the following message
      for (char* p=s; p<s+respContentLength; p++) {
        *(p-1) = *p;
      }

      // The newly found message body
      respBody = s-1;
      respBody[respContentLength] = '\0';     // TODO: check if it still works with the SIP INVITEs

      // parse the body
      if (respContentType!=NULL && !strcasecmp(respContentType, "application/sdp")) {
        parseSdp(respBody);
      } else if (respContentType!=NULL && !strcasecmp(respContentType, "text/plain")) {
        // Do nothing: this is is probably a message
      } else {
        log_e("not parsing SDP: unknown contentType=%s", respContentType!=NULL ? respContentType : "NULL");
      }

      buffStart = s + respContentLength;
    } else {
      // ERROR: message is probably concatenated - message body is too short
      log_e("message body is too short: %d, expected %d", len, respContentLength);
      buffStart = s + len;
      return TINY_SIP_ERR+6;
    }
  } else {
    buffStart = s;
  }

  if (buffStart < buff+buffLength) {
    // Show leftover buffer
    log_d("*********************parseAllHeaders*********************");
    log_d("Buffer leftover: %d", buffLength-(buffStart-buff));
    log_d("%s", buffStart);
    log_d("*********************************************************");
  }

  // TODO: skip any extra spaces

  // Check if the end of buffer reached (this seems unnecessary)
  if (s>=buffEnd) {
    buffStart = buff + buffLength;
  }

  return TINY_SIP_OK;
}

/*
 *  Description:
 *      parse specific types of headers
 *  param:
 *      index of the header to parse
 *  return:
 *      start of the next header in buff
 */
void TinySIP::parseHeader(uint16_t param) {
  char c0 = respHeaderName[param][0];   // first letter of the long header name

  // Compact forms of the header
  char compact = 0;
  if (respHeaderName[param][1]=='\0') {
    // Single character header name -> compact form
    compact = c0;

    // Translate compact name to first letter
    switch(compact) {
    case 'i':
      c0 = 'c';
      break;    // Call-ID
    case 'm':
      c0 = 'c';
      break;    // Contact
    case 'l':
      c0 = 'c';
      break;    // Content-Length
    case 'e':
      c0 = 'c';
      break;    // Content-Encoding
    case 'k':
      c0 = 's';
      break;    // Supported
      // No need to change c0 for these (c0 is same as compact)
      //case 'c': c0 = 'c'; break;    // Content-Type
      //case 'f': c0 = 'f'; break;    // From
      //case 's': c0 = 's'; break;    // Subject
      //case 't': c0 = 't'; break;    // To
      //case 'v': c0 = 'v'; break;    // Via
    }
  }

  // Parse specific types of headers
  if (c0=='t') {
    if (compact=='t' || !strcmp(respHeaderName[param],"to")) {
      // Grammar:     (RFC 3261, p. 231)
      //    To            =  ( "To" / "t" ) HCOLON ( name-addr / addr-spec ) *( SEMI to-param )
      //    name-addr     =  [ display-name ] LAQUOT addr-spec RAQUOT
      //    addr-spec     =  SIP-URI / SIPS-URI / absoluteURI
      //    to-param      =  tag-param / generic-param
      //    tag-param     =  "tag" EQUAL token
      //    token         =  1*(alphanum / "-" / "." / "!" / "%" / "*" / "_" / "+" / "`" / "'" / "~" )

      // We are only interested in the tag-param

      freeNull((void **) &respToTagDyn);
      if (isResponse) {
        freeNull((void **) &remoteToFromDyn);
        remoteToFromDyn = strdup(respHeaderValue[param]);
      }

      parseContactParam(respHeaderValue[param], &respToDispName, &respToAddrSpec, &respToParams);

      // Find tag parameter value
      if (respToParams!=NULL) {
        retrieveGenericParam(respToParams, "tag", TINY_SIP_SEMI, &respToTagDyn);
      }
    }
  } else if (c0=='f') {
    if (compact=='f' || !strcmp(respHeaderName[param],"from")) {
      // We are only interested in the tag-param

      char* headerParams = NULL;
      respFromDispName = respFromAddrSpec = NULL;
      freeNull((void **) &respFromTagDyn);

      if (!isResponse) {
        freeNull((void **) &remoteToFromDyn);
        remoteToFromDyn = strdup(respHeaderValue[param]);
      }

      parseContactParam(respHeaderValue[param], &respFromDispName, &respFromAddrSpec, &headerParams);
      log_d("name = %s, uri = %s, params = %s",
            respFromDispName ? respFromDispName : "null",
            respFromAddrSpec ? respFromAddrSpec : "null",
            headerParams ? headerParams : "null");

      if (headerParams!=NULL) {
        retrieveGenericParam(headerParams, "tag", TINY_SIP_SEMI, &respFromTagDyn);
      }
    }
  } else if (c0=='p' || c0=='w') {
    if (!strcmp(respHeaderName[param],"proxy-authenticate") || !strcmp(respHeaderName[param],"www-authenticate")) {
      // Grammar:     (RFC 3261, p. 230, 232)
      //    Proxy-Authenticate  =  "Proxy-Authenticate" HCOLON challenge
      //    WWW-Authenticate    =  "WWW-Authenticate" HCOLON challenge
      //
      //    challenge           =  ("Digest" LWS digest-cln *(COMMA digest-cln)) / other-challenge
      //    other-challenge     =  auth-scheme LWS auth-param *(COMMA auth-param)
      //    auth-scheme         =  token
      //    auth-param          =  auth-param-name EQUAL ( token / quoted-string )
      //    auth-param-name     =  token
      //    digest-cln          =  realm / domain / nonce / opaque / stale / algorithm / qop-options / auth-param
      //    realm               =  "realm" EQUAL quoted-string
      //    domain              =  "domain" EQUAL LDQUOT URI *( 1*SP URI ) RDQUOT
      //    URI                 =  absoluteURI / abs-path
      //    nonce               =  "nonce" EQUAL quoted-string
      //    opaque              =  "opaque" EQUAL quoted-string
      //    stale               =  "stale" EQUAL ( "true" / "false" )
      //    algorithm           =  "algorithm" EQUAL ( "MD5" / "MD5-sess" / token )
      //    qop-options         =  "qop" EQUAL LDQUOT qop-value *("," qop-value) RDQUOT
      //    qop-value           =  "auth" / "auth-int" / token

      // Extract challenge parameters (destructive)
      digestRealm = digestDomain = digestNonce = digestCNonce = digestOpaque = digestStale = digestAlgorithm = digestQopOpt = digestQopPref = NULL;
      *digestResponse = '\0';     // empty string

      char* p;
      char* e = skipToken(respHeaderValue[param]);
      respChallenge = respHeaderValue[param];
      *e = '\0';
      log_d("Challenge: %s", respChallenge);
      if (!strcasecmp(respChallenge, "digest")) {
        p = skipLinearSpace(e+1);     // start of header digest-cln
        while (p!=NULL && *p!='\0') {
          e = skipToken(p);           // end of parameter name
          if (e==p+5) {
            if (!strncasecmp(p, "realm", e-p)) {
              digestRealm = parseQuotedStringValue(&e, TINY_SIP_COMMA);
              if (digestRealm!=NULL) {
                log_d("Realm: %s", digestRealm);
                p = e;    // past separator after quoted string end
              }
            } else if (!strncasecmp(p, "nonce", e-p)) {
              digestNonce = parseQuotedStringValue(&e, TINY_SIP_COMMA);
              if (digestNonce!=NULL) {
                log_d("Nonce: %s", digestNonce);
                p = e;    // past separator after quoted string end
              }
            } else if (!strncasecmp(p, "stale", e-p)) {
              // TODO
            }
          }
          if (e==p+6) {
            if (!strncasecmp(p, "domain", e-p)) {
              // TODO
            } else if (!strncasecmp(p, "opaque", e-p)) {
              digestOpaque = parseQuotedStringValue(&e, TINY_SIP_COMMA);
              if (digestOpaque!=NULL) {
                log_d("Opaque: %s", digestOpaque);
                p = e;    // past separator after quoted string end
              }
            }
          } else {
            if (!strncasecmp(p, "qop", e-p)) {
              digestQopOpt = parseQuotedStringValue(&e, TINY_SIP_COMMA);
              if (digestQopOpt!=NULL) {
                log_d("Qop-Options: %s", digestQopOpt);
                p = e;    // past separator after quoted string end
                // Find acceptable qop
                char* pp = digestQopOpt, *ee;
                while (pp!=NULL && *pp) {
                  ee = skipToken(pp);
                  if (!strncasecmp(pp, "auth", ee-pp) || !strncasecmp(pp, "auth-int", ee-pp)) {
                    *ee = '\0';
                    digestQopPref = pp;
                    log_d("Qop: %s", digestQopPref);
                    break;
                  }
                  pp = nextParameter(pp, TINY_SIP_COMMA);
                }
              }
            } else if (!strncasecmp(p, "algorithm", e-p)) {
              p = nextParameter(p, TINY_SIP_COMMA);
              digestAlgorithm = skipCharLiteral(e, TINY_SIP_EQUAL);
              char* ee = skipToken(digestAlgorithm);
              if (ee && *ee) {
                *ee++='\0';
              }
              if (digestAlgorithm!=NULL) {
                log_d("Algorithm: %s", digestAlgorithm);
              }
            }
          }
          if (p<e) {
            // Unknown parameter auth-param: ignore
            p = nextParameter(p, TINY_SIP_COMMA);
          }
        }
        log_d("Challenge parsed");
      } else {
        // Unknown auth-scheme: ignore completely
      }
    }
  } else if (c0=='c') {
    if (compact=='l' || !strcmp(respHeaderName[param],"content-length")) {
      respContentLength = 0;
      char* e = skipToken(respHeaderValue[param]);
      if (e>respHeaderValue[param]) {
        respContentLength = atoi(respHeaderValue[param]);
      }
    } else if (compact=='c' || !strcmp(respHeaderName[param],"content-type")) {
      respContentType = respHeaderValue[param];
    } else if (compact=='i' || !strcmp(respHeaderName[param],"call-id")) {
      // Grammar:
      //    Call-ID  =  ( "Call-ID" / "i" ) HCOLON callid
      //    callid   =  word [ "@" word ]      ; alphabet = alphanum / "@" / "%" / "-" / "_" / "." / "!" / "~" / "*" / "'" / '"' / "(" / ")" / "`" /  "+" / ":" / "?" / "/" / "(" / ")" / "[" / "]" / "<" / ">" / "\" / "{" / "}"
      respCallId = respHeaderValue[param];
    } else if (compact=='m' || !strcmp(respHeaderName[param],"contact")) {
      // Grammar:
      //    Contact           =  ("Contact" / "m" ) HCOLON  ( STAR / (contact-param *(COMMA contact-param)))

      // We are only interested in SIP addr-spec, other contacts are ignored

      freeNull((void **) &respContDispNameDyn);
      freeNull((void **) &respContAddrSpecDyn);
      char* respContDispName;
      char* respContAddrSpec;

      // Special case: single token (probably STAR)
      char* e = skipToken(respHeaderValue[param]);
      char* n = skipLinearSpace(e);
      if (*n=='\0') {
        if (e == respHeaderValue[param] + 1) {  // single character, probably STAR
          respContAddrSpecDyn = strdup(respHeaderValue[param]);
          return;
        }
      }

      // STAR ruled out
      char* p = respHeaderValue[param];
      while (*p!='\0') {
        // Parse each contact-param separately
        char* params;
        p = parseContactParam(p, &respContDispName, &respContAddrSpec, &params);
        if (respContAddrSpec!=NULL && !strncasecmp(respContAddrSpec, "sip:", 4)) {
          // SIP contact found -> ignore the rest in this header
          respContAddrSpecDyn = strdup(respContAddrSpec);
          if (respContDispName!=NULL) {
            respContDispNameDyn = strdup(respContDispName);
          }
          break;
        }
        if (*p && *p==',') {
          p = skipCharLiteral(p, TINY_SIP_COMMA);
        }
      }
    } else if (!strcmp(respHeaderName[param],"cseq")) {
      respCSeq = atoi(respHeaderValue[param]);
      char* p = skipToken(respHeaderValue[param]);
      if (*p) {
        p = skipLinearSpace(p);
        if (*p) {
          respCSeqMethod = p;
        }
      }
    }
  } else if (c0=='r') {
    if (!strcmp(respHeaderName[param],"record-route")) {
      // Record-Route

      // Grammar:
      //    Record-Route      =  "Record-Route" HCOLON rec-route *(COMMA rec-route)
      //    rec-route         =  name-addr *( SEMI rr-param )
      //    name-addr         =  [ display-name ] LAQUOT addr-spec RAQUOT               ; display-name is usually not present in Record-Route
      //    rr-param          =  generic-param

      char *rrDispName = NULL;
      char *rrAddrSpec = NULL;
      char *rrParams = NULL;

      char* p = respHeaderValue[param];
      while (*p!='\0') {
        // Parse each rec-route separately
        p = parseContactParam(p, &rrDispName, &rrAddrSpec, &rrParams);
        if (rrAddrSpec!=NULL) {
          // Add this address to route set
          respRouteSet.add(rrAddrSpec, rrParams);
        }
        if (*p && *p==',') {
          p = skipCharLiteral(p, TINY_SIP_COMMA);
        }
      }
    }
  }
//  else if (c0=='v') {
//    if (compact=='v' || !strcmp(respHeaderName[param],"via")) {
//      // Via
//    }
//  }
}

/*
 * Description:
 *      Parse IP address and port where to send audio.
 *      This assumes that there is only once audio media descpription. If there is more, they are ignored.
 * implicit return parameters:
 *      remoteAudioAddrDyn
 *      remoteAudioPort
 *      audioFormat
 * return
 *      TINY_SIP_OK / TINY_SIP_ERR
 */
int TinySIP::parseSdp(const char* body) {
  log_d("SDP parsing:");

  // Zero the return values
  freeNull((void **) &remoteAudioAddrDyn);
  remoteAudioPort = 0;

  // Parse SDP line by line. What we do here is:
  //   1) ensure SDP version is correct;
  //   2) find first audio media type description;
  //      a) figure out the connection description for that media type;
  //      b) choose compatible audio type payload.

  bool audioMediaTypeFound = false;
  bool audioConnectionFound = false;
  char *s=(char *)body;
  char *connAddrDyn = NULL;
  while (*s!='\0' && (!audioMediaTypeFound || !audioConnectionFound)) {
    char *e = s+strcspn(s, "\r\n");     // end of value / end of line
    if (*(s+1)=='=') {
      if (*s=='v') {

        // SDP version
        // Example: "v=0\r\n"

        log_d("- version: %c", *(s+2));
        if (strncmp(s+2, "0", e-s-2)) {
          log_d("- version error");      // DEBUG
          return TINY_SIP_ERR;      // incorrect version
        }

      } else if (*s=='c') {

        // SDP connection information
        // Example: "c=IN IP4 136.243.23.236\r\n"
        // This can be listed once on session level (above any "m=" media description, or once for each media description)

        if (audioMediaTypeFound) {
          audioConnectionFound = true;
          log_d("- audio conn data found");
        }
        char *ee = s+2+strcspn(s+2, " \r\n");           // end of nettype (by first space)
        if (!strncmp(s+2, "IN", ee-s-2) && *ee==' ') {
          char *eee = ee+1+strcspn(ee+1, " \r\n");      // end of addrtype (by second space)
          if (!strncmp(ee+1, "IP4", eee-ee-1) && *eee==' ') {
            ee = eee+1+strcspn(eee+1, " \r\n");         // end of address (by third space or end)
            freeNull((void **) &connAddrDyn);
            connAddrDyn = strndup(eee+1, ee-eee-1);
            log_d("- connaddr: %s", connAddrDyn);
          } else {
            log_d("- addrtype error");
          }
        } else {
          log_d("- nettype error");
        }

      } else if (*s=='m') {

        // SDP media stream name and transport address
        // Example: "m=audio 55690 RTP/AVP 103 102 104 125 109 3 8 0 9 101"

        if (audioMediaTypeFound) {
          log_d("- ignore media descr");
          break;                 // currenctly we are taking into account only the first audio description; TODO: fix it
        }
        char *ee = s+2+strcspn(s+2, " \r\n");           // end of media stream type (by first space)
        if (!strncmp(s+2, "audio", ee-s-2) && *ee==' ') {
          audioMediaTypeFound = true;
          remoteAudioPort = atoi(ee+1);
          log_d("- audio port: %d", remoteAudioPort);
          ee++;   // skip space
          char *eee = ee + strcspn(ee, " \r\n");        // end of port (by second space)
          if (*eee) {
            eee++;  // skip space
            eee += strcspn(eee, " \r\n");               // end of proto (by third space)
            while (*eee==' ') {
              eee++;    // skip space
              int af = isdigit(*eee) ? atoi(eee) : NULL_RTP_PAYLOAD;
              if (isAudioSupported(af)) {
                log_d("- pref audio payload: %d", af);
                this->audioFormat = af;
                break;
              }
              eee += strcspn(eee, " \r\n");             // proceed to the next payload type
            }
          }
        } else {
          log_d("- not audio");
        }

      } else if (*s == 'a') {
        char* ee = s + 2 + strcspn(s + 2, " \r\n");           // end of media stream type (by first space)
        if (!strncmp(s + 2, "mid", ee - s - 2) && *ee == ' ') {
          this->audioFormat = NULL_RTP_PAYLOAD;
          audioMediaTypeFound = true;
          break;
        }
      }
    } else {
      // incorrect field -> skip
      log_d("- incorrect field");
    }
    // Skip to the end of field
    s = e + strspn(e, " \r\n");
  }
  //
  if (connAddrDyn!=NULL) {
    // TODO: taking the last one for simplicity
    remoteAudioAddrDyn = connAddrDyn;
    log_d("- final connaddr: %s", remoteAudioAddrDyn);
  }
  return TINY_SIP_OK;
}

// p - points to name of parameter (first token character)
// return - pointer at NUL or the name of the next parameter
char* TinySIP::nextParameter(const char* p, char sep, const char* terminateAt) {
  // Grammar:
  //    generic-param  =  token [ EQUAL gen-value ]               ; separated by SEMI (in the "To" header)
  //    gen-value      =  token / host / quoted-string            ; host allows '[', ']', ':' in addition to token characters
  //    auth-param     =  token EQUAL ( token / quoted-string )   ; separated by COMMA

  p = skipToken(p);     // skip name of the parameter
  char* e = skipCharLiteral(p, TINY_SIP_EQUAL);
  if (e!=NULL) {
    // Parameter value
    p = e;
    if (*p=='"') {
      // Quoted string can contain escaped characters (need to be skipped correctly)
      p = quotedStringEnd(p+1);
      if (p!=NULL && *p=='"') {
        p++;
      }
    } else {
      // skip all possible characters in token or host
      p = skipAlphanumAndSpecials(p, "-.!%*_+`'~:[]");
      if (*p!='\0') {
        p = skipLinearSpace(p);
      }
    }
  }   // else -> no parameter value
  // Terminal characters
  if (strchr(terminateAt, *p)) {
    return (char*) p;
  }
  // Skip parameter separator
  p = skipCharLiteral(p, sep);       // this will return NULL if no separator found
  return (char*) p;
}

/*
 * Description:
 *      parse generic-param constuct, return output in a newly allocated C-string
 * Parameters:
 *      p - pointer to the string containing parameters
 *      parName - name of parameter to retrieve
 *      sep - SEMI or COMMA, separator between parameters
 *      val - actual result, pointer to a new string, if not NULL, the string will be freed first (DANGEROUS)
 * return:
 *      parameter found (bool)
 */
bool TinySIP::retrieveGenericParam(const char* p, const char* const parName, const char sep, char** val) {
  // Grammar:
  //    generic-param =  token [EQUAL gen-value]
  //    gen-value     =  token / host / quoted-string
  //    host          =                                           ; alphabet = alphanum / "-" / "." / ":" / "[" / "]"
  freeNull((void **) val);      // ***THIS IS CORRECT, DO NOT CHANGE***, `val` is already of type char**
  bool res = false;
  while (p!=NULL && *p!='\0') {
    char* e = skipToken(p);     // end of parameter name
    if (e==p) {
      break;
    }
    if (!strncasecmp(p, parName, e-p)) {
      // Correct parameter found? -> parse and terminate
      res = true;
      char *ee = skipCharLiteral(e, TINY_SIP_EQUAL);   // end of EQUAL / start of value
      if (ee!=NULL) {
        e = ee;           // proceed to start of value
        if (*e=='"') {
          // Quoted string
          ee = quotedStringEnd(e+1);             // find closing double quote
          char* tmp = strndup(e+1, ee-e-1);      // copy without the closing doublequote
          char* e = parseQuotedString(tmp);      // unescape (does some OVERHEAD)
          *val = strndup(tmp, e-1-tmp);
          free(tmp);
        } else {
          // Token or host (doesn't check validity of host)
          ee = skipAlphanumAndSpecials(e, "-.!%*_+`'~:[]");
          *val = strndup(e, ee-e);
        }
        break;
      }
    }
    // Proceed to next parameter
    p = nextParameter(p, sep);          // OVERHEAD: inefficient -> does some work again
  }
  return res;
}

/* Description:
 *      turn string method type into an integer constant, parse only "acceptable" (from header "Accept") methods
 *      otherwise return TINY_SIP_METHOD_UNKNOWN
 */
uint8_t TinySIP::methodType(const char* methd) {
  if (methd==NULL) {
    return TINY_SIP_METHOD_NONE;
  }
  if (!strcasecmp(methd, "INVITE")) {
    return TINY_SIP_METHOD_INVITE;
  }
  if (!strcasecmp(methd, "REGISTER")) {
    return TINY_SIP_METHOD_REGISTER;
  }
  if (!strcasecmp(methd, "MESSAGE")) {
    return TINY_SIP_METHOD_MESSAGE;
  }
  if (!strcasecmp(methd, "ACK")) {
    return TINY_SIP_METHOD_ACK;
  }
  if (!strcasecmp(methd, "BYE")) {
    return TINY_SIP_METHOD_BYE;
  }
  if (!strcasecmp(methd, "CANCEL")) {
    return TINY_SIP_METHOD_CANCEL;
  }
  log_d("ERROR: unknown method: %s", methd);
  return TINY_SIP_METHOD_UNKNOWN;
}

// Skip all "token" characters        (RFC 3261, p. 221)
// p - is not NULL
inline char* TinySIP::skipToken(const char* p) {
  return skipAlphanumAndSpecials(p, "-.!%*_+`'~");
}

/*
 * Description:
 *      This function is used to find display name, address (SIP/SIPS URI + other URI) and any contact params.
 *      Grammar is the same (or compatible) for Contact, To, From. Record-Route and Route.
 *      NOTE: string `p` is modified to terminate
 * Return parameters:
 *      pointers to beginnings of the appropriate srtings inside string `p`, all of them nul-terminated:
 *          disp-name   (if available)
 *          addr-spec   (available, unless something is wrong; if p points to "  *  " (STAR), this is "*")
 *          params      (if available; usually follows generic-param grammar)
 * Return
 *      pointer past the last character of contact-param
 */
char* TinySIP::parseContactParam(char* p, char** dispName, char** addrSpec, char** contactParams) {
  // Grammar:
  //    STAR / (( name-addr / addr-spec ) *( SEMI ( contact-params / to-param / from-param ))
  // Explanation:
  //    name-addr         =  [ display-name ] LAQUOT addr-spec RAQUOT               ; can start as a series of tokens, quoted string or LAQUOT
  //      display-name    =  *(token LWS) / quoted-string
  //    addr-spec         =  SIP-URI / SIPS-URI / absoluteURI                       ; can parse as a token
  //
  //    contact-param     =  (name-addr / addr-spec) *( SEMI contact-params )       ; contact + parameters
  //      contact-params  =  c-p-q / c-p-expires / generic-param                    ; one of three types of a contact parameter
  //    to-spec           =  ( name-addr / addr-spec ) *( SEMI to-param )
  //      to-param        =  tag-param / generic-param
  //    from-spec         =  ( name-addr / addr-spec ) *( SEMI from-param )
  //      from-param      =  tag-param / generic-param
  //   Record-Route       =  "Record-Route" HCOLON rec-route *(COMMA rec-route)
  //    rec-route         =  name-addr *( SEMI generic-param )                      ; Record-Route and Route addresses always have angle quotes
  //   Route              =  "Route" HCOLON route-param *(COMMA route-param)
  //    route-param       =  name-addr *( SEMI generic-param )

  *dispName = *addrSpec = *contactParams = NULL;

  char* e;
  bool nameAddr = false;
  if (*p=='"' || *p=='<')  {
    nameAddr = true;
    if (*p=='"') {
      // Quoted string for name
      *dispName = skipCharLiteral(p, TINY_SIP_LDQUOT);
      p = parseQuotedString(*dispName);     // past closing DQUOT
      if (p==NULL) {
        return NULL;
      }
      p = skipLinearSpace(p);               // make sure p points to "<"
    } // else no display name, p points to "<" already -> do nothing
  } else {
    e = skipToken(p);
    if (*e==':') {
      // only addr-spec, no name-addr
      nameAddr = false;        // URI detected (token followed by COLON)
      *addrSpec = p;
      char* params = p + strcspn(p,";");
      if (*params==';') {
        // Contact parameters found
        *contactParams = skipCharLiteral(params, TINY_SIP_SEMI);
        *params = '\0';        // terminate addrSpec
      } else {
        // No contact parameters -> just skip all possible addr-spec characters (except ';' - it's not there)
        p = skipAlphanumAndSpecials(p, "-_.!~*'()%&=+$,?/:@[]&");
      }
    } else {
      // name-addr (with a list of tokens as disp-name)
      *dispName = p;
      nameAddr = true;
      if (e==p) {
        return NULL;  // unexpected character encountered
      }
      char *elast = e;                  // end of token
      char *lws = skipLinearSpace(e);   // end of space after token
      while (p!=lws) {
        // parse next token
        p = lws;                        // supposed start of next of token
        elast = e;                      // end of last token
        e = skipToken(p);
        lws = skipLinearSpace(e);
      }
      if (p==NULL || *p!='<') {
        return NULL;
      }
      *elast = '\0';                    // terminate display name (character after last token)
      normalizeLinearSpaces(*dispName);
    }
  }

  if (nameAddr) {
    //log_d("paraseContactParam: nameAddr");
    // p points to "<" of name-addr
    if (*p!='<') {
      return NULL;  // assert
    }
    //log_d("paraseContactParam: nameAddr OK");
    *addrSpec = skipCharLiteral(p, TINY_SIP_LAQUOT);
    p += strcspn(p, ">");
    //log_d("paraseContactParam: nameAddr %s", p);
    if (*p!='>') {
      return NULL;  // closing angel quote no found
    }
    *contactParams = skipCharLiteral(p, TINY_SIP_RAQUOT);
    *p = '\0';    // terminate addrSpec
    if (**contactParams==';') {
      *contactParams = skipCharLiteral(*contactParams, TINY_SIP_SEMI);
    } else {
      *contactParams = NULL;      // parameters not found -> ignore
    }
    p++;
  }

  // Skip contactParams to the end
  if (*contactParams != NULL) {
    //log_d("paraseContactParam: params");
    p = *contactParams;
    //log_d("p = %s", p);
    while (p!=NULL && p!='\0') {
      char* pp = nextParameter(p, TINY_SIP_SEMI, ",");      // terminate at NUL or COMMA  (Contact grammar at page 228, RFC 3261, Route grammaer at page 231)
      if (pp==p) {
        break;  // cannot parse -> means the end reached or incorrect value
      }
      p = pp;
      //log_d("p = %s", p);
    }
  }

  //log_d("paraseContactParam: end");
  return p;
}

/*
 *  Description:
 *      parse addr-spec address (SIP/SIPS or other URI), e.g. "sip:username@85.17.186.20:5060;tag=xyz?id=1234&true"
 *  Input parameters:
 *      p - pointer for string with addr-spec address; THIS STING WILL BE MODIFIED (NUL characters added to terminate substrings)
 *  Output parameters:
 *      scheme    - pointer to scheme part of URI terminated with NUL character (e.g. "sip", "sips")
 *      hostport  - pointer to hostport part of URI terminated with NUL character  (e.g. "85.17.186.20:5060")
 *      userinfo  - pointer to userinfo part of URI terminated with NUL character (e.g. "username")
 *      uriParams - pointer to URI parameters part of addr-spec terminated with NUL character (e.g. "tag=xyz")
 *      headers   - pointer to headers part of URI terminated with NUL character (e.g. "id=1234&true")
 *  Return:
 *      pointer to a character past address (unmodified), or NULL if there is major error
 */
char* TinySIP::parseAddrSpec(char* p, char** scheme, char** hostport, char** userinfo, char** uriParams, char** headers) {
  // Grammar:
  //      addr-spec        =  SIP-URI / SIPS-URI / absoluteURI
  // Components:
  //      SIP-URI          =  "sip:" [ userinfo ] hostport uri-parameters [ headers ]             ; only userinfo contains "@", first ";" is in uri-parameters
  //      SIPS-URI         =  "sips:" [ userinfo ] hostport uri-parameters [ headers ]
  //        userinfo       =  ( user / telephone-subscriber ) [ ":" password ] "@"                ; alphabet = alphanum / "%" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "&" / "=" / "+" / "$" / "," / ";" / "?" / "/" / ":" / "@"
  //        user           =  1*( unreserved / escaped / user-unreserved )                        ; alphabet = alphanum / "%" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "&" / "=" / "+" / "$" / "," / ";" / "?" / "/"
  //        unreserved     =  alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")"
  //        user-unreserved=  "&" / "=" / "+" / "$" / "," / ";" / "?" / "/"
  //        escaped        =  "%" HEXDIG HEXDIG                                                   ; alphabet = "%" / 0..9 / A..Z / a..z
  //        password       =  *( unreserved / escaped / "&" / "=" / "+" / "$" / "," )             ; alphabet = alphanum / "%" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "&" / "=" / "+" / "$" / ","
  //        hostport       =  host [ ":" port ]                                                   ; alphabet = alphanum / "-" / "." / ":" / "[" / "]"
  //        host           =  hostname / IPv4address / IPv6reference                              ; alphabet = alphanum / "-" / "." / ":" / "[" / "]"
  //        hostname       =  *( domainlabel "." ) toplabel [ "." ]                               ; alphabet = alphanum / "-" / "."
  //        domainlabel    =  alphanum / alphanum *( alphanum / "-" ) alphanum                    ; alphabet = alphanum / "-"
  //        toplabel       =  ALPHA / ALPHA *( alphanum / "-" ) alphanum                          ; alphabet = alphanum / "-"
  //        IPv4address    =  1*3DIGIT "." 1*3DIGIT "." 1*3DIGIT "." 1*3DIGIT                     ; alphabet = 0..9 / "."
  //        IPv6reference  =  "[" IPv6address "]"                                                 ; alphabet = 0..9 / A..Z / a..z / ":" / "." / "[" / "]"
  //        IPv6address    =  hexpart [ ":" IPv4address ]                                         ; alphabet = 0..9 / A..Z / a..z / ":" / "."
  //        hexpart        =  hexseq / hexseq "::" [ hexseq ] / "::" [ hexseq ]                   ; alphabet = 0..9 / A..Z / a..z / ":"
  //        hexseq         =  hex4 *( ":" hex4)                                                   ; alphabet = 0..9 / A..Z / a..z / ":"
  //        hex4           =  1*4HEXDIG                                                           ; alphabet = 0..9 / A..Z / a..z
  //        port           =  1*DIGIT                                                             ; alphabet = 0..9
  //      absoluteURI      =  scheme ":" ( hier-part / opaque-part )                              ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / "," / ";" / "/" / "?"
  //        scheme         =  ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
  //        hier-part      =  ( net-path / abs-path ) [ "?" query ]                               ; "?" only appears before query; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / "," / ";" / "/" / "?"
  //        net-path       =  "//" authority [ abs-path ]                                         ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / "," / ";" "/"
  //        abs-path       =  "/" path-segments                                                   ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / "," / ";" "/"
  //        path-segments  =  segment *( "/" segment )                                            ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / "," / ";" "/"
  //        segment        =  *pchar *( ";" param )                                               ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / "," / ";"
  //        param          =  *pchar                                                              ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / ","
  //        pchar          =  unreserved / escaped / ":" / "@" / "&" / "=" / "+" / "$" / ","      ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ":" / "@" / "&" / "=" / "+" / "$" / ","
  //        authority      =  srvr / reg-name                                                     ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "," / ";" / ":" / "%" / "$" / "&" / "=" / "+" / "@" / "?" / "/" / "[" / "]"
  //        srvr           =                                                                      ; alphabet = alphanum / "%" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "&" / "=" / "+" / "$" / "," / ";" / "?" / "/" / ":" / "@" / "[" / "]"
  //        reg-name       =                                                                      ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / "$" / "," / ";" / ":" / "@" / "&" / "=" / "+"
  //        query          = *( reserved / unreserved / escaped )                                 ; alphaber = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / ";" / "/" / "?" / ":" / "@" / "&" / "=" / "+" / "$" / ","
  //   telephone-subscriber=                  ; RFC 2806
  //          Example:    sip:+358-555-1234567;postd=pp22@foo.com;user=phone        ; this is equivalent to userinfo = "+358-555-1234567;postd=pp22@"
  //      uri-parameters   =  *( ";" uri-parameter)                                               ; alphabet = alphanum / '%' / "[" / "]" / "/" / ":" / "&" / "+" / "$" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "`" / "=" / ";"
  //        uri-parameter  =  transport-param / user-param / method-param / ttl-param / maddr-param / lr-param / other-param
  //                                                                                              ; alphabet = alphanum / '%' / "[" / "]" / "/" / ":" / "&" / "+" / "$" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "`" / "="
  //        transport-param=  "transport=" ( "udp" / "tcp" / "sctp" / "tls" / other-transport)
  //        other-transport=  token
  //        user-param     =  "user=" ( "phone" / "ip" / other-user)                              ; alphabet = alphanum / "-" / "." / "!" / "%" / "*" / "_" / "+" / "`" / "'" / "~" / "`" / "="
  //        other-user     =  token                                                               ; alphabet = alphanum / "-" / "." / "!" / "%" / "*" / "_" / "+" / "`" / "'" / "~"
  //        method-param   =  "method=" Method
  //        ttl-param      =  "ttl=" ttl
  //        maddr-param    =  "maddr=" host
  //        lr-param       =  "lr"
  //        other-param    =  pname [ "=" pvalue ]                                                ; alphabet = alphanum / '%' / "[" / "]" / "/" / ":" / "&" / "+" / "$" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")"
  //        pname          =  1*paramchar
  //        pvalue         =  1*paramchar
  //        paramchar      =  param-unreserved / unreserved / escaped                             ; alphabet = alphanum / '%' / "[" / "]" / "/" / ":" / "&" / "+" / "$" / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")"
  //        param-unreserved= "[" / "]" / "/" / ":" / "&" / "+" / "$"
  //      headers          =  "?" header *( "&" header )                                          ; does not contain "@"; ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / "[" / "]" / "/" / "?" / ":" / "+" / "$" / "?" / "&" / "="
  //        header         =  hname "=" hvalue                                                    ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / "[" / "]" / "/" / "?" / ":" / "+" / "$" / "=" / "="
  //        hname          =  1*( hnv-unreserved / unreserved / escaped )                         ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / "[" / "]" / "/" / "?" / ":" / "+" / "$"
  //        hvalue         =  *( hnv-unreserved / unreserved / escaped )                          ; alphabet = alphanum / "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")" / "%" / "[" / "]" / "/" / "?" / ":" / "+" / "$"
  //        hnv-unreserved =  "[" / "]" / "/" / "?" / ":" / "+" / "$"

  *scheme = *hostport = *userinfo = *uriParams = *headers = NULL;

  // Find scheme
  char* e = p + strcspn(p, ":");
  if (*e=='\0') {
    return NULL;  // scheme not found
  }
  *scheme = p;
  *e = '\0';
  p = e+1;

  if (strcasecmp(*scheme, "sip") || strcasecmp(*scheme, "sips")) {
    // SIP / SIPS URI -> continue parsing
    // Find userinfo
    e = p + strcspn(p, "@");
    if (*e=='@') {
      // userinfo found
      *userinfo = p;
      *e = '\0';
      p = e+1;
    }

    // Hostport here
    *hostport = p;

    // Find uri-parameters
    e = p + strcspn(p, ";");
    if (*e==';') {
      // uri-parameters found
      *e = '\0';    // terminate hostport
      *uriParams = p = e+1;
      p = e+1;
    }

    // Find headers
    e = p + strcspn(p, "?");
    if (*e=='?') {
      // headers found
      *e = '\0';    // terminate hostport or uri-parameters (if uri-parameters are empty)
      *headers = p = e+1;
    }

    // Skip to the end: whatever we have here (hostport, uri-parameters or headers), we skip entire alphabet for simplificy
    // Can be improved to parse correctly (LOW PRIO TODO)
    // NOTE: SIP/SIPS URI cannot contain linear space, which is helpful
    while (*p!='\0' &&  ((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || (*p>='A' && *p<='Z') || strchr("-_.!~*';()%[]/?:+$&=`", *p)!=NULL)) {
      p++;
    }

  } else {
    // absoluteURI    -> return only scheme and the rest as hostport
    *hostport = p;

    // Assumption: absoluteURI appear only as name-addr (inside "<" and ">") or separated by spaces, so we can skip the entire alphaber of absoluteURI;
    // Can be improved to parse absoluteUri correctly (LOW PRIO TODO)
    // NOTE: absoluteUri cannot contain linear space, which is helpful
    p = skipAlphanumAndSpecials(p, "-_.!~*'()%:@&=+$,;/?");
  }
  return p;
}


// Replace all LWS grammar with a single space withing a string
void TinySIP::normalizeLinearSpaces(char* p) {
  // Detect whitespace (while determnining length)
  int l = strcspn(p, " \t\r\n");
  if (*(p+l)=='\0') {
    return;  // no whitespace
  }
  // Normilize whitespaces
  l += strcspn(p+l, "");           // find index of NUL (end of the string)
  char* b = (char *) malloc((l+1)*sizeof(char));
  char* bi = b;
  char* pi = p;
  while (*pi) {
    if (*pi==' ' || *pi=='\t' || *pi=='\r' || *pi=='\n') {
      if (bi==b || *(bi-1)!=' ') {
        *bi = ' ';
        bi++;
      }
    } else {
      *bi = *pi;
      bi++;
    }
    pi++;
  }
  *bi = '\0';
  strcpy(p, b);
  free(b);
}

// Find end of quoted string
// p - points past the opening double quote
// return - points to closing double quote
char* TinySIP::quotedStringEnd(const char* p) {
  // Many characters can be escaped, even those, that do not require it (RFC 3261, p. 222)
  // Grammar:
  //    quoted-string  =  SWS DQUOTE *(qdtext / quoted-pair ) DQUOTE
  //    qdtext         =  LWS / %x21 / %x23-5B / %x5D-7E / UTF8-NONASCII
  //    quoted-pair    =  "\" (%x00-09 / %x0B-0C / %x0E-7F)               ; any character 0..127 can be escaped except CR and LF
  bool esc = false;
  while (*p!='\0') {
    if (esc) {
      esc = false;                  // ignore escaped characters
    } else if (*p=='\\') {
      esc = true;
    } else if (*p=='"') {
      break;  // unescaped double quote found
    }
    p++;
  }
  return (char *) p;
}

// Similar to quotedStringEnd, but unescapes and terminates the string (see above for grammar)  - DESTRUCTIVE
// Parameters:
//      p - points past the opening double quote
// return - points past closing double quote (or where it used to be)
char* TinySIP::parseQuotedString(char* p) {
  // First -> find and mark the end of the quoted string, count escaped characters
  char* e = p;
  bool esc = false;
  uint16_t escCnt = 0;
  while (*e!='\0') {
    if (esc) {
      esc = false;                  // ignore escaped characters
    } else if (*e=='\\') {
      esc = true;
      escCnt++;
    } else if (*e=='"' || *e=='\0') {
      break;  // unescaped double quote found
    }
    e++;
  }
  *e = '\0';

  // Unescaping
  if (escCnt) {
    // Most quoted strings will not have any escaped characters
    char* bff = (char *) malloc((e-p)*sizeof(char));      // TODO: escaped cnt can be deducted
    char* bbff = bff;
    char* q = p;
    while (q<e) {
      if (esc || *q!='\\') {
        esc = false;
        *(bff++) = *q;
      } else {    // *q == '\\'
        esc = true;
      }
      q++;
    }
    strncpy(p, bbff, bff-bbff);
    free(bbff);
    p[bff-bbff] = '\0';
  }

  return e+1;   // past closing double quote
}


/* Description:
 *     skip linear space according to SWS grammar (RFC 3261, p. 220)
 * Parameter:
 *     p - C-string (must not be null)
 * Return:
 *     pointer past proper linear space; p - if no valid linear space was found
 */
char* TinySIP::skipLinearSpace(const char* p) {
  // Grammar:
  // ; RFC 3261, p. 220
  //     SWS  =   [LWS]                   ; sep whitespace  (optional linear whitespace)
  //     LWS  =   [*WSP CRLF] 1*WSP       ; linear whitespace, folding allowed
  // ; RC 2234
  //     WSP  =   SP / HTAB
  const char* wsp = p + strspn(p, " \t");
  if (wsp[0]=='\r') {
    if (wsp[1]=='\n') {
      if (wsp[2]==' ' || wsp[2]=='\t') {
        return (char *) (wsp + 2 + strspn(wsp + 2, " \t"));
      }
    }
  }
  return (char*) wsp;
}

/*
 * Description:
 *     Skip one character literal (one character) together with the optional whitespaces on each side.
 * Grammar (RFC 3261, p. 221):
 *       "When tokens are used or separators are used between elements,
 *        whitespace is often allowed before or after these characters"
 *     STAR    =  SWS "*" SWS             ; asterisk
 *     SLASH   =  SWS "/" SWS             ; slash
 *     EQUAL   =  SWS "=" SWS             ; equal
 *     LPAREN  =  SWS "(" SWS             ; left parenthesis
 *     RPAREN  =  SWS ")" SWS             ; right parenthesis
 *     RAQUOT  =  ">" SWS                 ; right angle quote
 *     LAQUOT  =  SWS "<"                 ; left angle quote
 *     COMMA   =  SWS "," SWS             ; comma
 *     SEMI    =  SWS ";" SWS             ; semicolon
 *     COLON   =  SWS ":" SWS             ; colon
 *     HCOLON  =  *WSP ":" SWS            ; colon with no folding on the left allowed
 *     LDQUOT  =  SWS DQUOTE              ; open double quotation mark
 *     RDQUOT  =  DQUOTE SWS              ; close double quotation mark
 *
 *     SWS     =  [[*WSP CRLF] 1*WSP]     ;
 *     WSP     =   SP / HTAB
 * Parameters:
 *     c  - character literal (RFC 3261, p. 221), like TINY_SIP_STAR (*), TINY_SIP_LAQUOT (<), TINY_SIP_RAQUOT (>), TINY_SIP_COLON (:), TINY_SIP_HCOLON (:)
 */
char* TinySIP::skipCharLiteral(const char* p, char c) {
  if (p!=NULL && *p) {    // don't go past NUL character

    // Skip linear space on the left side of the character

    if (c==TINY_SIP_HCOLON) {
      p += strspn(p, " \t");  // in the case of HCOLON folding (CRLF + WSP) is not allowed on the left side of the colon character
    } else if (c!=TINY_SIP_RAQUOT && c!=TINY_SIP_RDQUOT) {
      p = skipLinearSpace(p);
    }
    if (p==NULL || *p=='\0') {
      return (char *) p;
    }

    // Ensure the right character is present

    if (c==TINY_SIP_LDQUOT || c==TINY_SIP_RDQUOT) {
      if (*p==TINY_SIP_DQUOT) {
        p++;
      } else {
        return NULL;  // character not found
      }
    } else if (*p==c) {
      p++;
    } else {
      return NULL;          // character not found
    }

    // Skip linear space on the right side of the character

    if (c!=TINY_SIP_LAQUOT && c!=TINY_SIP_LDQUOT) {
      p = skipLinearSpace(p);
    }
  }
  return (char *) p;
}

char* TinySIP::skipAlphanumAndSpecials(const char* p, const char* const specials) {
  while (*p!='\0' && ((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || (*p>='A' && *p<='Z') || strchr(specials, *p)!=NULL)) {
    p++;
  }
  return (char*) p;
}

// Routine to parse parameter with quoted string value
// Grammar:
//    "parameter" EQUAL quoted-string ( COMMA / SEMI )
// Parameters:
//    p - points past parameter name token; this value is modified to point past separator or to end of string (NUL character)    - OUTPUT
//    sep - TINY_SIP_COMMA or TINY_SIP_SEMI
// return - pointer to quoted string value (deescaped), the string is necessarily terminated by NUL character
char* TinySIP::parseQuotedStringValue(char** p, char sep) {
  char* e = skipCharLiteral(*p, TINY_SIP_EQUAL);
  if (e==NULL) {
    return NULL;  // incorrect EQUAL separator
  }
  if (*e!='"') {
    return NULL;  // DQUOTE not found
  }
  char* ret = ++e;
  e = parseQuotedString(ret);
  if (e==NULL) {
    return NULL;  // incorrect quoted string
  }
  *p = skipCharLiteral(e, TINY_SIP_COMMA);  // next parameter
  return ret;
}

// Header Via routine
void TinySIP::sendHeaderVia(Connection& tcp, String& thisIp, uint16_t port, const char* branch) {
  if(UDP_SIP) {
    TCP(tcp, "Via: SIP/2.0/UDP ");
  } else {
    TCP(tcp, "Via: SIP/2.0/TCP ");
  }
  TCP(tcp, thisIp.c_str());
  TCP(tcp, ":");
  TCP_PRINTF(tcp, "%d", port);
  TCP(tcp, ";rport;branch=");
  TCP(tcp, branch);
  TCP(tcp, ";alias\r\n");
}

/*
 * Description:
 *      copy Via headers from the received request
 */
void TinySIP::sendHeadersVia(Connection& tcp) {
  for (uint16_t i=0; i<respHeaderCnt; i++) {
    if (respHeaderName[i][0]=='v' && (!strcmp(respHeaderName[i],"via") || !strcmp(respHeaderName[i],"v"))) {
      TCP(tcp, "Via: ");
      TCP(tcp, respHeaderValue[i]);
      TCP(tcp, "\r\n");
    }
  }
}

/* Description:
 *     Send entire route set.
 *     If client (UAC) -> send the learned record route in reverse order.
 *     If server (UAS) -> effectively, copy all Record-Route headers from request.
 */
void TinySIP::sendRouteSetHeaders(Connection& tcp, bool isClient) {
  if (respRouteSet.size() <= 0) {
    return;
  }
  if (strstr(respRouteSet[0], ";lr") == NULL && strstr(respRouteSet[0], ";LR") == NULL) {       // NOTE: this is not a strict check (it can fail if there is parameter "lright", for example); use AddrSpec class in the future
    // Loose routing parameter absent -> need to change response URI for outdated strict routing
    log_d("ERROR: lr-param absent, TinySIP doesn't implement strict routing");
  }
  for (int i=0; i<respRouteSet.size(); i++) {
    TCP_PRINTF(tcp, "%sRoute: <%s>\r\n", !isClient ? "Record-" : "", respRouteSet[i]);  // route set gets reversed internally
  }
}

// Header To or From with local credentials
void TinySIP::sendHeaderToFromLocal(Connection& tcp, char TF, const Dialog* diag) {
  if (!diag) {
    TCP(tcp, TF=='T' ? "To: \"" : "From: \"");
    TCP(tcp, localNameDyn);    // display name
    TCP(tcp, "\" <");
    TCP(tcp, localUriDyn);
    TCP(tcp, ">;tag=");
    TCP(tcp, localTag);
    TCP(tcp, "\r\n");
  } else {
    TCP_PRINTF(tcp, "%s: \"%s\" <%s>;tag=%s\r\n",
               TF=='T' ? "To" : "From",
               diag->localNameDyn ? diag->localNameDyn : "null",
               diag->localUriDyn ? diag->localUriDyn : "null",
               diag->localTagDyn ? diag->localTagDyn : "null");
  }
}

// Headers To and From for the REGISTER request
void TinySIP::sendHeadersToFrom(Connection& tcp, const Dialog* diag) {
  if (!diag) {
    // REGISTER request is outside of dialogs, therefore it doesn't send To tag.
    // RFC 3261: "A request outside of a dialog MUST NOT contain a To tag; the tag in
    //            the To field of a request identifies the peer of the dialog."
    TCP(tcp, "To: \"");
    TCP(tcp, localNameDyn);    // display name
    TCP(tcp, "\" <");
    TCP(tcp, localUriDyn);
    TCP(tcp, ">\r\n");
    sendHeaderToFromLocal(tcp, 'F');
  } else {
    // Caller: To == remote, From == local
    // Callee: To == local,  From == remote
    TCP_PRINTF(tcp, "%s: \"%s\" <%s>;tag=%s\r\n",
               diag->caller ? "From" : "To",
               diag->localNameDyn ? diag->localNameDyn : "null",
               diag->localUriDyn ? diag->localUriDyn : "null",
               diag->localTagDyn ? diag->localTagDyn : "null");
    TCP_PRINTF(tcp, "%s: \"%s\" <%s>;tag=%s\r\n",
               diag->caller ? "To" : "From",
               diag->remoteNameDyn ? diag->remoteNameDyn : "null",
               diag->remoteUriDyn ? diag->remoteUriDyn : "null",
               diag->remoteTagDyn ? diag->remoteTagDyn : "null");
  }
}

void TinySIP::sendByeHeadersToFrom(Connection& tcp, const Dialog* diag) {

  TCP_PRINTF(tcp, "%s: \"%s\" <%s>;tag=%s\r\n", "From",
             diag->localNameDyn ? diag->localNameDyn : "null",
             diag->localUriDyn ? diag->localUriDyn : "null",
             diag->localTagDyn ? diag->localTagDyn : "null");
  TCP_PRINTF(tcp, "%s: \"%s\" <%s>;tag=%s\r\n", "To",
             diag->remoteNameDyn ? diag->remoteNameDyn : "null",
             diag->remoteUriDyn ? diag->remoteUriDyn : "null",
             diag->remoteTagDyn ? diag->remoteTagDyn : "null");

}

// Header Allow routine
void TinySIP::sendHeaderAllow(Connection& tcp) {
  TCP(tcp, "Allow: INVITE, ACK, BYE, CANCEL\r\n");          // "A UA that supports INVITE MUST also support ACK, CANCEL and / BYE" (RFC 3261, p. 78)
}

void TinySIP::sendHeaderToFromRemote(Connection& tcp, char TF, bool mirror, const char* toUri, const char* toTag) {
  TCP(tcp, TF=='T' ? "To: " : "From: ");
  if (mirror) {
    // Only duplicate parameters from response for ACK
    TCP(tcp, remoteToFromDyn);    // "To / header field in the ACK MUST equal the To header field in the / response being acknowledged" (RFC 3162, p. 129)
  } else if (toUri!=NULL) {
    TCP(tcp, "<");
    TCP(tcp, toUri);
    TCP(tcp, ">");
    if (toTag!=NULL) {
      TCP(tcp, ";tag=");
      TCP(tcp, toTag);
    }
  }
  TCP(tcp, "\r\n");
}

/*
 *  Description:
 *      send local call id provided in parameter; if parameter is null - send remote call-id
 */
void TinySIP::sendHeaderCallId(Connection& tcp, char* callid) {
  TCP(tcp, "Call-ID: ");
  TCP(tcp, callid ? callid : respCallId);
  TCP(tcp, "\r\n");
}

void TinySIP::sendHeaderExpires(Connection& tcp, uint32_t seconds) {
  TCP(tcp, "Expires: ");
  TCP_PRINTF(tcp, "%d", seconds);
  TCP(tcp, "\r\n");
}

/*
 *  Description:
 *      send local CSeq and method name; if CSeq parameter is zero - mirrow remote header
 */
void TinySIP::sendHeaderCSeq(Connection& tcp, uint16_t seq, const char* methd) {
  TCP(tcp, "CSeq: ");
  TCP_PRINTF(tcp, "%d", seq ? seq : respCSeq);
  TCP(tcp, " ");
  TCP(tcp, methd ? methd : respCSeqMethod);
  TCP(tcp, "\r\n");
}

void TinySIP::sendHeaderMaxForwards(Connection& tcp, uint8_t n) {
  TCP(tcp, "Max-Forwards: ");
  TCP_PRINTF(tcp, "%d", n);
  TCP(tcp, "\r\n");
}

void TinySIP::sendHeaderUserAgent(Connection& tcp) {
  TCP(tcp, "User-Agent: tinySIP/0.6.0alpha\r\n");
}

void TinySIP::sendHeaderAuthorization(Connection& tcp, const char* URI) {
  if ((respCode==UNAUTHORIZED_401 || respCode==PROXY_AUTHENTICATION_REQUIRED_407 || respCode == REQUEST_PENDING) &&
      digestResponse!=NULL && digestResponse[0]!='\0'
     ) {
    if (respCode==UNAUTHORIZED_401) {
      TCP(tcp, "Authorization: Digest");
    } else {
      TCP(tcp, "Proxy-Authorization: Digest");
    }

    // username
    TCP(tcp, " username=\"");
    if (localUserDyn!=NULL && *localUserDyn) {
      TCP(tcp, localUserDyn);
    } else {
      TCP(tcp, "anonymous");  // TODO: how does this work?
    }
    TCP(tcp, "\"");

    // realm
    if (digestRealm!=NULL && *digestRealm) {
      TCP(tcp, ", realm=\"");
      TCP(tcp, digestRealm);
      TCP(tcp, "\"");
    }

    // nonce
    if (digestNonce!=NULL && *digestNonce) {
      TCP(tcp, ", nonce=\"");
      TCP(tcp, digestNonce);
      TCP(tcp, "\"");
    }

    // opaque: should be returned  by the client unchanged (RFC 2617)
    if (digestOpaque!=NULL && *digestOpaque) {
      TCP(tcp, ", opaque=\"");
      TCP(tcp, digestOpaque);
      TCP(tcp, "\"");
    }

    if (digestQopPref!=NULL) {
      // New line
      // RFC 2617: "cnonce", and "nonce-count"  directives MUST be present IFF qop directive was sent
      // RFC 2617: The "response-auth", "cnonce", and "nonce-count"  directives MUST BE present if "qop=auth" or "qop=auth-int" is  specified. (TODO: response-auth)

      // qop
      log_d("\r\n ++");       // this will show up only in the logs
      TCP(tcp, ", qop=\"");
      TCP(tcp, digestQopPref);
      TCP(tcp, "\"");

      // nonce-count
      char nonceCountStr[9];
      sprintf(nonceCountStr, "%08x", nonceCount);
      TCP(tcp, ", nc=\"");
      TCP(tcp, nonceCountStr);
      TCP(tcp, "\"");

      // cnonce
      TCP(tcp, ", cnonce=\"");
      TCP(tcp, cnonce);
      TCP(tcp, "\"");
    }

    // Last line
    // URI
    log_d("\r\n ++");        // this will show up only in the logs
    TCP(tcp, ", uri=\"");
    TCP(tcp, URI);
    TCP(tcp, "\"");

    // response
    if (digestResponse!=NULL && *digestResponse) {
      TCP(tcp, ", response=\"");
      TCP(tcp, digestResponse);
      TCP(tcp, "\"");
    }

    // End
    TCP(tcp, "\r\n");
  }
}

void TinySIP::sendHeaderContact(Connection& tcp) {
  // TODO: if our IP-address changes, need to send re-INVITE within a dialog
  TCP_PRINTF(tcp, "Contact: <sip:%d@%s:%d;transport=%s;ob>;+sip.instance=\"<" TINYSIP_URN_UUID_PREFIX "%s>\"\r\n",
             phoneNumber, thisIP.c_str(), tcp.localPort(), (UDP_SIP ? "udp" : "tcp"), this->macHex);
}

void TinySIP::sendBodyHeaders(Connection& tcp, int len, const char* type) {
  if (len>0 && type!=NULL && *type) {
    TCP(tcp, "Content-Type: ");
    TCP(tcp, type);
    TCP(tcp, "\r\n");
  }
  TCP(tcp, "Content-Length: ");
  TCP_PRINTF(tcp, "%d", len);
  TCP(tcp, "\r\n\r\n");
}

void TinySIP::newBranch(char* branch) {
  sprintf(branch, TINYSIP_BRANCH_PREFIX);
  Random.randChars((char *)(branch+BRANCH_CONSTANT_LEN), BRANCH_VARIABLE_LEN);
}

void TinySIP::newLocalTag(bool caller) {
  // RFC 3261, Section 19.3:
  // "A property of this selection  requirement is that a UA will place a different tag into the From
  //  header of an INVITE than it would place into the To header of the response to the same INVITE.  This is needed in order for a UA to  invite itself to a session..."
  // TODO: this feature is not used: cannot invite oneself
  //localTag[0] = caller ? 'F' : 'T';
  localTag[0] = 'z';  // temporary
  Random.randChars((char*)(localTag+1), OWN_TAG_LENGTH-1);
}

void TinySIP::newCallId(char** str) {
  freeNull((void **) str);
  *str = (char*) malloc(CALL_ID_LENGTH+1);
  Random.randChars(*str, CALL_ID_LENGTH);
  log_v("Call-ID selected: %s", *str);
}

void TinySIP::newCNonce(char* str) {
  Random.randChars(str, CNONCE_LENGTH);
}

#ifdef TINY_SIP_DEBUG
void TinySIP::unitTest() {

  log_d("tinySIP unit test:");

  // Test parseQuotedString
  {
    log_d("  parseQuotedString: ");
    strcpy(buff, "\\0123\\'\\\"\\'\\4567\"; abc");        // overescaping added on purpose
    char* p = parseQuotedString(buff);
    log_d("%s", (!strcmp(p, "; abc") && !strcmp("0123'\"'4567", buff)) ? "OK" : "FAILED" );
  }

  // Test retrieveGenericParam
  {
    const char* const sParams[] PROGMEM = {
      "tag=123",
      "tag=\"123\"",
      "q=1.0,tag=123",
      "tag=\"123\",q=1.0",
      "hello ,  ipv6=[2001:0db8:0000:0000:0000:ff00:0042:8329],my=123.123.1.12,\r\n\ttag=\"123\",jesus",
    };
    bool succ = true;
    for (int i=0; i<sizeof(sParams)/sizeof(char*); i++) {
      char* str = NULL;
      bool found = retrieveGenericParam(sParams[i], "tag", TINY_SIP_COMMA, &str);
      if (!found || str==NULL || strcmp(str,"123")) {
        succ = false;
        break;
      }
      freeNull((void **) &str);
    }
    log_d("  parsing generic-param: %s", succ ? "OK" : "FAILED");
  }

  // Test with incorrect headers
  {
    const char pIncorrect[] PROGMEM = "To: Test Test <sip:test@test.info>;tag =\t abcedfghijklmnopqrtsuvwxyz.0123456789";
    strcpy(buff, pIncorrect);
    respHeaderCnt = 1;
    respHeaderName[0] = buff;
    respHeaderValue[0] = strchr(buff, ':')+2;
    *(respHeaderValue[0]-2) = '\0';
    parseHeader(0);
    log_d("  parsing incorrect header: OK");
  }

  // Test parseHeader for To
  {
    log_d("  parsing To: ");
    const char pTo[] PROGMEM = "to: Mei Mei <sip:test@test.sip2sip.info>;tag =\t abcedfghijklmnopqrtsuvwxyz.0123456789";      // TODO: why is there exception on "To"?
    strcpy(buff, pTo);
    char* p = strchr(buff, ':');
    respHeaderCnt = 1;
    respHeaderName[0] = buff;
    respHeaderValue[0] = p+2;
    //log_d(" p = %s", p-buff);
    //*p = '\0' // strangely this causes error          // TODO: replicate and ask on SO
    buff[p-buff] = '\0';
    //log_d("  - parsing: "); log_d("%s", respHeaderName[0]); log_d(" - %s", respHeaderValue[0]);
    parseHeader(0);
    if (!strcasecmp(respToDispName, "Mei Mei") && !strcmp(respToAddrSpec, "sip:test@test.sip2sip.info") &&
        respToTagDyn!=NULL && !strcmp(respToTagDyn, "abcedfghijklmnopqrtsuvwxyz.0123456789")) {
      log_d("OK");
    } else {
      log_d("FAILED");
    }
  }

  // Test parseHeader for Proxy-Authenticate
  {
    const char pAuthHeader1[] PROGMEM = "proxy-authenticate: Digest realm=\"WiPhone.org\", nonce=\"5aec\\\"1d1b\" ,  OPAQUE = \"0123456789abcdef\"";
    strcpy(buff, pAuthHeader1);
    respHeaderCnt = 1;
    respHeaderName[0] = buff;
    respHeaderValue[0] = strchr(buff, ':')+2;
    *(respHeaderValue[0]-2) = '\0';
    parseHeader(0);
    log_d("  parsing Proxy-Authenticate: ");
    if (!strcasecmp(respChallenge, "digest") && !strcmp(digestRealm, "WiPhone.org") && !strcmp(digestNonce, "5aec\"1d1b") && !strcmp(digestOpaque, "0123456789abcdef")) {
      log_d("OK");
    } else {
      log_d("FAILED");
    }
  }
  {
    const char pAuthHeader1[] PROGMEM = "www-authenticate: Digest realm=\"sip.wiphone.org\", nonce=\"abc123\", opaque=\"+GNywA==\", algorithm=MD5, qop=\"TOKEN , auth-int , auth\"";
    strcpy(buff, pAuthHeader1);
    respHeaderCnt = 1;
    respHeaderName[0] = buff;
    respHeaderValue[0] = strchr(buff, ':')+2;
    *(respHeaderValue[0]-2) = '\0';
    parseHeader(0);
    log_d("  parsing WWW-Authenticate: ");
    if (!strcasecmp(respChallenge, "digest") && !strcmp(digestRealm, "sip.wiphone.org") && !strcmp(digestNonce, "abc123") &&
        !strcmp(digestOpaque, "+GNywA==") && !strcmp(digestQopPref, "auth-int") && !strcmp(digestAlgorithm, "MD5")) {
      log_d("OK");
    } else {
      log_d("FAILED");
    }
  }

  // Test parseAddrSpec
  log_d("Memory: %d", (int) heap_caps_get_free_size(MALLOC_CAP_8BIT));
  {
    const char* const addrSpec[] PROGMEM = {
      "sip:74513980@192.168.1.107:37443;transport=tcp>Z",
      "sip:sylkserver@85.17.186.20:5060 Z",
      "sip:username@12.23.34.45;tag=xyz?id=1234&hello Z",
      "sips:+158-555-1234567;postd=pp22@foo.com;user=phone Z",      // p. 157         <-- this thing causes memory leak with AddrSpec, around 84.8 bytes get lost
      "sips:+258-555-1234567;postd=pp22@foo.com;user=phone Z",      // p. 157         <-- this thing causes memory leak with AddrSpec, around 84.8 bytes get lost
      "sips:+358-555-1234567;postd=pp22@foo.com;user=phone Z",      // p. 157         <-- this thing causes memory leak with AddrSpec, around 84.8 bytes get lost
      "sips:+458-555-1234567;postd=pp22@foo.com;user=phone Z",      // p. 157         <-- this thing causes memory leak with AddrSpec, around 84.8 bytes get lost
      "sips:+558-555-1234567;postd=pp22@foo.com;user=phone Z",      // p. 157         <-- this thing causes memory leak with AddrSpec, around 84.8 bytes get lost
      "mailto:watson@bell-telephone.com>Z",
      "sip:+12125551212@server.phone2net.com;tag=887s?hello>Z",
      "sipNON-SENSE Z",
      "sip:81.23.228.150;lr;ftag=b6fddfeb-097c-48f0-81b3-8a5aa37134d1;did=853.a749fca5>Z",      // Record-Route real world example
      "sip:bob@192.0.2.4 Z",
    };
    bool succ = true;
    for (int i=0; i<sizeof(addrSpec)/sizeof(char*); i++) {
      log_d("%d ", i);
      strcpy(buff, addrSpec[i]);
      log_d("%s", addrSpec[i]);
      AddrSpec addrParsed(addrSpec[i]);
      addrParsed.show();
      char* p = buff;
      char *scheme, *hostport, *userinfo, *uriParams, *headers;
      p = parseAddrSpec(p, &scheme, &hostport, &userinfo, &uriParams, &headers);
      if (p!=NULL) {
        if (strcmp(p+1,"Z")) {
          log_d("%s Z FAILED", p+1);
          break;
        }
        *p = '\0';
        log_d("    scheme = %s", scheme);
        if (userinfo!=NULL) {
          log_d("    userinfo = %s", userinfo);
        }
        log_d("    hostport = %s", hostport);
        if (uriParams!=NULL) {
          log_d("    uriParams = %s", uriParams);
        }
        if (headers!=NULL) {
          log_d("    headers = %s", headers);
        }
      } else {
        log_d("    incorrect");
      }
    }
    log_d("  parsing addr-spec: %s", succ ? "OK" : "FAILED");
  }
  log_d("Memory: %d", (int) heap_caps_get_free_size(MALLOC_CAP_8BIT));

  // Test parseContactParam
  {
    const char* const sToFrom[] PROGMEM = {         // all examples are from RFC 3261
      "Bob <sip:bob@biloxi.com>,Z",
      "Alice <sip:alice@atlanta.com>;tag=1928301774,Z",
      "The Operator <sip:operator@cs.columbia.edu>;tag=287447,Z",
      "<sip:bob@192.0.2.4>,Z",
      "Multi\r\n Line\r\n Ridiculous\r\n\tDisplay\tName <mailto:try@example.com;expires=1200>,Z",
      "<sip:alice@atlanta.com>;expires=3600,Z",
      "sip:caller@u1.example.com,Z",
      "Lee M. Foote <sips:lee.foote@example.com>,Z",
      "sip:caller@u1.example.com;nihao,Z",
      // Contact
      "\"Mr. Watson\"<sip:watson@worcester.bell-telephone.com>\r\n   ;q=0.7; expires=3600,Z",
      "\"Mr. W@tson\" <mailto:watson@bell-telephone.com> ;q=0.1,Z",
      "<sip:81.23.228.150;lr;ftag=b6fddfeb-097c-48f0-81b3-8a5aa37134d1;did=853.a749fca5>,Z",        // Record-Route real world example
    };
    bool succ = true;
    for (int i=0; i<sizeof(sToFrom)/sizeof(char*); i++) {
      strcpy(buff, sToFrom[i]);
      log_d("%d %s", i, buff);
      char* p = buff;
      char *dispName, *addrSpec, *params;
      p = parseContactParam(p, &dispName, &addrSpec, &params);
      if (p!=NULL) {
        if (*p) {
//          log_d("left: %s", p);
          *p = '\0';
          p++;
          if (!p || !*p || strcmp(p, "Z")) {
            log_d("Z ERROR: %s", p);
          }
        }
        if (dispName) {
          log_d("    name = %s", dispName);
        }
        log_d("    addr = %s", addrSpec);
        if (params!=NULL) {
          log_d("    params = %s", params);
        }
      } else {
        // incorrect character after parameter reached
        // or other mistake
        log_d("    incorrect");
      }
    }
    log_d("  parsing contact-param: %s", succ ? "OK" : "FAILED");
  }

  // Test: parse Contact headers
  {
    const char* const sContact[] PROGMEM = {
      "contact: *",
      "contact: \"Mr. Watson\" <sip:watson@worcester.bell-telephone.com>\r\n\t;q=0.7; expires=3600,\r\n\t\"Mr. Watson\" <mailto:watson@bell-telephone.com> ;q=0.1",
      "m: \"Mr. Watson\" <mailto:watson@bell-telephone.com> ;q=0.7,\r\n\t\"Mr. Watson\" <sip:watson@worcester.bell-telephone.com>\r\n\t;q=0.1; expires=3600",
      "m: \"Mr. Watson\" <mailto:watson@bell-telephone.com> ;q=0.7,\r\n\t\"Mr. Watson\" <sips:watson@worcester.bell-telephone.com>;q=0.1;expires=3600,\r\n\t\"Mr. Watson\"<sip:watson@worcester.bell-telephone.com>;q=0.1; expires=3600",
    };
    bool succ = true;
    for (int i=0; i<sizeof(sContact)/sizeof(char*); i++) {
      strcpy(buff, sContact[i]);
      char* p = strchr(buff, ':');
      respHeaderCnt = 1;
      respHeaderName[0] = buff;
      respHeaderValue[0] = p+2;
      *p = '\0';
      parseHeader(0);
      log_d("%d", i);
      log_d("    Name: %s", respContDispNameDyn==NULL ? "" : respContDispNameDyn);
      log_d("     SIP: %s", respContAddrSpecDyn==NULL ? "" : respContAddrSpecDyn);
    }
    log_d("  parsing contact: %s", succ ? "OK" : "FAILED");
  }

  // Test: parse Record-Route headers
  {
    const char bff[] PROGMEM = "SIP/2.0 200 OK\r\n"
                               "record-route: <sip:p4.domain.com;lr>\r\n"
                               "record-route: <sip:p3.middle.com>\r\n"
                               "record-route: <sip:p2.example.com;lr>\r\n"
                               "record-route: <sip:p1.example.com;lr>\r\n"
                               "record-route: <sip:bigbox3.site3.atlanta.com;lr>,\r\n      <sip:server10.biloxi.com;lr>\r\n"
                               "record-route: <sip:alice@atlanta.com>, <sip:bob@biloxi.com>,\r\n\t<sip:carol@chicago.com>\r\n\r\n";
    resetBuffer();
    strcpy(buff, bff);
    buffLength = strlen(bff);
    buffStart = buff;
    parseResponse();
    //char* p = strchr(buff, ':');
    //respHeaderCnt = 1;    respHeaderName[0] = buff;  respHeaderValue[0] = p+2;    *p = '\0';
    //parseHeader(0);
    log_d("  Route set size: %d", respRouteSet.size());
    log_d("  Route set order: %s", respRouteSet.isReverse() ? "REVERSE" : "STRAIGHT");
    for (int i=0; i<respRouteSet.size(); i++) {
      log_d("  Route: <%s>", respRouteSet[i] ? respRouteSet[i] : "NULL");
    }
    log_d("  parsing record-route: %s", (respRouteSet.size() == 9) ? "OK" : "FAILED");
  }

  // Test: parse (almost) real response
  {
    const char bff[] PROGMEM = "SIP/2.0 200 OK\r\n"
                               "Via: SIP/2.0/TCP 192.168.1.107:52370;rport=59635;received=113.90.232.72;branch=z9hG4bKPj6729b2e8-534e-4436-8e55-7c016be03971;alias\r\n"
                               "Record-Route: <sip:81.23.228.150;lr;ftag=b6fddfeb-097c-48f0-81b3-8a5aa37134d1;did=853.a749fca5>\r\n"
                               "Record-Route: <sip:81.23.228.129;lr;r2=on;ftag=b6fddfeb-097c-48f0-81b3-8a5aa37134d1;did=853.2ad1d951>\r\n"
                               "Record-Route: <sip:81.23.228.129;transport=tcp;lr;r2=on;ftag=b6fddfeb-097c-48f0-81b3-8a5aa37134d1;did=853.2ad1d951>\r\n"
                               "Call-ID: 534ba120-4b42-4318-96a0-5420b65370c4\r\n"
                               "From: \"Donald Knuth\" <sip:knuth@sip2sip.info>;tag=b6fddfeb-097c-48f0-81b3-8a5aa37134d1\r\n"
                               "To: <sip:echo@conference.sip2sip.info>;tag=7f9719a3-03bb-4c72-bd3f-38280746699e\r\n"
                               "CSeq: 29907 INVITE\r\n"
                               "Server: SylkServer-4.1.0\r\n"
                               "Allow: SUBSCRIBE, NOTIFY, PRACK, INVITE, ACK, BYE, CANCEL, UPDATE, MESSAGE, REFER\r\n"
                               "Contact: <sip:sylkserver@85.17.186.20:5060>\r\n"
                               "Supported: 100rel, replaces, norefersub, gruu\r\n"
                               "Content-Type: application/sdp\r\n"
                               "Content-Length: 315\r\n"
                               "\r\n"
                               "v=0\r\n"
                               "o=- 3733994759 3733994760 IN IP4 85.17.186.20\r\n"
                               "s=SylkServer-4.1.0\r\n"
                               "t=0 0\r\n"
                               "m=audio 52750 RTP/AVP 9 101\r\n"
                               "c=IN IP4 81.23.228.129\r\n"
                               "a=rtcp:52751\r\n"
                               "a=rtpmap:9 G722/8000\r\n"
                               "a=rtpmap:101 telephone-event/8000\r\n"
                               "a=fmtp:101 0-16\r\n"
                               "a=zrtp-hash:1.10 a1a2fc9b40182a2b8d18f689b1c0c353613b72696f839b199feb7831127fcb92\r\n"
                               "a=sendrecv\r\n";
    resetBuffer();
    strcpy(buff, bff);
    buffLength = strlen(bff);
    buffStart = buff;
    parseResponse();
    showParsed();
    //char* p = strchr(buff, ':');
    //respHeaderCnt = 1;    respHeaderName[0] = buff;  respHeaderValue[0] = p+2;    *p = '\0';
    //parseHeader(0);
    log_d("  Route set size: %d", respRouteSet.size());
    log_d("  Route set order: %s", respRouteSet.isReverse() ? "REVERSE" : "STRAIGHT");
    for (int i=0; i<respRouteSet.size(); i++) {
      log_d("  Route: <%s>", respRouteSet[i] ? respRouteSet[i] : "NULL");
    }
    log_d("  parsing entire response: %s", (respRouteSet.size() == 3) ? "OK" : "FAILED");
  }

  // Test: parse incomplete response
  {
    const char bff[] PROGMEM = "SIP/2.0 180 Ringing\r\n"
                               "Via: SIP/2.0/TCP 192.168.1.2:57;rport=22954;received=113.90.234.111;branch=z9hG4bKMZJ-OTLoe4pW3;alias\r\n"
                               "Record-Route: <sip:91.121.30.149;lr;ftag=ztSs4tQ1M>\r\n"
                               "Record-Route: <sip:81.23.228.150;lr;ftag=ztSs4tQ1M;did=da3.67dea88>\r\n"
                               "Record-Route: <sip:85.17.186.7;lr;r2=on;ftag=ztSs4tQ1M;did=da3.e8058f32>\r\n"
                               "Record-Route: <sip:85.17.186.7;transport=tcp;lr;r2=on;ftag=ztSs4tQ1M;did=da3.e8058f32>\r\n"
                               "Call-ID: ZUgYRDxz0\r\n"
                               "From: \"Andriy M.\" <s";
    resetBuffer();
    strcpy(buff, bff);
    buffLength = strlen(bff);
    buffStart = buff;
    parseResponse();
    showParsed();
  }

  // Test: parse real request with empty header value (X-CallId)
  {
    log_d("Parsing real request with empty header");
    const char bff[] PROGMEM = "INVITE sip:13477354383@113.90.233.219:53080;transport=tcp;ob SIP/2.0\r\n"
                               "Record-Route: <sip:206.191.159.247;transport=tcp;r2=on;lr=on;ftag=as15eaea99;vsf=AAAAAAAAAAAAAAAAAAAABQIYHAQIHxwDDRcfBgMNNTA4MA--;vst=AAAAAAUHAwMPcwcEBRYCeQAfAhsAHhwGBDMuMjE5;nat=yes>\r\n"
                               "Record-Route: <sip:206.191.159.247;r2=on;lr=on;ftag=as15eaea99;vsf=AAAAAAAAAAAAAAAAAAAABQIYHAQIHxwDDRcfBgMNNTA4MA--;vst=AAAAAAUHAwMPcwcEBRYCeQAfAhsAHhwGBDMuMjE5;nat=yes>\r\n"
                               "Via: SIP/2.0/TCP 206.191.159.247;branch=z9hG4bKc517.9996f544d6215510f29684106bc5f9e3.0\r\n"
                               "Via: SIP/2.0/UDP 72.251.228.147:5080;received=72.251.228.147;branch=z9hG4bK0bda7660;rport=5080\r\n"
                               "Max-Forwards: 69\r\n"
                               "From:  <sip:7702561135@206.191.159.247>;tag=as15eaea99\r\n"
                               "To: 13477354383 <sip:13477354383@113.90.233.219>\r\n"
                               "Contact: <sip:7702561135@72.251.228.147:5080>\r\n"
                               "Call-ID: 020450fc4deb7ecb40344bca28cb1d86@72.251.228.147:5080\r\n"
                               "CSeq: 102 INVITE\r\n"
                               "Date: Thu, 09 May 2019 07:07:38 GMT\r\n"
                               "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, SUBSCRIBE, NOTIFY, INFO, PUBLISH\r\n"
                               "Supported: replaces\r\n"
                               "X-CallId: \r\n"
                               "Content-Type: application/sdp\r\n"
                               "Content-Length: 352\r\n"
                               "User-Agent: DIDLogic SBC\r\n"
                               "\r\n"
                               "v=0\r\n"
                               "o=didlogic 564693333 564693333 IN IP4 72.251.228.147\r\n"
                               "s=DID Logic GW\r\n"
                               "c=IN IP4 72.251.228.147\r\n"
                               "t=0 0\r\n"
                               "m=audio 16916 RTP/AVP 8 0 9 18 3 101\r\n"
                               "a=rtpmap:8 PCMA/8000\r\n"
                               "a=rtpmap:0 PCMU/8000\r\n"
                               "a=rtpmap:9 G722/8000\r\n"
                               "a=rtpmap:18 G729/8000\r\n"
                               "a=fmtp:18 annexb=no\r\n"
                               "a=rtpmap:3 GSM/8000\r\n"
                               "a=rtpmap:101 telephone-event/8000\r\n"
                               "a=fmtp:101 0-16\r\n"
                               "a=ptime:20\r\n"
                               "a=sendrecv\r\n";
    resetBuffer();
    strcpy(buff, bff);
    buffLength = strlen(bff);
    buffStart = buff;
    parseRequest();
    showParsed();
  }

  // Test: parse SDP
  {
    const char bff[] PROGMEM = "v=0\r\n"
                               "o=- 3733994759 3733994760 IN IP4 85.17.186.20\r\n"
                               "s=SylkServer-4.1.0\r\n"
                               "t=0 0\r\n"
                               "m=audio 52750 RTP/AVP 9 101 8\r\n"
                               "c=IN IP4 81.23.228.129\r\n"
                               "a=rtcp:52751\r\n"
                               "a=rtpmap:9 G722/8000\r\n"
                               "a=rtpmap:101 telephone-event/8000\r\n"
                               "a=fmtp:101 0-16\r\n"
                               "a=zrtp-hash:1.10 a1a2fc9b40182a2b8d18f689b1c0c353613b72696f839b199feb7831127fcb92\r\n"
                               "a=sendrecv\r\n";
    resetBuffer();
    strcpy(buff, bff);
    buffLength = strlen(bff);
    buffStart = buff;
    parseSdp(buff);
  }

  log_d("SIP test complete");
}

const char* TinySIP::getReason() {
  log_d("getReason TinySIP");
  if ((isResponse && respCode && respReason!=NULL) || (!isResponse && respMethod!=NULL)) {
    freeNull((void **) &guiReasonDyn);
    if (isResponse) {
      guiReasonDyn = (char *) malloc(strlen(respReason) + 5);
      sprintf(guiReasonDyn, "%d %s", respCode, respReason);
    } else {
      guiReasonDyn = strdup(respMethod);
    }
    log_d("getReason TinySIP: %s", guiReasonDyn);
    return (const char*) guiReasonDyn;
  } else {
    log_d("getReason TinySIP: no reason");
    return "";
  }
}

const char* TinySIP::getRemoteName() {
  return isResponse ? respToDispName : respFromDispName;
}

const char* TinySIP::getRemoteUri() {
  return isResponse ? respToAddrSpec : respFromAddrSpec;
}

void TinySIP::xxd(char* b) {
  bool ended = false;
  //char* hundred = b + 100;
  int idx = 0;
  while (!ended /*|| b<hundred*/ && idx < 2048) {
    if (*b=='\n') {
      //log_d("\\xA");
      TmpStringToSIPLogs[idx++]='\n';
    } else if (*b=='\r') {
      //log_d("\\xD");
      TmpStringToSIPLogs[idx++]='\r';
    } else if (*b=='\0') {
      ended = true;
      //log_d("\\x0");
    } else if (*b>=32 && *b<=254) {
      //log_d("%c", *b);
      TmpStringToSIPLogs[idx++]=*b;
    } else {
      //log_d("\\0x%x", *b);
      TmpStringToSIPLogs[idx++]='\\';
      TmpStringToSIPLogs[idx++]='x';
      TmpStringToSIPLogs[idx++]='.';//*b;
      TmpStringToSIPLogs[idx++]='\n';
    }
    b++;
  }
  TmpStringToSIPLogs[idx] = 0;
  log_d("%s", TmpStringToSIPLogs);
}
#endif // TINY_SIP_DEBUG
