#include "Adafruit_TinyUSB.h"

//#define DEBUG 1

uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD(),
};

Adafruit_USBD_HID usb_hid;


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
  HID_KEY_F1,
  HID_KEY_F2,
  HID_KEY_F3,

  // 2nd row [04 05 06]
  HID_KEY_F4,
  HID_KEY_F5,
  HID_KEY_F6,

  // 3rd row [07 08 09]
  HID_KEY_F7,
  HID_KEY_F8,
  HID_KEY_F9,

  // 4th (bottom) row [10 11 12]
  //  HID_KEY_F3,
  //  HID_KEY_F2,
  //  HID_KEY_F4,
  HID_KEY_A,
  HID_KEY_B,
  HID_KEY_C,
};


// GPIO pins
#define KEYPAD_PIN 3
#define ACTIVITY_PIN 11
#define READY_PIN LED_BUILTIN

// used to remove noise from key readings (bounces & "riding the keys")
unsigned long sumLevel = 0;
unsigned long countLevel = 0;


void setup() {
  Serial.begin(115200);

  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  // make sure 12-bit resolution
  analogReadResolution(12);

  // set pins
  pinMode(KEYPAD_PIN, INPUT);
  pinMode(ACTIVITY_PIN, OUTPUT);
  pinMode(READY_PIN, OUTPUT);
  digitalWrite(READY_PIN, LOW);

  // wait until device mounted
  while ( !USBDevice.mounted() ) delay(1);
}


void loop() {
  // only if connection is ready
  if ( !usb_hid.ready() ) {
    digitalWrite(READY_PIN, LOW);
    return;
  }
  digitalWrite(READY_PIN, HIGH);

  // get reading from keypad
  int pinLevel = analogRead(KEYPAD_PIN);

  // if there is no key pressed, check if transition just happened to trigger "key up"
  if ( pinLevel < 100) {
    // throw out weird readings
    if ( (countLevel > 1000) && (countLevel < 200000) ) {
      // "key up"
      int key = translateToKey(sumLevel / countLevel);
      if ( key > 0 ) {
#ifdef DEBUG
        char kd[64];
        sprintf(kd, "sending keycode 0x%02X", key);
        Serial.println(kd);
#endif

        // show activity
        digitalWrite(ACTIVITY_PIN, LOW);

        // send keycode
        uint8_t keycodes[6] = { 0 };
        keycodes[0] = key;
        usb_hid.keyboardReport(0, 0, keycodes);
        delay(25);
        usb_hid.keyboardRelease(0);
        delay(25);

        // done
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
}

// take an analog pinLevel and return the character to send
uint8_t translateToKey(int pinLevel) {
  if ( pinLevel >= 1910 ) {
    if ( pinLevel <= 1990 ) {
      // 1950 avg
      return keyLookup[11];
    } else if ( pinLevel >= 2006 && pinLevel <= 2086 ) {
      // 2046 avg
      return keyLookup[10];
    } else if ( pinLevel >= 2112 && pinLevel <= 2192 ) {
      // 2152 avg
      return keyLookup[9];
    } else if ( pinLevel >= 2232 && pinLevel <= 2312 ) {
      // 2272 avg
      return keyLookup[8];
    } else if ( pinLevel >= 2369 && pinLevel <= 2449 ) {
      // 2409 avg
      return keyLookup[7];
    } else if ( pinLevel >= 2520 && pinLevel <= 2600 ) {
      // 2560 avg
      return keyLookup[6];
    } else if ( pinLevel >= 2687 && pinLevel <= 2767 ) {
      // 2727 avg
      return keyLookup[5];
    } else if ( pinLevel >= 2881 && pinLevel <= 2961 ) {
      // 2921 avg
      return keyLookup[4];
    } else if ( pinLevel >= 3107 && pinLevel <= 3175 ) {
      // 3147 avg
      return keyLookup[3];
    } else if ( pinLevel >= 3176 && pinLevel <= 3246 ) {
      // 3206 avg
      return keyLookup[2];
    } else if ( pinLevel >= 3675 && pinLevel <= 3755 ) {
      // 3715 avg
      return keyLookup[1];
    } else if ( pinLevel >= 3756 ) {
      // 4084 avg
      return keyLookup[0];
    }
  }
  return 0;
}
