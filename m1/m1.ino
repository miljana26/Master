#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <ESP32Servo.h>
#include <WebSocketsServer.h>
#include <time.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>



#define RX_PIN 16  
#define TX_PIN 17  
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *ssid = "MS";
const char *password = "zastomezezas";
const char* telegramToken = "7454592874:AAE7dneypwpAgrV2GxIsEVole_JwEyjh9RE";  // Bot tokenom
const char* chatId = "8017471176";  // chat_id korisnika


// Struktura za login korisnika
struct User {
  String username;
  String pin;
  String fingerprintID; 
  String voiceCommand;  
};



// Lista korisnika
std::vector<User> users;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
bool loggedIn = false;
bool userAdded = false;
User loggedInUser = {"", ""};

const int ledPin = 5;
const int pirPin = 15;
bool motionDetected = false;  // Flag za praćenje detekcije pokreta
const int blueLedPin = 2;  // Pin za plavu LED diodu (GPIO 2)
int blockTime = 0; // Vreme koje blokira korisnika nakon 3 pogrešna unosa
bool isEnteringPin = false;  // Prati da li je u toku unos PIN-a
bool isWaitingForMotion = true;  // Da li sistem čeka na pokret
bool loginFailed = false;  // Globalna promenljiva za praćenje neuspešnog logina
const unsigned long ledOnTimeout = 180000;  // 3 minute timeout for LED
unsigned long ledTurnOnTime = 0;
String lastGeneratedPin = "";  // Globalna promenljiva za čuvanje poslednjeg generisanog PIN-a
const unsigned long pirDebounceDelay = 500;  // 500 ms debounce period
unsigned long lastPirReadTime = 0;
bool lastPirState = LOW;
bool ledState = false;  // Trenutno stanje LED diode
bool motionState = false;  // Trenutno stanje PIR senzora
bool alarmState = false;  // Trenutno stanje alarma
bool doorState = false;  // Trenutno stanje vrata (zatvorena ili otvorena)
uint8_t loggedInClientNum = -1;  // Promenljiva za čuvanje identifikatora ulogovanog klijenta
String motionDetectedTime = "";  // Za čuvanje vremena detekcije pokreta
int currentStep = 0;  // Globalna promenljiva za praćenje koraka
bool noFingerMessageShown = false;
bool registrationActive = false;
bool fingerprintAdded = false;  // Praćenje statusa otiska prsta
int fingerprintID = 0;  // Podesi početnu vrednost na 0 ili neku odgovarajuću vrednost



HardwareSerial mySerial(2); // Koristi Serial2 za komunikaciju sa senzorom
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Keypad setup
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String enteredPassword = "";
String correctPassword = "133A";
int attempts = 0;
const int maxAttempts = 3;

// Servo setup
Servo myservo;
int servoPin = 18;
int pos = 0;

unsigned long pirActivationTime = 0;
const unsigned long pirTimeout = 180000;  // 3 minuta timeout


void sendTelegramMessage(const String& message) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(telegramToken) + "/sendMessage?chat_id=" + chatId + "&text=" + message;

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        Serial.printf("Telegram message sent. Response code: %d\n", httpResponseCode);
    } else {
        Serial.printf("Error sending Telegram message. Response code: %d\n", httpResponseCode);
    }

    http.end();
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        Serial.printf("[%u] Connected!\n", num);
        loggedInClientNum = num;
    } else if (type == WStype_DISCONNECTED) {
        Serial.printf("[%u] Disconnected!\n", num);
        if (num == loggedInClientNum) {
            loggedInClientNum = -1;
        }
    } else if (type == WStype_TEXT) {
        String message = (char *)payload;

        if (message == "start") {
            startFingerprintRegistration();
            Serial.println("Fingerprint registration started by client.");
        } else if (message == "cancel") {
            resetRegistrationProcess();
            Serial.println("Fingerprint registration canceled by client.");
        }
    }
}


String urlEncode(String str) {
  String encodedString = "";
  char c;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encodedString += c;
    } else {
      encodedString += '%';
      char code0 = (c >> 4) & 0xF;
      char code1 = c & 0xF;
      code0 += code0 > 9 ? 'A' - 10 : '0';
      code1 += code1 > 9 ? 'A' - 10 : '0';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

String urlDecode(String str) {
  String decoded = "";
  char c;
  int i, len = str.length();
  for (i = 0; i < len; i++) {
    c = str.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%' && i + 2 < len) {
      char code0 = str.charAt(++i);
      char code1 = str.charAt(++i);
      c = (hexToInt(code0) << 4) | hexToInt(code1);
      decoded += c;
    } else {
      decoded += c;
    }
  }
  return decoded;
}

int hexToInt(char c) {
  if ('0' <= c && c <= '9') return c - '0';
  if ('a' <= c && c <= 'f') return c - 'a' + 10;
  if ('A' <= c && c <= 'F') return c - 'A' + 10;
  return 0;
}



void sendAllStatusesToClient(uint8_t clientNum) {
  String ledStatusMsg = "{\"type\":\"led\",\"state\":" + String(ledState ? "true" : "false") + "}";
  String motionStatusMsg = "{\"type\":\"motion\",\"state\":" + String(motionState ? "true" : "false") + ", \"time\":\"" + motionDetectedTime + "\"}";
  String alarmStatusMsg = "{\"type\":\"alarm\",\"state\":" + String(alarmState ? "true" : "false") + "}";
  String doorStatusMsg = "{\"type\":\"doors\",\"state\":" + String(doorState ? "true" : "false") + "}";
  String pinStatusMsg = "{\"type\":\"pin\",\"value\":\"" + enteredPassword + "\"}";

  webSocket.sendTXT(clientNum, ledStatusMsg);
  webSocket.sendTXT(clientNum, motionStatusMsg);
  webSocket.sendTXT(clientNum, alarmStatusMsg);
  webSocket.sendTXT(clientNum, doorStatusMsg);
  webSocket.sendTXT(clientNum, pinStatusMsg);
}



String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Failed to obtain time";
  }
  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  return String(timeStr);
}


// Funkcija za unos PIN-a
void handlePasswordInput() {
  char key = keypad.getKey();  // Čitanje unosa sa fizičkog tastature

  if (key) {
    if (!isEnteringPin) {
      isEnteringPin = true;
      isWaitingForMotion = false;  // Više ne čekamo pokret
    }

    if (key == '#') {
      Serial.print("Unos završen: ");
      Serial.println(enteredPassword);
      if (enteredPassword == correctPassword) {
        Serial.println("Ispravan PIN!");
        displayWelcomeMessage(loggedInUser.username);  // Prikaži poruku sa imenom korisnika
        moveServo();  // Otvaranje vrata (servo motor)
        delay(3000);  // Zadrži poruku 3 sekunde pre povratka na "Waiting"
        resetPIRDetection();  // Resetuj sistem na "Waiting for motion" sa normalnim fontom
      } else {
        // Logika za pogrešan PIN
        attempts++;
        if (attempts >= maxAttempts) {
          Serial.println("Previše pogrešnih pokušaja!");
          activateErrorLED();  // Aktiviraj crvenu LED
          delay(3000);
          resetPIRDetection();
        } else {
          Serial.println("Neispravan PIN!");
          // Očisti OLED ekran nakon pogrešnog unosa
          display.clearDisplay();
          display.setCursor(0, 0);
          display.print("Enter password:");
          display.display();
        }
      }

      // Resetuj PIN unos i na OLED-u i na web stranici
      enteredPassword = "";
      webSocket.broadcastTXT("{\"type\":\"pin\",\"value\":\"\"}");  // Obriši unos PIN-a na web stranici

    } else if (key == '*') {  // Briši poslednji uneti karakter
      if (enteredPassword.length() > 0) {
        enteredPassword.remove(enteredPassword.length() - 1);
        // Ažuriraj prikaz na OLED-u
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Enter password:");
        display.setCursor(0, 20);
        for (int i = 0; i < enteredPassword.length(); i++) {
          display.print("*");
        }
        display.display();

        // Ažuriraj prikaz na web stranici u realnom vremenu
        webSocket.broadcastTXT("{\"type\":\"pin\",\"value\":\"" + enteredPassword + "\"}");
      }

    } else {
      enteredPassword += key;  // Dodaj uneseni karakter u lozinku

      // Ažuriraj prikaz na OLED-u
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("Enter password:");
      display.setCursor(0, 20);
      for (int i = 0; i < enteredPassword.length(); i++) {
        display.print("*");
      }
      display.display();

      // Ažuriraj prikaz na web stranici u realnom vremenu
      webSocket.broadcastTXT("{\"type\":\"pin\",\"value\":\"" + enteredPassword + "\"}");
    }
  }
}


