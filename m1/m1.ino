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


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *ssid = "MS";
const char *password = "zastomezezas";

// Struktura za login korisnika
struct User {
  String username;
  String password;
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
String correctPassword = "133A";  
int attempts = 0;  
const int maxAttempts = 3;  

// Servo setup
Servo myservo;
int servoPin = 18;
int pos = 0;

unsigned long pirActivationTime = 0;
const unsigned long pirTimeout = 180000;  // 3 minuta timeout




void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String message = String((char*)payload);
    // Ovaj deo možeš koristiti ako želiš da primaš poruke sa web stranice (trenutno nije potrebno)
  }
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
        displayWelcomeMessage();  // Prikaži poruku "Welcome home!"
        moveServo();  // Otvaranje vrata (servo motor)
        delay(3000);  // Zadrži poruku 3 sekunde pre povratka na "Waiting"
        resetPIRDetection();  // Resetuj sistem na "Waiting for motion" sa normalnim fontom
      } else {
        // Logika za pogrešan PIN
        attempts++;
        if (attempts >= maxAttempts) {
          Serial.println("Previše pogrešnih pokušaja!");
          activateErrorLED();  // Aktiviraj crvenu LED
          blockTime = millis();  // Počni blokadu
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
    if (pirState == HIGH && isWaitingForMotion) {  // Samo ako čeka na pokret
        Serial.println("Pokret je detektovan!");
        String detectionTime = getFormattedTime();
        Serial.println("Vreme detekcije: " + detectionTime);
        pirActivationTime = millis();
        ledTurnOnTime = millis();  // Zabeleži vreme kada je LED uključena
        digitalWrite(ledPin, HIGH);  // Aktiviraj LED na pinu 5

        // Ažuriraj status LED diode na web stranici
        webSocket.broadcastTXT("{\"type\":\"led\",\"state\":true}"); 

        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Motion at:");
        display.setCursor(0, 20);
        display.print(detectionTime);
        display.display();
        
        // Ažuriraj status na web stranici
        webSocket.broadcastTXT("{\"type\":\"motion\",\"state\":true, \"time\":\"" + detectionTime + "\"}");

        isEnteringPin = true;  // Postavi stanje na unos PIN-a
        isWaitingForMotion = false;  // Više ne čekamo pokret
    } 
    // Provera da li je prošlo 3 minuta bez unosa šifre
    else if (isEnteringPin && millis() - ledTurnOnTime > ledOnTimeout) {
        Serial.println("Prošlo je 3 minuta, LED se isključuje.");
        resetPIRDetection();  // Vraćanje sistema u stanje čekanja na pokret
    } else if (millis() - pirActivationTime > pirTimeout) {
        resetPIRDetection();  // Resetuje se PIR detekcija ako je prošlo više od 3 minuta bez aktivnosti
    }
}

void resetPIRDetection() {
    digitalWrite(ledPin, LOW);  // Isključi LED diodu
    webSocket.broadcastTXT("{\"type\":\"led\",\"state\":false}");  // Ažuriraj web stranicu da LED više ne svetli
    digitalWrite(blueLedPin, LOW);  // Isključi plavu LED diodu
    enteredPassword = "";
    attempts = 0;
    isEnteringPin = false;  // Završava unos PIN-a
    isWaitingForMotion = true;  // Vraća se u stanje čekanja na pokret

    // Prikaz na OLED-u sa normalnom veličinom teksta
    display.clearDisplay();
    display.setTextSize(1);  // Vrati veličinu teksta na normalnu
    display.setCursor(0, 0);
    display.print("Waiting for motion...");
    display.display();

    // Ažuriraj status na web stranici
    webSocket.broadcastTXT("{\"type\":\"motion\",\"state\":false}");
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

    // Prikaz korisničke stranice sa LED sijalicom i žicom
    server.send(200, "text/html",
    "<html><head>"
    "<style>"
    "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); color: white; font-family: Arial, sans-serif; display: flex; justify-content: flex-start; height: 100vh; margin: 0; position: relative; }"

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
    ".pin-keypad-container { display: flex; flex-direction: column; align-items: flex-start; margin-left: 100px; margin-top: 100px; }"
    ".screen { background-color: #1A4D8A; color: white; padding: 10px; font-size: 18px; margin-bottom: 10px; margin-top: 50px; width: 250px; text-align: left; border-radius: 10px; display: flex; flex-direction: column; justify-content: center; align-items: center; height: 55px; }"
    ".pin-display { font-size: 22px; margin-top: 5px; }"
    ".keypad { display: grid; grid-template-columns: repeat(4, 61px); grid-gap: 8px; margin-top: 10px; justify-content: flex-start; }"
    ".key { background-color: #00d4ff; color: white; padding: 19px; font-size: 20px; border-radius: 8px; cursor: pointer; text-align: center; }"
    ".key:hover { background-color: #00a3cc; }"

    /* Stil za statusne boksove */
    ".right-panel { position: absolute; top: var(--top-offset, 250px); right: var(--right-offset, 100px); display: flex; flex-direction: column; gap: 20px; }"
    ".status-box { width: var(--status-width, 200px); padding: 10px; border: 2px solid white; text-align: center; background-color: rgba(0, 0, 0, 0.5); }"
    ".alarm { display: none; }"
    ".logout-button { background-color: #00d4ff; color: white; padding: 15px; font-size: 18px; border-radius: 20px; cursor: pointer; position: absolute; top: 20px; right: 20px; }"
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

    // Right Panel with Status Boxes
    "<div class='right-panel'>"
    "<div class='status-box' id='motion-box'>Waiting for motion...</div>"
    "<div class='status-box alarm' id='alarm-box'>ALARM</div>"
    "<div class='status-box' id='doors-box'>Closed</div>"
    "</div>"

    // Logout Button
    "<a href='/logout'><button class='logout-button'>Logout</button></a>"

     "<script>"
    "let ws = new WebSocket('ws://' + window.location.hostname + ':81/');"
    "ws.onmessage = function(event) {"
    "  let data = JSON.parse(event.data);"
    "  if (data.type === 'pin') {"
    "    document.getElementById('pin-display').innerText = '*'.repeat(data.value.length);"
    "  } else if (data.type === 'motion') {"
    "    document.getElementById('motion-box').innerText = data.state ? 'Motion detected at ' + data.time : 'Waiting for motion...';"
    "    document.getElementById('motion-box').style.borderColor = data.state ? 'green' : 'white';"
    "  } else if (data.type === 'led') {"
    "    toggleLed(data.state);"
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
    "</script>"
    
    "</body></html>");
}


// Funkcija za prikaz dodavanja korisnika
void showAddUserPage(String alertMessage = "") {
    String message = "";
    if (userAdded) {
        message = "User added successfully!";
        userAdded = false;  // Reset status after showing message
    }

    // Set headers to prevent caching
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");

    server.send(200, "text/html",
    "<html><head>"
    "<style>"
    "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial, sans-serif; }"
    ".login-container { display: flex; justify-content: center; align-items: center; height: 100vh; }"
    ".login-box { background-color: #1A4D8A; padding: 60px; border-radius: 15px; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); width: 350px; min-height: 200px; }"
    ".login-box h1 { color: #00d4ff; text-align: center; margin-bottom: 20px; font-size: 24px; }"
    ".login-box label { color: #000000; font-size: 16px; margin-bottom: 5px; display: block; }"
    ".login-box input { width: 350px; padding: 15px; margin: 15px 0; border: none; border-radius: 5px; font-size: 16px; }"
    ".login-box input[type='text'] { background-color: #112240; color: #ffffff; }"
    ".login-box input[type='submit'], .back-button { width: 300px; padding: 15px; margin: 10px 0; background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; font-size: 16px; text-align: center; text-decoration: none; display: block; margin-left: auto; margin-right: auto; }"
    ".login-box input[type='submit']:hover, .back-button:hover { background-color: #00a3cc; }"

    /* Tooltip styles */
    ".tooltip { position: relative; display: inline-block; }"
    ".tooltip .tooltiptext { visibility: hidden; width: 200px; background-color: #00d4ff; color: #fff; text-align: center; border-radius: 6px; padding: 10px; position: absolute; z-index: 1; left: 320px; top: 0px; }"
    ".tooltip:hover .tooltiptext { visibility: visible; }"

    /* CAPTCHA styles */
    ".captcha { display: flex; align-items: center; justify-content: center; margin-top: 10px; }"
    ".captcha input[type='checkbox'] { width: 20px; height: 20px; margin-right: 8px; }"
    ".captcha label { color: #00d4ff; font-family: 'Roboto', sans-serif; font-size: 16px; font-weight: bold; }"
    "</style>"
    "<script>"
    "window.onload = function() {"
    "  var alertMessage = '" + alertMessage + "';"
    "  if (alertMessage !== '') {"
    "    alert(alertMessage);"
    "  }"
    "};"
    "</script>"
    "</head><body>"
    "<div class='login-container'>"
    "<div class='login-box'>"
    "<h1>Add User</h1>"
    "<form action='/addUser' method='POST'>"
    "<label for='username'>Username:</label>"
    "<input type='text' name='username' id='username' class='tooltip'>"
    "<span class='tooltiptext'>"
    "Username rules:<br>"
    "- Must start with a letter<br>"
    "- Only letters and numbers allowed<br>"
    "- No symbols or spaces"
    "</span><br>"
    "<div class='captcha'>"
    "<input type='checkbox' name='captcha' value='not_a_robot'>"
    "<label for='captcha'>I am not a robot</label>"
    "</div>"
    "<input type='submit' value='Add User'>"
    "</form>"
    "<a href='/' class='back-button'>Back to Main Page</a>"
    "</div></div>"
    "</body></html>");
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
        }
    }
}


