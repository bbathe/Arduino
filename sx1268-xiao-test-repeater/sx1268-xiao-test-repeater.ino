#include <RadioLib.h>
#include <TM1637Display.h>

// SX1268 to Xiao SMAD21 connections
// MOSI pin: 10
// MISO pin:  9
// SCK pin:   8
// NSS pin:   3
// DIO1 pin:  1
// RST pin:   5
// BUSY pin:  6
SX1268 radio = new Module(3, 1, 5, 6);

// TM1637 to Xiao SMAD21 connections
// CLK pin: 2
// DIO pin: 4
TM1637Display display = TM1637Display(2, 4);

static const PROGMEM uint8_t err[] = {
  0,                                      // blank
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,  // E
  SEG_E | SEG_G,                          // r
  SEG_E | SEG_G,                          // r
};

static const PROGMEM uint8_t dash[] = {
  SEG_G,  // -
  SEG_G,  // -
  SEG_G,  // -
  SEG_G,  // -
};


void setup() {
  Serial.begin(9600);

  // initialize 7 segment display
  display.clear();
  display.setBrightness(0);
  display.setSegments(dash);

  // initialize radio
  int status = radio.begin(434.0, 500.0, 12, 8, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0, false);
  if (status != RADIOLIB_ERR_NONE) {
    display.setSegments(err);
    Serial.print(F("radio.begin failed "));
    Serial.println(status);
    while (true)
      ;
  }
}

void loop() {
  uint8_t data[2];

  int status = radio.receive(data, sizeof(data));
  if (status == RADIOLIB_ERR_NONE) {
    uint16_t counter = ((uint16_t)data[0] << 8) + (uint16_t)data[1];

    display.showNumberDec(counter);

    // response
    status = radio.transmit(data, sizeof(data));
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print(F("radio.transmit failed "));
      Serial.println(status);
    }
  } else if (status != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print(F("radio.receive failed "));
    Serial.println(status);
  }
}