void handlePIRSensor() {
  int pirState = digitalRead(pirPin);
  unsigned long currentTime = millis();

  // Provera promene stanja PIR senzora
  if (pirState != lastPirState) {
    lastPirReadTime = currentTime;
  }

  // Debounce period i detekcija pokreta
  if ((currentTime - lastPirReadTime) > pirDebounceDelay && pirState == HIGH && isWaitingForMotion) {
    Serial.println("Pokret je detektovan!");
    String detectionTime = getFormattedTime();
    motionDetectedTime = detectionTime; // Sačuvaj vreme detekcije
    pirActivationTime = millis();  // Beleženje vremena detekcije pokreta
    ledTurnOnTime = millis();  // Beleženje vremena kada je LED uključena
    digitalWrite(ledPin, HIGH);  // Uključi LED
    ledState = true;
    motionState = true;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Motion at:");
    display.setCursor(0, 20);
    display.print(detectionTime);
    display.display();

    isEnteringPin = true;  // Pokrenut unos PIN-a
    isWaitingForMotion = false;  // Prestanak čekanja na pokret

    // Pošalji novo stanje pokreta klijentu
    if (loggedInClientNum != -1) {
      String motionStatusMsg = "{\"type\":\"motion\",\"state\":true, \"time\":\"" + motionDetectedTime + "\"}";
      webSocket.sendTXT(loggedInClientNum, motionStatusMsg);
    }
  }

  // Provera da li je prošlo 3 minuta bez unosa šifre (isključivanje LED)
  if (ledState && (millis() - ledTurnOnTime > ledOnTimeout)) {
    Serial.println("Prošlo je 3 minuta, LED se isključuje.");
    resetPIRDetection();  // Resetuje sistem i vraća u stanje čekanja pokreta
  }

  lastPirState = pirState;
}



void resetPIRDetection() {
  digitalWrite(ledPin, LOW);
  ledState = false;

  digitalWrite(blueLedPin, LOW);
  enteredPassword = "";
  attempts = 0;
  isEnteringPin = false;
  isWaitingForMotion = true;
  alarmState = false;

  motionDetectedTime = ""; // Resetuj vreme detekcije pokreta
  motionState = false;     // Resetuj stanje pokreta

  // Pošalji novo stanje pokreta klijentu
  if (loggedInClientNum != -1) {
    String motionStatusMsg = "{\"type\":\"motion\",\"state\":false, \"time\":\"\"}";
    webSocket.sendTXT(loggedInClientNum, motionStatusMsg);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Waiting for motion...");
  display.display();
}


void moveServo() {
  for (pos = 0; pos <= 180; pos += 1) {
    myservo.write(pos);
    delay(10);
  }
  doorState = true;  // Vrata su otvorena

  // Pošalji novo stanje vrata klijentu
  if (loggedInClientNum != -1) {
    String doorStatusMsg = "{\"type\":\"doors\",\"state\":true}";
    webSocket.sendTXT(loggedInClientNum, doorStatusMsg);
  }

  delay(1000);
  for (pos = 180; pos >= 0; pos -= 1) {
    myservo.write(pos);
    delay(10);
  }
  doorState = false;  // Vrata su zatvorena

  // Pošalji novo stanje vrata klijentu
  if (loggedInClientNum != -1) {
    String doorStatusMsg = "{\"type\":\"doors\",\"state\":false}";
    webSocket.sendTXT(loggedInClientNum, doorStatusMsg);
  }
}



void activateErrorLED() {
    digitalWrite(blueLedPin, HIGH);  
    alarmState = true;  // Ažuriraj stanje alarma

    // Pošalji Telegram poruku kada se alarm uključi
    sendTelegramMessage("Alarm je uključen!");

    // Pošalji novo stanje alarma klijentu
    if (loggedInClientNum != -1) {
        String alarmStatusMsg = "{\"type\":\"alarm\",\"state\":true}";
        webSocket.sendTXT(loggedInClientNum, alarmStatusMsg);
    }

    delay(5000);  
    digitalWrite(blueLedPin, LOW);
    alarmState = false;  // Ažuriraj stanje alarma

    // Pošalji novo stanje alarma klijentu
    if (loggedInClientNum != -1) {
        String alarmStatusMsg = "{\"type\":\"alarm\",\"state\":false}";
        webSocket.sendTXT(loggedInClientNum, alarmStatusMsg);
    }
}



// Funkcija koja prikazuje poruku "Welcome home (ime korisnika)" na OLED-u
void displayWelcomeMessage(String username) {
  display.clearDisplay();
  display.setTextSize(1);  // Postavi veličinu teksta na 1

  // Prikaži "Welcome" centrirano na ekranu
  display.setCursor((SCREEN_WIDTH - 6 * 7) / 2, 10);  // 6 piksela po karakteru, reč "Welcome" ima 7 karaktera
  display.print("Welcome");

  // Prikaži "home!" centrirano na ekranu ispod "Welcome"
  display.setCursor((SCREEN_WIDTH - 6 * 5) / 2, 25);  // 6 piksela po karakteru, reč "home!" ima 5 karaktera
  display.print("home!");

  // Prikaži ime korisnika centrirano na ekranu ispod "home!"
  display.setCursor((SCREEN_WIDTH - 6 * username.length()) / 2, 40);  // Računa se dužina korisničkog imena
  display.print(username);

  // Osveži ekran
  display.display();
}

void startFingerprintRegistration() {
    resetRegistrationProcess();
    registrationActive = true;
}

// Generiše novi ID ako nije zauzet
int generateNewFingerprintID() {
    for (int id = 1; id <= finger.capacity; id++) {
        if (checkIDAvailability(id)) {
            return id;  // Prvi slobodan ID
        }
    }
    Serial.println("No available IDs!");
    return -1;  // Nema slobodnih ID-eva
}



bool checkIDAvailability(int id) {
    Serial.print("Checking availability for ID ");
    Serial.println(id);

    int p = finger.loadModel(id);
    if (p == FINGERPRINT_OK) {
        Serial.println("ID " + String(id) + " is occupied.");
        return false;  // ID je zauzet
    } else {
        Serial.println("ID " + String(id) + " is available.");
        return true;  // ID je slobodan
    }
}


void assignFingerprintID() {
    refreshFingerprintIDs();  // Osvežavanje pre dodeljivanja ID-a
    fingerprintID = generateNewFingerprintID();
    if (fingerprintID > 0) {
        Serial.print("Assigned Fingerprint ID: ");
        Serial.println(fingerprintID);
    } else {
        Serial.println("Error: No available Fingerprint ID.");
        resetRegistrationProcess();
    }
}



void refreshFingerprintIDs() {
    Serial.println("Refreshing fingerprint IDs...");
    for (int id = 1; id <= finger.capacity; id++) {
        int p = finger.loadModel(id);
        if (p == FINGERPRINT_OK) {
            Serial.println("ID " + String(id) + " is occupied.");
        } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
            Serial.println("Error reading ID " + String(id));
        } else {
            Serial.println("ID " + String(id) + " is available.");
        }
    }
}



