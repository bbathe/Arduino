#include <esp_task_wdt.h>
#include <RadioLib.h>
#include <TinyGPS++.h>
#include <TM1637Display.h>

#define DEBUG false
#define Serial \
  if (DEBUG) Serial

struct Position {
  double lat;
  double lng;
};

struct LocationMessage {
  char type[4];  // 'HELO' or 'GBYE'
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
TM1637Display displayDistance = TM1637Display(32, 33);
TM1637Display displayRSSI = TM1637Display(33, 32);

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



// our last known position
Position ourPosition;

// guard access to the radio & ourPosition
SemaphoreHandle_t xRadioMutex;
SemaphoreHandle_t xPositionMutex;

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  pinMode(SEND_BUTTON, INPUT_PULLUP);

  // displays
  displayDistance.clear();
  displayDistance.setBrightness(0);
  displayDistance.setSegments(dash);
  displayRSSI.clear();
  displayRSSI.setBrightness(0);
  displayRSSI.setSegments(dash);

  // create mutexes
  xRadioMutex = xSemaphoreCreateMutex();
  if (xRadioMutex == NULL) {
    Serial.println(F("radio mutex create failed"));
    displayDistance.setSegments(err);
    displayRSSI.setSegments(err);
    while (1) yield();
  }
  xPositionMutex = xSemaphoreCreateMutex();
  if (xPositionMutex == NULL) {
    Serial.println(F("position mutex create failed"));
    displayDistance.setSegments(err);
    displayRSSI.setSegments(err);
    while (1) yield();
  }

  // initialize radio
  if (radioReset() != RADIOLIB_ERR_NONE) {
    Serial.println(F("radio initialization failed"));
    displayDistance.setSegments(err);
    displayRSSI.setSegments(err);
    while (1) yield();
  }

  // initialize watchdog timer
  if (esp_task_wdt_init(10, true) != ESP_OK) {
    Serial.println(F("TWDT initialization failed"));
    displayDistance.setSegments(err);
    displayRSSI.setSegments(err);
    while (1) yield();
  }

  // create tasks
  if (xTaskCreatePinnedToCore(sendLocationLoop, "SendLocation", 8192, (void*)NULL, 1, NULL, 0) != pdPASS) {
    Serial.println(F("SendLocation task create failed"));
    displayDistance.setSegments(err);
    displayRSSI.setSegments(err);
    while (1) yield();
  }
  if (xTaskCreatePinnedToCore(recieveLocationLoop, "RecieveLocation", 8192, (void*)NULL, 1, NULL, 1) != pdPASS) {
    Serial.println(F("RecieveLocation task create failed"));
    displayDistance.setSegments(err);
    displayRSSI.setSegments(err);
    while (1) yield();
  }
}

void loop() {}

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

// mutex protected get radio.receive
std::tuple<float, int> radioRecieve(uint8_t* data, size_t len) {
  int status = 1;
  float rssi = 1.0;

  if (xSemaphoreTake(xRadioMutex, portMAX_DELAY) == pdTRUE) {
    status = radio.receive(data, len);
    if (status != RADIOLIB_ERR_NONE) {
      xSemaphoreGive(xRadioMutex);
      return { rssi, status };
    }
    rssi = radio.getRSSI();

    xSemaphoreGive(xRadioMutex);
  }
  return { rssi, status };
}

// mutex protected get radio.transmit
int radioTransmit(uint8_t* data, size_t len) {
  int status = 1;

  if (xSemaphoreTake(xRadioMutex, portMAX_DELAY) == pdTRUE) {
    status = radio.transmit(data, len);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.printf("radio.transmit failed %d\n", status);
    }

    xSemaphoreGive(xRadioMutex);
  }
  return status;
}

// mutex protected get radio.reset
int radioReset() {
  int status = 1;

  if (xSemaphoreTake(xRadioMutex, portMAX_DELAY) == pdTRUE) {
    // reset
    status = radio.reset();
    if (status != RADIOLIB_ERR_NONE) {
      Serial.printf("radio.reset failed %d\n", status);
      xSemaphoreGive(xRadioMutex);
      return status;
    }

    // initialize radio
    status = radio.begin(434.0, 500.0, 12, 8, 0x73, 10, 8, 0, false);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.printf("radio.begin failed %d\n", status);
      xSemaphoreGive(xRadioMutex);
      return status;
    }

    xSemaphoreGive(xRadioMutex);
  }
  return status;
}

