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

    listOccupiedIDs();

}

void loop() {
  Serial.println("Enter an ID to check/enroll (e.g., '11' to enroll or '11--' to delete):");
  String input = readString();

  // Provera da li unos sadrži "--" za brisanje
  if (input.endsWith("--")) {
    int id = input.substring(0, input.length() - 2).toInt(); // Ukloni "--" i konvertuj u broj
    if (deleteFingerprint(id)) {
      Serial.print("Fingerprint ID #"); Serial.print(id); Serial.println(" has been deleted.");
    } else {
      Serial.print("Failed to delete fingerprint ID #"); Serial.println(id);
    }
  } else {
    int id = input.toInt();
    if (id == 0) { // ID #0 nije dozvoljen
      Serial.println("ID #0 is not allowed. Try again.");
      return;
    }

    // Proveri da li je ID zauzet
    if (checkIDAvailability(id)) {
      Serial.print("ID "); Serial.print(id); Serial.println(" is available.");

      // Proveri da li otisak već postoji pre nego što započneš snimanje
      if (checkFingerprint()) {
        Serial.println("This fingerprint already exists in the database. Enrollment aborted.");
      } else {
        Serial.print("Enrolling new fingerprint as ID #"); Serial.println(id);

        // Pokreni snimanje otiska
        while (!getFingerprintEnroll(id));
      }
    } else {
      Serial.print("ID "); Serial.print(id); Serial.println(" is already occupied. Try a different ID.");
    }
  }
  
  delay(5000); // Čekaj 5 sekundi pre sledeće provere
}

// Funkcija za unos ID-a kao string
String readString() {
  String input = "";
  while (Serial.available() == 0); // Čekaj dok se ne unese podatak
  input = Serial.readStringUntil('\n'); // Pročitaj unos do kraja linije
  input.trim(); // Ukloni praznine sa početka i kraja
  return input;
}

// Funkcija za brisanje otiska sa određenim ID-om
bool deleteFingerprint(int id) {
  int p = finger.deleteModel(id);
  return (p == FINGERPRINT_OK);  // Vraća true ako je brisanje uspešno
}

// Funkcija za proveru dostupnosti ID-a
bool checkIDAvailability(uint8_t id) {
  int p = finger.loadModel(id);  // Pokušaj da učitaš otisak sa zadatim ID-om
  return (p != FINGERPRINT_OK);  // Ako učitavanje nije uspelo, ID je slobodan
}

// Funkcija za proveru postojanja otiska u bazi
bool checkFingerprint() {
  int p = -1;

  // Pokušaj da skeniraš otisak sve dok ne uspe
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p != FINGERPRINT_OK) {
      if (!noFingerMessageShown) {
        Serial.println("No valid finger detected. Try again.");
        noFingerMessageShown = true;
      } else {
        Serial.print("."); // Prikaži tačkicu kada nema otiska, bez ponavljanja poruke
      }
      delay(1000);
    }
  }

  // Resetovanje flag-a kada se otisak detektuje
  noFingerMessageShown = false;
  Serial.println(); // Novi red za čišćenje tačkica

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Could not convert image. Try again.");
    return false;
  }

  // Pretraži bazu podataka za otisak
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("Fingerprint already exists in database with ID #");
    Serial.println(finger.fingerID);
    return true;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint not found in database.");
    return false;
  } else {
    Serial.println("Error searching database.");
    return false;
  }
}

// Funkcija za snimanje otiska
uint8_t getFingerprintEnroll(uint8_t id) {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      if (!noFingerMessageShown) {
        Serial.println("No valid finger detected. Try again.");
        noFingerMessageShown = true;
      } else {
        Serial.print("."); // Prikaži tačkicu kada nema otiska, bez ponavljanja poruke
        if (Serial.availableForWrite() == 0) {
          Serial.println(); // Kada dođe do kraja linije, pređi u novi red
        }
      }
      delay(1000);
    } else {
      noFingerMessageShown = false;
      Serial.println("Image taken");
    }
  }

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

  Serial.print("ID "); Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      if (!noFingerMessageShown) {
        Serial.println("No valid finger detected. Try again.");
        noFingerMessageShown = true;
      } else {
        Serial.print(".");
        if (Serial.availableForWrite() == 0) {
          Serial.println(); // Kada dođe do kraja linije, pređi u novi red
        }
      }
      delay(1000);
    } else {
      noFingerMessageShown = false;
      Serial.println("Image taken");
    }
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image. Try again.");
    return p;
  }

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  Serial.print("Storing ID #"); Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
  } else {
    Serial.println("Error writing to flash.");
  }

  return true;
}

void listOccupiedIDs() {
  Serial.print("Occupied IDs: ");
  for (uint16_t id = 1; id < finger.capacity; id++) {
    int p = finger.loadModel(id);
    if (p == FINGERPRINT_OK) {
      Serial.print(id); Serial.print(" ");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("Communication error");
      break;
    }
    // Ako p nije FINGERPRINT_OK, ID je slobodan ili se dogodila greška
  }
  Serial.println();
}