void sendProgressUpdate(int step, const char* message) {
    DynamicJsonDocument doc(1024);
    doc["type"] = "progress";
    doc["step"] = step;
    doc["message"] = message;
    String json;
    serializeJson(doc, json);

    Serial.print("Sending progress update: ");
    Serial.println(json);  // Ispisuje JSON podatke za debagovanje

    webSocket.broadcastTXT(json);
}



// Prvo skeniranje otiska
bool getFingerprintImage() {
    int attempts = 0;  // Broji pokušaje
    while (attempts < 3) {  // Maksimalno tri pokušaja
        int p = finger.getImage();
        
        if (p == FINGERPRINT_OK) {
            attempts++;  // Povećava broj pokušaja
            delay(50);   // Kratko odlaganje pre ponovnog skeniranja
        } else if (p == FINGERPRINT_NOFINGER) {
            Serial.println("Waiting for finger...");
            delay(1000); // Čeka 1 sekundu pre sledeće provere
            return false;  // Ako prsta nema, izlazi iz funkcije
        } else {
            Serial.println("Error capturing fingerprint image");
            resetProgress();  // Resetuje proces u slučaju greške
            return false;
        }
    }

    // Ako su sva tri pokušaja uspešna, prihvata otisak
    if (attempts == 3) {
        Serial.println("Image confirmed after 3 attempts");
        finger.image2Tz(1);  // Konvertuje sliku i prelazi na sledeći korak
        return true;
    } else {
        Serial.println("Failed to confirm fingerprint after 3 attempts");
        resetProgress();  // Resetuje proces ako otisak nije potvrđen
        return false;
    }
}




// Drugo skeniranje otiska
bool confirmSecondScan() {
    Serial.println("Remove your finger and place it again.");
    delay(2000); // Pauza za uklanjanje prsta

    int p = finger.getImage();
    if (p == FINGERPRINT_OK) {
        finger.image2Tz(2);
        if (finger.createModel() == FINGERPRINT_OK) {
            Serial.println("Fingerprint matched.");
            return true; // Korak uspešan
        } else {
            Serial.println("Fingerprints did not match. Try again.");
            resetProgress();
        }
    } else if (p == FINGERPRINT_NOFINGER) {
        delay(1000); // Čeka da korisnik postavi prst
    }
    return false;
}

bool getFingerprintAdded() {
    if (!SPIFFS.exists("/fingerprintAdded.txt")) {
        Serial.println("Fingerprint status file does not exist. Returning false.");
        return false;
    }
    File file = SPIFFS.open("/fingerprintAdded.txt", FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false;
    }
    String value = file.readString();
    file.close();
    Serial.print("Retrieved fingerprintAdded from SPIFFS: ");
    Serial.println(value);
    return value == "true";
}


// Čuvanje otiska u memoriju
void saveFingerprintAdded(bool status) {
    File file = SPIFFS.open("/fingerprintAdded.txt", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    file.print(status ? "true" : "false");
    file.close();
    Serial.println("Saved fingerprintAdded to SPIFFS");
}

bool saveFingerprint() {
    int p = finger.storeModel(fingerprintID);
    if (p == FINGERPRINT_OK) {
        Serial.println("Fingerprint stored successfully.");
        fingerprintAdded = "true";
        saveFingerprintAdded(true);  // Save to SPIFFS
        Serial.println("Saved fingerprintAdded as true in SPIFFS");

        // Test retrieval
        bool testRetrieve = getFingerprintAdded();
        Serial.print("Test retrieve after saving: ");
        Serial.println(testRetrieve ? "true" : "false");
        Serial.println("Refreshing fingerprint ID in saveFingerprint() function...");

                refreshFingerprintIDs();  // Osvežavanje posle dodavanja otiska


        return true;
    } else {
        Serial.println("Error storing fingerprint.");
        fingerprintAdded = "false";
        saveFingerprintAdded(false);  // Save to SPIFFS
        return false;
    }
}


void resetRegistrationProcess() {
    registrationActive = false;
    currentStep = 0;
    fingerprintAdded = "false";
    //fingerprintID = 0; // Resetujte ID ako je potrebno
    // Takođe, resetujte bilo koje delimične podatke ili bafer ako je potrebno
}

void handleGetFingerprintStatus() {
    DynamicJsonDocument jsonResponse(1024);
    jsonResponse["fingerprintAdded"] = getFingerprintAdded();
    String response;
    serializeJson(jsonResponse, response);
    server.send(200, "application/json", response);
}


void handleResetFingerprintStatus() {
    saveFingerprintAdded(false);  // Ažuriranje statusa u SPIFFS ili drugom skladištu
    Serial.println("Fingerprint status reset to false.");
    server.send(200, "application/json", "{\"success\": true}");
}



// Funkcija za resetovanje napretka ako korisnik ne uspe
void resetProgress() {
    Serial.println("Process failed. Restarting...");
    currentStep = 0;
}


// Funkcija za inicijalizaciju fajla sa korisnicima
void initializeUserFile() {
  Serial.println("Initializing user file...");
  
  if (!SPIFFS.exists("/users.txt")) {
    // Kreiraj fajl ako ne postoji i dodaj admin/admin korisnika
    File file = SPIFFS.open("/users.txt", "w");
    if (!file) {
      Serial.println("Greška pri kreiranju fajla za korisnike");
      return;
    }

    // Dodaj default admin korisnika u fajl sa samo username-om i pin-om
    file.println("admin,admin,,"); // Prazna polja za fingerprintID i voiceCommand
    file.close();
    Serial.println("Kreiran fajl sa default admin korisnikom");
  } else {
    Serial.println("User file already exists.");
    bool adminExists = false;
    File file = SPIFFS.open("/users.txt", "r");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();  // Ukloni prazne prostore
      if (line.startsWith("admin,")) {
        adminExists = true;
        break;
      }
    }
    file.close();

    // Ako admin ne postoji, dodaj ga u fajl
    if (!adminExists) {
      File file = SPIFFS.open("/users.txt", "a");
      file.println("admin,admin,,"); // Correct format with extra commas
      file.close();
      Serial.println("Dodao admin korisnika u postojeći fajl");
    }
  }
      Serial.println("Initialization complete.");

}


// Funkcija za učitavanje korisnika iz fajla
void loadUsersFromFile() {
      Serial.println("Loading users from file...");

  File file = SPIFFS.open("/users.txt", FILE_READ);
  if (!file) {
    Serial.println("Fajl sa korisnicima ne postoji");
    return;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int commaIndex1 = line.indexOf(',');
    int commaIndex2 = line.indexOf(',', commaIndex1 + 1);
    int commaIndex3 = line.indexOf(',', commaIndex2 + 1);

    if (commaIndex1 > 0) {
      String username = line.substring(0, commaIndex1);
      String pin;
      String fingerprintID = "";
      String voiceCommand = "";

      if (commaIndex2 > commaIndex1) {
        pin = line.substring(commaIndex1 + 1, commaIndex2);
      } else {
        pin = line.substring(commaIndex1 + 1);
      }

      if (commaIndex2 > commaIndex1 && commaIndex3 > commaIndex2) {
        fingerprintID = line.substring(commaIndex2 + 1, commaIndex3);
        voiceCommand = line.substring(commaIndex3 + 1);
      } else if (commaIndex2 > commaIndex1) {
        fingerprintID = line.substring(commaIndex2 + 1);
      }

      users.push_back({username, pin, fingerprintID, voiceCommand});
                  Serial.printf("Loaded user: Username: %s, PIN: %s, Fingerprint ID: %s, Voice Command: %s\n",
                          username.c_str(), pin.c_str(), fingerprintID.c_str(), voiceCommand.c_str());
    } else {
      Serial.println("Failed to parse line: " + line);
    }
  }

  file.close();
      Serial.println("Finished loading users.");

}




