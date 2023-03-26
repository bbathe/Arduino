#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <TinyGPS++.h>

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On a Seeduino Xiao: 4(SDA), 5(SCL)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


TinyGPSPlus gps;

// update display based on smallest unit shown
byte last_minute;

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  // initialize oled display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));

    // stall
    for (;;)
      ;
  }
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  while (Serial1.available() > 0)
    gps.encode(Serial1.read());

  if (gps.time.isUpdated() && gps.time.isValid() && gps.date.isValid()) {
    byte minute = gps.time.minute();

    // update display?
    if (last_minute != minute) {
      last_minute = minute;

      showDateTime(gps.date, gps.time);
    }
  }

  if (gps.time.age() > 1500 || gps.date.age() > 1500) {
    showString("GPS LOST FIX");
  }
}

// draw string centered across the display
void drawStringCenter(const char* buf, const int y) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;


  // get width of string
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  // set cursor so text will be centered
  display.setCursor((display.width() / 2) - (w / 2), y);

  display.print(buf);
}

// draw current date & time on the screen
void showDateTime(TinyGPSDate date, TinyGPSTime time) {
  const static char* monthNames[] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  char b[64];


  display.clearDisplay();

  // draw time
  display.setFont(&FreeSansBold24pt7b);
  sprintf(b, "%02d:%02d", time.hour(), time.minute());
  drawStringCenter(b, 40);

  // draw date
  display.setFont(&FreeSans9pt7b);
  sprintf(b, "%s. %d, %d", monthNames[date.month()], date.day(), date.year());
  drawStringCenter(b, 60);

  display.display();
}

// draw string on the screen
void showString(const char* message) {
  display.clearDisplay();

  // draw string
  display.setFont(&FreeSans9pt7b);
  drawStringCenter(message, 36);

  display.display();
}
