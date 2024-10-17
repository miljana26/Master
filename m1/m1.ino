#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <ESP32Servo.h>  // Biblioteka za kontrolu servoa

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *ssid = "MS";
const char *password = "zastomezezas";

// Struktura za login kredencijale
struct User {
  String username;
  String password;
};

// Lista korisnika
std::vector<User> users;

WebServer server(80);
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


// Keypad setup
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String enteredPassword = "";  
String correctPassword = "123A";  
int attempts = 0;  
const int maxAttempts = 3;  

// Servo setup
Servo myservo;
int servoPin = 18;
int pos = 0;

unsigned long pirActivationTime = 0;
const unsigned long pirTimeout = 180000;  // 3 minuta timeout

void setup() {
  Serial.begin(115200);

  // Inicijalizacija SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Greška pri montiranju SPIFFS");
    return;
  }

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

  server.begin();

  // Servo setup
  ESP32PWM::allocateTimer(0);
  myservo.setPeriodHertz(50);  
  myservo.attach(servoPin, 1000, 2000); 

  // Start server handler
  server.on("/", showMainPage);  
}

void loop() {
  server.handleClient();  
  handlePIRSensor();  
  handlePasswordInput();  
}


// Funkcija za unos PIN-a
void handlePasswordInput() {
  if (attempts >= maxAttempts) {
    // Ako je previše pogrešnih pokušaja, ne dozvoljava dalji unos
    Serial.println("Unos je blokiran zbog previše pogrešnih pokušaja!");
    return;
  }

  char key = keypad.getKey();

  if (key) {
    if (!isEnteringPin) {
      // Kada korisnik počne da unosi PIN, prebaci sistem u stanje unosa PIN-a
      isEnteringPin = true;
      isWaitingForMotion = false;  // Više ne čekamo pokret
    }

    // Prvo ažuriraj promenljivu `enteredPassword`
    if (key == '#') {
      Serial.print("Unos završen: ");
      Serial.println(enteredPassword);
      if (enteredPassword == correctPassword) {
        Serial.println("Ispravan PIN!");
        displayWelcomeMessage();  // Prikaži poruku "Welcome home!"
        moveServo();
        delay(3000);  // Prikaži poruku 3 sekunde pre povratka na "Waiting"
        resetPIRDetection();  // Resetuj na "Waiting for motion"
      } else {
        attempts++;
        if (attempts >= maxAttempts) {
          Serial.println("Previše pogrešnih pokušaja!");
          activateErrorLED();  // Aktiviraj crvenu LED za pogrešne pokušaje
          blockTime = millis();  // Zabeleži vreme kada je unos blokiran
          delay(3000);  // Ostaviti vreme da LED svetli
          resetPIRDetection();  // Resetuj sistem i vrati ga na "Waiting for motion"
        } else {
          Serial.println("Neispravan PIN!");
        }
      }
      enteredPassword = "";  
    } else if (key == '*') {  
      if (enteredPassword.length() > 0) {
        enteredPassword.remove(enteredPassword.length() - 1);
      }
    } else {
      enteredPassword += key;  // Dodaj uneseni karakter pre nego što ažuriraš OLED
    }

    // Ažuriraj OLED ekran samo kada je unos u toku
    if (isEnteringPin) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);  // Vraća veličinu teksta na standardnu
      display.print("Enter pin:");
      display.setCursor(0, 20);
      for (int i = 0; i < enteredPassword.length(); i++) {
        display.print("*");
      }
      display.display();
    }
  }
}


void resetPIRDetection() {
  digitalWrite(ledPin, LOW);  
  digitalWrite(blueLedPin, LOW);  // Isključi plavu LED diodu
  enteredPassword = "";
  attempts = 0;
  isEnteringPin = false;  // Završava unos PIN-a
  isWaitingForMotion = true;  // Vraća se u stanje čekanja na pokret
  display.clearDisplay();
  display.setTextSize(1);  // Vrati veličinu teksta na normalnu
  display.setCursor(0, 0);
  display.print("Waiting for motion...");
  display.display();
}

