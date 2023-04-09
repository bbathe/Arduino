#include <esp_task_wdt.h>
#include <RadioLib.h>
#include <TinyGPS++.h>
#include <TM1637Display.h>

struct Position {
  double lat;
  double lng;
};

struct LocationMessage {
  uint16_t readingID;
  uint16_t readingSequence;
  Position position;
};

// return segments to display on 7 segment for course
// refactored from TinyGPS++ cardinal method
const uint8_t* cardinalCourse7Segment(double course) {
  static const uint8_t directions[][4] = {
    { 0, 0, 0, SEG_C | SEG_E | SEG_G },                                                                                                          // n
    { 0, SEG_C | SEG_E | SEG_G, SEG_C | SEG_E | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                  // nnE
    { 0, 0, SEG_C | SEG_E | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                                      // nE
    { 0, 0, 0, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                                                          // E
    { 0, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                  // ESE
    { 0, 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                      // SE
    { 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                  // SSE
    { 0, 0, 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G },                                                                                          // S
    { 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },          // SSW
    { 0, 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                                              // SW
    { 0, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },  // WSW
    { 0, 0, 0, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                                                                                  // W
    { 0, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, SEG_C | SEG_E | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                  // WnW
    { 0, 0, SEG_C | SEG_E | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                                                              // nW
    { 0, SEG_C | SEG_E | SEG_G, SEG_C | SEG_E | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G }                                           // nnW
  };

  int direction = (int)((course + 11.25f) / 22.5f);
  return directions[direction % 16];
}

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
// TXD pin: ?
// RXD pin: ?
TinyGPSPlus gps;

// our last known position
Position ourPosition;

// guard access to the radio & ourPosition
SemaphoreHandle_t xRadioMutex;
SemaphoreHandle_t xPositionMutex;

void setup() {
  Serial.begin(9600);

  // create mutexes
  xRadioMutex = xSemaphoreCreateMutex();
  if (xRadioMutex == NULL) {
    Serial.println(F("radio mutex create failed"));
    while (1) yield();
  }
  xPositionMutex = xSemaphoreCreateMutex();
  if (xPositionMutex == NULL) {
    Serial.println(F("position mutex create failed"));
    while (1) yield();
  }

  // create tasks
  if (xTaskCreatePinnedToCore(sendLocationLoop, "SendLocation", 8192, (void*)NULL, 1, NULL, 0) != pdPASS) {
    Serial.println(F("SendLocation task create failed"));
    while (1) yield();
  }
  if (xTaskCreatePinnedToCore(recieveLocationLoop, "RecieveLocation", 8192, (void*)NULL, 1, NULL, 1) != pdPASS) {
    Serial.println(F("RecieveLocation task create failed"));
    while (1) yield();
  }

  // initialize watchdog timer
  if (esp_task_wdt_init(10, true) != ESP_OK) {
    Serial.println(F("TWDT initialization failed"));
    while (1) yield();
  }

  // initialize radio
  if (radioReset() != RADIOLIB_ERR_NONE) {
    Serial.println(F("radio initialization failed"));
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
      Serial.printf("radio.receive failed %d\n", status);
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
    status = radio.begin(434.0, 500.0, 12, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0, false);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.printf("radio.begin failed %d\n", status);
      xSemaphoreGive(xRadioMutex);
      return status;
    }

    xSemaphoreGive(xRadioMutex);
  }
  return status;
}

void recieveLocationLoop(void* p) {
  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    LocationMessage payload;

    // receive another reading
    auto [rssi, statusRadio] = radioRecieve((uint8_t*)&payload, sizeof(payload));
    if (statusRadio == RADIOLIB_ERR_NONE) {
      Position position = getOurPosition();

      double distance = gps.distanceBetween(position.lat, position.lng, payload.position.lat, payload.position.lng);
      double course = gps.courseTo(position.lat, position.lng, payload.position.lat, payload.position.lng);

      // MOCK
      setOurPosition(payload.position);

      // Serial.printf("%d / %d: %0.6f %0.6f ",
      //               payload.readingID,
      //               payload.readingSequence,
      //               payload.position.lat,
      //               payload.position.lng);

      Serial.printf("%s %d %d\n",
                    (payload.readingSequence == 0 ? "+" : "-"),
                    (int)(distance * 3.28084),
                    (int)rssi);
    } else if (statusRadio != RADIOLIB_ERR_RX_TIMEOUT) {
      Serial.printf("radioRecieve failed %d\n", statusRadio);

      // reset radio on any unexpected failure
      statusRadio = radioReset();
      if (statusRadio != RADIOLIB_ERR_NONE) {
        Serial.printf("radioReset failed %d\n", statusRadio);
      }
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void sendLocationLoop(void* p) {
  uint16_t lastReadingID;
  uint16_t lastReadingSequence;

  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    LocationMessage payload;
    int statusRadio;

    lastReadingSequence = lastReadingSequence + 1;
    // if (gps.location.isValid() && gps.location.isUpdated()) {
    //   // signify another reading
    //   lastReadingID = lastReadingID + 1;
    //   lastReadingSequence = 0;
    //   setOurPosition({ gps.location.lat(), gps.location.lng() });
    // }

    // MOCK
    lastReadingID = lastReadingID + 1;
    lastReadingSequence = 0;

    Position position = getOurPosition();
    payload = { lastReadingID, lastReadingSequence, position.lat, position.lng };

    // transmit another reading
    statusRadio = radioTransmit((uint8_t*)&payload, sizeof(payload));
    if (statusRadio != RADIOLIB_ERR_NONE) {
      Serial.printf("radioTransmit failed %d\n", statusRadio);

      // reset radio on any unexpected failure
      statusRadio = radioReset();
      if (statusRadio != RADIOLIB_ERR_NONE) {
        Serial.printf("radioReset failed %d\n", statusRadio);
      }
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}