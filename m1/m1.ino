#include <WiFi.h>
#include <WebServer.h>  // Uvezi pravu biblioteku

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

// Funkcija za prikaz login stranice (sa POST metodom)
void showLoginPage() {
  server.send(200, "text/html",
  "<html><head>"
  "<style>"
  "body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial; }"
  ".login-container { display: flex; justify-content: center; align-items: center; height: 100vh; }"
  ".login-box { background-color: #1A4D8A; padding: 40px; border-radius: 10px; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); }"
  ".login-box h1 { color: #00d4ff; text-align: center; margin-bottom: 20px; }"
  ".login-box input { width: 100%; padding: 10px; margin: 10px 0; border: none; border-radius: 5px; }"
  ".login-box input[type='text'], .login-box input[type='password'] { background-color: #112240; color: #ffffff; }"
  ".login-box input[type='submit'] { background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; }"
  ".login-box input[type='submit']:hover { background-color: #00a3cc; }"
  "</style></head><body>"
  "<div class='login-container'><div class='login-box'>"
  "<h1>Login</h1><form action='/login' method='POST'>"  // Promenjeno na POST
  "Username: <input type='text' name='username'><br>"
  "Password: <input type='password' name='password'><br>"
  "<input type='submit' value='Login'>"
  "</form></div></div></body></html>");
}

// Funkcija za obradu logovanja
void handleLogin() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String enteredUsername = server.arg("username");
    String enteredPassword = server.arg("password");

    // Debug login podaci
    Serial.print("Pokušaj logina sa korisnikom: ");
    Serial.println(enteredUsername);
    Serial.print("Pokušaj lozinke: ");
    Serial.println(enteredPassword);

    // Provera kredencijala
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

    // Kreiranje novog korisnika ako ne postoji
    if (!userExists && !enteredUsername.isEmpty() && !enteredPassword.isEmpty()) {
      User newUser = {enteredUsername, enteredPassword};
      users.push_back(newUser);
      loggedIn = true;
      loggedInUser = newUser;
    }

    // Prikaz stranice na osnovu stanja prijave
    if (loggedIn) {
      if (loggedInUser.username == "admin") {
        showAdminPage();
      } else {
        showUserPage();
      }
    } else {
      server.send(200, "text/html", "<html><body><h1>Invalid login. Please try again.</h1></body></html>");
    }
  }
}

// Funkcija za prikaz korisničke stranice
void showUserPage() {
  server.send(200, "text/html",
  "<html><body>"
  "Click <a href=\"/H\">here</a> to turn the LED on.<br>"
  "Click <a href=\"/L\">here</a> to turn the LED off.<br>"
  "</body></html>");
}

// Funkcija za prikaz admin panela (prikaz admina bez opcije brisanja)
void showAdminPage() {
  String page = "<html><head><style>body { background-color: #0399FA; font-family: Arial; }"
                "table { width: 100%; border-collapse: collapse; }"
                "th, td { padding: 10px; border: 1px solid black; }"
                "</style></head><body><h1>Admin Panel</h1>"
                "<table><tr><th>Username</th><th>Action</th></tr>";

  // Loop kroz sve korisnike
  for (User &user : users) {
    page += "<tr><td>" + user.username + "</td>";
    if (user.username != "admin") {
      page += "<td><a href='/delete?username=" + user.username + "'>Delete</a></td></tr>";
    } else {
      page += "<td>Admin</td></tr>";
    }
  }

  page += "</table><br><a href='/logout'><button>Logout</button></a></body></html>";
  server.send(200, "text/html", page);
}

// Funkcija za odjavu korisnika
void handleLogout() {
  loggedIn = false;
  loggedInUser = {"", ""};  // Resetovanje stanja prijave
  showLoginPage();
}
