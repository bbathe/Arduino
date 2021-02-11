#include "esp_sleep.h"
#include <BleKeyboard.h>

/*
  keypad mapping

  keypad # -> character to send

  3x4 keypad
  01 02 03
  04 05 06
  07 08 09
  10 11 12
*/
#define KEYPAD_KEY_COUNT 12

uint8_t keyLookup[KEYPAD_KEY_COUNT] = {
  // 1st (top) row [01 02 03]
  KEY_F1,
  KEY_F2,
  KEY_F3,

  // 2nd row [04 05 06]
  KEY_F4,
  KEY_F5,
  KEY_F6,

  // 3rd row [07 08 09]
  KEY_F7,
  KEY_F8,
  KEY_F9,

  // 4th (bottom) row [10 11 12]
  //KEY_F10,
  //KEY_F11,
  //KEY_F12,
  KEY_F3,
  KEY_F2,
  KEY_F4,
};

// bluetooth connection
BleKeyboard bleKeyboard("contester-fpad", "bbathe", 100);

// GPIO pins
#define KEYPAD_PIN 34
#define ACTIVITY_PIN 32

// used to remove noise from key readings (bounces & "riding the keys")
unsigned long sumLevel = 0;
unsigned long countLevel = 0;


void setup() {
  Serial.begin(115200);

  // make sure 12-bit resolution
  analogReadResolution(12);

  // set pins
  pinMode(KEYPAD_PIN, INPUT);
  pinMode(ACTIVITY_PIN, OUTPUT);

  // start bluetooth keyboard to send keystrokes to computer
  bleKeyboard.begin();
}


void loop() {
  // wait until bluetooth connection is good
  if ( bleKeyboard.isConnected() ) {
    digitalWrite(ACTIVITY_PIN, HIGH);

    // get key press from keypad
    int pinLevel = analogRead(KEYPAD_PIN);

    // if there is no key pressed, check if transition just happened to trigger "key up"
    if ( pinLevel == 0 ) {
      // throw out weird readings
      if ( (countLevel > 1000) && (countLevel < 200000) ) {
        // "key up"
        int key = translateToKey(sumLevel / countLevel);
        if ( key > 0 ) {
          char kd[16];
          sprintf(kd, "0x%02X", key);
          Serial.println(kd);

          // send character and show activity
          digitalWrite(ACTIVITY_PIN, LOW);

          bleKeyboard.write(key);

          delay(50);
          digitalWrite(ACTIVITY_PIN, HIGH);
        }
      }

      // reset key reading
      sumLevel = 0;
      countLevel = 0;
    } else {
      // for calculating what key is pressed
      sumLevel += pinLevel;
      countLevel++;
    }
  } else {
    // show some activity so we don't look dead
    digitalWrite(ACTIVITY_PIN, HIGH);
    delay(50);
    digitalWrite(ACTIVITY_PIN, LOW);
    delay(3000);
  }
}

// take an analog pinLevel and return the character to send
uint8_t translateToKey(int pinLevel) {
  if ( pinLevel >= 1706 ) {
    if ( pinLevel <= 1786 ) {
      // 1746 avg
      return keyLookup[11];
    } else if ( pinLevel >= 1800 && pinLevel <= 1880 ) {
      // 1840 avg
      return keyLookup[10];
    } else if ( pinLevel >= 1904 && pinLevel <= 1984 ) {
      // 1944 avg
      return keyLookup[9];
    } else if ( pinLevel >= 2022 && pinLevel <= 2102 ) {
      // 2062 avg
      return keyLookup[8];
    } else if ( pinLevel >= 2150 && pinLevel <= 2230 ) {
      // 2190 avg
      return keyLookup[7];
    } else if ( pinLevel >= 2296 && pinLevel <= 2376 ) {
      // 2336 avg
      return keyLookup[6];
    } else if ( pinLevel >= 2459 && pinLevel <= 2539 ) {
      // 2499 avg
      return keyLookup[5];
    } else if ( pinLevel >= 2655 && pinLevel <= 2735 ) {
      // 2695 avg
      return keyLookup[4];
    } else if ( pinLevel >= 2889 && pinLevel <= 2969 ) {
      // 2929 avg
      return keyLookup[3];
    } else if ( pinLevel >= 3209 && pinLevel <= 3289 ) {
      // 3249 avg
      return keyLookup[2];
    } else if ( pinLevel >= 3702 && pinLevel <= 3782 ) {
      // 3742 avg
      return keyLookup[1];
    } else if ( pinLevel >= 4054 ) {
      // 4094 avg
      return keyLookup[0];
    }
  }
  return 0;
}
