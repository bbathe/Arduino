#include <RadioLib.h>
#include <TinyGPS++.h>

// SX1268 to Xiao SMAD21 connections
// MOSI pin: 10
// MISO pin:  9
// SCK pin:   8
// NSS pin:   3
// DIO1 pin:  1
// RST pin:   5
// BUSY pin:  0
SX1268 radio = new Module(3, 1, 5, 0);

// GT-U7 to Xiao SMAD21 connections
// TXD pin: 7
// RXD pin: 6
TinyGPSPlus gps;

// last sent readings
uint16_t last_reading_id;
uint16_t last_reading_sequence;
float last_lat;
float last_lng;


void initializeRadio() {
  int status = radio.begin(434.0, 500.0, 12, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0, false);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio.begin failed "));
    Serial.println(status);
  }
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  initializeRadio();
}

void loop() {
  last_reading_sequence = last_reading_sequence + 1;

  if (gps.location.isValid() && gps.location.isUpdated()) {
    // signify another reading
    last_reading_id = last_reading_id + 1;
    last_reading_sequence = 0;
    last_lat = (float)gps.location.lat();
    last_lng = (float)gps.location.lng();
  }

  // encode into byte buffer
  uint8_t data[12];
  ((uint16_t*)data)[0] = last_reading_id;        // bytes 0, 1
  ((uint16_t*)data)[1] = last_reading_sequence;  // bytes 2, 3
  ((float*)data)[1] = last_lat;                  // bytes 4, 5, 6, 7
  ((float*)data)[2] = last_lng;                  // bytes 8, 9, 10, 11

  // for (int i = 0; i < sizeof(data); i++) {
  //   Serial.print(data[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();

  // transmit another reading
  int status = radio.transmit(data, sizeof(data));
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio.transmit failed "));
    Serial.println(status);

    // reset radio on any unexpected failure
    status = radio.reset(true);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print(F("radio.reset failed "));
      Serial.println(status);
    }

    // re-initialize radio
    initializeRadio();
  }

  Delay(1000);
}

// Delay ensures that the gps object is being "fed"
static void Delay(unsigned long ms) {
  unsigned long start = millis();
  do {
    while (Serial1.available() > 0)
      gps.encode(Serial1.read());
  } while (millis() - start < ms);
}
