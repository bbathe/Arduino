#include <WiFi.h>
#include <DS3231.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1327.h>
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
int rtcExpiration = EXPIRES_RTCFROMNTP + 1;

// ESP32 to OLED SPI
//
// VCC of OLED -> Vcc
// GND of OLED -> GND
// DIN of OLED -> IO23 (MOSI)
// CLK of OLED -> IO18 (SCK)
// CS of OLED -> IO5 (SS)
// DC of OLED -> IO25
// RST of OLED -> N/A

#define OLED_DC    25
#define OLED_CS    5
#define OLED_RESET -1
Adafruit_SSD1327 display(128, 128, &SPI, OLED_DC, OLED_RESET, OLED_CS);


void setup()
{
  Serial.begin(115200);

  // join i2c bus
  Wire.begin();

  // initialize oled display
  if ( !display.begin() ) {
    Serial.println(F("display allocation failed"));

    // stall here
    while ( 1 ) yield();
  }

  display.setRotation(2);
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
  WiFi.setHostname("stnclk");
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
  display.getTextBounds(buf, 0, y, &x1, &y1, &w, &h);

  // set cursor so text will be centered
  display.setCursor((display.width() / 2) - (w / 2) - (w % 2) - x1, y);

  display.print(buf);
}

#define BASE_LINE 31

// draw current date & time on the screen
void showTime(bool err)
{
  const static char errorMessage[] PROGMEM = "ERROR";
  const static char* monthNames[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  bool century = false;
  bool h12Flag;
  bool pmFlag;
  char b[16];


  display.clearDisplay();
  display.setTextColor(SSD1327_WHITE);

  if ( err ) {
    display.setFont(&FreeSans9pt7b);
    drawStringCenter(errorMessage, 20);
  }

  // draw time
  display.setFont(&FreeSansBold24pt7b);
  sprintf(b, "%02d:%02d", rtc.getHour(h12Flag, pmFlag), rtc.getMinute());
  drawStringCenter(b, 36 + BASE_LINE);

  // draw date
  display.setFont(&FreeSans9pt7b);
  sprintf(b, "%s. %d, %d", monthNames[rtc.getMonth(century)], rtc.getDate(), rtc.getYear() + 2000);
  drawStringCenter(b, 60 + BASE_LINE);

  if ( err ) {
    display.setFont(&FreeSans9pt7b);
    drawStringCenter(errorMessage, 119);
  }
  
  display.display();
}

//// draw test date & time on the screen
//void showTime(bool err)
//{
//  display.clearDisplay();
//  display.setTextColor(SSD1327_WHITE);
//
//  // draw a grid so we can see what going on
//  for ( int x = 7; x < display.width(); x += 7 ) {
//   display.drawLine(x, 0, x, display.height(), SSD1327_WHITE);
//  }
//  for ( int y = 7; y < display.height(); y += 7 ) {
//    display.drawLine(0, y, display.width(), y, SSD1327_WHITE);
//  }
//  display.drawRect(0, 0, display.width(), display.height(), SSD1327_WHITE);
//
//  // draw mock strings
//  display.setFont(&FreeSans9pt7b);
//  drawStringCenter("XXXXX", 20);
//
//  display.setFont(&FreeSansBold24pt7b);
//  drawStringCenter("88:88", 36 + BASE_LINE);
//
//  display.setFont(&FreeSans9pt7b);
//  drawStringCenter("XXX. 88, 8888", 60 + BASE_LINE);
//
//  display.setFont(&FreeSans9pt7b);
//  drawStringCenter("XXXXX", 119);
//
// 
//  display.display();
//}
