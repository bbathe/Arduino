#include <RadioLib.h>

// SX1268 to EzSBC ESP32 Dev Board connections
// MOSI pin: 23
// MISO pin: 19
// SCK pin:  18
// NSS pin:   5
// DIO1 pin: 22
// RST pin:  21
// BUSY pin: 27
SX1268 radio = new Module(5, 22, 21, 27);

// last recieved reading id
uint16_t last_reading_id;


void initializeRadio() {
  int status = radio.begin(434.0, 500.0, 12, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0, false);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio.begin failed "));
    Serial.println(status);
  }
}

void setup() {
  Serial.begin(9600);

  initializeRadio();
}

void loop() {
  uint8_t data[12];

  // receive another reading
  int status = radio.receive(data, sizeof(data));
  if (status == RADIOLIB_ERR_NONE) {
    // for (int i = 0; i < sizeof(data); i++) {
    //   Serial.print(data[i], HEX);
    //   Serial.print(" ");
    // }
    // Serial.println();

    // decode from byte buffer
    uint16_t reading_id = ((uint16_t*)data)[0];        // bytes 0, 1
    uint16_t reading_sequence = ((uint16_t*)data)[1];  // bytes 2, 3
    float lat = ((float*)data)[1];                     // bytes 4, 5, 6, 7
    float lng = ((float*)data)[2];                     // bytes 8, 9, 10, 11

    // display reading
    //if (reading_id != last_reading_id) {
    last_reading_id = reading_id;

    Serial.print(reading_id);
    Serial.print(F("/"));
    Serial.print(reading_sequence);
    Serial.print(F(": "));
    Serial.print(lat, 4);
    Serial.print(F(" "));
    Serial.println(lng, 4);
    //}
  } else if (status != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print(F("radio.receive failed "));
    Serial.println(status);

    // reset radio on any unexpected failure
    status = radio.reset();
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print(F("radio.reset failed "));
      Serial.println(status);
    }

    // re-initialize radio
    initializeRadio();
  }
}