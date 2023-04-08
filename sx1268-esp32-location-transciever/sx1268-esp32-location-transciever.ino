#include <esp_task_wdt.h>
#include <RadioLib.h>

struct Position {
  double lat;
  double lng;
};

struct LocationMessage {
  uint16_t readingID;
  uint16_t readingSequence;
  Position position;
};

double setPrecision(double n, float i) {
  return floor(pow(10, i) * n) / pow(10, i);
}

// returns the great-circle distance (in yards) between two points on the globe
// lat1, lat2, lon1, lon2 must be provided in degrees
double haversine(double lat1, double lon1, double lat2, double lon2) {
  const double rEarth = 6967410.3237095;  // yards

  double x = pow(sin(((lat2 - lat1) * M_PI / 180.0) / 2.0), 2.0);
  double y = cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0);
  double z = pow(sin(((lon2 - lon1) * M_PI / 180.0) / 2.0), 2.0);
  double a = x + y * z;
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  double d = rEarth * c;

  return d;
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
      Serial.print(F("radio.begin failed "));
      Serial.println(status);

      xSemaphoreGive(xRadioMutex);
      return status;
    }

    // initialize radio
    status = radio.begin(434.0, 500.0, 12, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0, false);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print(F("radio.begin failed "));
      Serial.println(status);

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

      double distance = haversine(position.lat, position.lng, payload.position.lat, payload.position.lng);

      // MOCK
      setOurPosition(payload.position);

      // Serial.print(payload.readingID);
      // Serial.print(F("/"));
      // Serial.print(payload.readingSequence);
      // Serial.print(F(": "));
      // Serial.print(payload.position.lat, 10);
      // Serial.print(F(" "));
      // Serial.print(payload.position.lng, 10);
      // Serial.print(F(" "));
      // Serial.print(distance, 6);
      // Serial.print(F(" "));

      if (payload.readingSequence == 0) {
        Serial.print(F("+ "));
      } else {
        Serial.print(F("- "));
      }
      Serial.print(setPrecision(distance, 0), 0);
      Serial.print(F(" "));
      Serial.println(rssi, 0);
    } else if (statusRadio != RADIOLIB_ERR_RX_TIMEOUT) {
      Serial.print(F("radio.receive failed "));
      Serial.println(statusRadio);

      // reset radio on any unexpected failure
      radioReset();
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
      Serial.print(F("radio.transmit failed "));
      Serial.println(statusRadio);

      // reset radio on any unexpected failure
      radioReset();
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}