// Funkcija za dodavanje korisnika u fajl
void saveUserToFile(const String& username, const String& pin, const String& fingerprintID, const String& voiceCommand) {
    Serial.printf("Saving user: Username: %s, PIN: %s, Fingerprint ID: %s, Voice Command: %s\n",
                  username.c_str(), pin.c_str(), fingerprintID.c_str(), voiceCommand.c_str());

    File file = SPIFFS.open("/users.txt", FILE_APPEND);
    if (!file) {
        Serial.println("Error opening user file for appending.");
        return;
    }

    file.println(username + "," + pin + "," + fingerprintID + "," + voiceCommand);
    file.close();
    Serial.println("User saved to file.");
}




// Funkcija za brisanje korisnika iz fajla
void deleteUserFromFile(const String& usernameToDelete) {
    Serial.println("Entered deleteUserFromFile function.");

    bool userFound = false;
    int fingerprintIDToDelete = -1;

    // Search for the user in memory to find their fingerprint ID
    for (User &u : users) {
        if (u.username == usernameToDelete) {
            userFound = true;
            fingerprintIDToDelete = u.fingerprintID.toInt();
            Serial.println("Found user to delete: " + u.username);
            break;
        }
    }

    if (!userFound) {
        Serial.println("User not found in memory, no action taken for deletion.");
        return;
    }

    // Attempt to delete the fingerprint from the sensor
    if (fingerprintIDToDelete >= 0) {
        int deleteStatus = finger.deleteModel(fingerprintIDToDelete);
        if (deleteStatus == FINGERPRINT_OK) {
            Serial.println("Fingerprint ID " + String(fingerprintIDToDelete) + " deleted successfully.");
        } else {
            Serial.println("Failed to delete Fingerprint ID " + String(fingerprintIDToDelete) + ". Error code: " + String(deleteStatus));
        }
    }

    // Remove the user from the users.txt file by creating a temporary file and excluding the target user
    File tempFile = SPIFFS.open("/users_temp.txt", "w");
    if (!tempFile) {
        Serial.println("Failed to open temporary file for writing.");
        return;
    }

    for (User &u : users) {
        if (u.username != usernameToDelete) {
            tempFile.println(u.username + "," + u.pin + "," + u.fingerprintID + "," + u.voiceCommand);
            Serial.println("Writing user to temp file: " + u.username);
        }
    }

    tempFile.close();

    // Replace users.txt with the updated file
    if (SPIFFS.remove("/users.txt")) {
        Serial.println("Deleted original users.txt file.");
    } else {
        Serial.println("Failed to delete original users.txt file.");
    }

    if (SPIFFS.rename("/users_temp.txt", "/users.txt")) {
        Serial.println("Renamed temp file to users.txt successfully.");
    } else {
        Serial.println("Failed to rename temp file to users.txt.");
    }

    // Update the in-memory list by removing the deleted user
    users.erase(std::remove_if(users.begin(), users.end(),
                [&](User &u) { return u.username == usernameToDelete; }), users.end());

    Serial.println("User " + usernameToDelete + " fully removed from memory and file.");
    
    Serial.println("Refreshing fingerprint IDs after deletion...");
    refreshFingerprintIDs();

    Serial.println("Completed deleteUserFromFile function.");
}


void printUsersFile() {
  File file = SPIFFS.open("/users.txt", "r");
  if (!file) {
    Serial.println("Unable to open users.txt");
    return;
  }

  Serial.println("Contents of users.txt:");
  while (file.available()) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
  }
  file.close();
}

// Funkcija za prikaz glavnog prozora sa dugmadima
void showMainPage() {
  server.send(200, "text/html",
              "<html><head>"
              "<style>"
              "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial; height: 100vh; margin: 0; display: flex; justify-content: center; align-items: center; }"
              ".main-container { display: flex; justify-content: center; align-items: center; flex-direction: column; }"
              ".main-container button { background-color: #00d4ff; color: #ffffff; padding: 20px 40px; margin: 20px; border-radius: 10px; font-size: 20px; border: none; cursor: pointer; width: 300px; }"
              ".main-container button:hover { background-color: #00a3cc; }"
              "</style>"
              "</head><body>"
              "<div class='main-container'>"
              "<button onclick=\"location.href='/loginPage'\">Login</button>"
              "<button onclick=\"location.href='/addUserPage'\">Add User</button>"
              "</div>"
              "</body></html>");
}


// Funkcija za prikaz login prozora
void showLoginPage() {
  String message = "";
  if (loginFailed) {
    message = "<p style='color:red; text-align:center;'>Wrong username or password!</p>";
    loginFailed = false;  // Resetuj status nakon prikaza poruke
  }

  // Postavljanje HTTP zaglavlja kako bi se onemogućilo keširanje
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html",
              "<html><head>"
              "<style>"
              "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial, sans-serif; }"
              ".login-container { display: flex; justify-content: center; align-items: center; height: 100vh; }"
              ".login-box { background-color: #1A4D8A; padding: 60px; border-radius: 15px; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); width: 400px; }"
              ".login-box h1 { color: #00d4ff; text-align: center; margin-bottom: 30px; font-size: 24px; }"
              ".login-box input { width: 100%; padding: 15px; margin: 15px 0; border: none; border-radius: 5px; font-size: 16px; }"
              ".login-box input[type='text'], .login-box input[type='password'] { background-color: #112240; color: #ffffff; }"
              ".login-box input[type='submit'], .back-button { width: 300px; padding: 15px; margin: 10px 0; background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; font-size: 16px; text-align: center; text-decoration: none; display: block; margin-left: auto; margin-right: auto; }"
              ".login-box input[type='submit']:hover, .back-button:hover { background-color: #00a3cc; }"
              "</style>"
              "<script>"
              "window.onload = function() {"
              "  if (window.history && window.history.pushState) {"
              "    window.history.pushState(null, '', window.location.href);"
              "    window.onpopstate = function() {"
              "      window.history.pushState(null, '', window.location.href);"
              "    };"
              "  }"
              "};"
              "</script>"
              "</head><body>"
              "<div class='login-container'>"
              "<div class='login-box'>"
              "<h1>Login</h1>"
              "<form action='/login' method='POST'>"
              "Username: <input type='text' name='username'><br>"
              "Password: <input type='password' name='password'><br>"
              "<input type='submit' value='Login'>"
              "</form>"
              "<a href='/' class='back-button'>Back to Main Page</a>"
              + message +
              "</div></div></body></html>");
}



