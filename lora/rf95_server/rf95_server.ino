#include <SPI.h>
#include <RH_RF95.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);

// singleton instance of the radio driver
RH_RF95 rf95;


void setup()
{
  Serial.begin(115200);
  while (!Serial);

  // init lora shield
  if (!rf95.init()) {
    Serial.println("init failed");
  }

  rf95.setFrequency(915.0);
  rf95.setModemConfig(rf95.Bw125Cr48Sf4096);
  rf95.setTxPower(23);

  // init lcd
  lcd.init();
  lcd.clear();
  lcd.backlight();
}

void loop()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("listening");

  // wait for a message
  if (rf95.available()) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);

    // get message
    if (rf95.recv(buf, &len)) {
      // send acknowledgement
      uint8_t data[RH_RF95_MAX_MESSAGE_LEN];
      int n = sprintf(data, "ACK %d %d", rf95.lastRssi(), rf95.lastSNR());
      rf95.send(data, n + 1);

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("send ack");

      lcd.setCursor(0, 1);
      lcd.print((char*)data);
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("recv failed");
    }
  }

  delay(2000);
}
