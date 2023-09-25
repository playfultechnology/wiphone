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

#ifndef _WIFI_NETWORKS_
#define _WIFI_NETWORKS_

#include <WiFi.h>
#include "WiFiUdp.h"
#include "Storage.h"
#include "config.h"
#include "lwip/netdb.h"
#include "src/ping/ping.h"
#include <ESPmDNS.h>

#ifdef WIPHONE_PRODUCTION
#define WIFI_DEBUG(fmt, ...)
#else
#define WIFI_DEBUG(fmt, ...)      DEBUG("[wifi] " fmt, ##__VA_ARGS__)
#endif // WIPHONE_PRODUCTION

//IP address to send UDP data to: either use the ip address of the server or a network broadcast address
static const int localUdpPort = 51002;

extern WiFiUDP udp;
extern WiFiUDP udpRtcp;

extern void connectToWiFi(const char* ssid, const char* pwd);
extern IPAddress resolveDomain(const char* hostName);

// Class to save/load WiFi networks data from Flash
class Networks {
public:
  Networks(void);
  ~Networks(void);

  void init();
  void getMac(uint8_t* mac);

  // Interfaces
  bool loadNetworkSettings(const char* ssid);
  void loadPreferred();

  inline bool isConnected(void) {
    return connected;
  }
  inline bool isConnectionEvent(void) {
    return connectionEvent;
  }
  inline bool doReconnect(void) {
    return reconnect;
  }
  inline void setConnected(bool conn) {
    connected = conn;
  }
  inline void setConnected(bool conn, bool event) {
    connected = conn;
    connectionEvent = event;
  }

  // WiFi network staff
  bool connectToPreferred(void);
  bool hasPreferredSsid(void);
  bool connectTo(const char* ssid);
  void disconnect(void);
  void disable(void);
  bool scan(void);

  // Properties
  const char* ssid() {
    return wifiSsidDyn;
  }
  const char* pass() {
    return wifiPassDyn;
  }
  const char* prefSsid() {
    return prefSsidDyn;
  }

  bool userDisabled() {
    return _userDisabled;
  }

  static constexpr const char* filename = "/networks.ini";
  bool mdnsOk;                // MDNS service is operational

protected:
  char* prefSsidDyn;
  char* wifiSsidDyn;    // current (or last) WiFi network
  char* wifiPassDyn;

  bool _userDisabled;
  bool reconnect;             // should it try to reconnect when disconnected? TODO: save this in configs somehow
  bool connected;
  bool connectionEvent;       // connection/disconnection event processed

  MDNSResponder mdnsResponder;

  CriticalFile ini;
};

extern Networks wifiState;

#endif // _WIFI_NETWORKS_
