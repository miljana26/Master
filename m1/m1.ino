#include <WiFi.h>

const char *ssid = "MS";
const char *password = "zastomezezas";

// Login credentials
const String username = "admin";
const String userPassword = "admin";

NetworkServer server(80);

bool loggedIn = false;  // Variable to track login status

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);  // set the LED pin mode

  delay(10);

  // Connect to WiFi network
  Serial.println();
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
}

void loop() {
  NetworkClient client = server.accept();  // listen for incoming clients

  if (client) {                     // if you get a client,
    Serial.println("New Client.");  // print a message out the serial port
    String currentLine = "";        // make a String to hold incoming data from the client
    while (client.connected()) {    // loop while the client's connected
      if (client.available()) {     // if there's bytes to read from the client,
        char c = client.read();     // read a byte, then
        Serial.write(c);            // print it out the serial monitor
        if (c == '\n') {            // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // Send login page if not logged in
            if (!loggedIn) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();
              client.print("<html>");
              client.print("<head>");
              client.print("<style>");
              client.print("body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial, sans-serif; margin: 0; padding: 0; }");
              client.print(".login-container { display: flex; justify-content: center; align-items: center; height: 100vh; }");
              client.print(".login-box { background-color: #1A4D8A; border-radius: 10px; padding: 40px; box-shadow: 0 0 20px rgba(0, 255, 255, 0.2); }");
              client.print(".login-box h1 { color: #00d4ff; text-align: center; margin-bottom: 20px; }");
              client.print(".login-box input { width: 100%; padding: 10px; margin: 10px 0; border: none; border-radius: 5px; }");
              client.print(".login-box input[type='text'], .login-box input[type='password'] { background-color: #112240; color: #ffffff; }");
              client.print(".login-box input[type='submit'] { background-color: #00d4ff; color: #ffffff; cursor: pointer; border-radius: 5px; }");
              client.print(".login-box input[type='submit']:hover { background-color: #00a3cc; }");
              client.print("</style>");
              client.print("</head>");
              client.print("<body>");
              client.print("<div class='login-container'>");
              client.print("<div class='login-box'>");
              client.print("<h1>Login</h1>");
              client.print("<form action=\"/login\" method=\"GET\">");
              client.print("Username: <input type=\"text\" name=\"username\"><br>");
              client.print("Password: <input type=\"password\" name=\"password\"><br>");
              client.print("<input type=\"submit\" value=\"Login\">");
              client.print("</form>");
              client.print("</div>");
              client.print("</div>");
              client.print("</body>");
              client.print("</html>");
              client.println();
            } 
            // Send control page if logged in
            else {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();
              client.print("<html>");
              client.print("<head>");
              client.print("<style>");
              client.print("body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial, sans-serif; margin: 0; padding: 0; text-align: center; color: white; }");
              client.print("</style>");
              client.print("</head>");
              client.print("<body>");
              client.print("Click <a href=\"/H\">here</a> to turn the LED on.<br>");
              client.print("Click <a href=\"/L\">here</a> to turn the LED off.<br>");
              client.print("</body>");
              client.print("</html>");
              client.println();
            }

            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }

        // Handle login request
        if (currentLine.indexOf("GET /login?username=") >= 0) {
          int userIndex = currentLine.indexOf("username=");
          int passIndex = currentLine.indexOf("password=");
          String enteredUsername = currentLine.substring(userIndex + 9, currentLine.indexOf("&", userIndex));
          String enteredPassword = currentLine.substring(passIndex + 9);

          if (enteredUsername == username && enteredPassword == userPassword) {
            loggedIn = true;
          }
        }

        // Handle LED control after login
        if (loggedIn) {
          if (currentLine.endsWith("GET /H")) {
            digitalWrite(2, HIGH);  //LED on
          }
          if (currentLine.endsWith("GET /L")) {
            digitalWrite(2, LOW);  //LED off
          }
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }
}
