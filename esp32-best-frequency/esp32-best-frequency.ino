#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <TM1637Display.h>
#include "secrets.h"

// sharing pins with multiple displays
// https://esphome.io/components/display/tm1637.html#connect-multiple-displays
TM1637Display display0 = TM1637Display(23, 32);
TM1637Display display1 = TM1637Display(32, 33);
TM1637Display display2 = TM1637Display(33, 23);

const uint8_t err[] = {
  0,                                      // blank
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,  // E
  SEG_E | SEG_G,                          // r
  SEG_E | SEG_G,                          // r
};

const uint8_t dash[] = {
  SEG_G,  // -
  SEG_G,  // -
  SEG_G,  // -
  SEG_G,  // -
};

// return segments to display on 7 segment for course
// refactored from TinyGPS++ cardinal method
const uint8_t* cardinalCourse7Segment(double course) {
  static const uint8_t directions[][4] = {
    { 0, 0, 0, SEG_C | SEG_E | SEG_G },                                                                                                          // n
    { 0, SEG_C | SEG_E | SEG_G, SEG_C | SEG_E | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                  // nnE
    { 0, 0, SEG_C | SEG_E | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                                      // nE
    { 0, 0, 0, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                                                          // E
    { 0, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                  // ESE
    { 0, 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                                                      // SE
    { 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G },                  // SSE
    { 0, 0, 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G },                                                                                          // S
    { 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },          // SSW
    { 0, 0, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                                              // SW
    { 0, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },  // WSW
    { 0, 0, 0, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                                                                                  // W
    { 0, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, SEG_C | SEG_E | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                  // WnW
    { 0, 0, SEG_C | SEG_E | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G },                                                              // nW
    { 0, SEG_C | SEG_E | SEG_G, SEG_C | SEG_E | SEG_G, SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G }                                           // nnW
  };

  int direction = (int)((course + 11.25f) / 22.5f);
  return directions[direction % 16];
}

// guard access to the displays
SemaphoreHandle_t xDisplayMutex;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  // displays
  display0.clear();
  display0.setBrightness(0);
  display0.setSegments(dash);
  display1.clear();
  display1.setBrightness(0);
  display1.setSegments(dash);
  display2.clear();
  display2.setBrightness(0);
  display2.setSegments(dash);

  // create mutexes
  xDisplayMutex = xSemaphoreCreateMutex();
  if (xDisplayMutex == NULL) {
    Serial.println(F("display mutex create failed"));
    display0.setSegments(err);
    display1.setSegments(err);
    display2.setSegments(err);
    while (1) yield();
  }

  // wifi
  WiFi.disconnect();
  WiFi.useStaticBuffers(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // tasks
  esp_task_wdt_init(10, true);
  xTaskCreatePinnedToCore(cpu0Loop, "BestFreq", 8192, (void*)NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(cpu1Loop, "MUF", 8192, (void*)NULL, 1, NULL, 1);
}

// display.setSegments protected by xDisplayMutex
void setDisplaySegments(TM1637Display display, const uint8_t segments[]) {
  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
    display.setSegments(segments);
    xSemaphoreGive(xDisplayMutex);
  }
}

// display.showNumberDec protected by xDisplayMutex
void setDisplayShowNumberDec(TM1637Display display, int num) {
  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
    display.showNumberDec(num);
    xSemaphoreGive(xDisplayMutex);
  }
}

// display.setBrightness protected by xDisplayMutex
void setDisplaySetBrightness(TM1637Display display, uint8_t brightness) {
  if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
    display.setBrightness(brightness);
    xSemaphoreGive(xDisplayMutex);
  }
}

double course = 0;

void loop() {
  course++;
  if (course >= 360.0) {
    course = 0;
  }

  setDisplaySegments(display2, cardinalCourse7Segment(course));

  delay(1000);
}



unsigned long nextMillis0 = 0;
int lastBestFreq = 0;

void cpu0Loop(void* p) {
  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    bool rampDisplay = false;

    if ((nextMillis0 == 0) || (millis() - nextMillis0 > 300000)) {
      nextMillis0 = millis();

      HTTPClient http;
      http.setTimeout(5000);

      String url = String("https://pskreporter.info/cgi-bin/psk-freq.pl?grid=");
      url += SECRET_GRID_SQUARE_4;
      http.begin(url);

      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        //  best freq parsing
        char* pLine;
        char* pField;
        int bestFreq;
        for (char* line = strtok_r(payload.begin(), "\n", &pLine); line != NULL; line = strtok_r(NULL, "\n", &pLine)) {
          char* field = strtok_r(line, " ", &pField);
          if (field != NULL) {
            char b[9];
            sprintf(b, "%08s", field);

            b[2] = '\0';
            Serial.printf("best freq: %s\n", b);
            String fstr = String(b);

            bestFreq = fstr.toInt();
            break;
          }
        }

        if (bestFreq != lastBestFreq) {
          lastBestFreq = bestFreq;
          rampDisplay = true;

          setDisplaySetBrightness(display0, 7);
          setDisplayShowNumberDec(display0, lastBestFreq);
        }

      } else {
        setDisplaySegments(display0, err);
        Serial.printf("httpCode %d: %s\n", httpCode, http.errorToString(httpCode).c_str());
      }

      http.end();
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (rampDisplay) {
      setDisplaySetBrightness(display0, 0);
      setDisplayShowNumberDec(display0, lastBestFreq);
    }
  }
}

unsigned long nextMillis1 = 0;
int lastMuf = 0;

void cpu1Loop(void* p) {
  // subscribe task to watchdog timer
  esp_task_wdt_add(NULL);

  for (;;) {
    bool rampDisplay = false;

    if ((nextMillis1 == 0) || (millis() - nextMillis1 > 300000)) {
      nextMillis1 = millis();

      HTTPClient http;
      http.setTimeout(5000);

      String url = String("https://prop.kc2g.com/api/ptp.json?from_grid=");
      url += SECRET_GRID_SQUARE_6;
      url += "&to_grid=";
      url += SECRET_GRID_SQUARE_6;
      http.begin(url);

      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        // get current muf_lp
        String f = payload.substring(payload.indexOf("muf_lp"));
        f = f.substring(8, f.indexOf("."));
        Serial.printf("current muf: %d\n", f.toInt());

        int Muf = f.toInt();
        if (Muf != lastMuf) {
          lastMuf = Muf;
          rampDisplay = true;
          setDisplaySetBrightness(display1, 7);
          setDisplayShowNumberDec(display1, lastMuf);
        }

      } else {
        setDisplaySegments(display1, err);
        Serial.printf("httpCode %d: %s\n", httpCode, http.errorToString(httpCode).c_str());
      }

      http.end();
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (rampDisplay) {
      setDisplaySetBrightness(display1, 0);
      setDisplayShowNumberDec(display1, lastMuf);
    }
  }
}