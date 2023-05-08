#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <RadioLib.h>
#include <UniversalTelegramBot.h>
#include <TinyGPS++.h>
#include "secrets.h"

#define DEBUG true
#define Serial \
  if (DEBUG) Serial

struct Position {
  float lat;
  float lng;
};

struct LocationMessage {
  uint32_t messageID;
  Position position;
};

// just for calculating distances
TinyGPSPlus gps;

// telegram bot for posting traffic
WiFiClientSecure net;
UniversalTelegramBot bot(BOT_TOKEN, net);

// LLCC68 to EzSBC ESP32 Dev Board connections
// MOSI pin: 23
// MISO pin: 19
// SCK pin:  18
// NSS pin:   5
// DIO1 pin: 22
// RST pin:  21
// BUSY pin: 27
LLCC68 radio = new Module(5, 22, 21, 27);

void setup() {
  Serial.begin(9600);

  // radio
  int statusRadio = radioBegin();
  if (statusRadio != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio initialization failed "));
    Serial.println(statusRadio);
    while (1) yield();
  }

  // wifi
  WiFi.disconnect();
  WiFi.useStaticBuffers(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  net.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // watchdog timer
  if (esp_task_wdt_init(10, true) != ESP_OK) {
    Serial.println(F("TWDT initialization failed"));
    while (1) yield();
  }

  // create tasks
  if (xTaskCreatePinnedToCore(recieveLocationLoop, "RecieveLocation", 8192, (void *)NULL, 1, NULL, 1) != pdPASS) {
    Serial.println(F("RecieveLocation task create failed"));
    while (1) yield();
  }
}

void loop() {}

// a central place to call radio.begin with the parameters we want to use
int radioBegin() {
  int statusRadio = radio.begin(432.0, 125.0, 9, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22, 8, 0, false);
  if (statusRadio == RADIOLIB_ERR_NONE) {
    statusRadio = radio.setRxBoostedGainMode(true, false);
  }
  return statusRadio;
}

void recieveLocationLoop(void *p) {
  uint32_t lastMessageID = UINT32_MAX;

  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    // receive a reading
    LocationMessage payload;
    int statusRadio = radio.receive((uint8_t *)&payload, sizeof(payload));
    if (statusRadio == RADIOLIB_ERR_NONE) {
      // don't process duplicate messages
      if (lastMessageID != payload.messageID) {
        lastMessageID = payload.messageID;

        float rssi = radio.getRSSI();
        float snr = radio.getSNR();

        // calculate distance in feet
        int distance = (int)(gps.distanceBetween(LOCATION_LAT, LOCATION_LNG, payload.position.lat, payload.position.lng) * 3.28084);

        char msg[255];
        sprintf(msg, "id: %d\nlat: %0.6f\nlng: %0.6f\ndistance: %d\nrssi: %.0f\nsnr: %.0f",
                payload.messageID,
                payload.position.lat,
                payload.position.lng,
                distance,
                rssi,
                snr);
        Serial.println(msg);

        // relay to telegram channel
        bot.sendMessage(CHAT_ID, msg);
      }
    } else if (statusRadio != RADIOLIB_ERR_RX_TIMEOUT && statusRadio != RADIOLIB_ERR_SPI_CMD_TIMEOUT) {
      Serial.printf("radio.receive failed %d\n", statusRadio);

      // reset radio on any unexpected failure
      statusRadio = radio.reset();
      if (statusRadio != RADIOLIB_ERR_NONE) {
        Serial.printf("radio.reset failed %d\n", statusRadio);
      }

      // initialize radio
      statusRadio = radioBegin();
      if (statusRadio != RADIOLIB_ERR_NONE) {
        Serial.printf("radio.begin failed %d\n", statusRadio);
      }
    }

    esp_task_wdt_reset();
  }
}
