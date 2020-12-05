#include <WiFi.h>
#include <DS3231.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "time.h"
#include "secrets.h"


// wifi network
const char* wifiSSID = SECRET_SSID;
const char* wifiPwd = SECRET_PASS;

// network time protocol server
const char* ntpServer = "pool.ntp.org";

// real time clock so we are't slamming the ntp server
DS3231 rtc;

// how often to update rtc with ntp, in seconds
#define EXPIRES_RTCFROMNTP (24 * 60 * 60 / 3)
int rtcExpiration = 0;

// oled display size
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ssd1306 display connected to i2c
#define OLED_RESET 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


void setup()
{
  Serial.begin(115200);

  // join i2c bus
  Wire.begin();

  // initialize oled display
  if ( !display.begin(SSD1306_SWITCHCAPVCC, 0x3C) ) {
    Serial.println(F("SSD1306 allocation failed"));

    // stall here
    for (;;);
  }

  //display.setRotation(2);
}

void loop()
{
  bool err = false;

  // rtc in valid state?
  if ( !isRTCValid() ) {
    // force rtc to be set from ntp
    rtcExpiration = EXPIRES_RTCFROMNTP + 1;
  }

  // update rtc from ntp?
  if ( rtcExpiration > EXPIRES_RTCFROMNTP ) {
    if ( setRTCFromNTP() ) {
      // only reset on success
      // we will retry next time thru if there was an error
      rtcExpiration = 0;
    } else {
      // show error status on display
      err = true;
    }
  }

  // update screen
  showTime(err);

  // track when to update rtc from ntp
  rtcExpiration++;

  // wait
  delay(1000);
}

// update the RTC from NTP
bool setRTCFromNTP()
{
  struct tm t;

  // connect to wifi to reach ntp server
  int tries;
  WiFi.begin(wifiSSID, wifiPwd);
  while ( WiFi.status() != WL_CONNECTED ) {
    tries++;
    if ( tries > 30 ) {
      WiFi.disconnect(true);
      Serial.println(F("Can't get WiFi connection"));
      return false;
    }
    delay(333);
  }

  // setup time with ntp server
  configTime(0, 0, ntpServer);

  // get current date/time from ntp
  if ( !getLocalTime(&t) ) {
    WiFi.disconnect(true);
    Serial.println(F("Failed to obtain time"));
    return false;
  }

  // update rtc date/time
  rtc.setClockMode(false);
  rtc.setYear(t.tm_year % 100);
  rtc.setMonth(t.tm_mon + 1);
  rtc.setDate(t.tm_mday);
  rtc.setHour(t.tm_hour);
  rtc.setMinute(t.tm_min);
  rtc.setSecond(t.tm_sec);

  // don't keep persistent connection
  WiFi.disconnect(true);

  // return based on whether rtc is now valid
  if ( isRTCValid() ) {
    return true;
  }

  Serial.println(F("RTC invalid"));
  return false;
}

// return true if RTC appears to be in a valid state
bool isRTCValid() {
  if ( rtc.getDate() > 31 ) {
    return false;
  }
  return true;
}

// draw string centered across the display
void drawStringCenter(const char *buf, const int y)
{
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
void showTime(bool err)
{
  const static char* monthNames[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  bool century = false;
  bool h12Flag;
  bool pmFlag;
  char b[16];


  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // draw time
  display.setFont(&FreeSansBold24pt7b);
  sprintf(b, "%02d:%02d", rtc.getHour(h12Flag, pmFlag), rtc.getMinute());
  drawStringCenter(b, 36);

  // draw date
  display.setFont(&FreeSans9pt7b);
  sprintf(b, "%s %d, %d", monthNames[rtc.getMonth(century)], rtc.getDate(), rtc.getYear() + 2000);
  drawStringCenter(b, 60);

  // invert based on whether we are in error state
  display.invertDisplay(err);

  display.display();
}
