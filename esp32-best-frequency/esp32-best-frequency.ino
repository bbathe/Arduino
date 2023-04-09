#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <TM1637Display.h>
#include "secrets.h"

// sharing pins with multiple displays
// https://esphome.io/components/display/tm1637.html#connect-multiple-displays
TM1637Display display0 = TM1637Display(32, 33);
TM1637Display display1 = TM1637Display(33, 32);

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

void loop() {}



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
          display0.setBrightness(7);
          display0.showNumberDec(lastBestFreq);
        }

      } else {
        display0.setSegments(err);
        Serial.printf("httpCode %d: %s\n", httpCode, http.errorToString(httpCode).c_str());
      }

      http.end();
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (rampDisplay) {
      display0.setBrightness(0);
      display0.showNumberDec(lastBestFreq);
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
          display1.setBrightness(7);
          display1.showNumberDec(lastMuf);
        }

      } else {
        display1.setSegments(err);
        Serial.printf("httpCode %d: %s\n", httpCode, http.errorToString(httpCode).c_str());
      }

      http.end();
    }

    // done for a while
    esp_task_wdt_reset();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (rampDisplay) {
      display1.setBrightness(0);
      display1.showNumberDec(lastMuf);
    }
  }
}