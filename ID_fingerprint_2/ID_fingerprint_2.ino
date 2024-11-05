#include <Adafruit_Fingerprint.h>

// Korišćenje hardverskog serijskog porta na ESP32
#define RX_PIN 16  // Poveži RX senzora na GPIO16
#define TX_PIN 17  // Poveži TX senzora na GPIO17

HardwareSerial mySerial(2); // Koristi Serial2 za komunikaciju sa senzorom
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

bool noFingerMessageShown = false; // Flag za prikazivanje poruke "No finger detected"

void setup() {
  Serial.begin(115200); // Glavni serijski za debagovanje
  delay(100);

  // Inicijalizacija serijskog porta za senzor
  mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);
  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }

  Serial.println(F("Reading sensor parameters"));
  finger.getParameters();
  Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);

  finger.getTemplateCount();

  if (finger.templateCount == 0) {
    Serial.print("Sensor doesn't contain any fingerprint data. Please run the 'enroll' example.");
  }
  else {
    Serial.println("Waiting for valid finger...");
    Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
  }
}

void loop() {
  getFingerprintID();
  delay(50); // Ne mora da se izvršava punom brzinom
}

// Funkcija za detekciju otiska prsta i prikaz poruke dobrodošlice
void getFingerprintID() {
  uint8_t p = finger.getImage();

  if (p == FINGERPRINT_NOFINGER) {
    if (!noFingerMessageShown) {
      Serial.println("No finger detected");
      noFingerMessageShown = true; // Postavi flag da je poruka prikazana
    } else {
      Serial.print("."); // Prikaži tačkicu kada nema otiska, bez ponavljanja poruke
    }
    return;
  }

  // Ako je otisak detektovan, resetuj flag
  noFingerMessageShown = false;
  Serial.println(); // Novi red za čišćenje tačkica

  // Ako je otisak uspešno očitan
  if (p == FINGERPRINT_OK) {
    Serial.println("Image taken");
  } else {
    Serial.println("Error reading fingerprint");
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Could not convert image. Try again.");
    return;
  }

  // Pretražuje bazu podataka za odgovarajući otisak
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("Welcome ID#"); Serial.println(finger.fingerID);  // Prikaži ID pronađenog otiska
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Did not find a match");
  } else {
    Serial.println("Error searching database");
  }
}
