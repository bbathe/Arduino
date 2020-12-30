#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <esp_task_wdt.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1327.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "secrets.h"


// wifi network
const char* wifiSSID = SECRET_SSID;
const char* wifiPwd = SECRET_PASS;

// network time protocol server
const char* ntpServer = "pool.ntp.org";

// real time clock so we are't slamming the ntp server
RTC_DS3231 rtc;

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

// how often to update rtc from ntp, in seconds
#define EXPIRES_RTCFROMNTP (24 * 60 * 60 / 3)

// track when to update rtc from ntp
int rtcExpiration = EXPIRES_RTCFROMNTP + 1;

// in error state?
volatile bool err = false;

// guard access to the RTC
SemaphoreHandle_t xRTCMutex = NULL;

// baseline on display
#define BASE_LINE 31



void setup()
{
  Serial.begin(115200);

  // join i2c bus
  Wire.begin();

  // initialize rtc
  if ( !rtc.begin() ) {
    Serial.println(F("Couldn't find RTC"));

    // stall here
    while ( 1 ) yield();
  }

  // initialize oled display
  if ( !display.begin() ) {
    Serial.println(F("Display allocation failed"));

    // stall here
    while ( 1 ) yield();
  }
  display.setRotation(2);

  // create RTC mutex semaphore
  xRTCMutex = xSemaphoreCreateMutex();
  if ( xRTCMutex != NULL ) {
    // seperate task for updating the display
    xTaskCreatePinnedToCore(cpu0Loop, "DisplayClock", 8192, (void*)NULL, 1, NULL, 0);
  } else {
    Serial.println(F("RTC mutex create failed"));

    // stall here
    while ( 1 ) yield();
  }
}

// cpu 0 is responsible for displaying the current time from the RTC
void cpu0Loop(void* p)
{
  int lastMinute = 99;

  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for ( ;; ) {
    if ( xSemaphoreTake(xRTCMutex, portMAX_DELAY) == pdTRUE ) {
      // only update the display if the minute value has changed
      DateTime now = rtc.now();
      int m = now.minute();
      if ( lastMinute != m ) {
        // update
        showTime(err);

        // ready for next check
        lastMinute = m;
      }

      xSemaphoreGive(xRTCMutex);
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// cpu 1 is responsible for keeping the RTC in-sync with NTP
void loop()
{
  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for ( ;; ) {
    // update rtc from ntp?
    if ( rtcExpiration > EXPIRES_RTCFROMNTP ) {
      if ( xSemaphoreTake(xRTCMutex, portMAX_DELAY) == pdTRUE ) {
        if ( setRTCFromNTP() ) {
          // clear error status
          err = false;

          // reset
          rtcExpiration = 0;
        } else {
          // failed error status
          err = true;

          // don't reset rtcExpiration so we will retry next time thru
        }

        xSemaphoreGive(xRTCMutex);
      }
    }

    // track when to update rtc from ntp
    rtcExpiration++;

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}


// update the RTC from NTP
// returns true if update was successful
bool setRTCFromNTP()
{
  struct tm t;

  // connect to wifi to reach ntp server
  WiFi.begin(wifiSSID, wifiPwd);
  WiFi.setHostname("stnclk");
  int tries = 0;
  while ( WiFi.status() != WL_CONNECTED ) {
    tries++;
    if ( tries > 10 ) {
      WiFi.disconnect(true);
      Serial.println(F("Can't get WiFi connection"));
      return false;
    }
    delay(200);
  }

  // setup time via ntp server
  configTime(0, 0, ntpServer);

  // get current date/time from ntp
  if ( !getLocalTime(&t) ) {
    WiFi.disconnect(true);
    Serial.println(F("Failed to obtain time"));
    return false;
  }

  // update rtc date/time
  rtc.adjust(DateTime(t.tm_year % 100, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec));

  // don't keep persistent connection
  WiFi.disconnect(true);

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

// draw current date & time on the screen
void showTime(bool err)
{
  const static char errorMessage[] PROGMEM = "ERROR";
  const static char* monthNames[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char b[16];


  display.clearDisplay();
  display.setTextColor(SSD1327_WHITE);

  if ( err ) {
    display.setFont(&FreeSans9pt7b);
    drawStringCenter(errorMessage, 20);
  }

  DateTime now = rtc.now();

  // draw time
  display.setFont(&FreeSansBold24pt7b);
  sprintf(b, "%02d:%02d", now.hour(), now.minute());
  drawStringCenter(b, 36 + BASE_LINE);

  // draw date
  display.setFont(&FreeSans9pt7b);
  sprintf(b, "%s. %d, %d", monthNames[now.month()], now.day(), now.year());
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