// Funkcija za dodavanje korisnika
void handleAddUser() {
    if (server.hasArg("username") && server.hasArg("captcha")) {  // Check if CAPTCHA is confirmed
        String newUsername = server.arg("username");

        bool userExists = false;
        for (User &u : users) {
            if (u.username == newUsername) {
                userExists = true;
                break;
            }
        }

        if (!userExists && isValidUsername(newUsername)) {  // Check if user already exists
            // Generate a unique PIN
            String newPin = generateUniquePin();

            // Add new user with generated PIN
            User newUser = {newUsername, newPin};
            users.push_back(newUser);
            userAdded = true;
            lastGeneratedPin = newPin;  // Save the generated PIN for display

            // Save the new user to the file
            saveUserToFile(newUsername, newPin);

            showAddUserPage("User added successfully!");  // Show page after adding user
        } else if (userExists) {  // If username already exists
            showAddUserPage("Error: User already exists!");  // Show error message
        } else {
            showAddUserPage("Invalid username!");  // Show invalid username message
        }
    } else {
        showAddUserPage("Please confirm you are not a robot.");  // Show CAPTCHA message
    }
}




// Funkcija za generisanje jedinstvenog PIN-a
String generateUniquePin() {
  String pin = "";
  for (int i = 0; i < 4; i++) {
    pin += String(random(0, 10));  // Generiši PIN od 4 cifre
  }
  return pin;
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

  // Postavljanje zaglavlja da se onemogući keširanje
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  // Preusmeravanje na glavnu stranicu
  showMainPage();
}


void setup() {
  Serial.begin(115200);

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
  server.on("/", showMainPage);  // Glavni prozor sa dugmadima
  server.on("/loginPage", showLoginPage);  // Login prozor
  server.on("/addUserPage", []() {
        showAddUserPage();  // Poziva showAddUserPage bez argumenata
    });

  server.on("/addUser", HTTP_POST, handleAddUser);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/addUser", HTTP_POST, handleAddUser);
  server.on("/delete", HTTP_GET, handleUserDeletion);
  server.on("/logout", HTTP_GET, handleLogout);
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
}
