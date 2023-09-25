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

#include "Networks.h"
#include "helpers.h"
#include "esp_wifi.h"
#include "esp_bt.h"

void Networks::getMac(uint8_t* mac) {
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  log_d("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ===================================================== EXTERNS =====================================================

WiFiUDP udp;
WiFiUDP udpRtcp;
MDNSResponder mdnsResponder;

Networks wifiState;

//wifi event handler
void processWiFiEvent(WiFiEvent_t event) {
  switch(event) {
  case SYSTEM_EVENT_STA_GOT_IP:
    //initializes the UDP state
    //This initializes the transfer buffer
    delay(100);               // without the delay, occasionally throws errors with Arduino-ESP32 ver. 1.0.4
    udp.begin(localUdpPort);
    udpRtcp.begin(localUdpPort+1);
    //delay(100);
    wifiState.setConnected(true, true);
    //When connected set
    log_d("connected! IP address: %s", WiFi.localIP().toString().c_str());
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    log_d("lost connection");
    wifiState.setConnected(false, true);
    break;
  case SYSTEM_EVENT_WIFI_READY:
    //log_d("ready");      // comes up very frequently
    break;
  case SYSTEM_EVENT_SCAN_DONE:
    log_d("scan done");
    break;
  case SYSTEM_EVENT_STA_START:
  case SYSTEM_EVENT_STA_STOP:
  case SYSTEM_EVENT_STA_CONNECTED:
  case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
  case SYSTEM_EVENT_STA_LOST_IP:
  case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
  case SYSTEM_EVENT_STA_WPS_ER_FAILED:
  case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
  case SYSTEM_EVENT_STA_WPS_ER_PIN:
  case SYSTEM_EVENT_AP_START:
  case SYSTEM_EVENT_AP_STOP:
  default:
    break;
  }
}

void connectToWiFi(const char* ssid, const char* pwd) {

  // TODO: do not connect while scanning for networks

  log_d("Connecting to network: %s", ssid);

  // delete old config
  WiFi.disconnect(true);
  wifiState.setConnected(false, false);
  //register event handler
  WiFi.onEvent(processWiFiEvent);

  //Initiate connection
  WiFi.begin(ssid, pwd);

  // Limit transmit power to 14 dBm
  int rv = 0;
  if ((rv = esp_wifi_set_max_tx_power(56)) != ESP_OK) {
    log_e("failed to limit transmit power: %d", rv);
  }

  log_d("Waiting for connection...");
}

// Inspired by: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/WiFi.cpp
// Alternative way: use dns_gethostbyname. See: https://gist.github.com/MakerAsia/37d2659310484bdbba9d38558e2c3cdb
IPAddress resolveDomain(const char* hostName) {
  if (wifiState.mdnsOk) {
    IPAddress addr = mdnsResponder.queryHost(hostName, 500);
    if (addr) { // where is the class definition of IPAddress? Need to know if this is a valid way to test if addr is set.
      log_i("resolved: %s -> %d.%d.%d.%d", hostName, addr[3], addr[2], addr[1], addr[0]);
      return addr;
    } else {
      log_e("%s not found on local network", hostName);
    }
  }

  unsigned long retAddr;
  struct hostent* he = lwip_gethostbyname(hostName);
  if (he != nullptr) {
    retAddr = *(unsigned long*) (he->h_addr_list[0]);       // take only first address
    log_d("resolved: %s -> %d.%d.%d.%d", hostName, retAddr & 0xFF, (retAddr >> 8) & 0xFF, (retAddr >> 16) & 0xFF, (retAddr >> 24) & 0xFF);
  } else {
    retAddr = 0;
    log_e("errno=%d: unable to resolve \"%s\"", h_errno, hostName);
  }
  return IPAddress(retAddr);
}

// ===================================================== WIFI STATE =====================================================

Networks::Networks() : ini(filename), _userDisabled(false) {
  prefSsidDyn = NULL;
  wifiSsidDyn = NULL;
  wifiPassDyn = NULL;
  connected = false;
  reconnect = true;
}

Networks::~Networks() {
  freeNull((void **) &prefSsidDyn);
  freeNull((void **) &wifiSsidDyn);
  freeNull((void **) &wifiPassDyn);
}

void Networks::init() {
  // Reset WiFi (these are needed for proper scanning!!!)
  WiFi.mode(WIFI_STA);
  log_v("Free memory after wifi mode: %d", ESP.getFreeHeap());
  WiFi.disconnect();
  const char* host = "WiPhone"; // Later append serial number here
  log_v("Free memory after disconnect: %d", ESP.getFreeHeap());
  if (mdnsResponder.begin(host)) {
    mdnsOk = true;
    log_i("MDNS Responder Hostname: %s", host);
  } else {
    mdnsOk = false;
    log_e("MDNS Responder Hostname: %s failed to initialize", host);
  }

  log_v("Free memory after responder begin: %d", ESP.getFreeHeap());
  delay(100);
}

// ===================================================== NETWORK CONNECTIONS =====================================================

void Networks::disconnect() {
  WiFi.disconnect(true, true);  // wifioff = true, eraseap = true (erasing might be needed when using Arduino-ESP32 ver. >= 1.0.3)
  connected = false;
  reconnect = false;
}

/*
 * Disable the radio.
 */

void Networks::disable() {
  disconnect();
  WiFi.mode(WIFI_OFF);
  btStop(); // we don't currently use bluetooth for anything, leave it off to save power
  esp_wifi_stop(); //likely unnecessary
  esp_bt_controller_disable(); //likely unnecessary
  log_d("WiFi and BT disabled");
}

/* Description:
 *      load password for a network and set the network as current network (SSID/pass get remembered)
 */
bool Networks::loadNetworkSettings(const char* ssid) {
  log_d("loadNetworkSettings: %s", ssid);
  // TODO: unload ini after using if it is too big (or there is no PSRAM)
  // TODO: consider that there might be multiple networks with the same name
  ini.unload();
  if ((ini.load() || ini.restore()) && !ini.isEmpty()) {
    int i = ini.query("s", ssid);
    if (i>=0 && ini[i].hasKey("p")) {   // check correctness        TODO: use mnemonics for these tiny key names
      // One network found
      log_d("found");
      freeNull((void **) &wifiSsidDyn);
      freeNull((void **) &wifiPassDyn);
      wifiSsidDyn = strdup(ssid);
      wifiPassDyn = strdup(ini[i]["p"]);
      return true;
    }
  }
  return false;
}

/* Desctiption:
 *      load name of the preferred network
 */
void Networks::loadPreferred() {
  log_d("loadPreferred");
  freeNull((void **) &prefSsidDyn);
  ini.unload();
  if ((ini.load() || ini.restore()) && !ini.isEmpty()) {
    int i = ini.findKey("m");
    if (i>=0 && ini[i].hasKey("s")) {
      // Preferred network found
      log_d("preferred network = %s", ini[i]["s"]);
      prefSsidDyn = strdup(ini[i]["s"]);
      const char *disabled = ini[i]["disabled"];
      log_d("loadPreferred: 0x%x", disabled);
      if (disabled != NULL) {
        log_d("loadPreferred is: %s", disabled);
        if (strcmp(disabled, "true") == 0) {
          _userDisabled = true;
        } else {
          _userDisabled = false;
        }
      }
    }
  }
}

bool Networks::connectTo(const char* ssid) {
  log_d("connectTo");
  // Load password and connect to WiFi network
  if (this->loadNetworkSettings(ssid)) {
    connectToWiFi(wifiSsidDyn, wifiPassDyn);    // async
    return true;
  }
  return false;
}

bool Networks::hasPreferredSsid(void) {
  log_d("hasPreferredSsid");
  // Do we have a saved default WiFi network?
  if (prefSsidDyn==NULL) {
    this->loadPreferred();
  }
  if (prefSsidDyn==NULL) {
    log_d("SSID NOT LOADED");
    return false;
  }
  return true;
}

bool Networks::connectToPreferred(void) {
  log_d("connectToPreferred");
  // Load name of preferred WiFi network
  // TODO: find the exact preferred network, not just find the name and then search by name
  if (prefSsidDyn==NULL) {
    this->loadPreferred();
  }
  if (prefSsidDyn==NULL) {
    log_d("SSID NOT LOADED");
    return false;
  }
  return connectTo(prefSsidDyn);
}

bool Networks::scan(void) {
  // used as a reference only
  WiFi.mode(WIFI_STA);
  disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  log_d("scan done");
  log_d("networks: %d", n);
  for (int i=0; i<n; i++) {
    log_d("%d: %s (%d) %s", i, WiFi.SSID(i), WiFi.RSSI(i), (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)? "\t- OPEN":"\t- closed");
    delay(10);
  }
  return n >= 0;
}
