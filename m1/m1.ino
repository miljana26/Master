#include <WiFi.h>
#include <WebServer.h>

const char *ssid = "MS";
const char *password = "zastomezezas";

// Struktura za login kredencijale
struct User {
  String username;
  String password;
};

// Lista korisnika
std::vector<User> users = { {"admin", "admin"} };  // Predefinisan admin korisnik

WebServer server(80);  // Kreiraj WebServer objekat
bool loggedIn = false;
bool userAdded = false;  // Varijabla za status dodavanja korisnika
User loggedInUser = {"", ""};  // Trenutno prijavljeni korisnik

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);  // LED pin

  // Povezivanje na WiFi mrežu
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server.begin();

  // Handleri za različite URL-ove
  server.on("/", handleRoot);
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

void handleRoot() {
  if (!loggedIn) {
    showLoginPage();
  } else if (loggedInUser.username == "admin") {
    showAdminPage();
  } else {
    showUserPage();
  }
}

// Funkcija za prikaz login stranice
void showLoginPage() {
  String message = "";
  if (userAdded) {
    message = "<p style='color:green;'>User added successfully!</p>";
    userAdded = false;  // Resetuj status dodavanja nakon prikaza poruke
  }

  server.send(200, "text/html",
  "<html><head>"
  "<style>"
  "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial; }"
  ".login-container { display: flex; justify-content: center; align-items: center; height: 100vh; }"
  ".login-box { background-color: #1A4D8A; padding: 60px; border-radius: 15px; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); width: 400px; }"
  ".login-box h1 { color: #00d4ff; text-align: center; margin-bottom: 30px; font-size: 24px; }"
  ".login-box input { width: 100%; padding: 15px; margin: 15px 0; border: none; border-radius: 5px; font-size: 16px; }"
  ".login-box input[type='text'], .login-box input[type='password'] { background-color: #112240; color: #ffffff; }"
  ".login-box input[type='submit'], .login-box input[type='button'] { width: 48%; padding: 15px; margin: 10px 1%; background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; font-size: 16px; }"
  ".login-box input[type='submit']:hover, .login-box input[type='button']:hover { background-color: #00a3cc; }"
  ".logout-button { background-color: red; color: white; padding: 20px; font-size: 18px; border: none; border-radius: 25px; cursor: pointer; margin-top: 20px; }"
  ".logout-button:hover { background-color: darkred; }"
  "</style>"
  "<script>"
  "function addUser() {"
  "  var username = document.querySelector('input[name=\"username\"]').value;"
  "  var password = document.querySelector('input[name=\"password\"]').value;"
  "  if (validateUsername(username) && validatePassword(password)) {"
  "    document.getElementById('newusername').value = username;"
  "    document.getElementById('newpassword').value = password;"
  "    document.getElementById('adduser').submit();"
  "  }"
  "}"
  "function validateUsername(username) {"
  "  var regex = /^[a-zA-Z][a-zA-Z0-9]*$/;"
  "  if (!regex.test(username)) {"
  "    alert('Username must start with a letter and contain only letters and numbers.');"
  "    return false;"
  "  }"
  "  return true;"
  "}"
  "function validatePassword(password) {"
  "  if (password.length <= 6) {"
  "    alert('Password must be longer than 6 characters.');"
  "    return false;"
  "  }"
  "  if (!/[a-z]/.test(password) || !/[A-Z]/.test(password) || !/[0-9]/.test(password)) {"
  "    alert('Password must contain at least one lowercase letter, one uppercase letter, and one number.');"
  "    return false;"
  "  }"
  "  return true;"
  "}"
  "</script>"
  "</head><body>"
  "<div class='login-container'><div class='login-box'>"
  "<h1>Login</h1>"
  "<form action='/login' method='POST'>"
  "Username: <input type='text' name='username'><br>"
  "Password: <input type='password' name='password'><br>"
  "<div style='display: flex; justify-content: space-between;'>"
  "<input type='button' value='Add User' onclick=\"addUser();\">"
  "<input type='submit' value='Login'>"
  "</div>"
  "</form>"
  "<form id='adduser' action='/addUser' method='POST' style='display:none;'>"
  "<input type='hidden' name='username' id='newusername'>"
  "<input type='hidden' name='password' id='newpassword'>"
  "</form>"
  + message +
  "</div></div></body></html>");
}

// Funkcija za obradu logovanja
void handleLogin() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String enteredUsername = server.arg("username");
    String enteredPassword = server.arg("password");

    bool userExists = false;
    for (User &u : users) {
      if (u.username == enteredUsername) {
        userExists = true;
        if (u.password == enteredPassword) {
          loggedIn = true;
          loggedInUser = u;
          break;
        }
        break;
      }
    }

    if (!userExists) {
      server.send(200, "text/html",
      "<html><body>"
      "<div style='text-align: center;'>"
      "<h2 style='color: red;'>Wrong username or password</h2>"
      "<p>Returning to login...</p>"
      "</div>"
      "<meta http-equiv='refresh' content='3;url=/' />"
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

    if (!userExists && !newUsername.isEmpty() && !newPassword.isEmpty()) {
      User newUser = {newUsername, newPassword};
      users.push_back(newUser);
      userAdded = true;
      handleRoot();
    } else {
      server.send(200, "text/html",
      "<html><body>"
      "<p>Error: User already exists or invalid input!</p>"
      "<meta http-equiv='refresh' content='2;url=/' />"
      "</body></html>");
    }
  }
}

// Funkcija za brisanje korisnika
void handleUserDeletion() {
  if (server.hasArg("username")) {
    String usernameToDelete = server.arg("username");
    for (auto it = users.begin(); it != users.end(); ++it) {
      if (it->username == usernameToDelete) {
        users.erase(it);
        break;
      }
    }
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
  showLoginPage();
}
