#include <esp_task_wdt.h>
#include <RadioLib.h>
#include <TinyGPS++.h>
#include <TM1637Display.h>
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

// SX1268 to EzSBC ESP32 Dev Board connections
// MOSI pin: 23
// MISO pin: 19
// SCK pin:  18
// NSS pin:   5
// DIO1 pin: 22
// RST pin:  21
// BUSY pin: 27
SX1268 radio = new Module(5, 22, 21, 27);

// GT-U7 to EzSBC ESP32 Dev Board connections
// TXD pin: 16
// RXD pin: 17
TinyGPSPlus gps;

// Button to EzSBC ESP32 Dev Board connections
// Button: 13
#define SEND_BUTTON 13

// sharing pins with multiple displays
// https://esphome.io/components/display/tm1637.html#connect-multiple-displays
TM1637Display display0 = TM1637Display(32, 33);
TM1637Display display1 = TM1637Display(33, 32);

const uint8_t err[] = {
  0,                                      // blank
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,  // E
  SEG_E | SEG_G,                          // r
  SEG_E | SEG_G,                          // r
};

const uint8_t dash[] = {
  SEG_G,  // -
  SEG_G,  // -
  SEG_G,  // -
  SEG_G,  // -
};

const uint8_t sent[] = {
  SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,  // S
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,  // E
  SEG_C | SEG_E | SEG_G,                  // n
  SEG_D | SEG_E | SEG_F | SEG_G,          // t
};


// our last known position
Position ourPosition;

// guard access to the radio & ourPosition
SemaphoreHandle_t xRadioMutex;
SemaphoreHandle_t xPositionMutex;

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  // pin for 'send' button
  pinMode(SEND_BUTTON, INPUT_PULLUP);

  // displays
  display0.clear();
  display0.setBrightness(0);
  display0.setSegments(dash);
  display1.clear();
  display1.setBrightness(0);
  display1.setSegments(dash);

  // create mutexes
  xRadioMutex = xSemaphoreCreateMutex();
  if (xRadioMutex == NULL) {
    Serial.println(F("radio mutex create failed"));
    display0.setSegments(err);
    display1.setSegments(err);
    while (1) yield();
  }
  xPositionMutex = xSemaphoreCreateMutex();
  if (xPositionMutex == NULL) {
    Serial.println(F("position mutex create failed"));
    display0.setSegments(err);
    display1.setSegments(err);
    while (1) yield();
  }

  // initialize radio
  int statusRadio = radioBegin();
  if (statusRadio != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio initialization failed "));
    Serial.println(statusRadio);
    display0.setSegments(err);
    display1.setSegments(err);
    while (1) yield();
  }

  // initialize watchdog timer
  if (esp_task_wdt_init(10, true) != ESP_OK) {
    Serial.println(F("TWDT initialization failed"));
    display0.setSegments(err);
    display1.setSegments(err);
    while (1) yield();
  }

  // create tasks
  if (xTaskCreatePinnedToCore(sendLocationLoop, "SendLocation", 8192, (void*)NULL, 1, NULL, 0) != pdPASS) {
    Serial.println(F("SendLocation task create failed"));
    display0.setSegments(err);
    display1.setSegments(err);
    while (1) yield();
  }
  if (xTaskCreatePinnedToCore(feedGPSLoop, "FeedGPS", 8192, (void*)NULL, 1, NULL, 1) != pdPASS) {
    Serial.println(F("FeedGPS task create failed"));
    display0.setSegments(err);
    display1.setSegments(err);
    while (1) yield();
  }
}

void loop() {}

int radioBegin() {
  int statusRadio = radio.begin(432.0, 125.0, 9, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22, 8, 0, false);
  if (statusRadio == RADIOLIB_ERR_NONE) {
    statusRadio = radio.setRxBoostedGainMode(true, false);
  }
  return statusRadio;
}

// mutex protected set ourPosition
bool setOurPosition(Position position) {
  bool set = false;

  if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE) {
    ourPosition.lat = position.lat;
    ourPosition.lng = position.lng;
    set = true;

    xSemaphoreGive(xPositionMutex);
  }
  return set;
}

// mutex protected get ourPosition
Position getOurPosition() {
  Position position;

  if (xSemaphoreTake(xPositionMutex, portMAX_DELAY) == pdTRUE) {
    position.lat = ourPosition.lat;
    position.lng = ourPosition.lng;

    xSemaphoreGive(xPositionMutex);
  }
  return position;
}

// round off value to prec decimal places
float roundoff(float value, unsigned char prec) {
  float pow10 = pow(10.0f, (float)prec);
  return round(value * pow10) / pow10;
}

void feedGPSLoop(void* p) {
  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    // feed gps
    while (Serial2.available() > 0) {
      gps.encode(Serial2.read());
    }

    // update our location
    if (gps.location.isValid() && gps.location.isUpdated()) {
      setOurPosition({ .lat = roundoff((float)gps.location.lat(), 6), .lng = roundoff((float)gps.location.lng(), 6) });
    }

    esp_task_wdt_reset();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void sendLocationLoop(void* p) {
  uint32_t lastMessageID = 0;
  int lastButtonState = LOW;

  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    // read the state of the button
    int currentButtonState = digitalRead(SEND_BUTTON);
    if (lastButtonState == HIGH && currentButtonState == LOW) {
      lastMessageID++;

      // populate payload
      LocationMessage payload = {
        .messageID = lastMessageID,
        .position = getOurPosition()
      };

      // calculate distance in feet
      int distance = (int)(gps.distanceBetween(LOCATION_LAT, LOCATION_LNG, payload.position.lat, payload.position.lng) * 3.28084);
      if (distance < 10000) {
        display1.showNumberDec(distance);
      }

      Serial.printf("SENDING %d %0.6f %0.6f\n",
                    payload.messageID,
                    payload.position.lat,
                    payload.position.lng);

      for (int i = 0; i < 3; i++) {
        // transmit reading
        int statusRadio = radio.transmit((uint8_t*)&payload, sizeof(payload));
        if (statusRadio == RADIOLIB_ERR_NONE) {
          display0.setSegments(sent);
        } else {
          display0.setSegments(err);
          Serial.printf("radio.transmit failed %d\n", statusRadio);

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

          break;
        }
      }

      esp_task_wdt_reset();
      vTaskDelay(1000 / portTICK_PERIOD_MS);

      display0.setSegments(dash);
      display1.setSegments(dash);
    }

    lastButtonState = currentButtonState;
    esp_task_wdt_reset();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