// Funkcija za prikaz dodavanja korisnika
void showUserPage() {
  if (!loggedIn) {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/html",
                "<html><body><script>alert('Unauthorized access! Please log in.');</script><meta http-equiv='refresh' content='0;url=/loginPage' /></body></html>");
    return;
  }

  // Zaglavlja za sprečavanje keširanja
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  // Prikaz korisničke stranice sa ažuriranim HTML, CSS i JavaScript kodom
  server.send(200, "text/html",
              "<html><head>"
              "<style>"
              "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); color: white; font-family: Arial, sans-serif; height: 100vh; margin: 0; position: relative; }"

              /* Stil za žicu */
              ".wire { position: absolute; left: calc(50% - 2px); bottom: 50%; width: 4px; height: 60vh; background: #000; z-index: 1; }"

              /* Stil za sijalicu */
              ".bulb { position: absolute; top: calc(40vh + 80px); left: 50%; transform: translate(-50%, -20px); width: 80px; height: 80px; background: #444; border-radius: 50%; z-index: 2; }"
              ".bulb:before { content: ''; position: absolute; left: 22.5px; top: -50px; width: 35px; height: 80px; background: #444; border-top: 30px solid #000; border-radius: 10px; }"

              /* Kada je sijalica upaljena */
              "body.on .bulb::after { content: ''; position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 120px; height: 120px; background: #fff; border-radius: 50%; filter: blur(40px); }"
              "body.on .bulb { background-color: #fff; box-shadow: 0 0 50px #fff, 0 0 100px #fff, 0 0 150px #fff, 0 0 200px #fff, 0 0 250px #fff, 0 0 300px #fff, 0 0 350px #fff; }"
              "body.on .bulb::before { background: #fff; }"

              /* Stil za PIN i tastaturu */
              ".pin-keypad-container { display: flex; flex-direction: column; align-items: flex-start; position: absolute; left: 100px; top: 150px; }"
              ".screen { background-color: #1A4D8A; color: white; padding: 10px; font-size: 18px; margin-bottom: 10px; width: 250px; text-align: center; border-radius: 10px; height: 55px; display: flex; flex-direction: column; justify-content: center; }"
              ".pin-display { font-size: 22px; margin-top: 5px; }"
              ".keypad { display: grid; grid-template-columns: repeat(4, 61px); grid-gap: 8px; margin-top: 10px; }"
              ".key { background-color: #00d4ff; color: white; padding: 19px; font-size: 20px; border-radius: 8px; cursor: pointer; text-align: center; }"
              ".key:hover { background-color: #00a3cc; }"

              /* Stil za statusne boksove */
              ".right-panel { position: absolute; top: 200px; right: 100px; display: flex; flex-direction: column; gap: 30px; }"
              ".status-box { background-color: #1A4D8A; color: white; padding: 10px; font-size: 18px; width: 250px; text-align: center; border-radius: 10px; height: 55px; display: flex; align-items: center; justify-content: center; }"
              ".status-box.motion-box { /* Individualni stilovi za Motion */ }"
              ".status-box.alarm-box { /* Individualni stilovi za Alarm */ }"
              ".status-box.alarm-box.alarm-active { background-color: red; }"
              ".status-box.door-box { /* Individualni stilovi za Door */ }"
              ".status-box.door-box.opened { background-color: green; }"

              /* Logout dugme */
              ".logout-button { background-color: #00d4ff; color: white; padding: 15px; font-size: 18px; border-radius: 15px; cursor: pointer; position: absolute; top: 20px; right: 20px; }"
              ".logout-button:hover { background-color: #00a3cc; }"
              "</style>"
              "</head><body>"

              // Žica i sijalica
              "<div class='wire'></div>"
              "<div class='bulb' id='ledCircle'></div>"

              // Ekran i tastatura
              "<div class='pin-keypad-container'>"
              "<div class='screen'>"
              "Entered PIN:"
              "<div class='pin-display' id='pin-display'></div>"
              "</div>"
              "<div class='keypad'>"
              "<div class='key'>1</div><div class='key'>2</div><div class='key'>3</div><div class='key'>A</div>"
              "<div class='key'>4</div><div class='key'>5</div><div class='key'>6</div><div class='key'>B</div>"
              "<div class='key'>7</div><div class='key'>8</div><div class='key'>9</div><div class='key'>C</div>"
              "<div class='key'>*</div><div class='key'>0</div><div class='key'>#</div><div class='key'>D</div>"
              "</div>"
              "</div>"

              // Desni panel sa statusnim boksovima
              "<div class='right-panel'>"
              "<div class='status-box motion-box' id='motion-box'>Waiting for motion...</div>"
              "<div class='status-box alarm-box' id='alarm-box'>No Alarm</div>"
              "<div class='status-box door-box' id='doors-box'>Closed</div>"
              "</div>"

              // Logout dugme
              "<a href='/logout'><button class='logout-button'>Logout</button></a>"

              "<script>"
              "let ws = new WebSocket('ws://' + window.location.hostname + ':81/');"
              "ws.onmessage = function(event) {"
              "  let data = JSON.parse(event.data);"
              "  if (data.type === 'pin') {"
              "    document.getElementById('pin-display').innerText = '*'.repeat(data.value.length);"
              "  } else if (data.type === 'motion') {"
              "    if (data.state) {"
              "      document.getElementById('motion-box').innerText = 'Motion detected at ' + data.time;"
              "      document.getElementById('motion-box').style.borderColor = 'green';"
              "    } else {"
              "      document.getElementById('motion-box').innerText = 'Waiting for motion...';"
              "      document.getElementById('motion-box').style.borderColor = 'white';"
              "    }"
              "  } else if (data.type === 'led') {"
              "    toggleLed(data.state);"
              "  } else if (data.type === 'alarm') {"
              "    toggleAlarm(data.state);"
              "  } else if (data.type === 'doors') {"
              "    updateDoorStatus(data.state);"
              "  }"
              "};"
              "function toggleLed(isOn) {"
              "  const body = document.querySelector('body');"
              "  if (isOn) {"
              "    body.classList.add('on');"
              "  } else {"
              "    body.classList.remove('on');"
              "  }"
              "}"
              "function toggleAlarm(isActive) {"
              "  const alarmBox = document.getElementById('alarm-box');"
              "  if (isActive) {"
              "    alarmBox.classList.add('alarm-active');"
              "    alarmBox.innerText = 'ALARM';"
              "  } else {"
              "    alarmBox.classList.remove('alarm-active');"
              "    alarmBox.innerText = 'No Alarm';"
              "  }"
              "}"
              "function updateDoorStatus(isOpened) {"
              "  const doorBox = document.getElementById('doors-box');"
              "  doorBox.innerText = isOpened ? 'Opened' : 'Closed';"
              "  if (isOpened) {"
              "    doorBox.classList.add('opened');"
              "  } else {"
              "    doorBox.classList.remove('opened');"
              "  }"
              "}"
              "</script>"

              "</body></html>");
}


