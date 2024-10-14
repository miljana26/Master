#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

const char *ssid = "MS";
const char *password = "zastomezezas";

// Struktura za login kredencijale
struct User {
  String username;
  String password;
};

// Lista korisnika
std::vector<User> users;  // Lista korisnika se sada puni iz SPIFFS fajla

WebServer server(80);  // Kreiraj WebServer objekat
bool loggedIn = false;
bool userAdded = false;  // Varijabla za status dodavanja korisnika
User loggedInUser = {"", ""};  // Trenutno prijavljeni korisnik

void setup() {
  Serial.begin(115200);

  // Inicijalizacija SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Greška pri montiranju SPIFFS");
    return;
  }

  // Proveri da li postoji fajl sa korisnicima i inicijalizuj ako je potrebno
  initializeUserFile();

  // Učitavanje korisnika iz fajla pri pokretanju
  loadUsersFromFile();

  pinMode(2, OUTPUT);  // LED pin

  // Povezivanje na WiFi mrežu
  Serial.println();
  Serial.print("Povezivanje na ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi povezan.");
  Serial.println("IP adresa: ");
  Serial.println(WiFi.localIP());

  server.begin();

  // Handleri za različite URL-ove
  server.on("/", showMainPage);  // Glavni prozor sa dugmadima
  server.on("/loginPage", showLoginPage);  // Login prozor
  server.on("/addUserPage", showAddUserPage);  // Dodavanje korisnika
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/addUser", HTTP_POST, handleAddUser);
  server.on("/delete", HTTP_GET, handleUserDeletion);
  server.on("/H", HTTP_GET, handleLEDOn);
  server.on("/L", HTTP_GET, handleLEDOff);
  server.on("/logout", HTTP_GET, handleLogout);
}

void loop() {
  server.handleClient();  // Obrada HTTP zahteva
}

// Funkcija za inicijalizaciju fajla sa korisnicima
void initializeUserFile() {
  if (!SPIFFS.exists("/users.txt")) {
    File file = SPIFFS.open("/users.txt", "w");  // Kreiraj fajl ako ne postoji
    if (!file) {
      Serial.println("Greška pri kreiranju fajla za korisnike");
      return;
    }

    // Dodaj default admin korisnika u fajl
    file.println("admin,admin");
    file.close();
    Serial.println("Kreiran fajl sa default admin korisnikom");
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
    line.trim();
    int separatorIndex = line.indexOf(',');
    if (separatorIndex > 0) {
      String username = line.substring(0, separatorIndex);
      String password = line.substring(separatorIndex + 1);
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
  "</style>"
  "<script>"
  "function validateForm() {"
  "  var username = document.querySelector('input[name=\"username\"]').value;"
  "  var password = document.querySelector('input[name=\"password\"]').value;"
  "  var confirmPassword = document.querySelector('input[name=\"confirmPassword\"]').value;"
  "  if (!validateUsername(username)) {"
  "    alert('Username must start with a letter and contain only letters and numbers.');"
  "    return false;"
  "  }"
  "  if (!validatePassword(password)) {"
  "    alert('Password must be longer than 6 characters and contain at least one lowercase letter, one uppercase letter, and one number.');"
  "    return false;"
  "  }"
  "  if (password !== confirmPassword) {"
  "    alert('Passwords do not match.');"
  "    return false;"
  "  }"
  "  return true;"
  "}"
  "</script>"
  "</head><body>"
  "<div class='login-container'>"
  "<div class='login-box'>"
  "<h1>Add User</h1>"
  "<form action='/addUser' method='POST' onsubmit='return validateForm()'>"
  "Username: <input type='text' name='username'><br>"
  "Password: <input type='password' name='password'><br>"
  "Confirm Password: <input type='password' name='confirmPassword'><br>"
  "<input type='submit' value='Add User'>"
  "</form>"
  "<a href='/' class='back-button'>Back to Main Page</a>"
  + message +
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
      server.send(200, "text/html",
      "<html><body>"
      "<script>"
      "alert('Wrong username or password!');"
      "window.location.href = '/loginPage';"
      "</script>"
      "</body></html>");
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

// Funkcija za uključivanje LED-a
void handleLEDOn() {
  digitalWrite(2, HIGH);
  server.send(200, "text/html",
  "<html><body><h1>LED is ON</h1><meta http-equiv='refresh' content='1;url=/' /></body></html>");
}

// Funkcija za isključivanje LED-a
void handleLEDOff() {
  digitalWrite(2, LOW);
  server.send(200, "text/html",
  "<html><body><h1>LED is OFF</h1><meta http-equiv='refresh' content='1;url=/' /></body></html>");
}

// Funkcija za prikaz korisničke stranice
void showUserPage() {
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
  showMainPage();
}