// Funkcija za detekciju pokreta PIR senzora
void handlePIRSensor() {
  int pirState = digitalRead(pirPin);

  if (pirState == HIGH && isWaitingForMotion) {  // Samo ako čeka na pokret
    Serial.println("Pokret je detektovan!");
    pirActivationTime = millis();
    digitalWrite(ledPin, HIGH);  // Aktiviraj LED na pinu 5
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Enter pin:");
    display.display();
    isEnteringPin = true;  // Postavi stanje na unos PIN-a
    isWaitingForMotion = false;  // Više ne čekamo pokret
  } else if (millis() - pirActivationTime > pirTimeout) {
    resetPIRDetection();
  }
}

// Funkcija koja prikazuje poruku "Welcome home!" na OLED-u
void displayWelcomeMessage() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);  // Povećaj veličinu teksta za "Welcome home"
  display.print("Welcome");
  display.setCursor(0, 30);  // Drugi red
  display.print("home!");
  display.display();
}



// Funkcija za pomeranje servoa
void moveServo() {
  for (pos = 0; pos <= 180; pos += 1) {
    myservo.write(pos);
    delay(10);
  }
  delay(1000);
  for (pos = 180; pos >= 0; pos -= 1) {
    myservo.write(pos);
    delay(10);
  }
}

// Aktiviraj plavu LED diodu na ESP32 kada ima previše neispravnih pokušaja
void activateErrorLED() {
  digitalWrite(blueLedPin , HIGH);  
  delay(5000);  
  digitalWrite(blueLedPin , LOW);
}


// Funkcija za inicijalizaciju fajla sa korisnicima
void initializeUserFile() {
  // Proveravamo da li postoji fajl
  if (!SPIFFS.exists("/users.txt")) {
    // Kreiraj fajl ako ne postoji i dodaj admin/admin korisnika
    File file = SPIFFS.open("/users.txt", "w");
    if (!file) {
      Serial.println("Greška pri kreiranju fajla za korisnike");
      return;
    }

    // Dodaj default admin korisnika u fajl
    file.println("admin,admin");
    file.close();
    Serial.println("Kreiran fajl sa default admin korisnikom");
  } else {
    // Proveri da li admin korisnik postoji u fajlu
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
      file.println("admin,admin");
      file.close();
      Serial.println("Dodao admin korisnika u postojeći fajl");
    }
  }
}

// Funkcija za učitavanje korisnika iz fajla
void loadUsersFromFile() {
  File file = SPIFFS.open("/users.txt", "r");
  if (!file) {
    Serial.println("Neuspešno otvaranje fajla za korisnike");
    return;
  }

  users.clear();  // Očisti trenutnu listu korisnika

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();  // Dodaj trim() da ukloni prazne prostore
    int separatorIndex = line.indexOf(',');
    if (separatorIndex > 0) {
      String username = line.substring(0, separatorIndex);
      username.trim();  // Dodaj trim() nakon substring
      String password = line.substring(separatorIndex + 1);
      password.trim();  // Dodaj trim() nakon substring
      users.push_back({username, password});
    }
  }

  file.close();
}

// Funkcija za dodavanje korisnika u fajl
void saveUserToFile(const String& username, const String& password) {
  File file = SPIFFS.open("/users.txt", "a");  // Otvaranje fajla u append modu
  if (!file) {
    Serial.println("Neuspešno otvaranje fajla za dodavanje korisnika");
    return;
  }

  file.println(username + "," + password);  // Sačuvaj korisnika u formatu "username,password"
  file.close();
}

// Funkcija za brisanje korisnika iz fajla
void deleteUserFromFile(const String& usernameToDelete) {
  File file = SPIFFS.open("/users_temp.txt", "w");  // Privremeni fajl
  if (!file) {
    Serial.println("Neuspešno otvaranje privremenog fajla");
    return;
  }

  for (User &u : users) {
    // Ne dozvoli brisanje admin korisnika
    if (u.username != usernameToDelete && u.username != "admin") {
      file.println(u.username + "," + u.password);  // Piši samo korisnike koji nisu obrisani
    }
  }

  file.close();

  SPIFFS.remove("/users.txt");  // Izbriši originalni fajl
  SPIFFS.rename("/users_temp.txt", "/users.txt");  // Preimenuj privremeni fajl u originalni
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
  "</div></div></body></html>");
}