void showAddUserPage() {
  // Set headers to prevent caching
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  // Create the HTML content using R"rawliteral(...)"
  String pageContent = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <style>
        body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial, sans-serif; }
        .login-container { display: flex; justify-content: center; align-items: center; height: 100vh; }
        .login-box { background-color: #1A4D8A; padding: 40px; border-radius: 15px; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); width: 350px; }
        .login-box h1 { color: #00d4ff; text-align: center; margin-bottom: 20px; font-size: 24px; }
        .username-label { color: #000000; font-size: 16px; margin-bottom: 5px; display: block; }
        .username-input-field { width: 143%; padding: 15px; border: none; border-radius: 5px; font-size: 16px; background-color: #112240; color: #ffffff; }
        .input-error { border: 1px solid red; }
        .error-message { color: red; font-size: 14px; margin-top: 10px; }
        .captcha { display: flex; align-items: center; margin-top: 20px; justify-content: center;}
        .captcha input[type='checkbox'] { width: 20px; height: 20px; margin-right: 10px; }
        .captcha-error input[type='checkbox'] { outline: 2px solid red; }
        .captcha label { color: #00d4ff; font-size: 16px; font-weight: bold; }
        .captcha-error label { color: red; }
        .add-fingerprint-button { width: 101%; padding: 15px; margin-top: 12px; background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; font-size: 16px; border: none; }
        .add-fingerprint-button:hover { background-color: #00a3cc; }
        .voice-command-label { color: #000000; font-size: 16px; margin-top: 15px; display: block; }
        .dropdown { width: 101%; padding: 15px; margin-top: 5px; border: none; border-radius: 5px; font-size: 16px; background-color: #112240; color: #ffffff; }
        .dropdown option { background-color: #112240; color: #ffffff; }
        .fingerprint-error, .voice-command-error {border: 1px solid red;}
        .error-message {color: red;font-size: 14px;margin-top: 5px;}
        .add-user-button { width: 85%; padding: 15px; margin-top: 20px; margin-left: 29px; background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; font-size: 16px; border: none; }
        .add-user-button:hover { background-color: #00a3cc; }
        .back-button { width: 93%; padding: 15px; margin-top: 10px; background-color: #00d4ff; color: #ffffff; text-align: center; text-decoration: none; display: block; border-radius: 5px; font-size: 16px; }
        .back-button:hover { background-color: #00a3cc; }
        .username-tooltip { position: relative; display: inline-block; }
        .tooltiptext { visibility: hidden; width: 200px; background-color: #00d4ff; color: #fff; text-align: center; border-radius: 6px; padding: 10px; position: absolute; z-index: 1; left: 160%; top: -30px; }
        .username-tooltip:hover .tooltiptext { visibility: visible; }

         /* Stilovi za krug napretka */
        @keyframes progress { 0% { --percentage: 0; } 100% { --percentage: var(--value); } }
        @property --percentage { syntax: '<number>'; inherits: true; initial-value: 0; }
        [role="progressbar"] { --percentage: var(--value); --primary: #369; --secondary: #adf; --size: 150px; animation: progress 2s 0.5s forwards; width: var(--size); aspect-ratio: 1; border-radius: 50%; position: relative; overflow: hidden; display: grid; place-items: center; box-shadow: 0 0 20px rgba(0, 255, 255, 0.4); } 
        [role="progressbar"]::before { content: ""; position: absolute; top: 0; left: 0; width: 100%; height: 100%; background: conic-gradient(var(--primary) calc(var(--percentage) * 1%), var(--secondary) 0); mask: radial-gradient(white 55%, transparent 0); mask-mode: alpha; -webkit-mask: radial-gradient(#0000 55%, #000 0); -webkit-mask-mode: alpha; } 
        [role="progressbar"]::after { counter-reset: percentage var(--value); content: counter(percentage) '%'; font-family: Helvetica, Arial, sans-serif; font-size: calc(var(--size) / 5); color: var(--primary); }

        .modal { display: none; flex-direction: column; justify-content: center; align-items: center; position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 260px; height: 260px; background-color: #1A4D8A; border-radius: 15px; padding: 20px; text-align: center; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); color: #00d4ff; }
        
        .close-button { margin-top: 15px; padding: 10px 20px; background-color: #00d4ff; color: #ffffff; border: none; border-radius: 5px; cursor: pointer; }
        
        .close-button:hover { background-color: #00a3cc; }

    </style>
    </style>
</head>
<body>
    <div class="login-container">
        <div class="login-box">
            <h1>Add User</h1>
            <form id="addUserForm">
                <!-- Username field with tooltip -->
                <label for="username" class="username-label">Username:</label>
                <div class="username-tooltip">
                    <input type="text" name="username" id="username" class="username-input-field">
                    <span class="tooltiptext">
                        Username rules:<br>
                        - Must start with a letter<br>
                        - Only letters and numbers allowed<br>
                        - No symbols or spaces
                    </span>
                </div>
                <div id="username-error" class="error-message"></div>

                <!-- Add Fingerprint button -->
                <input type="button" value="Add Fingerprint" id="add-fingerprint" class="add-fingerprint-button" onclick="showFingerprintModal()">
                <div id="fingerprint-error" class="error-message"></div>


                <!-- Voice Command dropdown -->
                <label for="voice-command" class="voice-command-label">Voice command:</label>
                <select name="voiceCommand" id="voice-command" class="dropdown">
                    <option value="">Please select a command</option>
                    <option value="option1">Option 1</option>
                    <option value="option2">Option 2</option>
                    <option value="option3">Option 3</option>
                    <option value="option4">Option 4</option>
                    <option value="option5">Option 5</option>
                    <option value="option6">Option 6</option>
                    <option value="option7">Option 7</option>
                </select>
                <div id="voice-command-error" class="error-message"></div>


                <!-- CAPTCHA section -->
                <div class="captcha" id="captcha-section">
                    <input type="checkbox" name="captcha" value="not_a_robot" id="captcha-checkbox">
                    <label for="captcha-checkbox">I am not a robot</label>
                </div>

                <!-- Submit button -->
                <input type="submit" value="Add User" class="add-user-button">
            </form>

            <!-- Back to Main Page link -->
            <a href="/" class="back-button">Back to Main Page</a>
        </div>
    </div>
    
        <!-- Modal za registraciju otiska prsta -->
    <div id="fingerprint-modal" class="modal">
        <p id="fingerprint-status">Registering Fingerprint...</p>
        <div role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100" style="--value: 0"></div>
        <button class="close-button" onclick="closeFingerprintModal()">Close</button>
    </div>



    <script>
        
        
       let ws;

       window.onload = function() {
            fetch('/resetFingerprintStatus', { method: 'POST' })
                .then(response => console.log("Fingerprint status reset."))
                .catch(err => console.error("Error resetting fingerprint status:", err));
        };
        
      window.onbeforeunload = function() {
          fetch('/resetFingerprintStatus', { method: 'POST' });
      };



       fetch('/getFingerprintStatus')
    .then(response => response.json())
    .then(data => {
        if (data.fingerprintAdded === true) {
            fetch('/resetFingerprintStatus', { method: 'POST' });
        }
    })
    .catch(err => console.error("Error checking fingerprint status:", err));




        function showFingerprintModal() {
        // Ako postoji otvorena WebSocket veza, zatvori je
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.close();
            }
            
            const progressBar = document.querySelector('[role="progressbar"]');
            progressBar.style.setProperty('--value', 0); // Reset progresivne trake na 0%
            progressBar.setAttribute('aria-valuenow', 0); // Resetiraj trenutnu vrednost
            document.getElementById('fingerprint-status').innerText = 'Assigning ID...'; // Resetiraj poruku
            document.getElementById('fingerprint-modal').style.display = 'flex';
            currentStep = 0;
        
            // Ponovno otvaranje WebSocket veze
            ws = new WebSocket('ws://' + window.location.hostname + ':81/');
            ws.onopen = function() {
                ws.send("start"); // Pokreni proces registracije
            };
            ws.onmessage = function(event) {
                const data = JSON.parse(event.data);
                if (data.type === 'progress') {
                    updateProgress(data.step, data.message);
                }
            };
        }


        function updateProgress(step, message) {
            const progressBar = document.querySelector('[role="progressbar"]');
            let targetValue;
            
            switch (step) {
                case 0:
                    targetValue = 25;
                    break;
                case 1:
                    targetValue = 50;
                    break;
                case 2:
                    targetValue = 75;
                    break;
                case 3:
                    targetValue = 100;
                    break;
                default:
                    targetValue = 0;
            }
        
            let currentValue = parseInt(progressBar.style.getPropertyValue('--value')) || 0;
            if (!currentValue) {
                currentValue = parseInt(progressBar.getAttribute('aria-valuenow')) || 0;
            }
        
            const increment = currentValue < targetValue ? 1 : -1;
            
            function animateProgress() {
                if (currentValue === targetValue) {
                    document.getElementById('fingerprint-status').innerText = message;
                    return;
                }
        
                currentValue += increment;
                progressBar.style.setProperty('--value', currentValue);
                progressBar.setAttribute('aria-valuenow', currentValue);
                setTimeout(animateProgress, 10);
            }
        
            animateProgress();
        }


        
      
        // Funkcija za zatvaranje modala na dugme "Close"
         function closeFingerprintModal() {
            document.getElementById('fingerprint-modal').style.display = 'none';
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send("cancel"); // Obavesti ESP32 da resetuje proces registracije
                ws.close(); // Zatvori WebSocket vezu
            }
        }


              
        document.addEventListener('DOMContentLoaded', function() {
            var form = document.getElementById('addUserForm');
            form.addEventListener('submit', function(event) {
                event.preventDefault(); // Prevent default form submission

                // Collect form data
                var formData = new FormData(form);

                // Send form data via AJAX
                var xhr = new XMLHttpRequest();
                xhr.open('POST', '/addUser', true);

                xhr.onload = function() {
                    if (xhr.status === 200) {
                        var response = JSON.parse(xhr.responseText);

                        // Clear previous error messages
                        clearErrorMessages();

                        if (response.success) {
                            alert('New User Added Successfully! PIN: ' + response.pin);
                            form.reset(); // Reset form fields
                        } else {
                            displayErrorMessages(response.errors);
                        }
                    } else {
                        alert('An error occurred while processing your request.');
                    }
                };

                xhr.send(formData);
            });

            function clearErrorMessages() {
                // Remove error messages and styles
                var usernameError = document.getElementById('username-error');
                usernameError.innerText = '';

                var usernameField = document.getElementById('username');
                usernameField.classList.remove('input-error');

                var captchaSection = document.getElementById('captcha-section');
                captchaSection.classList.remove('captcha-error');

                var captchaLabel = document.querySelector('.captcha label');
                captchaLabel.classList.remove('captcha-error-label');

                var fingerprintError = document.getElementById('fingerprint-error');
                fingerprintError.innerText = '';
            
                var fingerprintButton = document.getElementById('add-fingerprint');
                fingerprintButton.classList.remove('fingerprint-error');
            
                var voiceCommandError = document.getElementById('voice-command-error');
                voiceCommandError.innerText = '';
            
                var voiceCommandSelect = document.getElementById('voice-command');
                voiceCommandSelect.classList.remove('voice-command-error');
            }

            function displayErrorMessages(errors) {
                if (errors.username) {
                    var usernameError = document.getElementById('username-error');
                    usernameError.innerText = errors.username;

                    var usernameField = document.getElementById('username');
                    usernameField.classList.add('input-error');
                }

                if (errors.captcha) {
                    var captchaSection = document.getElementById('captcha-section');
                    captchaSection.classList.add('captcha-error');
                }

            if (errors.fingerprint) {
                var fingerprintError = document.getElementById('fingerprint-error');
                fingerprintError.innerText = "Fingerprint not added";
        
                var fingerprintButton = document.getElementById('add-fingerprint');
                fingerprintButton.classList.add('fingerprint-error');
            }
        
            if (errors.voiceCommand) {
                var voiceCommandError = document.getElementById('voice-command-error');
                voiceCommandError.innerText = errors.voiceCommand;  // Prikazuje poruku greške
        
                var voiceCommandSelect = document.getElementById('voice-command');
                voiceCommandSelect.classList.add('voice-command-error');  // Dodaje klasu za grešku
            }
          }
        });
    </script>
</body>
</html>
    )rawliteral";

  server.send(200, "text/html", pageContent);
}




// Funkcija za obradu logovanja
void handleLogin() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String enteredUsername = server.arg("username");
    String enteredPassword = server.arg("password");

    bool userExists = false;
    bool passwordCorrect = false;

    for (User &u : users) {
      if (u.username == enteredUsername) {
        userExists = true;
        if (u.pin == enteredPassword) {
          passwordCorrect = true;
          loggedIn = true;  // Korisnik je sada ulogovan
          loggedInUser = u;
          break;
        }
      }
    }

    if (!userExists || !passwordCorrect) {
      loginFailed = true;  // Postavi da je login neuspešan
      showLoginPage();  // Prikaz login stranice sa porukom o grešci
    } else {
      // Preusmeravanje na korisničku ili admin stranicu
      if (loggedInUser.username == "admin") {
        showAdminPage();  // Idi na admin panel ako je korisnik admin
      } else {
        showUserPage();  // Idi na korisničku stranicu ako nije admin
      }

      // Pošalji trenutne statuse svim komponentama korisniku koji se upravo ulogovao
      if (loggedInClientNum != -1) {
        sendAllStatusesToClient(loggedInClientNum);
      }
    }
  }
}

// Funkcija za dodavanje korisnika
void handleAddUser() {
    Serial.println("Entering handleAddUser...");
    DynamicJsonDocument jsonResponse(1024);
    JsonObject errors = jsonResponse.createNestedObject("errors");
    bool success = true;

    String enteredUsername = server.arg("username");
    String selectedVoiceCommand = server.arg("voiceCommand");
    String fingerprintAdded = getFingerprintAdded() ? "true" : "false";

    Serial.print("Retrieved fingerprintAdded from SPIFFS in handleAddUser(): ");
    Serial.println(fingerprintAdded);

    // Validacija CAPTCHA
    if (!server.hasArg("captcha")) {
        errors["captcha"] = true;
        success = false;
    }

    // Validacija korisničkog imena
    if (enteredUsername == "") {
        errors["username"] = "Please enter a username.";
        success = false;
    } else if (!isValidUsername(enteredUsername)) {
        errors["username"] = "Invalid username! Please follow the rules.";
        success = false;
    } else {
        bool userExists = false;
        for (User &u : users) {
            if (u.username == enteredUsername) {
                userExists = true;
                break;
            }
        }
        if (userExists) {
            errors["username"] = "Error: User already exists!";
            success = false;
        }
    }

    // Validacija otiska prsta
    if (fingerprintAdded != "true") {
        errors["fingerprint"] = "Fingerprint not added.";
        success = false;
    }

    // Validacija izbora voice command-a
    if (selectedVoiceCommand == "") {
        errors["voiceCommand"] = "Voice command not selected.";
        success = false;
    }

    if (!success) {
        jsonResponse["success"] = false;
        String response;
        serializeJson(jsonResponse, response);
        server.send(200, "application/json", response);
        return;
    }

    // Korišćenje već dodeljenog ID-a 
    Serial.printf("Current Fingerprint ID before saving: %d\n", fingerprintID);

    String fingerprintID_handleAddUser = String(fingerprintID); // Dodeljen u `assignFingerprintID`
    String newPin = generateUniquePin();

    // Kreiraj novog korisnika i dodaj u listu
    User newUser = {enteredUsername, newPin, fingerprintID_handleAddUser, selectedVoiceCommand};
    users.push_back(newUser);
    saveUserToFile(enteredUsername, newPin, fingerprintID_handleAddUser, selectedVoiceCommand);

    Serial.printf("Added user: Username: %s, PIN: %s, Fingerprint ID: %s, Voice Command: %s\n",
                  enteredUsername.c_str(), newPin.c_str(), fingerprintID_handleAddUser.c_str(), selectedVoiceCommand.c_str());

    // Resetuj fingerprint status
    fingerprintAdded = "false";
    saveFingerprintAdded(false);

    // Odgovor za klijenta
    jsonResponse["success"] = true;
    jsonResponse["pin"] = newPin;
    String response;
    serializeJson(jsonResponse, response);
    server.send(200, "application/json", response);

     Serial.println("Refreshing fingerprint IDs after adding user...");
    refreshFingerprintIDs();

}





// Funkcija za generisanje jedinstvenog PIN-a
String generateUniquePin() {
  String pin;
  bool unique = false;

  while (!unique) {
    // Generišite nasumičan PIN od 4 cifre
    pin = String(random(1000, 9999));

    // Proverite da li PIN već postoji
    unique = true;
    for (User &u : users) {
      if (u.pin == pin) {
        unique = false;
        break;
      }
    }
  }

  return pin;
}


// Funkcija za validaciju username-a
bool isValidUsername(const String& username) {
  if (username.length() == 0) {
    return false;
  }

  // Provera da li prvo slovo jeste slovo
  if (!isalpha(username.charAt(0))) {
    return false;
  }

  // Provera da li su svi karakteri slova ili brojevi
  for (size_t i = 0; i < username.length(); i++) {
    char c = username.charAt(i);
    if (!isalnum(c)) {
      return false;
    }
  }

  return true;
}


// Funkcija za validaciju password-a
bool isValidPassword(String password) {
  if (password.length() <= 6) return false;
  bool hasLower = false, hasUpper = false, hasDigit = false;

  for (int i = 0; i < password.length(); i++) {
    if (islower(password[i])) hasLower = true;
    if (isupper(password[i])) hasUpper = true;
    if (isdigit(password[i])) hasDigit = true;
  }

  return hasLower && hasUpper && hasDigit;
}

// Funkcija za brisanje korisnika
void handleUserDeletion() {
    if (server.hasArg("username")) {
        String usernameToDelete = server.arg("username");

        // Prevent deletion of the admin user
        if (usernameToDelete == "admin") {
            server.send(200, "text/html",
                        "<html><body><script>alert('Admin user cannot be deleted!');</script>"
                        "<meta http-equiv='refresh' content='0;url=/' /></body></html>");
            return;
        }

        // Call deleteUserFromFile first to handle file and fingerprint deletion
        deleteUserFromFile(usernameToDelete);

        // Now remove the user from the in-memory list
        for (auto it = users.begin(); it != users.end(); ++it) {
            if (it->username == usernameToDelete) {
                users.erase(it);
                break;
            }
        }

        // Refresh the admin page
        showAdminPage();
    }
}



// Funkcija za prikaz admin panela
void showAdminPage() {
  if (!loggedIn || loggedInUser.username != "admin") {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/html",
                "<html><body><script>alert('Unauthorized access! Please log in.');</script><meta http-equiv='refresh' content='0;url=/loginPage' /></body></html>");
    return;
  }

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  String page = "<html><head><style>"
                "body { background-color: #0399FA; font-family: Arial; }"
                ".container { display: flex; justify-content: center; align-items: center; flex-direction: column; height: 100vh; }"
                "table { width: 60%; border-collapse: collapse; margin: 20px auto; background-color: white; border-radius: 10px; overflow: hidden; box-shadow: 0 0 20px rgba(0, 0, 0, 0.1); }"
                "th, td { padding: 15px; text-align: left; border-bottom: 1px solid #ddd; }"
                "th { background-color: #1A4D8A; color: white; }"
                "td { color: #333; }"
                "tr:hover { background-color: #f1f1f1; }"
                "td + td { border-left: 1px solid #ddd; }"
                ".delete-button { background-color: red; color: white; padding: 10px; border: none; border-radius: 10px; cursor: pointer; }"
                ".delete-button:hover { background-color: darkred; }"
                ".logout-button { background-color: #1A4D8A; color: white; padding: 20px; font-size: 18px; border: none; border-radius: 25px; cursor: pointer; margin-top: 20px; }"
                ".logout-button:hover { background-color: #155082; }"
                "</style></head><body>"
                "<div class='container'><h1>Admin Panel</h1>"
                "<table><tr><th>Username</th><th>Action</th></tr>";

  // Prikaz admin korisnika prvo
  page += "<tr><td>admin</td><td>Default User</td></tr>";

  // Prikaz ostalih korisnika
  for (User &user : users) {
    if (user.username != "admin") {
      page += "<tr><td>" + user.username + "</td>"
              "<td><a href='/delete?username=" + user.username + "'><button class='delete-button'>Delete</button></a></td></tr>";
    }
  }

  page += "</table><a href='/logout'><button class='logout-button'>Logout</button></a></div></body></html>";
  server.send(200, "text/html", page);
}



// Funkcija za odjavu korisnika
void handleLogout() {
  loggedIn = false;
  loggedInUser = {"", ""};  // Resetovanje stanja prijave

  // Postavljanje zaglavlja da se onemogući keširanje
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  // Preusmeravanje na glavnu stranicu
  showMainPage();
}


void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));
  
  mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);
  finger.begin(57600);


  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);  // Podesi vremensku zonu za Centralnoevropsko vreme (CET)

  // Inicijalizacija SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Greška pri montiranju SPIFFS");
    return;
  }

  // Proveri da li postoji fajl sa korisnicima i inicijalizuj ako je potrebno
  initializeUserFile();

  // Učitavanje korisnika iz fajla pri pokretanju
  loadUsersFromFile();

  pinMode(ledPin, OUTPUT);      // LED dioda na pinu 5
  pinMode(blueLedPin, OUTPUT);  // Plava LED dioda na GPIO 2
  pinMode(pirPin, INPUT);       // PIR senzor

  // Inicijalizacija OLED-a
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED nije pronađen"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Waiting for motion...");
  display.display();

  // WiFi konekcija
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi povezan. IP adresa: ");
  Serial.println(WiFi.localIP());

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  server.begin();

  // Servo setup
  ESP32PWM::allocateTimer(0);
  myservo.setPeriodHertz(50);
  myservo.attach(servoPin, 1000, 2000);

  // Handleri za različite URL-ove
  server.on("/", showMainPage);              // Main page
  server.on("/loginPage", showLoginPage);    // Login page
  // Ruta za Add User stranicu (GET zahtev)
  server.on("/addUserPage", HTTP_GET, showAddUserPage);

  // Ruta za obradu Add User forme (POST zahtev)
  server.on("/addUser", HTTP_POST, handleAddUser);
  server.on("/resetFingerprintStatus", HTTP_POST, handleResetFingerprintStatus);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/delete", HTTP_GET, handleUserDeletion);
  server.on("/logout", HTTP_GET, handleLogout);


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
      refreshFingerprintIDs();

  
}

void loop() {
  // Obrada klijent server zahteva
  server.handleClient();

  // Detekcija pokreta i upravljanje LED diodom i OLED ekranom
  handlePIRSensor();

  // Ažuriranje unosa PIN-a sa fizičkog tastature i prikaz na OLED-u i web stranici
  handlePasswordInput();

  // Obrada WebSocket komunikacije
  webSocket.loop();

    if (registrationActive) {  // Proces se pokreće samo kada je registracija aktivna
        switch (currentStep) {
            case 0:
                Serial.println("Step 0: Assigning ID...");
                assignFingerprintID();
                sendProgressUpdate(0, "Assigning ID...");
                currentStep = 1;
                break;

            case 1:
                Serial.println("Step 1: Waiting for first scan...");
                if (getFingerprintImage()) {
                    sendProgressUpdate(1, "First scan complete. Remove finger.");
                    currentStep = 2;
                }
                break;

            case 2:
                Serial.println("Step 2: Waiting for second scan...");
                if (confirmSecondScan()) {
                    sendProgressUpdate(2, "Second scan complete. Creating model...");
                    currentStep = 3;
                }
                break;

                case 3:
                    Serial.println("Step 3: Saving fingerprint...");
                    if (saveFingerprint()) {
                        sendProgressUpdate(3, "Fingerprint successfully registered!");
                        registrationActive = false;  // Deaktiviraj registraciju
                        currentStep = 0;  // Resetuj korak
                    } else {
                        Serial.println("Failed to save fingerprint.");
                        resetProgress();
                    }
                    break;

        }
                delay(500);  // Mali delay kako bi omogućio da se podaci pošalju pre prekida
  }
  // Ako je korisnik ulogovan, pošalji sva trenutna stanja
  if (loggedInClientNum != -1) {
    sendAllStatusesToClient(loggedInClientNum);
  }

}
