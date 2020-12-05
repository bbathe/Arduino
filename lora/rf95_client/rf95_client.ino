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
    Serial.println("rf95 init failed");
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
  lcd.print("sending request");

  uint8_t data[] = "HELO";
  rf95.send(data, sizeof(data));
  rf95.waitPacketSent();

  // wait for a reply
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);
  if (rf95.waitAvailableTimeout(3000)) {
    // get reply message
    if (rf95.recv(buf, &len)) {
      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("recvd response");

      lcd.setCursor(0, 1);
      lcd.print((char*)buf);
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("recv failed");
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("no recv");
  }
  delay(2000);
}