// Funkcija za prikaz dodavanja korisnika
void showAddUserPage() {
    String message = "";
    if (userAdded) {
        message = "<p style='color:green; text-align:center;'>User added successfully!</p>";
        userAdded = false;  // Resetuj status dodavanja nakon prikaza poruke
    }

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
    ".bubble { background-color: #f1f1f1; border-radius: 10px; padding: 10px; width: 300px; margin-left: 30px; font-family: 'Helvetica Neue', sans-serif; font-size: 14px; color: #333333; display: flex; justify-content: center; align-items: center; flex-direction: column; height: auto; margin-top: 30px; }"
    ".form-container { display: flex; justify-content: space-between; align-items: center; }"  // Poravnavanje oba prozora
    ".rules-header { font-size: 18px; font-weight: bold; margin-bottom: 10px; font-family: 'Georgia', serif; }"
    ".rules-text { font-size: 14px; line-height: 1.5; color: #333333; font-family: 'Georgia', serif; text-align: center; }"
    "</style>"
    "<script>"
    "function validateForm() {"
    "  var username = document.querySelector('input[name=\"username\"]').value;"
    "  var password = document.querySelector('input[name=\"password\"]').value;"
    "  if (!validateUsername(username)) {"
    "    alert('Username must start with a letter and contain only letters and numbers.');"
    "    return false;"
    "  }"
    "  if (!validatePassword(password)) {"
    "    alert('Password must be longer than 6 characters and contain at least one lowercase letter, one uppercase letter, and one number.');"
    "    return false;"
    "  }"
    "  return true;"
    "}"
    "function validateUsername(username) {"
    "  var regex = /^[a-zA-Z][a-zA-Z0-9]*$/;"
    "  return regex.test(username);"
    "}"
    "function validatePassword(password) {"
    "  if (password.length <= 6) return false;"
    "  if (!/[a-z]/.test(password) || !/[A-Z]/.test(password) || !/[0-9]/.test(password)) return false;"
    "  return true;"
    "}"
    "</script>"
    "</head><body>"
    "<div class='login-container'>"
    "<div class='form-container'>"
    "<div class='login-box'>"
    "<h1>Add User</h1>"
    "<form action='/addUser' method='POST' onsubmit='return validateForm()'>"
    "Username: <input type='text' name='username'><br>"
    "Password: <input type='password' name='password'><br>"
    "<input type='submit' value='Add User'>"
    "</form>"
    "<a href='/' class='back-button'>Back to Main Page</a>"
    + message +
    "</div>"
    "<div class='bubble'>"
    "<p class='rules-header'>Username rules:</p>"
    "<p class='rules-text'>- Must not start with a number<br>- No symbols allowed<br>- Only letters and numbers are valid</p>"
    "<p class='rules-header'>Password rules:</p>"
    "<p class='rules-text'>- At least one uppercase letter<br>- At least one lowercase letter<br>- At least one number<br>- Longer than 6 characters</p>"
    "</div>"
    "</div></div></body></html>");
}


// Funkcija za prikaz login prozora
void showLoginPage(String errorMessage = "") {
  String message = "";
  if (errorMessage != "") {
    message = "<p style='color:red; text-align:center; margin-top:20px;'>" + errorMessage + "</p>";
  }

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
  + message +  // Poruka o grešci ispod dugmeta
  "</div></div></body></html>");
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
        if (u.password == enteredPassword) {
          passwordCorrect = true;
          loggedIn = true;
          loggedInUser = u;
          break;
        }
      }
    }

    if (!userExists || !passwordCorrect) {
      // Prikazivanje greške direktno na login stranici
      showLoginPage("Wrong username or password!");
    } else {
      if (loggedInUser.username == "admin") {
        showAdminPage();
      } else {
        showUserPage();
      }
    }
  }
}


