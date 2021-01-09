#define DEBUG 1

// CI-V serial port pin mapping

// RX from Radio -> GPIO 34
#define CIV_RX 34

// TX to Radio not used
#define CIV_TX -1


// bcd pin mapping

// BIT 0 -> GPIO 16
#define BCD_BIT0 16

// BIT 1 -> GPIO 17
#define BCD_BIT1 17

// BIT 2 -> GPIO 33
#define BCD_BIT2 33

// BIT 3 -> GPIO 32
#define BCD_BIT3 32


// band datatype
enum Band {
  BAND_UNKNOWN,
  BAND_160M,
  BAND_80M,
  BAND_60M,
  BAND_40M,
  BAND_30M,
  BAND_20M,
  BAND_17M,
  BAND_15M,
  BAND_12M,
  BAND_10M,
  BAND_6M,

  BAND_COUNT  // must be last, for BCD_BITS array size
};


// bcd bits per band
uint8_t BCD_BITS[BAND_COUNT][4] = {
  {HIGH, HIGH, HIGH, HIGH}, // unknown
  {HIGH, LOW , LOW , LOW }, // 160m
  {LOW , HIGH, LOW , LOW }, // 80m
  {LOW , LOW , LOW , LOW }, // 60m
  {HIGH, HIGH, LOW , LOW }, // 40m
  {LOW , LOW , HIGH, LOW }, // 30m
  {HIGH, LOW , HIGH, LOW }, // 20m
  {LOW , HIGH, HIGH, LOW }, // 17m
  {HIGH, HIGH, HIGH, LOW }, // 15m
  {LOW , LOW , LOW , HIGH}, // 12m
  {HIGH, LOW , LOW , HIGH}, // 10m
  {LOW , HIGH, LOW , HIGH}, // 6m
};


// holding space while ci-v message is being recieved
static uint8_t civMessage[12];


void setup() {
  // serial monitor port
  Serial.begin(115200);

  // ci-v port
  Serial2.begin(9600, SERIAL_8N1, CIV_RX, CIV_TX);

  // bcd pins
  pinMode(BCD_BIT0, OUTPUT);
  pinMode(BCD_BIT1, OUTPUT);
  pinMode(BCD_BIT2, OUTPUT);
  pinMode(BCD_BIT3, OUTPUT);
}

void loop() {
  if ( readCIVMessageFromSerial(Serial2, civMessage, sizeof(civMessage)) ) {
#ifdef DEBUG
    for ( int i = 0; i < sizeof(civMessage); i++ ) {
      char b [4];

      sprintf(b, "%02X", civMessage[i]);
      Serial.print(b);

      if ( civMessage[i] == 0xFD ) {
        break;
      }
    }
    Serial.print(" ");
#endif

    // is it transfer operating frequency data?
    if ( civMessage[10] == 0xFD && civMessage[4] == 0x00 ) {
      char fd [16];

      // radio sends as least significant byte first, flip order of bytes
      sprintf(fd, "%02X%02X%02X%02X%02X", civMessage[9], civMessage[8], civMessage[7], civMessage[6], civMessage[5]);

#ifdef DEBUG
      Serial.print(fd);
      Serial.print(" ");
#endif

      // convert to number
      long freq = atol(fd);

#ifdef DEBUG
      Serial.print(freq);
      Serial.print(" ");
#endif

      // get band
      Band band = bandFromFrequency(freq);
#ifdef DEBUG
      switch ( band ) {
        case BAND_160M:
          Serial.print("160m ");
          break;
        case BAND_80M:
          Serial.print("80m ");
          break;
        case BAND_60M:
          Serial.print("60m ");
          break;
        case BAND_40M:
          Serial.print("40m ");
          break;
        case BAND_30M:
          Serial.print("30m ");
          break;
        case BAND_20M:
          Serial.print("20m ");
          break;
        case BAND_17M:
          Serial.print("17m ");
          break;
        case BAND_15M:
          Serial.print("15m ");
          break;
        case BAND_12M:
          Serial.print("12m ");
          break;
        case BAND_10M:
          Serial.print("10m ");
          break;
        case BAND_6M:
          Serial.print("6m ");
          break;
      }
#endif

      // send band data
      if ( band != BAND_UNKNOWN ) {
#ifdef DEBUG
        char b [8];

        sprintf(b, "%d%d%d%d ", BCD_BITS[band][0], BCD_BITS[band][1], BCD_BITS[band][2], BCD_BITS[band][3]);
        Serial.print(b);
#endif

        digitalWrite(BCD_BIT0, BCD_BITS[band][0]);
        digitalWrite(BCD_BIT1, BCD_BITS[band][1]);
        digitalWrite(BCD_BIT2, BCD_BITS[band][2]);
        digitalWrite(BCD_BIT3, BCD_BITS[band][3]);
#ifdef DEBUG
      } else {
        Serial.print("band unknown");
#endif
      }
#ifdef DEBUG
    } else {
      Serial.print("not transfer operating frequency");
#endif
    }
#ifdef DEBUG
    Serial.println();
#endif
  }
}

// return band corresponding to frequency
Band bandFromFrequency(long freq) {
  if ( freq >= 1800000 && freq <= 2000000 ) {
    return BAND_160M;
  } else if ( freq >= 3500000 && freq <= 4000000 ) {
    return BAND_80M;
  } else if ( freq >= 5250000 && freq <= 5500000 ) {
    return BAND_60M;
  } else if ( freq >= 7000000 && freq <= 7300000 ) {
    return BAND_40M;
  } else if ( freq >= 10100000 && freq <= 10150000 ) {
    return BAND_30M;
  } else if ( freq >= 14000000 && freq <= 14350000 ) {
    return BAND_20M;
  } else if ( freq >= 18068000 && freq <= 18168000 ) {
    return BAND_17M;
  } else if ( freq >= 21000000 && freq <= 21450000 ) {
    return BAND_15M;
  } else if ( freq >= 24890000 && freq <= 24990000 ) {
    return BAND_12M;
  } else if ( freq >= 28000000 && freq <= 29700000 ) {
    return BAND_10M;
  } else if ( freq >= 50000000 && freq <= 54000000 ) {
    return BAND_6M;
  }

  // invalid
  return BAND_UNKNOWN;
}

// read ci-v message, returns true when a complete message has been copied into buffer
bool readCIVMessageFromSerial(HardwareSerial p, uint8_t* buffer, int len) {
  static int pos = 0;
  uint8_t b;


  // read byte, if available
  b = p.read();
  if ( b != 0xFF ) {
    // clear destination at start of new message
    if ( pos == 0 ) {
      for ( int i = 0; i < len; i++ ) {
        buffer[i] = 0;
      }
    }

    // accumulate message bytes
    if ( pos < len ) {
      buffer[pos++] = b;
    }

    // complete message?
    if ( b == 0xFD ) {
      pos = 0;
      return true;
    }
  }

  return false;
}
