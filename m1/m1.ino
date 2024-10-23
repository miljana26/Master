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
  server.on("/addUserPage", showAddUserPage);  // Dodavanje korisnika
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


void resetPIRDetection() {
    digitalWrite(ledPin, LOW);  // Isključi LED diodu
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


// Funkcija za detekciju pokreta PIR senzora
void handlePIRSensor() {
    int pirState = digitalRead(pirPin);
    if (pirState == HIGH && isWaitingForMotion) {  // Samo ako čeka na pokret
        Serial.println("Pokret je detektovan!");
        String detectionTime = getFormattedTime();
        Serial.println("Vreme detekcije: " + detectionTime);
        pirActivationTime = millis();
        ledTurnOnTime = millis();  // Zabeleži vreme kada je LED uključena
        digitalWrite(ledPin, HIGH);  // Aktiviraj LED na pinu 5
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

    // Prikaz korisničke stranice sa LED krugom, tastaturom (estetika), prikazom unosa PIN-a i logout dugmetom
    server.send(200, "text/html",
    "<html><head>"
    "<style>"
    "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); color: white; font-family: Arial, sans-serif; text-align: center; }"
    ".led-circle { width: 100px; height: 100px; border-radius: 50%; background-color: red; margin: 20px auto; }"
    ".screen { background-color: #1A4D8A; color: white; padding: 10px; font-size: 20px; margin: 20px auto; width: 250px; height: 50px; text-align: center; border-radius: 10px; }"
    ".keypad { display: grid; grid-template-columns: repeat(4, 50px); grid-gap: 10px; margin: 20px auto; justify-content: center; }"
    ".key { background-color: #00d4ff; color: white; padding: 20px; font-size: 18px; border-radius: 5px; cursor: pointer; }"
    ".status { font-size: 16px; margin-top: 20px; }"
    ".logout-button { background-color: #00d4ff; color: white; padding: 15px 40px; font-size: 18px; border-radius: 25px; position: absolute; top: 20px; right: 20px; cursor: pointer; }"
    ".logout-button:hover { background-color: #00a3cc; }"
    "</style>"
    "</head><body>"

    "<div class='led-circle' id='ledCircle'></div>"
    "<div class='screen' id='screen'>Entered PIN:</div>"

    // Tastatura koja je samo estetska, bez funkcionalnosti
    "<div class='keypad'>"
    "<div class='key'>1</div><div class='key'>2</div><div class='key'>3</div><div class='key'>A</div>"
    "<div class='key'>4</div><div class='key'>5</div><div class='key'>6</div><div class='key'>B</div>"
    "<div class='key'>7</div><div class='key'>8</div><div class='key'>9</div><div class='key'>C</div>"
    "<div class='key'>*</div><div class='key'>0</div><div class='key'>#</div><div class='key'>D</div>"
    "</div>"

    "<div class='status' id='status'>Waiting for motion...</div>"

    "<a href='/logout'><button class='logout-button'>Logout</button></a>"

    "<script>"
    "let ws = new WebSocket('ws://' + window.location.hostname + ':81/');"
    "ws.onmessage = function(event) {"
    "  let data = JSON.parse(event.data);"
    "  if (data.type === 'motion') {"
    "    document.getElementById('status').innerText = data.state ? 'Motion detected at ' + data.time : 'Waiting for motion...';"
    "    toggleLed(data.state);"
    "  } else if (data.type === 'pin') {"
    "    document.getElementById('screen').innerText = 'Entered PIN: ' + '*'.repeat(data.value.length);"
    "  }"
    "};"

    "function toggleLed(isOn) {"
    "  const ledCircle = document.getElementById('ledCircle');"
    "  ledCircle.style.backgroundColor = isOn ? 'green' : 'red';"
    "} "
    "</script>"

    "</body></html>"
    );
}


// Funkcija za prikaz dodavanja korisnika
void showAddUserPage() {
    String message = "";
    if (userAdded) {
        message = "<p style='color:green; text-align:center;'>User added successfully!</p>";
        userAdded = false;  // Resetuj status dodavanja nakon prikaza poruke
    }

    // Zaglavlja za sprečavanje keširanja
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
    ".login-box label { color: #000000; font-size: 16px; margin-bottom: 5px; display: block; }" // Label iznad polja za unos
    ".login-box input { width: 350px; padding: 15px; margin: 15px 0; border: none; border-radius: 5px; font-size: 16px; }"
    ".login-box input[type='text'] { background-color: #112240; color: #000000; }"
    ".login-box input[type='submit'], .back-button { width: 300px; padding: 15px; margin: 10px 0; background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; font-size: 16px; text-align: center; text-decoration: none; display: block; margin-left: auto; margin-right: auto; }"
    ".login-box input[type='submit']:hover, .back-button:hover { background-color: #00a3cc; }"
    
    /* Tooltip stilovi */
    ".tooltip { position: relative; display: inline-block; }"
    ".tooltip .tooltiptext { visibility: hidden; width: 200px; background-color: #00d4ff; color: #fff; text-align: center; border-radius: 6px; padding: 10px; position: absolute; z-index: 1; left: 320px; top: 0px; }"
    ".tooltip:hover .tooltiptext { visibility: visible; }"

    /* Stilovi za CAPTCHA */
    ".captcha { display: flex; align-items: center; justify-content: center; margin-top: 10px; }"
    ".captcha input[type='checkbox'] { width: 20px; height: 20px; margin-right: 8px; }"
    ".captcha label { color: #00d4ff; font-family: 'Roboto', sans-serif; font-size: 16px; font-weight: bold; }"
    "</style>"
    "<script>"
    "function validateForm() {"
    "  var username = document.querySelector('input[name=\"username\"]').value;"
    "  var captchaChecked = document.querySelector('input[name=\"captcha\"]').checked;"
    "  if (!validateUsername(username)) {"
    "    alert('Username must start with a letter and contain only letters and numbers.');"
    "    return false;"
    "  }"
    "  if (!captchaChecked) {"
    "    alert('Please confirm you are not a robot.');"
    "    return false;"
    "  }"
    "  return true;"
    "} "
    "function validateUsername(username) {"
    "  var regex = /^[a-zA-Z][a-zA-Z0-9]*$/;"
    "  return regex.test(username);"
    "} "
    "</script>"
    "</head><body>"
    "<div class='login-container'>"
    "<div class='login-box'>"
    "<h1>Add User</h1>"
    "<form action='/addUser' method='POST' onsubmit='return validateForm()'>"
    "<label for='username'>Username:</label>"  // Username iznad polja za unos
    "<div class='tooltip'>"
    "<input type='text' name='username' id='username'><br>"
    "<span class='tooltiptext'>"
    "Username rules:<br>"
    "- Must start with a letter<br>"
    "- Only letters and numbers allowed<br>"
    "- No symbols or spaces"
    "</span>"
    "</div>"
    "<div class='captcha'>"
    "<input type='checkbox' name='captcha' value='not_a_robot'>"
    "<label for='captcha'>I am not a robot</label>"
    "</div>"
    "<input type='submit' value='Add User'>"
    "</form>"
    "<a href='/' class='back-button'>Back to Main Page</a>"
    + message +  // Uspešna poruka dodavanja korisnika
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
  if (server.hasArg("username") && server.hasArg("captcha")) {
    String newUsername = server.arg("username");

    bool userExists = false;
    for (User &u : users) {
      if (u.username == newUsername) {
        userExists = true;
        break;
      }
    }

    if (!userExists && isValidUsername(newUsername)) {
      // Generiši jedinstveni PIN
      String newPin = generateUniquePin();

      // Dodaj novog korisnika sa generisanim PIN-om
      User newUser = {newUsername, newPin};
      users.push_back(newUser);
      userAdded = true;
      lastGeneratedPin = newPin;  // Sačuvaj generisani PIN za prikaz

      // Sačuvaj novog korisnika u fajl
      saveUserToFile(newUsername, newPin);

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