// Funkcija za dodavanje korisnika
void handleAddUser() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String newUsername = server.arg("username");
    String newPassword = server.arg("password");

    bool userExists = false;
    for (User &u : users) {
      if (u.username == newUsername) {
        userExists = true;
        break;
      }
    }

    if (!userExists && isValidUsername(newUsername) && isValidPassword(newPassword)) {
      User newUser = {newUsername, newPassword};
      users.push_back(newUser);
      userAdded = true;

      // Sačuvaj novog korisnika u fajl
      saveUserToFile(newUsername, newPassword);

      showAddUserPage();  // Prikaži stranicu nakon dodavanja korisnika
    } else {
      server.send(200, "text/html",
      "<html><body>"
      "<script>alert('Error: User already exists or invalid input!');</script>"
      "<meta http-equiv='refresh' content='0;url=/addUserPage' />"
      "</body></html>");
    }
  }
}

// Funkcija za validaciju username-a
bool isValidUsername(String username) {
  if (username.length() == 0) return false;
  if (!isAlpha(username[0])) return false;
  for (int i = 0; i < username.length(); i++) {
    if (!isAlphaNumeric(username[i])) return false;
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

    // Ne dozvoli brisanje admin korisnika
    if (usernameToDelete == "admin") {
      server.send(200, "text/html",
      "<html><body><script>alert('Admin user cannot be deleted!');</script>"
      "<meta http-equiv='refresh' content='0;url=/' /></body></html>");
      return;
    }

    for (auto it = users.begin(); it != users.end(); ++it) {
      if (it->username == usernameToDelete) {
        users.erase(it);
        break;
      }
    }

    // Izbriši korisnika iz fajla
    deleteUserFromFile(usernameToDelete);

    showAdminPage();
  }
}

// Funkcija za uključivanje LED-a na GPIO 2
void handleLEDOn() {
  digitalWrite(2, HIGH);  // Uključi LED na GPIO 2
  showUserPage();  // Vrati korisnika na user page umesto da ga izloguješ
}

// Funkcija za isključivanje LED-a na GPIO 2
void handleLEDOff() {
  digitalWrite(2, LOW);  // Isključi LED na GPIO 2
  showUserPage();  // Vrati korisnika na user page umesto da ga izloguješ
}


// Funkcija za prikaz korisničke stranice
void showUserPage() {
  if (!loggedIn) {
    // Ako korisnik nije prijavljen, preusmeri ga na login
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/html",
    "<html><body><script>alert('Unauthorized access! Please log in.');</script><meta http-equiv='refresh' content='0;url=/loginPage' /></body></html>");
    return;
  }

  // Prikaz korisničke stranice
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html",
  "<html><body>"
  "<h2>LED Control</h2>"
  "Click <a href=\"/H\">here</a> to turn the LED on.<br>"
  "Click <a href=\"/L\">here</a> to turn the LED off.<br>"
  "<div style='margin-top: 20px;'><a href='/logout'><button class='logout-button'>Logout</button></a></div>"
  "</body></html>");
}


// Funkcija za prikaz admin panela
void showAdminPage() {
  if (!loggedIn || loggedInUser.username != "admin") {
    // Ako korisnik nije prijavljen ili nije admin, preusmeri ga na login
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "text/html",
    "<html><body><script>alert('Unauthorized access! Please log in.');</script><meta http-equiv='refresh' content='0;url=/loginPage' /></body></html>");
    return;
  }

  // Prikaz admin stranice ako je prijavljen kao admin
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

  for (User &user : users) {
    page += "<tr><td>" + user.username + "</td>";
    if (user.username != "admin") {
      page += "<td><a href='/delete?username=" + user.username + "'><button class='delete-button'>Delete</button></a></td></tr>";
    } else {
      page += "<td>Default User</td></tr>";
    }
  }

  page += "</table><a href='/logout'><button class='logout-button'>Logout</button></a></div></body></html>";
  server.send(200, "text/html", page);
}


// Funkcija za odjavu korisnika
void handleLogout() {
  loggedIn = false;
  loggedInUser = {"", ""};  // Resetovanje stanja prijave

  // Postavljanje header-a da se onemogući keširanje
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  // Preusmeravanje na glavnu stranicu
  showMainPage();
}