int sendOurPosition(char* type) {
  // populate payload
  LocationMessage payload = { .position = getOurPosition() };
  memcpy(&payload.type, type, sizeof(payload.type));

  Serial.printf("SENDING %s %0.6f %0.6f\n",
                payload.type,
                payload.position.lat,
                payload.position.lng);

  // transmit reading
  int statusRadio = radioTransmit((uint8_t*)&payload, sizeof(payload));
  if (statusRadio != RADIOLIB_ERR_NONE) {
    Serial.printf("radioTransmit failed %d\n", statusRadio);

    // reset radio on any unexpected failure
    statusRadio = radioReset();
    if (statusRadio != RADIOLIB_ERR_NONE) {
      Serial.printf("radioReset failed %d\n", statusRadio);
    }
  }
  return statusRadio;
}

void recieveLocationLoop(void* p) {
  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    // receive a reading
    LocationMessage payload;
    auto [rssi, statusRadio] = radioRecieve((uint8_t*)&payload, sizeof(payload));
    if (statusRadio == RADIOLIB_ERR_NONE) {
      Serial.printf("RECIEVED %s %0.6f %0.6f\n",
                    payload.type,
                    payload.position.lat,
                    payload.position.lng);

      Position position = getOurPosition();
      double distance = gps.distanceBetween(position.lat, position.lng, payload.position.lat, payload.position.lng) * 3.28084;

      Serial.printf("CALCULATED %d %d\n",
                    (int)distance,
                    (int)rssi);


      displayDistance.setBrightness(7);
      displayRSSI.setBrightness(7);
      if (distance > 9999.0) {
        displayDistance.setSegments(dash);
      } else {
        displayDistance.showNumberDec((int)distance);
      }
      displayRSSI.showNumberDec((int)rssi);

      if (memcmp(payload.type, "HELO", sizeof(payload.type)) == 0) {
        // respond with our position, end conversation
        int statusRadio = sendOurPosition("GBYE");
        if (statusRadio != RADIOLIB_ERR_NONE) {
          Serial.printf("sendOurPosition failed %d\n", statusRadio);
        }
      }

      esp_task_wdt_reset();
      vTaskDelay(500 / portTICK_PERIOD_MS);

      displayDistance.setBrightness(0);
      displayRSSI.setBrightness(0);
      if (distance > 9999.0) {
        displayDistance.setSegments(dash);
      } else {
        displayDistance.showNumberDec((int)distance);
      }
      displayRSSI.showNumberDec((int)rssi);

    } else if (statusRadio != RADIOLIB_ERR_RX_TIMEOUT) {
      Serial.printf("radioRecieve failed %d\n", statusRadio);

      // reset radio on any unexpected failure
      statusRadio = radioReset();
      if (statusRadio != RADIOLIB_ERR_NONE) {
        Serial.printf("radioReset failed %d\n", statusRadio);
      }
    }

    esp_task_wdt_reset();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void sendLocationLoop(void* p) {
  int lastButtonState = LOW;


  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    // feed gps
    while (Serial2.available() > 0) {
      gps.encode(Serial2.read());
    }

    // update our location
    if (gps.location.isValid() && gps.location.isUpdated()) {
      setOurPosition({ .lat = gps.location.lat(), .lng = gps.location.lng() });
    }

    // read the state of the button
    int currentButtonState = digitalRead(SEND_BUTTON);
    if (lastButtonState == HIGH && currentButtonState == LOW) {
      displayDistance.clear();
      displayRSSI.clear();

      // send with our position, start conversation
      int statusRadio = sendOurPosition("HELO");
      if (statusRadio != RADIOLIB_ERR_NONE) {
        Serial.printf("sendOurPosition failed %d\n", statusRadio);
      }
    }

    lastButtonState = currentButtonState;
    esp_task_wdt_reset();
  }
}
