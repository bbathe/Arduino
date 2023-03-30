#include <TM1637Display.h>
#include <TinyGPS++.h>

#define CLK 2
#define DIO 3

// create a display object of type TM1637Display
TM1637Display display = TM1637Display(CLK, DIO);

const uint8_t err[] = {
  0,                                      // blank
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,  // E
  SEG_E | SEG_G,                          // r
  SEG_E | SEG_G,                          // r
};


TinyGPSPlus gps;

// update display based on smallest unit shown
byte last_minute;

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  // initialize 7 segment display
  display.clear();
  display.setBrightness(0);
}

void loop() {
  while (Serial1.available() > 0)
    gps.encode(Serial1.read());

  if (gps.time.isUpdated() && gps.time.isValid() && gps.date.isValid()) {
    byte minute = gps.time.minute();

    // update display?
    if (last_minute != minute) {
      last_minute = minute;

      display.showNumberDecEx(gps.time.hour() * 100 + gps.time.minute(), 0b01000000, true);
    }
  }

  if (gps.time.age() > 1500 || gps.date.age() > 1500) {
    display.setSegments(err);
  }
}