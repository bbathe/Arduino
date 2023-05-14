#include <WiFi.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include <esp_wifi.h>
#include "secrets.h"

#define DEBUG true
#define Serial \
  if (DEBUG) Serial


// web server
WebServer server(80);

// SSD1309 display to EzSBC ESP32 Dev Board connections
// SCK: 18
// SDA: 23
//  CS:  5
//  DC:  4
// RES: 22
U8G2_SSD1309_128X64_NONAME0_1_4W_HW_SPI u8g2(U8G2_R0, 5, 4, 22);
U8G2LOG u8g2log;
#define U8LOG_WIDTH 25
#define U8LOG_HEIGHT 8
uint8_t u8log_buffer[U8LOG_WIDTH * U8LOG_HEIGHT];

// toggle button with debounce logic to turn on/off display powersave
// Button to EzSBC ESP32 Dev Board connections
// Button: 13
#define POWER_SAVE_BUTTON 13
int lastButtonState;
int currentButtonState;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
bool powerSave = false;


// print event details to display
void WiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_READY:
      u8g2log.printf("WiFi ready\n");
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      u8g2log.printf("Scan done\n");
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      u8g2log.printf("Client started\n");
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      u8g2log.printf("Client stopped\n");
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      u8g2log.printf("AP started\n");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      u8g2log.printf("AP stopped\n");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      u8g2log.printf("Client connected\n");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      u8g2log.printf("Client disconnected\n");
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      char buf[255];
      sprintf(buf, IPSTR, IP2STR(&info.wifi_ap_staipassigned.ip));
      u8g2log.printf("Client IP %s\n", buf);
      break;
    default:
      u8g2log.printf("WiFi event %d\n", event);
      break;
  }
}

// for qsort of WiFi channels
int compare(const void* a, const void* b) {
  int numA = *((int*)a);
  int numB = *((int*)b);

  if (numA < numB) {
    return -1;
  } else if (numA > numB) {
    return 1;
  } else {
    return 0;
  }
}

// scan WiFi networks to determine the best channel to use
// will always return a valid channel, even if ts not really free
int findFreeChannel(void) {
  int channelsInUse[14];
  memset(channelsInUse, 0, sizeof(channelsInUse));

  // channel 14 off limits in US
  channelsInUse[0] = 14;

  // scan & record what channels are in use
  WiFi.mode(WIFI_STA);
  int networks = WiFi.scanNetworks(false, true);
  for (int i = 0; i < networks; i++) {
    int channel = WiFi.channel(i);

    // don't add duplicates
    bool duplicate = false;
    for (int j = 0; j < i; j++) {
      if (channelsInUse[j] == channel) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    channelsInUse[i] = channel;
  }
  WiFi.scanDelete();
  WiFi.disconnect(true, true);

  // find the 2 channels with the largest gap
  int count = sizeof(channelsInUse) / sizeof(channelsInUse[0]);
  qsort(channelsInUse, count, sizeof(int), compare);

  int a = 0;
  int b = 0;
  int largest_gap = 0;
  for (int i = 0; i < count - 1; i++) {
    int gap = channelsInUse[i + 1] - channelsInUse[i];
    if (gap > largest_gap) {
      largest_gap = gap;
      a = channelsInUse[i];
      b = channelsInUse[i + 1];
    }
  }

  // set our channel in between
  int c = a + ((b - a) / 2);
  if (c == 0) {
    return 13;
  }
  return c;
}

void handleNotFound() {
  server.send(200, "text/plain", "urls available:\n/clients\n");
}

void handleClients() {
  // get wifi stations connected
  wifi_sta_list_t wifi_sta_list;
  memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
  esp_err_t err = esp_wifi_ap_get_sta_list(&wifi_sta_list);
  if (err != ESP_OK) {
    Serial.printf("esp_wifi_ap_get_sta_list error %d", err);
    return;
  }

  // and the cooresponding tcpip info for them
  tcpip_adapter_sta_list_t tcpip_sta_list;
  memset(&tcpip_sta_list, 0, sizeof(tcpip_sta_list));
  err = tcpip_adapter_get_sta_list(&wifi_sta_list, &tcpip_sta_list);
  if (err != ESP_OK) {
    Serial.printf("tcpip_adapter_get_sta_list error %d", err);
    return;
  }

  // form body
  String body;
  if (tcpip_sta_list.num == 0) {
    body = "no clients connected\n";
  } else {
    char line[64];
    char mac[32];
    char ip[32];

    sprintf(line, "%-20s%s\n", "MAC", "IP");
    body += line;

    for (int i = 0; i < tcpip_sta_list.num; i++) {
      tcpip_adapter_sta_info_t station = tcpip_sta_list.sta[i];

      sprintf(mac, MACSTR, MAC2STR(station.mac));
      sprintf(ip, IPSTR, IP2STR(&station.ip));
      sprintf(line, "%-20s%s\n", mac, ip);
      body += line;
    }
  }

  server.send(200, "text/plain", body);
}

void setup() {
  Serial.begin(9600);

  // power save button initialization
  pinMode(POWER_SAVE_BUTTON, INPUT_PULLUP);
  lastButtonState = currentButtonState = digitalRead(POWER_SAVE_BUTTON);

  // initialize & config display
  if (!u8g2.begin()) {
    Serial.printf("u8g2.begin failed\n");
    while (1)
      ;
  }
  u8g2.setFont(u8g2_font_5x8_tr);

  if (!u8g2log.begin(u8g2, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer)) {
    Serial.printf("u8g2log.begin failed\n");
    while (1)
      ;
  }
  u8g2log.setLineHeightOffset(1);
  u8g2log.setRedrawMode(0);

  // start Wifi
  WiFi.onEvent(WiFiEventHandler);
  WiFi.useStaticBuffers(true);

  int channel = findFreeChannel();
  u8g2log.printf("AP using channel %d\n", channel);

  if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, channel)) {
    u8g2log.printf("AP creation failed\n");
    while (1)
      ;
  }

  u8g2log.printf("AP IP %s\n", WiFi.softAPIP().toString().c_str());
  u8g2log.printf("%s %s\n", WIFI_SSID, WIFI_PASSWORD);

  // start http server
  server.on("/clients", handleClients);
  server.onNotFound(handleNotFound);
  server.begin();
  u8g2log.printf("HTTP server started\n");
}

void loop() {
  // toggle button with debounce logic to turn on/off display powersave
  int reading = digitalRead(POWER_SAVE_BUTTON);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != currentButtonState) {
      currentButtonState = reading;
      if (currentButtonState == HIGH) {
        powerSave = !powerSave;
        u8g2.setPowerSave(powerSave);
      }
    }
  }

  lastButtonState = reading;

  server.handleClient();
}