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

uint16_t counter;


void setup() {
  Serial.begin(9600);

  // initialize radio
  int status = radio.begin(434.0, 500.0, 12, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0, false);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio.begin failed "));
    Serial.println(status);
    while (true)
      ;
  }
}

void loop() {
  uint8_t data[2];

  counter = counter + 1;
  if (counter > 9999) {
    counter = 0;
  }
  data[0] = (uint8_t)(counter >> 8);
  data[1] = (uint8_t)(counter);

  int status = radio.transmit(data, sizeof(data));
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print(F("radio.transmit failed "));
    Serial.println(status);
  }

  status = radio.receive(data, sizeof(data));
  if (status == RADIOLIB_ERR_NONE) {
    uint16_t rcvd = ((uint16_t)data[0] << 8) + (uint16_t)data[1];

    if (rcvd != counter) {
      Serial.println(rcvd);
      Serial.print(F(" != "));
      Serial.print(counter);
    }
  } else if (status == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print(counter);
    Serial.println(F(": no response"));
  } else {
    Serial.print(F("radio.receive failed "));
    Serial.println(status);
  }
}