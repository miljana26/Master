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
#include "DFRobot_DF2301Q.h"

#define RX_PIN 16
#define TX_PIN 17
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define DEBUG_WEBSOCKET true
#define DEBUG_INTERVAL 5000
#define DEBUG_REGISTRATION true
#define REGISTRATION_DEBUG_INTERVAL 1000
#define OLED_SDA 21
#define OLED_SCL 22
#define VOICE_SDA 33
#define VOICE_SCL 32

unsigned long lastDebugPrint = 0;
unsigned long lastPing = 0;
volatile int connectedClients = 0;
#define MAX_CLIENTS 5
bool clientConnected[MAX_CLIENTS] = {false};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DFRobot_DF2301Q_I2C asr(&Wire1);

const char *ssid = "MS";
const char *password = "sifra";
const char *telegramToken = "bot_token";
const char *chatId = "chat_ID";

struct User
{
    String username;
    String pin;
    String fingerprintID;
    String voiceCommand;
};

struct RegistrationState
{
    int assignedID;
    bool registrationComplete;
    unsigned long startTime;
} currentRegistration;

std::vector<User> users;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
bool loggedIn = false;
bool userAdded = false;
User loggedInUser = {"", ""};

const int ledPin = 5;
const int pirPin = 23;
bool motionDetected = false;
const int blueLedPin = 2;
int blockTime = 0;
bool isEnteringPin = false;
bool isWaitingForMotion = true;
bool loginFailed = false;
const unsigned long ledOnTimeout = 180000;
unsigned long ledTurnOnTime = 0;
String lastGeneratedPin = "";
const unsigned long pirDebounceDelay = 500;
unsigned long lastPirReadTime = 0;
bool lastPirState = LOW;
bool ledState = false;
bool motionState = false;
bool alarmState = false;
bool doorState = false;
uint8_t loggedInClientNum = -1;
String motionDetectedTime = "";
int currentStep = 0;
bool noFingerMessageShown = false;
bool registrationActive = false;
bool fingerprintAdded = false;
int fingerprintID = 0;
volatile bool isAssigningId = false;
volatile bool idAssigned = false;
SemaphoreHandle_t fingerprintMutex;
bool startRegistrationPending = false;
unsigned long lastRegistrationDebug = 0;
bool registrationDebugEnabled = true;
bool firstStepVerified = false;
String expectedVoiceCommand = "";

int lastDebuggedStep = -1;
bool lastDebuggedIDAssigned = false;
bool lastDebuggedFingerprintAdded = false;
bool lastDebuggedRegistrationActive = false;
static bool wakeWordDetected = false;
User *authenticatedUser = nullptr;

HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 15, 2};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String enteredPassword = "";
int attempts = 0;
const int maxAttempts = 3;
int voiceAttempts = 0;
const int maxVoiceAttempts = 3;

Servo myservo;
int servoPin = 18;
int pos = 0;

unsigned long pirActivationTime = 0;
const unsigned long pirTimeout = 180000;

void broadcastState(const char *type, bool state = false, const char *additional = nullptr)
{
    DynamicJsonDocument doc(256);
    doc["type"] = type;
    doc["state"] = state;
    if (additional != nullptr)
    {
        doc["time"] = additional;
    }

    String message;
    serializeJson(doc, message);
    webSocket.broadcastTXT(message);

    Serial.printf("Broadcasting %s state: %s\n", type, state ? "true" : "false");
}

void sendAllStatusesToClient(uint8_t clientNum)
{
    DynamicJsonDocument stateDoc(256);
    String stateMsg;

    stateDoc["type"] = "led";
    stateDoc["state"] = ledState;
    serializeJson(stateDoc, stateMsg);
    webSocket.sendTXT(clientNum, stateMsg);

    stateDoc.clear();
    stateDoc["type"] = "motion";
    stateDoc["state"] = motionState;
    stateDoc["time"] = motionDetectedTime;
    stateMsg = "";
    serializeJson(stateDoc, stateMsg);
    webSocket.sendTXT(clientNum, stateMsg);

    stateDoc.clear();
    stateDoc["type"] = "pin";
    stateDoc["value"] = enteredPassword;
    stateMsg = "";
    serializeJson(stateDoc, stateMsg);
    webSocket.sendTXT(clientNum, stateMsg);

    stateDoc.clear();
    stateDoc["type"] = "alarm";
    stateDoc["state"] = alarmState;
    stateMsg = "";
    serializeJson(stateDoc, stateMsg);
    webSocket.sendTXT(clientNum, stateMsg);

    stateDoc.clear();
    stateDoc["type"] = "doors";
    stateDoc["state"] = doorState;
    stateMsg = "";
    serializeJson(stateDoc, stateMsg);
    webSocket.sendTXT(clientNum, stateMsg);
}

void sendTelegramMessage(const String &message)
{
    HTTPClient http;
    String url = "https:

                 http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
        Serial.printf("Telegram message sent. Response code: %d\n", httpResponseCode);
    }
    else
    {
        Serial.printf("Error sending Telegram message. Response code: %d\n", httpResponseCode);
    }

    http.end();
}

bool isIDLinkedToUser(int id)
{
    for (const User &user : users)
    {
        if (user.fingerprintID.toInt() == id)
        {
            return true;
        }
    }
    return false;
}

void resetRegistrationProcess()
{
    static bool resetInProgress = false;
    if (resetInProgress)
        return;
    resetInProgress = true;

    Serial.println("Resetting registration process");

    if (currentRegistration.assignedID > 0 && !currentRegistration.registrationComplete)
    {
        Serial.printf("Releasing incomplete registration ID: %d\n", currentRegistration.assignedID);
        finger.deleteModel(currentRegistration.assignedID);
        currentRegistration.assignedID = -1;
    }

    registrationActive = false;
    currentStep = 0;
    idAssigned = false;
    isAssigningId = false;
    fingerprintAdded = false;

    if (!fingerprintAdded)
    {
        saveFingerprintAdded(false);
    }

    currentRegistration = {
        -1,
        false,
        0};

    if (webSocket.connectedClients() > 0)
    {
        String resetMsg = "{\"type\":\"progress\",\"step\":0,\"message\":\"Registration reset\"}";
        webSocket.broadcastTXT(resetMsg);
    }

    resetInProgress = false;
}

void initializeRegistrationState()
{
    currentRegistration = {
        -1,
        false,
        0};
}

void checkRegistrationTimeout()
{
    if (registrationActive && currentRegistration.startTime > 0)
    {
        if (millis() - currentRegistration.startTime > 180000)
        {
            Serial.println("Registration timed out");
            resetRegistrationProcess();
        }
    }
}

void saveFingerprintAdded(bool status)
{
    File file = SPIFFS.open("/fingerprintAdded.txt", FILE_WRITE);
    if (!file)
    {
        Serial.println("Failed to open file for writing");
        return;
    }
    file.print(status ? "true" : "false");
    file.close();
    Serial.println("Saved fingerprintAdded to SPIFFS");
}

void startFingerprintRegistration()
{
    Serial.println("\n[Registration] Starting new fingerprint registration...");
    logRegistrationStatus("Before Registration Start");

    registrationActive = true;
    currentStep = 0;
    isAssigningId = true;
    idAssigned = false;
    fingerprintAdded = false;
    saveFingerprintAdded(false);
    startRegistrationPending = false;

    logRegistrationStatus("After Registration Start");

    DynamicJsonDocument doc(200);
    doc["type"] = "progress";
    doc["step"] = 0;
    doc["message"] = "Place your finger on the sensor";
    String response;
    serializeJson(doc, response);
    webSocket.broadcastTXT(response);
}

void logRegistrationStatus(const char *location)
{
    bool stateChanged = false;

    if (lastDebuggedStep != currentStep ||
        lastDebuggedIDAssigned != idAssigned ||
        lastDebuggedFingerprintAdded != fingerprintAdded ||
        lastDebuggedRegistrationActive != registrationActive)
    {

        Serial.printf("\n=== Registration Status at %s ===\n", location);
        Serial.printf("registrationActive: %s\n", registrationActive ? "true" : "false");
        Serial.printf("currentStep: %d\n", currentStep);
        Serial.printf("isAssigningId: %s\n", isAssigningId ? "true" : "false");
        Serial.printf("idAssigned: %s\n", idAssigned ? "true" : "false");

        lastDebuggedStep = currentStep;
        lastDebuggedIDAssigned = idAssigned;
        lastDebuggedFingerprintAdded = fingerprintAdded;
        lastDebuggedRegistrationActive = registrationActive;
    }
}

void updateMotionState(bool detected, const char *time = nullptr)
{
    motionState = detected;
    digitalWrite(ledPin, detected ? HIGH : LOW);
    ledState = detected;

    broadcastState("motion", detected, time);
    broadcastState("led", ledState);
}

void updateLEDState(bool state)
{
    ledState = state;
    digitalWrite(ledPin, state ? HIGH : LOW);
    broadcastState("led", state);
}

void updateAlarmState(bool state)
{
    alarmState = state;
    digitalWrite(blueLedPin, state ? HIGH : LOW);
    broadcastState("alarm", state);
}

void updateDoorState(bool state)
{
    doorState = state;
    broadcastState("doors", state);
}

void updatePINState(const String &pin)
{
    DynamicJsonDocument doc(200);
    doc["type"] = "pin";
    doc["value"] = pin;
    String pinMsg;
    serializeJson(doc, pinMsg);
    webSocket.broadcastTXT(pinMsg);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.printf("[WebSocket] Client #%u disconnected\n", num);
        if (num < MAX_CLIENTS)
        {
            clientConnected[num] = false;
            if (connectedClients > 0)
            {
                connectedClients--;
            }
        }
        break;

    case WStype_CONNECTED:
    {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[WebSocket] Client #%u connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);

        if (num < MAX_CLIENTS && !clientConnected[num])
        {
            clientConnected[num] = true;
            connectedClients++;
        }

        DynamicJsonDocument stateDoc(256);
        String stateMsg;

        stateDoc["type"] = "motion";
        stateDoc["state"] = motionState;
        stateDoc["time"] = motionDetectedTime;
        serializeJson(stateDoc, stateMsg);
        webSocket.sendTXT(num, stateMsg);

        stateDoc.clear();
        stateMsg = "";
        stateDoc["type"] = "led";
        stateDoc["state"] = ledState;
        serializeJson(stateDoc, stateMsg);
        webSocket.sendTXT(num, stateMsg);

        stateDoc.clear();
        stateMsg = "";
        stateDoc["type"] = "alarm";
        stateDoc["state"] = alarmState;
        serializeJson(stateDoc, stateMsg);
        webSocket.sendTXT(num, stateMsg);

        stateDoc.clear();
        stateMsg = "";
        stateDoc["type"] = "doors";
        stateDoc["state"] = doorState;
        serializeJson(stateDoc, stateMsg);
        webSocket.sendTXT(num, stateMsg);

        stateDoc.clear();
        stateMsg = "";
        stateDoc["type"] = "pin";
        stateDoc["value"] = enteredPassword;
        serializeJson(stateDoc, stateMsg);
        webSocket.sendTXT(num, stateMsg);
    }
    break;

    case WStype_TEXT:
    {
        String text = String((char *)payload);
        Serial.printf("[WebSocket] Received text from #%u: %s\n", num, text.c_str());

        if (text == "refresh" || text == "getStates")
        {

            DynamicJsonDocument stateDoc(256);
            String stateMsg;

            stateDoc["type"] = "motion";
            stateDoc["state"] = motionState;
            stateDoc["time"] = motionDetectedTime;
            serializeJson(stateDoc, stateMsg);
            webSocket.sendTXT(num, stateMsg);

            stateDoc.clear();
            stateMsg = "";
            stateDoc["type"] = "led";
            stateDoc["state"] = ledState;
            serializeJson(stateDoc, stateMsg);
            webSocket.sendTXT(num, stateMsg);

            stateDoc.clear();
            stateMsg = "";
            stateDoc["type"] = "alarm";
            stateDoc["state"] = alarmState;
            serializeJson(stateDoc, stateMsg);
            webSocket.sendTXT(num, stateMsg);

            stateDoc.clear();
            stateMsg = "";
            stateDoc["type"] = "doors";
            stateDoc["state"] = doorState;
            serializeJson(stateDoc, stateMsg);
            webSocket.sendTXT(num, stateMsg);

            stateDoc.clear();
            stateMsg = "";
            stateDoc["type"] = "pin";
            stateDoc["value"] = enteredPassword;
            serializeJson(stateDoc, stateMsg);
            webSocket.sendTXT(num, stateMsg);
        }
        else if (text == "start")
        {
            if (!registrationActive)
            {
                startFingerprintRegistration();
            }
        }
        else if (text == "cancel")
        {
            if (registrationActive)
            {
                resetRegistrationProcess();
                DynamicJsonDocument doc(128);
                doc["type"] = "cancel";
                doc["message"] = "Registration cancelled";
                String response;
                serializeJson(doc, response);
                webSocket.sendTXT(num, response);
            }
        }
    }
    break;

    case WStype_ERROR:
        Serial.printf("[WebSocket] Error from client #%u\n", num);
        if (num < MAX_CLIENTS)
        {
            clientConnected[num] = false;
            if (connectedClients > 0)
            {
                connectedClients--;
            }
        }
        break;

    case WStype_PING:
        Serial.printf("[WebSocket] Ping from client #%u\n", num);
        break;

    case WStype_PONG:
        Serial.printf("[WebSocket] Pong from client #%u\n", num);
        break;
    }
}

void setupWebSocketRoutes()
{
    server.on("/ws-info", HTTP_GET, []()
              {
        String info = "WebSocket Server Info:\n";
        info += "Server IP: " + WiFi.localIP().toString() + "\n";
        info += "Server Port: 81\n";
        info += "Connected Clients: " + String(webSocket.connectedClients()) + "\n";
        info += "WiFi Status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\n";
        info += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
        server.send(200, "text/plain", info); });
}

void handleWebSocket()
{
    unsigned long currentMillis = millis();

    webSocket.loop();

    if (DEBUG_WEBSOCKET && currentMillis - lastDebugPrint > DEBUG_INTERVAL)
    {
        Serial.println("\n=== WebSocket Server Status ===");
        Serial.printf("Connected clients: %d\n", webSocket.connectedClients());
        Serial.printf("WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        if (registrationActive)
        {
            Serial.println("Registration is active");
            Serial.printf("Current Step: %d\n", currentStep);
        }
        Serial.println("============================\n");
        lastDebugPrint = currentMillis;
    }

    if (currentMillis - lastPing > 15000)
    {
        webSocket.broadcastPing();
        lastPing = currentMillis;
    }
}

String urlEncode(String str)
{
    String encodedString = "";
    char c;
    for (unsigned int i = 0; i < str.length(); i++)
    {
        c = str.charAt(i);
        if (isalnum(c))
        {
            encodedString += c;
        }
        else
        {
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

String urlDecode(String str)
{
    String decoded = "";
    char c;
    int i, len = str.length();
    for (i = 0; i < len; i++)
    {
        c = str.charAt(i);
        if (c == '+')
        {
            decoded += ' ';
        }
        else if (c == '%' && i + 2 < len)
        {
            char code0 = str.charAt(++i);
            char code1 = str.charAt(++i);
            c = (hexToInt(code0) << 4) | hexToInt(code1);
            decoded += c;
        }
        else
        {
            decoded += c;
        }
    }
    return decoded;
}

int hexToInt(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    if ('A' <= c && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

String getFormattedTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return "Failed to obtain time";
    }
    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
}

int getFingerprintID()
{
    if (!firstStepVerified)
    {
        int fingerprintID = finger.getImage();
        if (fingerprintID == FINGERPRINT_OK)
        {
            fingerprintID = finger.image2Tz();
            if (fingerprintID == FINGERPRINT_OK)
            {
                fingerprintID = finger.fingerFastSearch();
                if (fingerprintID == FINGERPRINT_OK)
                {
                    for (User &user : users)
                    {
                        if (user.fingerprintID == String(finger.fingerID))
                        {
                            authenticatedUser = &user;
                            firstStepVerified = true;
                            expectedVoiceCommand = user.voiceCommand;

                            display.clearDisplay();
                            display.setCursor(0, 0);
                            display.println("Say wake word:");
                            display.println("'hello robot'");
                            display.display();

                            return finger.fingerID;
                        }
                    }
                }
            }
        }
        return -1;
    }
    return -1;
}

void resetPIRDetection()
{
    digitalWrite(ledPin, LOW);
    ledState = false;
    digitalWrite(blueLedPin, LOW);
    enteredPassword = "";
    attempts = 0;
    isEnteringPin = false;
    isWaitingForMotion = true;
    alarmState = false;
    motionDetectedTime = "";
    motionState = false;

    DynamicJsonDocument ledDoc(200);
    ledDoc["type"] = "led";
    ledDoc["state"] = false;
    String ledMsg;
    serializeJson(ledDoc, ledMsg);
    webSocket.broadcastTXT(ledMsg);

    DynamicJsonDocument motionDoc(200);
    motionDoc["type"] = "motion";
    motionDoc["state"] = false;
    motionDoc["time"] = "";
    String motionMsg;
    serializeJson(motionDoc, motionMsg);
    webSocket.broadcastTXT(motionMsg);

    DynamicJsonDocument pinDoc(200);
    pinDoc["type"] = "pin";
    pinDoc["value"] = "";
    String pinMsg;
    serializeJson(pinDoc, pinMsg);
    webSocket.broadcastTXT(pinMsg);

    DynamicJsonDocument alarmDoc(200);
    alarmDoc["type"] = "alarm";
    alarmDoc["state"] = false;
    String alarmMsg;
    serializeJson(alarmDoc, alarmMsg);
    webSocket.broadcastTXT(alarmMsg);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Waiting for motion...");
    display.display();

    Serial.println("All states reset and broadcast");
}

void displayWelcomeMessage(String username)
{
    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor((SCREEN_WIDTH - 7 * 6) / 2, 10);
    display.print("Welcome");

    display.setCursor((SCREEN_WIDTH - 4 * 6) / 2, 25);
    display.print("home");

    display.setCursor((SCREEN_WIDTH - username.length() * 6) / 2, 40);
    display.print(username);

    display.display();
}

void moveServo()
{
    Serial.println("Moving servo - Opening door");
    doorState = true;
    broadcastState("doors", doorState);

    for (pos = 0; pos <= 180; pos += 1)
    {
        myservo.write(pos);
    }

    delay(2000);

    for (pos = 180; pos >= 0; pos -= 1)
    {
        myservo.write(pos);
    }

    doorState = false;
    broadcastState("doors", doorState);
    Serial.println("Door movement complete");
}

void handlePasswordInput()
{
    char key = keypad.getKey();

    if (key && !firstStepVerified)
    {

        if (!isEnteringPin && !motionDetected)
        {

            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("Motion detection");
            display.println("required first!");
            display.display();
            delay(2000);

            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("Waiting for motion...");
            display.display();
            return;
        }

        if (!isEnteringPin)
        {
            isEnteringPin = true;
            isWaitingForMotion = false;
            digitalWrite(ledPin, HIGH);
            ledState = true;
            broadcastState("led", true);

            display.clearDisplay();
            display.setCursor(0, 0);
            display.print("Enter PIN:");
            display.display();
        }

        if (key == '#')
        {

            for (User &user : users)
            {
                if (user.pin == enteredPassword)
                {
                    authenticatedUser = &user;
                    firstStepVerified = true;
                    expectedVoiceCommand = user.voiceCommand;

                    Serial.println("Correct PIN!");

                    display.clearDisplay();
                    display.setCursor(0, 0);
                    display.println("Say wake word:");
                    display.println("'hello robot'");
                    display.display();

                    enteredPassword = "";
                    updatePINState("");
                    return;
                }
            }

            attempts++;
            if (attempts >= maxAttempts)
            {
                Serial.println("Too many failed attempts!");
                digitalWrite(blueLedPin, HIGH);
                alarmState = true;
                broadcastState("alarm", true);
                sendTelegramMessage("Warning: Multiple failed PIN attempts detected!");
                delay(3000);
                resetPIRDetection();
            }
            else
            {
                Serial.println("Incorrect PIN!");
                display.clearDisplay();
                display.setCursor(0, 0);
                display.print("Wrong PIN!");
                display.setCursor(0, 20);
                display.printf("Attempts left: %d", maxAttempts - attempts);
                display.display();
                delay(2000);

                display.clearDisplay();
                display.setCursor(0, 0);
                display.print("Enter PIN:");
                display.display();
            }

            enteredPassword = "";
            updatePINState("");
        }
        else if (key == '*')
        {

            if (enteredPassword.length() > 0)
            {
                enteredPassword.remove(enteredPassword.length() - 1);
                updatePINState(enteredPassword);

                display.clearDisplay();
                display.setCursor(0, 0);
                display.print("Enter PIN:");
                display.setCursor(0, 20);
                for (int i = 0; i < enteredPassword.length(); i++)
                {
                    display.print("*");
                }
                display.display();
            }
        }
        else
        {

            enteredPassword += key;
            updatePINState(enteredPassword);

            display.clearDisplay();
            display.setCursor(0, 0);
            display.print("Enter PIN:");
            display.setCursor(0, 20);
            for (int i = 0; i < enteredPassword.length(); i++)
            {
                display.print("*");
            }
            display.display();
        }
    }
}

void handlePIRSensor()
{
    int pirState = digitalRead(pirPin);
    unsigned long currentTime = millis();

    if (pirState != lastPirState)
    {
        lastPirReadTime = currentTime;
    }

    if ((currentTime - lastPirReadTime) > pirDebounceDelay && pirState == HIGH && isWaitingForMotion)
    {
        Serial.println("Motion detected!");
        String detectionTime = getFormattedTime();
        motionDetectedTime = detectionTime;
        pirActivationTime = millis();
        ledTurnOnTime = millis();

        digitalWrite(ledPin, HIGH);
        ledState = true;
        motionState = true;

        broadcastState("led", ledState);
        broadcastState("motion", motionState, detectionTime.c_str());

        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Motion detected!");
        display.println("Use PIN or");
        display.println("Fingerprint");
        display.display();

        isEnteringPin = true;
        isWaitingForMotion = false;
        firstStepVerified = false;
        authenticatedUser = nullptr;
    }

    if (ledState && (currentTime - ledTurnOnTime > ledOnTimeout))
    {
        resetPIRDetection();
    }

    lastPirState = pirState;
}

void activateErrorLED()
{
    digitalWrite(blueLedPin, HIGH);
    alarmState = true;

    broadcastState("alarm", alarmState);

    sendTelegramMessage("Alarm activated!");
    delay(5000);

    digitalWrite(blueLedPin, LOW);
    alarmState = false;

    broadcastState("alarm", alarmState);
}

int generateNewFingerprintID()
{
    for (int id = 1; id <= finger.capacity; id++)
    {
        if (checkIDAvailability(id))
        {
            return id;
        }
    }
    Serial.println("No available IDs!");
    return -1;
}

void cleanupOrphanedFingerprints()
{
    Serial.println("Cleaning up orphaned fingerprints...");

    std::vector<int> usedIDs;
    for (const User &user : users)
    {
        if (!user.fingerprintID.isEmpty())
        {
            usedIDs.push_back(user.fingerprintID.toInt());
        }
    }

    for (int id = 1; id <= finger.capacity; id++)
    {
        if (finger.loadModel(id) == FINGERPRINT_OK)
        {

            if (std::find(usedIDs.begin(), usedIDs.end(), id) == usedIDs.end())
            {
                Serial.printf("Deleting orphaned fingerprint ID: %d\n", id);
                finger.deleteModel(id);
            }
        }
    }
    Serial.println("Cleanup complete");
}

bool checkIDAvailability(int id)
{
    uint8_t p = finger.loadModel(id);
    if (p == FINGERPRINT_OK)
    {
        Serial.println("ID " + String(id) + " is occupied.");
        return false;
    }
    else
    {
        Serial.println("ID " + String(id) + " is available.");
        return true;
    }
}

bool isIDInUse(int id)
{
    for (const User &user : users)
    {
        if (user.fingerprintID.toInt() == id)
        {
            return true;
        }
    }
    return false;
}

void assignFingerprintID()
{
    if (!isAssigningId || idAssigned)
        return;

    Serial.println("Starting ID assignment...");
    static unsigned long lastProgressUpdate = 0;
    static int stepProgress = 0;

    if (millis() - lastProgressUpdate > 200)
    {
        stepProgress = (stepProgress + 10) % 100;
        int mappedProgress = stepProgress * 25 / 100;

        DynamicJsonDocument doc(200);
        doc["type"] = "progress";
        doc["step"] = 0;
        doc["animate"] = true;
        doc["progressValue"] = mappedProgress;
        doc["message"] = "Assigning fingerprint ID...";
        String response;
        serializeJson(doc, response);
        webSocket.broadcastTXT(response);
        lastProgressUpdate = millis();
    }

    if (currentRegistration.assignedID > 0 && !currentRegistration.registrationComplete)
    {
        Serial.printf("Found incomplete registration with ID %d, releasing it...\n",
                      currentRegistration.assignedID);
        finger.deleteModel(currentRegistration.assignedID);
        currentRegistration.assignedID = -1;
    }

    if (fingerprintID > 0 && !isIDInUse(fingerprintID))
    {

        uint8_t p = finger.loadModel(fingerprintID);
        if (p == FINGERPRINT_OK)
        {

            idAssigned = true;
            isAssigningId = false;
            currentRegistration.assignedID = fingerprintID;
            currentRegistration.registrationComplete = false;
            currentRegistration.startTime = millis();

            DynamicJsonDocument doc(200);
            doc["type"] = "progress";
            doc["step"] = 0;
            doc["animate"] = false;
            doc["progressValue"] = 25;
            doc["message"] = "Previous ID reassigned successfully!";
            String response;
            serializeJson(doc, response);
            webSocket.broadcastTXT(response);

            Serial.printf("Reusing previous Fingerprint ID: %d\n", fingerprintID);
            return;
        }
    }

    for (int id = 1; id <= finger.capacity; id++)
    {
        if (!isIDInUse(id))
        {
            uint8_t p = finger.loadModel(id);
            if (p != FINGERPRINT_OK)
            {
                fingerprintID = id;
                currentRegistration.assignedID = id;
                currentRegistration.registrationComplete = false;
                currentRegistration.startTime = millis();

                idAssigned = true;
                isAssigningId = false;

                DynamicJsonDocument doc(200);
                doc["type"] = "progress";
                doc["step"] = 0;
                doc["animate"] = false;
                doc["progressValue"] = 25;
                doc["message"] = "New fingerprint ID assigned!";
                String response;
                serializeJson(doc, response);
                webSocket.broadcastTXT(response);

                Serial.printf("Assigned new Fingerprint ID: %d\n", fingerprintID);
                return;
            }
        }
    }

    Serial.println("No available fingerprint IDs!");

    DynamicJsonDocument doc(200);
    doc["type"] = "progress";
    doc["step"] = 0;
    doc["animate"] = false;
    doc["progressValue"] = 0;
    doc["message"] = "Error: No available IDs!";
    String response;
    serializeJson(doc, response);
    webSocket.broadcastTXT(response);

    resetRegistrationProcess();
}

void refreshFingerprintIDs()
{
    Serial.println("Refreshing fingerprint IDs...");
    for (int id = 1; id <= finger.capacity; id++)
    {
        int p = finger.loadModel(id);
        if (p == FINGERPRINT_OK)
        {
            Serial.println("ID " + String(id) + " is occupied.");
        }
        else if (p == FINGERPRINT_PACKETRECIEVEERR)
        {
            Serial.println("Error reading ID " + String(id));
        }
        else
        {
            Serial.println("ID " + String(id) + " is available.");
        }
    }
}

void sendProgressUpdate(int step, const String &message)
{
    if (!registrationActive)
        return;

    int progressValue = (step == 3) ? 100 : mapProgress(0, step);

    DynamicJsonDocument doc(1024);
    doc["type"] = "progress";
    doc["step"] = step;
    doc["progressValue"] = progressValue;
    doc["animate"] = false;
    doc["message"] = message;

    String json;
    serializeJson(doc, json);
    Serial.print("Sending progress update: ");
    Serial.println(json);

    if (webSocket.connectedClients() > 0)
    {
        webSocket.broadcastTXT(json);
    }
}

int mapProgress(int stepProgress, int stepNumber)
{

    if (stepNumber == 3)
        return 100;

    const int RANGES[4][2] = {
        {0, 25},
        {25, 50},
        {50, 75},
        {75, 100}};

    int rangeStart = RANGES[stepNumber][0];
    int rangeEnd = RANGES[stepNumber][1];
    return rangeStart + (stepProgress * (rangeEnd - rangeStart) / 100);
}

bool getFingerprintImage()
{
    int p = finger.getImage();
    static int scanAttempts = 0;

    switch (p)
    {
    case FINGERPRINT_OK:
        scanAttempts++;
        if (scanAttempts >= 3)
        {
            scanAttempts = 0;
            if (finger.image2Tz(1) == FINGERPRINT_OK)
            {
                sendProgressUpdate(1, "First scan successful!");
                return true;
            }
        }
        break;
    case FINGERPRINT_NOFINGER:
        scanAttempts = 0;
        break;
    default:
        sendProgressUpdate(1, "Scan error, please try again");
        delay(2000);
        break;
    }
    return false;
}

bool confirmSecondScan()
{
    static bool waitingForFingerRemoval = true;
    static int stepProgress = 50;
    static unsigned long lastProgressUpdate = 0;

    if (waitingForFingerRemoval)
    {
        if (finger.getImage() == FINGERPRINT_NOFINGER)
        {
            waitingForFingerRemoval = false;
            stepProgress = 50;
            sendProgressUpdate(2, "Now place your finger again for the second scan");
            delay(1000);
            return false;
        }
        return false;
    }

    if (finger.getImage() == FINGERPRINT_OK)
    {

        if (finger.image2Tz(2) == FINGERPRINT_OK)
        {
            if (finger.createModel() == FINGERPRINT_OK)
            {

                sendProgressUpdate(2, "Second scan successful! Matching scans...");
                return true;
            }
            else
            {

                waitingForFingerRemoval = true;
                sendProgressUpdate(1, "Scans don't match. Restarting first scan...");
                currentStep = 1;
                delay(2000);
            }
        }
        else
        {
            sendProgressUpdate(2, "Error during second scan. Please try again.");
            delay(2000);
        }
        return false;
    }

    if (millis() - lastProgressUpdate > 200)
    {
        stepProgress = stepProgress < 75 ? stepProgress + 1 : 75;
        DynamicJsonDocument doc(200);
        doc["type"] = "progress";
        doc["step"] = 2;
        doc["animate"] = true;
        doc["progressValue"] = stepProgress;
        doc["message"] = "Place your finger for the second scan...";
        String response;
        serializeJson(doc, response);
        webSocket.broadcastTXT(response);
        lastProgressUpdate = millis();
    }

    return false;
}

bool getFingerprintAdded()
{
    if (!SPIFFS.exists("/fingerprintAdded.txt"))
    {
        Serial.println("Fingerprint status file does not exist. Returning false.");
        return false;
    }
    File file = SPIFFS.open("/fingerprintAdded.txt", FILE_READ);
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return false;
    }
    String value = file.readString();
    file.close();
    Serial.print("Retrieved fingerprintAdded from SPIFFS: ");
    Serial.println(value);
    return value == "true";
}

bool saveFingerprint()
{
    int p = finger.storeModel(fingerprintID);
    if (p == FINGERPRINT_OK)
    {
        Serial.println("Fingerprint stored successfully.");
        fingerprintAdded = true;
        saveFingerprintAdded(true);

        currentRegistration.registrationComplete = true;

        DynamicJsonDocument doc(200);
        doc["type"] = "progress";
        doc["step"] = 3;
        doc["animate"] = false;
        doc["progressValue"] = 100;
        doc["message"] = "Registration complete!";
        String response;
        serializeJson(doc, response);
        webSocket.broadcastTXT(response);

        Serial.println("Saved fingerprintAdded as true in SPIFFS");
        refreshFingerprintIDs();
        return true;
    }
    else
    {
        Serial.println("Error storing fingerprint.");

        finger.deleteModel(fingerprintID);
        currentRegistration.assignedID = -1;
        return false;
    }
}

void handleGetFingerprintStatus()
{
    DynamicJsonDocument jsonResponse(1024);
    jsonResponse["fingerprintAdded"] = getFingerprintAdded();
    String response;
    serializeJson(jsonResponse, response);
    server.send(200, "application/json", response);
}

void handleResetFingerprintStatus()
{
    if (!registrationActive)
    {

        fingerprintAdded = false;
        saveFingerprintAdded(false);
        Serial.println("Fingerprint status reset before new registration.");
        server.send(200, "application/json", "{\"success\": true}");
    }
    else
    {
        Serial.println("Fingerprint reset prevented - registration active.");
        server.send(200, "application/json", "{\"success\": false, \"message\": \"Registration in progress\"}");
    }
}

void resetProgress()
{
    Serial.println("Process failed. Restarting...");
    currentStep = 0;
}

void initializeUserFile()
{
    Serial.println("Initializing user file...");

    if (!SPIFFS.exists("/users.txt"))
    {

        File file = SPIFFS.open("/users.txt", "w");
        if (!file)
        {
            Serial.println("Greška pri kreiranju fajla za korisnike");
            return;
        }

        file.println("admin,admin,,");
        file.close();
        Serial.println("Kreiran fajl sa default admin korisnikom");
    }
    else
    {
        Serial.println("User file already exists.");
        bool adminExists = false;
        File file = SPIFFS.open("/users.txt", "r");
        while (file.available())
        {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.startsWith("admin,"))
            {
                adminExists = true;
                break;
            }
        }
        file.close();

        if (!adminExists)
        {
            File file = SPIFFS.open("/users.txt", "a");
            file.println("admin,admin,,");
            file.close();
            Serial.println("Dodao admin korisnika u postojeći fajl");
        }
    }
    Serial.println("Initialization complete.");
}

void loadUsersFromFile()
{
    Serial.println("Loading users from file...");

    File file = SPIFFS.open("/users.txt", FILE_READ);
    if (!file)
    {
        Serial.println("Fajl sa korisnicima ne postoji");
        return;
    }

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            continue;

        int commaIndex1 = line.indexOf(',');
        int commaIndex2 = line.indexOf(',', commaIndex1 + 1);
        int commaIndex3 = line.indexOf(',', commaIndex2 + 1);

        if (commaIndex1 > 0)
        {
            String username = line.substring(0, commaIndex1);
            String pin;
            String fingerprintID = "";
            String voiceCommand = "";

            if (commaIndex2 > commaIndex1)
            {
                pin = line.substring(commaIndex1 + 1, commaIndex2);
            }
            else
            {
                pin = line.substring(commaIndex1 + 1);
            }

            if (commaIndex2 > commaIndex1 && commaIndex3 > commaIndex2)
            {
                fingerprintID = line.substring(commaIndex2 + 1, commaIndex3);
                voiceCommand = line.substring(commaIndex3 + 1);
            }
            else if (commaIndex2 > commaIndex1)
            {
                fingerprintID = line.substring(commaIndex2 + 1);
            }

            users.push_back({username, pin, fingerprintID, voiceCommand});
            Serial.printf("Loaded user: Username: %s, PIN: %s, Fingerprint ID: %s, Voice Command: %s\n",
                          username.c_str(), pin.c_str(), fingerprintID.c_str(), voiceCommand.c_str());
        }
        else
        {
            Serial.println("Failed to parse line: " + line);
        }
    }

    file.close();
    Serial.println("Finished loading users.");
}

void saveUserToFile(const String &username, const String &pin, const String &fingerprintID, const String &voiceCommand)
{
    Serial.printf("Saving user: Username: %s, PIN: %s, Fingerprint ID: %s, Voice Command: %s\n",
                  username.c_str(), pin.c_str(), fingerprintID.c_str(), voiceCommand.c_str());

    File file = SPIFFS.open("/users.txt", FILE_APPEND);
    if (!file)
    {
        Serial.println("Error opening user file for appending.");
        return;
    }

    file.println(username + "," + pin + "," + fingerprintID + "," + voiceCommand);
    file.close();
    Serial.println("User saved to file.");
}

void deleteUserFromFile(const String &usernameToDelete)
{
    Serial.println("Entered deleteUserFromFile function.");

    bool userFound = false;
    int fingerprintIDToDelete = -1;

    for (User &u : users)
    {
        if (u.username == usernameToDelete)
        {
            userFound = true;
            fingerprintIDToDelete = u.fingerprintID.toInt();
            Serial.println("Found user to delete: " + u.username);
            break;
        }
    }

    if (!userFound)
    {
        Serial.println("User not found in memory, no action taken for deletion.");
        return;
    }

    if (fingerprintIDToDelete >= 0)
    {
        int deleteStatus = finger.deleteModel(fingerprintIDToDelete);
        if (deleteStatus == FINGERPRINT_OK)
        {
            Serial.println("Fingerprint ID " + String(fingerprintIDToDelete) + " deleted successfully.");
        }
        else
        {
            Serial.println("Failed to delete Fingerprint ID " + String(fingerprintIDToDelete) + ". Error code: " + String(deleteStatus));
        }
    }

    File tempFile = SPIFFS.open("/users_temp.txt", "w");
    if (!tempFile)
    {
        Serial.println("Failed to open temporary file for writing.");
        return;
    }

    for (User &u : users)
    {
        if (u.username != usernameToDelete)
        {
            tempFile.println(u.username + "," + u.pin + "," + u.fingerprintID + "," + u.voiceCommand);
            Serial.println("Writing user to temp file: " + u.username);
        }
    }

    tempFile.close();

    if (SPIFFS.remove("/users.txt"))
    {
        Serial.println("Deleted original users.txt file.");
    }
    else
    {
        Serial.println("Failed to delete original users.txt file.");
    }

    if (SPIFFS.rename("/users_temp.txt", "/users.txt"))
    {
        Serial.println("Renamed temp file to users.txt successfully.");
    }
    else
    {
        Serial.println("Failed to rename temp file to users.txt.");
    }

    users.erase(std::remove_if(users.begin(), users.end(),
                               [&](User &u)
                               { return u.username == usernameToDelete; }),
                users.end());

    Serial.println("User " + usernameToDelete + " fully removed from memory and file.");

    Serial.println("Refreshing fingerprint IDs after deletion...");
    refreshFingerprintIDs();

    Serial.println("Completed deleteUserFromFile function.");
}

void printUsersFile()
{
    File file = SPIFFS.open("/users.txt", "r");
    if (!file)
    {
        Serial.println("Unable to open users.txt");
        return;
    }

    Serial.println("Contents of users.txt:");
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        Serial.println(line);
    }
    file.close();
}

void showMainPage()
{
    String page = R"rawliteral(
    <html><head>
    <style>
    body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial; height: 100vh; margin: 0; display: flex; justify-content: center; align-items: center; }
    .main-container { display: flex; justify-content: center; align-items: center; flex-direction: column; }
    .main-container button { background-color: #00d4ff; color: #ffffff; padding: 20px 40px; margin: 20px; border-radius: 10px; font-size: 20px; border: none; cursor: pointer; width: 300px; }
    .main-container button:hover { background-color: #00a3cc; }
    </style>
    </head>
    <body>
    <div class='main-container'>
      <button onclick="location.href='/loginPage'">Login</button>
      <button onclick="location.href='/addUserPage'">Add User</button>
    </div>
    </body></html>
    )rawliteral";

    server.send(200, "text/html", page);
}

void showLoginPage()
{
    String message = "";
    if (loginFailed)
    {
        message = "<p style='color:red; text-align:center;'>Wrong username or password!</p>";
        loginFailed = false;
    }

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
                "<a href='/' class='back-button'>Back to Main Page</a>" +
                    message +
                    "</div></div></body></html>");
}

void showUserPage()
{
    if (!loggedIn)
    {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "-1");
        server.send(200, "text/html",
                    "<html><body><script>alert('Unauthorized access! Please log in.');</script>"
                    "<meta http-equiv='refresh' content='0;url=/loginPage' /></body></html>");
        return;
    }

    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");

    String page = R"rawliteral(
<html><head><style>
  body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); color: white; font-family: Arial, sans-serif; height: 100vh; margin: 0; position: relative; }
  .wire { position: absolute; left: calc(50% - 2px); bottom: 50%; width: 4px; height: 60vh; background: #000; z-index: 1; }
  .bulb { position: absolute; top: calc(40vh + 80px); left: 50%; transform: translate(-50%, -20px); width: 80px; height: 80px; background: #444; border-radius: 50%; z-index: 2; }
  .bulb:before { content: ''; position: absolute; left: 22.5px; top: -50px; width: 35px; height: 80px; background: #444; border-top: 30px solid #000; border-radius: 10px; }
  body.on .bulb::after { content: ''; position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 120px; height: 120px; background: #fff; border-radius: 50%; filter: blur(40px); }
  body.on .bulb { background-color: #fff; box-shadow: 0 0 50px #fff, 0 0 100px #fff, 0 0 150px #fff, 0 0 200px #fff, 0 0 250px #fff, 0 0 300px #fff, 0 0 350px #fff; }
  body.on .bulb::before { background: #fff; }
  .pin-keypad-container { display: flex; flex-direction: column; align-items: flex-start; position: absolute; left: 100px; top: 150px; }
  .screen { background-color: #1A4D8A; color: white; padding: 10px; font-size: 18px; margin-bottom: 10px; width: 250px; text-align: center; border-radius: 10px; height: 55px; display: flex; flex-direction: column; justify-content: center; }
  .pin-display { font-size: 22px; margin-top: 5px; }
  .keypad { display: grid; grid-template-columns: repeat(4, 61px); grid-gap: 8px; margin-top: 10px; }
  .key { background-color: #00d4ff; color: white; padding: 19px; font-size: 20px; border-radius: 8px; cursor: pointer; text-align: center; }
  .key:hover { background-color: #00a3cc; }
  .right-panel { position: absolute; top: 200px; right: 100px; display: flex; flex-direction: column; gap: 30px; }
  .status-box { background-color: #1A4D8A; color: white; padding: 10px; font-size: 18px; width: 250px; text-align: center; border-radius: 10px; height: 55px; display: flex; align-items: center; justify-content: center; }
  .status-box.alarm-box.alarm-active { background-color: red; }
  .status-box.door-box.opened { background-color: green; }
  .logout-button { background-color: #00d4ff; color: white; padding: 15px; font-size: 18px; border-radius: 15px; cursor: pointer; position: absolute; top: 20px; right: 20px; }
  .logout-button:hover { background-color: #00a3cc; }
</style></head>
<body>
  <div class='wire'></div>
  <div class='bulb' id='ledCircle'></div>
  <div class='pin-keypad-container'>
    <div class='screen'>Entered PIN:<div class='pin-display' id='pin-display'></div></div>
    <div class='keypad'>
      <div class='key'>1</div><div class='key'>2</div><div class='key'>3</div><div class='key'>A</div>
      <div class='key'>4</div><div class='key'>5</div><div class='key'>6</div><div class='key'>B</div>
      <div class='key'>7</div><div class='key'>8</div><div class='key'>9</div><div class='key'>C</div>
      <div class='key'>*</div><div class='key'>0</div><div class='key'>#</div><div class='key'>D</div>
    </div>
  </div>
  <div class='right-panel'>
    <div class='status-box motion-box' id='motion-box'>Waiting for motion...</div>
    <div class='status-box alarm-box' id='alarm-box'>No Alarm</div>
    <div class='status-box door-box' id='doors-box'>Closed</div>
  </div>
  <a href='/logout'><button class='logout-button'>Logout</button></a>
  <script>
    let ws = null;
    let reconnectInterval = null;

    async function resolveServerIP() {
        try {
            const response = await fetch('/ws-info');
            const serverInfo = await response.text();
            const ipMatch = serverInfo.match(/Server IP: ([\d\.]+)/);
            if (ipMatch && ipMatch[1]) return ipMatch[1];
            return window.location.hostname;
        } catch (error) {
            console.error('Failed to resolve server IP:', error);
            return window.location.hostname;
        }
    }

    async function connectWebSocket() {
        if (ws) {
            ws.close();
        }

        const serverIP = await resolveServerIP();
        const wsUrl = `ws:
        console.log('Connecting to WebSocket at:', wsUrl);
        
        ws = new WebSocket(wsUrl);

        ws.onopen = function() {
            console.log('WebSocket Connected');
            clearInterval(reconnectInterval);
            ws.send('getStates');
        };

        ws.onclose = function() {
            console.log('WebSocket Disconnected');
            if (!reconnectInterval) {
                reconnectInterval = setInterval(connectWebSocket, 3000);
            }
        };

        ws.onerror = function(error) {
            console.error('WebSocket Error:', error);
        };

        ws.onmessage = function(event) {
            console.log('Received:', event.data);
            try {
                const data = JSON.parse(event.data);
                switch(data.type) {
                    case 'led':
                        if (data.state) {
                            document.body.classList.add('on');
                        } else {
                            document.body.classList.remove('on');
                        }
                        break;
                    case 'motion':
                        const motionBox = document.getElementById('motion-box');
                        if (motionBox) {
                            motionBox.innerText = data.state ? 'Motion detected at ' + data.time : 'Waiting for motion...';
                            motionBox.style.backgroundColor = data.state ? '#4CAF50' : '#1A4D8A';
                        }
                        break;
                    case 'pin':
                        const pinDisplay = document.getElementById('pin-display');
                        if (pinDisplay) {
                            pinDisplay.innerText = data.value ? '*'.repeat(data.value.length) : '';
                        }
                        break;
                    case 'alarm':
                        const alarmBox = document.getElementById('alarm-box');
                        if (alarmBox) {
                            alarmBox.innerText = data.state ? 'ALARM' : 'No Alarm';
                            alarmBox.style.backgroundColor = data.state ? '#ff0000' : '#1A4D8A';
                        }
                        break;
                    case 'doors':
                        const doorBox = document.getElementById('doors-box');
                        if (doorBox) {
                            doorBox.innerText = data.state ? 'Door Open' : 'Door Closed';
                            doorBox.style.backgroundColor = data.state ? '#4CAF50' : '#1A4D8A';
                        }
                        break;
                }
            } catch (error) {
                console.error('Error processing message:', error);
            }
        };
    }

    window.addEventListener('load', connectWebSocket);

    window.addEventListener('beforeunload', function() {
        if (ws) {
            ws.close();
        }
        if (reconnectInterval) {
            clearInterval(reconnectInterval);
        }
    });
  </script>
</body></html>
)rawliteral";

    server.send(200, "text/html", page);
}

void showAddUserPage()
{

    fingerprintAdded = false;
    saveFingerprintAdded(false);
    registrationActive = false;
    currentStep = 0;
    isAssigningId = false;
    idAssigned = false;

    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");

    String pageContent = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
 <meta charset="UTF-8">                                         
 <meta http-equiv="Content-Type" content="text/html; charset=UTF-8"> 
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
        .add-fingerprint-button:disabled { background-color: #aaa7ad; cursor: not-allowed; }
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

       
        @keyframes progress { 0% { --percentage: 0; } 100% { --percentage: var(--value); } }
        @property --percentage { syntax: '<number>'; inherits: true; initial-value: 0; }
        [role="progressbar"] { --percentage: var(--value); --primary: #369; --secondary: #adf; --size: 150px; animation: progress 2s 0.5s forwards; width: var(--size); aspect-ratio: 1; border-radius: 50%; position: relative; overflow: hidden; display: grid; place-items: center; box-shadow: 0 0 20px rgba(0, 255, 255, 0.4); } 
        [role="progressbar"]::before { content: ""; position: absolute; top: 0; left: 0; width: 100%; height: 100%; background: conic-gradient(var(--primary) calc(var(--percentage) * 1%), var(--secondary) 0); mask: radial-gradient(white 55%, transparent 0); mask-mode: alpha; -webkit-mask: radial-gradient(#0000 55%, #000 0); -webkit-mask-mode: alpha; } 
        [role="progressbar"]::after { counter-reset: percentage var(--value); content: counter(percentage) '%'; font-family: Helvetica, Arial, sans-serif; font-size: calc(var(--size) / 5); color: var(--primary); }

       
        .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background-color: rgba(0, 0, 0, 0.5); justify-content: center; align-items: center; z-index: 1000; } 
        .modal-content { background-color: #1A4D8A; padding: 20px; border-radius: 15px; display: flex; flex-direction: column; align-items: center; width: 260px; text-align: center; } 
        .close-button { margin-top: 15px; padding: 10px 20px; background-color: #00d4ff; color: #ffffff; border: none; border-radius: 5px; cursor: pointer; } 
        .close-button:hover { background-color: #00a3cc; } 
        #fingerprint-status { color: #00d4ff; margin-bottom: 20px; }
    </style>
</head>
<body>
    <div class="login-container">
        <div class="login-box">
            <h1>Add User</h1>
            <form id="addUserForm">
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

                <input type="button" value="Add Fingerprint" id="add-fingerprint" class="add-fingerprint-button" onclick="showFingerprintModal()">
                <div id="fingerprint-error" class="error-message"></div>

                <label for="voice-command" class="voice-command-label">Voice command:</label>
                  <select name="voiceCommand" id="voice-command" class="dropdown">
                      <option value="">Please select a command</option>
                      <option value="5">otvori se</option>
                      <option value="6">otključaj</option> 
                      <option value="7">zatvori</option>
                      <option value="82">reset</option>
                      <option value="130">auto mode</option>
                      <option value="141">open the door</option>
                      <option value="142">close the door</option>
                  </select>
                <div id="voice-command-error" class="error-message"></div>

                <div class="captcha" id="captcha-section">
                    <input type="checkbox" name="captcha" value="not_a_robot" id="captcha-checkbox">
                    <label for="captcha-checkbox">I am not a robot</label>
                </div>

                <input type="submit" value="Add User" class="add-user-button">
            </form>
            <a href="/" class="back-button">Back to Main Page</a>
        </div>
    </div>

    <div id="debug-info" style="position: fixed; bottom: 10px; right: 10px; background: rgba(0,0,0,0.7); color: white; padding: 10px; border-radius: 5px; font-size: 12px;"></div>

    
    <div id="fingerprint-modal" class="modal">
        <div class="modal-content">
            <p id="fingerprint-status">Ready to start...</p>
            <div role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100" style="--value: 0"></div>
            <button class="close-button" onclick="closeFingerprintModal()">Close</button>
            <div id="connection-error" style="color: red; margin-top: 10px; display: none;"></div>
        </div>
    </div>

    <script>
  let ws;
  let registrationInProgress = false;
  let debugMode = true;
  let fingerprintAdded = false;
  
  setInterval(() => {
      if (ws) {
          console.log('WebSocket state:', {
              readyState: ws.readyState,
              bufferedAmount: ws.bufferedAmount,
              protocol: ws.protocol,
              url: ws.url
          });
      }
  }, 5000);
  
  let wsStatusInterval = setInterval(() => {
      if (ws) {
          const states = ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'];
          console.log('WebSocket status:', {
              state: states[ws.readyState],
              readyState: ws.readyState,
              url: ws.url
          });
      }
  }, 3000);
  
  async function resolveServerIP() {
      try {
          
          const response = await fetch('/ws-info');
          const serverInfo = await response.text();
          
          
          const ipMatch = serverInfo.match(/Server IP: ([\d\.]+)/);
          if (ipMatch && ipMatch[1]) {
              console.log('Resolved server IP:', ipMatch[1]);
              return ipMatch[1];
          }
          throw new Error('Could not extract IP from server info');
      } catch (error) {
          console.error('Failed to resolve server IP:', error);
          return null;
      }
  }
  
  function updateConnectionStatus(connected) {
      const status = document.getElementById('fingerprint-status');
      if (status) {
          if (connected) {
              status.style.color = '#00d4ff';
              status.innerText = 'Connected to device';
          } else {
              status.style.color = 'red';
              status.innerText = 'Disconnected from device';
          }
      }
  }
  
  function verifyConnection() {
      const currentIP = window.location.hostname;
      console.log('Current page IP:', currentIP);
      document.getElementById('debug-info').textContent = `Connected to: ${currentIP}`;
  }
  
  window.addEventListener('load', verifyConnection);
  
  document.addEventListener('DOMContentLoaded', function() {
      const closeButton = document.querySelector('.close-button');
      if (closeButton) {
          closeButton.addEventListener('click', function(e) {
              e.preventDefault();
              console.log('Close button clicked');
              closeFingerprintModal();
          });
      }
  
      document.addEventListener('keydown', function(e) {
          if (e.key === 'Escape') {
              const modal = document.getElementById('fingerprint-modal');
              if (modal && modal.style.display === 'flex') {
                  console.log('Escape key pressed, closing modal');
                  closeFingerprintModal();
              }
          }
      });
  });
  
  function log(message, isError = false) {
      if (debugMode) {
          const method = isError ? console.error : console.log;
          method(`[Fingerprint Registration] ${message}`);
      }
  }
  
  async function checkESP32Connection() {
      try {
          log('Checking ESP32 connection...');
          const response = await fetch('/', {
              method: 'HEAD',
              cache: 'no-cache'
          });
          if (response.ok) {
              log('ESP32 is reachable');
              return true;
          }
          log('ESP32 is not reachable', true);
          return false;
      } catch (error) {
          log(`ESP32 connection check failed: ${error}`, true);
          return false;
      }
  }
  
  function initializeWebSocket(wsUrl, status) {
      try {
          ws = new WebSocket(wsUrl);
          
          ws.onopen = function() {
              console.log('WebSocket Connected successfully');
              status.innerText = 'Connected, starting registration...';
              status.style.color = '#00d4ff';
              
              setTimeout(() => {
                  if (ws.readyState === WebSocket.OPEN) {
                      console.log('Sending start command to server');
                      ws.send('start');
                      registrationInProgress = true;
                  } else {
                      console.error('WebSocket not ready for start command');
                      status.innerText = 'Connection error. Please try again.';
                      status.style.color = 'red';
                  }
              }, 500);
          };
  
          
          ws.onmessage = function(event) {
              console.log('Received from server:', event.data);
              try {
                  const data = JSON.parse(event.data);
                  if (data.type === 'progress') {
                      const progressBar = document.querySelector('[role="progressbar"]');
                      const status = document.getElementById('fingerprint-status');
                      
                      if (progressBar && status) {
                          
                          status.innerText = data.message;
                          
                          
                          if (data.animate) {
                              progressBar.style.setProperty('--value', data.progressValue.toString());
                              progressBar.setAttribute('aria-valuenow', data.progressValue.toString());
                          } else {
                              
                              progressBar.style.setProperty('--value', data.progressValue.toString());
                              progressBar.setAttribute('aria-valuenow', data.progressValue.toString());
                          }
                          
                          
                          if (data.message.includes('error') || data.message.includes('don\'t match')) {
                              status.style.color = 'red';
                              setTimeout(() => {
                                  status.style.color = '#00d4ff';
                              }, 2000);
                          }
                      }
                  }
              } catch (error) {
                  console.error('Error parsing message:', error);
              }
          };
  
          ws.onerror = function(error) {
              console.error('WebSocket error:', error);
              status.innerText = 'Connection error occurred';
              status.style.color = 'red';
          };
  
          ws.onclose = function(event) {
              console.log('WebSocket connection closed:', event.code, event.reason);
              if (registrationInProgress) {
                  status.innerText = 'Connection lost';
                  status.style.color = 'red';
              }
          };
      } catch (error) {
          console.error('Error creating WebSocket:', error);
          status.innerText = 'Failed to create connection';
          status.style.color = 'red';
      }
  }
  
  
  function setupWebSocketHandlers() {
      ws.onopen = function() {
          log('WebSocket connected successfully');
          document.getElementById('fingerprint-status').innerText = 'Connected. Starting registration...';
          
          setTimeout(() => {
              if (ws.readyState === WebSocket.OPEN) {
                  log('Sending start command');
                  ws.send("start");
              } else {
                  log('WebSocket not ready for start command', true);
                  handleConnectionError('Connection not ready');
              }
          }, 500);
      };
  
      ws.onclose = function(event) {
          log(`WebSocket closed: Code ${event.code}, Reason: ${event.reason}`);
          if (registrationInProgress) {
              handleConnectionError('Connection closed unexpectedly');
          }
      };
  
      ws.onerror = function(error) {
          log(`WebSocket error: ${error}`, true);
          handleConnectionError('Connection error occurred');
      };
  
      ws.onmessage = function(event) {
          try {
              log(`Received message: ${event.data}`);
              const data = JSON.parse(event.data);
              
              switch(data.type) {
                  case 'progress':
                      updateProgress(data.step, data.message);
                      break;
                  case 'cancel':
                      handleCancelConfirmation();
                      break;
                  case 'error':
                      handleConnectionError(data.message);
                      break;
                  case 'fingerprint':
                      handleFingerprintMessage(data);
                      break;
                  default:
                      log(`Unhandled message type: ${data.type}`);
              }
          } catch (error) {
              log(`Error parsing message: ${error}`, true);
              handleConnectionError('Invalid response from device');
          }
      };
  }
  
  function handleFingerprintMessage(data) {
      const { status, message } = data;
      const fingerprintStatus = document.getElementById('fingerprint-status');
      const fingerprintError = document.getElementById('fingerprint-error');
      
      if (fingerprintStatus) {
          fingerprintStatus.innerText = message;
          
          if (status === 'waiting') {
              fingerprintStatus.style.color = '#00d4ff';
          } else if (status === 'error') {
              fingerprintStatus.style.color = 'red';
              setTimeout(() => {
                  fingerprintStatus.style.color = '#00d4ff';
              }, 2000);
          }
      }
  
      
      if (status === 'success' && fingerprintError) {
          fingerprintError.innerText = '';
      }
  }
  
  function handleCancelConfirmation() {
      if (!registrationInProgress) {
          console.warn("Cancel ignored: No active registration.");
          return;
      }
      
      
      registrationInProgress = false;
      document.getElementById('fingerprint-modal').style.display = 'none';
      
      
      if (!fingerprintAdded) {
          resetFingerprintButton();
      }
  }
  
  
  function disableFingerprintButton() {
      const fingerprintButton = document.getElementById("add-fingerprint");
      fingerprintButton.disabled = true;
      fingerprintButton.style.backgroundColor = "#aaa7ad";
      fingerprintButton.style.cursor = "not-allowed";
  }
  
  function resetFingerprintButton() {
      const fingerprintButton = document.getElementById("add-fingerprint");
      fingerprintButton.disabled = false;
      fingerprintButton.style.backgroundColor = "#00d4ff";
      fingerprintButton.style.cursor = "pointer";
  }
  
  function updateFingerprintStatus(isComplete) {
      const fingerprintButton = document.getElementById("add-fingerprint");
      const fingerprintError = document.getElementById("fingerprint-error");
      
      if (isComplete) {
          
          fingerprintButton.disabled = true;
          fingerprintButton.style.backgroundColor = "#aaa7ad";
          fingerprintButton.style.cursor = "not-allowed";
          
          
          if (fingerprintError) {
              fingerprintError.innerText = '';
          }
          
          fingerprintAdded = true;
      } else {
          
          fingerprintButton.disabled = false;
          fingerprintButton.style.backgroundColor = "#00d4ff";
          fingerprintButton.style.cursor = "pointer";
          
          
          if (fingerprintError) {
              fingerprintError.innerText = 'Fingerprint not added';
          }
          
          fingerprintAdded = false;
      }
  }
  
  function showFingerprintModal() {
      console.log('Showing fingerprint modal...');
      
      if (registrationInProgress) {
          console.log('Registration already in progress');
          return;
      }
  
      const modal = document.getElementById('fingerprint-modal');
      const progressBar = document.querySelector('[role="progressbar"]');
      const status = document.getElementById('fingerprint-status');
      
      modal.style.display = 'flex';
      progressBar.style.setProperty('--value', '0');
      status.innerText = 'Resolving connection...';
  
      
      if (ws) {
          console.log('Closing existing WebSocket connection');
          ws.close();
          ws = null;
      }
  
      
      resolveServerIP().then(serverIP => {
          if (!serverIP) {
              throw new Error('Could not resolve server IP');
          }
  
          
          const wsUrl = `ws:
          console.log('Attempting WebSocket connection to:', wsUrl);
  
          ws = new WebSocket(wsUrl);
          
          ws.onopen = function() {
              console.log('WebSocket Connected successfully to', wsUrl);
              status.innerText = 'Connected, starting registration...';
              status.style.color = '#00d4ff';
              
              
              const debugInfo = document.getElementById('debug-info');
              if (debugInfo) {
                  debugInfo.textContent = `Connected to: ${serverIP} (resolved from ${window.location.hostname})`;
              }
              
              setTimeout(() => {
                  if (ws.readyState === WebSocket.OPEN) {
                      console.log('Sending start command to server');
                      ws.send('start');
                      registrationInProgress = true;
                  } else {
                      console.error('WebSocket not ready for start command');
                      status.innerText = 'Connection error. Please try again.';
                      status.style.color = 'red';
                  }
              }, 500);
          };
  
          ws.onmessage = function(event) {
              console.log('Received from server:', event.data);
              try {
                  const data = JSON.parse(event.data);
                  if (data.type === 'progress') {
                      updateProgress(data.step, data.message);
                  } else if (data.type === 'error') {
                      handleConnectionError(data.message);
                  }
              } catch (error) {
                  console.error('Error parsing message:', error);
              }
          };
  
          ws.onerror = function(error) {
              console.error('WebSocket error:', error);
              status.innerText = 'Connection error occurred';
              status.style.color = 'red';
          };
  
          ws.onclose = function(event) {
              console.log('WebSocket connection closed:', event.code, event.reason);
              if (registrationInProgress) {
                  status.innerText = 'Connection lost';
                  status.style.color = 'red';
              }
          };
      }).catch(error => {
          console.error('Connection error:', error);
          status.innerText = 'Failed to resolve server address';
          status.style.color = 'red';
      });
  }
  
  
  window.addEventListener('load', async () => {
      const serverIP = await resolveServerIP();
      const debugInfo = document.getElementById('debug-info');
      if (debugInfo) {
          debugInfo.textContent = `Page loaded from: ${window.location.hostname} (Server IP: ${serverIP || 'unresolved'})`;
      }
  });
  
  function closeFingerprintModal() {
      console.log('Closing fingerprint modal...');
      console.log('Current fingerprint status:', fingerprintAdded);
      
      if (ws && ws.readyState === WebSocket.OPEN) {
          console.log('Sending cancel command to server...');
          ws.send("cancel");
      }
      
      document.getElementById('fingerprint-modal').style.display = 'none';
      const progressBar = document.querySelector('[role="progressbar"]');
      if (progressBar) {
          progressBar.style.setProperty('--value', '0');
          progressBar.setAttribute('aria-valuenow', '0');
      }
      
      
      if (!fingerprintAdded) {
          document.getElementById('fingerprint-status').innerText = 'Ready to start...';
      }
      
      
      registrationInProgress = false;
      
      
      const fingerprintButton = document.getElementById('add-fingerprint');
      if (fingerprintButton && !fingerprintAdded) {
          resetFingerprintButton();
      }
      
      
      if (fingerprintAdded) {
          const fingerprintError = document.getElementById('fingerprint-error');
          if (fingerprintError) {
              fingerprintError.innerText = '';
          }
      }
  }
  
  function updateProgress(step, message) {
      console.log(`Updating progress - Step: ${step}, Message: ${message}`);
      const progressBar = document.querySelector('[role="progressbar"]');
      const status = document.getElementById('fingerprint-status');
      const fingerprintError = document.getElementById('fingerprint-error');
      
      if (!progressBar || !status) {
          console.error('Progress elements not found');
          return;
      }
      
      let targetValue;
      switch (step) {
          case 0: targetValue = 25; break;
          case 1: targetValue = 50; break;
          case 2: targetValue = 75; break;
          case 3: 
              targetValue = 100;  
              registrationInProgress = false;
              fingerprintAdded = true;
              updateFingerprintStatus(true);  
              
              
              if (fingerprintError) {
                  fingerprintError.innerText = '';
              }
              
              
              progressBar.style.setProperty('--value', '100');
              progressBar.setAttribute('aria-valuenow', '100');
              break;
          default: targetValue = 0;
      }
  
      
      if (step !== 3) {
          progressBar.style.setProperty('--value', targetValue.toString());
          progressBar.setAttribute('aria-valuenow', targetValue.toString());
      }
      
      status.innerText = message;
      
      console.log(`Progress updated - Value: ${targetValue}, Message: ${message}`);
  }
  
  function handleConnectionError(message) {
      console.error('Connection error:', message);
      const status = document.getElementById('fingerprint-status');
      if (status) {
          status.innerText = `Error: ${message}`;
          status.style.color = 'red';
      }
      registrationInProgress = false;
  }
  
  
  window.onload = async function() {
      try {
          await fetch('/resetFingerprintStatus', { method: 'POST' });
          console.log("Fingerprint status reset.");
          
          const response = await fetch('/getFingerprintStatus');
          const data = await response.json();
          
          fingerprintAdded = data.fingerprintAdded;
          if (fingerprintAdded) {
              disableFingerprintButton();
              
              const fingerprintError = document.getElementById('fingerprint-error');
              if (fingerprintError) {
                  fingerprintError.innerText = '';
              }
          } else {
              resetFingerprintButton();
          }
      } catch (err) {
          console.error("Error during initialization:", err);
      }
  };
  
  window.onbeforeunload = function() {
      if (ws) {
          ws.close();
      }
      resetRegistrationState();
      return fetch('/resetFingerprintStatus', { method: 'POST' });
  };
  
  document.addEventListener('DOMContentLoaded', function() {
      var form = document.getElementById('addUserForm');
      form.addEventListener('submit', function(event) {
      event.preventDefault();
      
      console.log("Form submission - Current states:");
      console.log("fingerprintAdded:", fingerprintAdded);
      console.log("registrationInProgress:", registrationInProgress);
      
      var formData = new FormData(form);
          var xhr = new XMLHttpRequest();
          xhr.open('POST', '/addUser', true);
  
          xhr.onload = function() {
              if (xhr.status === 200) {
                  var response = JSON.parse(xhr.responseText);
                  clearErrorMessages();
          
                  if (response.success) {
                      alert('New User Added Successfully! PIN: ' + response.pin);
                      form.reset();
                      resetFingerprintButton();  
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
              voiceCommandError.innerText = errors.voiceCommand;
              var voiceCommandSelect = document.getElementById('voice-command');
              voiceCommandSelect.classList.add('voice-command-error');
          }
      }
  });
    </script>
    </body>
    </html>
    )rawliteral";

    server.send(200, "text/html", pageContent);
}

void showAdminPage()
{
    if (!loggedIn || loggedInUser.username != "admin")
    {
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
                  " body { background: linear-gradient(to bottom, #0399FA, #0B2E6D); font-family: Arial, sans-serif; }"
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

    page += "<tr><td>admin</td><td>Default User</td></tr>";

    for (User &user : users)
    {
        if (user.username != "admin")
        {
            page += "<tr><td>" + user.username + "</td>"
                                                 "<td><a href='/delete?username=" +
                    user.username + "'><button class='delete-button'>Delete</button></a></td></tr>";
        }
    }

    page += "</table><a href='/logout'><button class='logout-button'>Logout</button></a></div></body></html>";
    server.send(200, "text/html", page);
}

void handleLogin()
{
    if (server.hasArg("username") && server.hasArg("password"))
    {
        String enteredUsername = server.arg("username");
        String enteredPassword = server.arg("password");

        bool userExists = false;
        bool passwordCorrect = false;

        for (User &u : users)
        {
            if (u.username == enteredUsername)
            {
                userExists = true;
                if (u.pin == enteredPassword)
                {
                    passwordCorrect = true;
                    loggedIn = true;
                    loggedInUser = u;
                    break;
                }
            }
        }

        if (!userExists || !passwordCorrect)
        {
            loginFailed = true;
            showLoginPage();
        }
        else
        {

            if (loggedInUser.username == "admin")
            {
                showAdminPage();
            }
            else
            {
                showUserPage();
            }

            if (loggedInClientNum != -1)
            {
                sendAllStatusesToClient(loggedInClientNum);
            }
        }
    }
}

void handleAddUser()
{
    Serial.println("Entering handleAddUser...");
    DynamicJsonDocument jsonResponse(1024);
    JsonObject errors = jsonResponse.createNestedObject("errors");
    bool success = true;

    String enteredUsername = server.arg("username");
    String selectedVoiceCommand = server.arg("voiceCommand");

    Serial.printf("Current Registration State:\n");
    Serial.printf("fingerprintAdded: %s\n", fingerprintAdded ? "true" : "false");
    Serial.printf("currentRegistration.registrationComplete: %s\n",
                  currentRegistration.registrationComplete ? "true" : "false");
    Serial.printf("currentRegistration.assignedID: %d\n",
                  currentRegistration.assignedID);

    bool isFingerprintValid = fingerprintAdded &&
                              currentRegistration.registrationComplete &&
                              currentRegistration.assignedID > 0;

    if (!isFingerprintValid)
    {
        Serial.println("Fingerprint validation failed:");
        Serial.printf("fingerprintAdded: %s\n", fingerprintAdded ? "true" : "false");
        Serial.printf("registrationComplete: %s\n",
                      currentRegistration.registrationComplete ? "true" : "false");
        Serial.printf("assignedID: %d\n", currentRegistration.assignedID);
        errors["fingerprint"] = "Fingerprint not added.";
        success = false;
    }

    Serial.printf("Received data - Username: %s, Voice Command: %s, Fingerprint Added: %s, Registration Complete: %s\n",
                  enteredUsername.c_str(),
                  selectedVoiceCommand.c_str(),
                  fingerprintAdded ? "true" : "false",
                  currentRegistration.registrationComplete ? "true" : "false");

    if (!server.hasArg("captcha"))
    {
        Serial.println("CAPTCHA validation failed");
        errors["captcha"] = true;
        success = false;
    }

    if (enteredUsername.isEmpty())
    {
        Serial.println("Username is empty");
        errors["username"] = "Please enter a username.";
        success = false;
    }
    else if (!isValidUsername(enteredUsername))
    {
        Serial.println("Username is invalid");
        errors["username"] = "Invalid username! Must start with a letter and contain only letters and numbers.";
        success = false;
    }
    else
    {

        bool userExists = false;
        for (const User &u : users)
        {
            if (u.username == enteredUsername)
            {
                userExists = true;
                break;
            }
        }
        if (userExists)
        {
            Serial.println("Username already exists");
            errors["username"] = "Error: User already exists!";
            success = false;
        }
    }

    if (!fingerprintAdded || !currentRegistration.registrationComplete)
    {
        Serial.println("Fingerprint not properly added or registration incomplete");
        errors["fingerprint"] = "Fingerprint not added.";
        success = false;
    }

    if (selectedVoiceCommand.isEmpty())
    {
        Serial.println("Voice command not selected");
        errors["voiceCommand"] = "Voice command not selected.";
        success = false;
    }

    if (!success)
    {
        Serial.println("Validation failed, sending error response");
        jsonResponse["success"] = false;
        String response;
        serializeJson(jsonResponse, response);
        server.send(200, "application/json", response);
        return;
    }

    Serial.printf("Current Fingerprint ID: %d\n", fingerprintID);

    String newPin = generateUniquePin();
    Serial.printf("Generated PIN: %s\n", newPin.c_str());

    String fingerprintID_str = String(currentRegistration.assignedID);
    User newUser = {
        enteredUsername,
        newPin,
        fingerprintID_str,
        selectedVoiceCommand};

    users.push_back(newUser);
    saveUserToFile(enteredUsername, newPin, fingerprintID_str, selectedVoiceCommand);

    Serial.printf("Added user: Username: %s, PIN: %s, Fingerprint ID: %s, Voice Command: %s\n",
                  enteredUsername.c_str(),
                  newPin.c_str(),
                  fingerprintID_str.c_str(),
                  selectedVoiceCommand.c_str());

    fingerprintAdded = false;
    saveFingerprintAdded(false);
    currentRegistration = {
        -1,
        false,
        0};

    jsonResponse["success"] = true;
    jsonResponse["pin"] = newPin;
    String response;
    serializeJson(jsonResponse, response);

    server.send(200, "application/json", response);

    Serial.println("Refreshing fingerprint IDs after adding user...");
    refreshFingerprintIDs();

    Serial.println("Successfully completed handleAddUser");
}

String generateUniquePin()
{
    String pin;
    bool unique = false;

    while (!unique)
    {

        pin = String(random(1000, 9999));

        unique = true;
        for (User &u : users)
        {
            if (u.pin == pin)
            {
                unique = false;
                break;
            }
        }
    }

    return pin;
}

bool isValidUsername(const String &username)
{
    if (username.length() == 0)
    {
        return false;
    }

    if (!isalpha(username.charAt(0)))
    {
        return false;
    }

    for (size_t i = 0; i < username.length(); i++)
    {
        char c = username.charAt(i);
        if (!isalnum(c))
        {
            return false;
        }
    }

    return true;
}

bool isValidPassword(String password)
{
    if (password.length() <= 6)
        return false;
    bool hasLower = false, hasUpper = false, hasDigit = false;

    for (int i = 0; i < password.length(); i++)
    {
        if (islower(password[i]))
            hasLower = true;
        if (isupper(password[i]))
            hasUpper = true;
        if (isdigit(password[i]))
            hasDigit = true;
    }

    return hasLower && hasUpper && hasDigit;
}

void handleUserDeletion()
{
    if (server.hasArg("username"))
    {
        String usernameToDelete = server.arg("username");

        if (usernameToDelete == "admin")
        {
            server.send(200, "text/html",
                        "<html><body><script>alert('Admin user cannot be deleted!');</script>"
                        "<meta http-equiv='refresh' content='0;url=/' /></body></html>");
            return;
        }

        deleteUserFromFile(usernameToDelete);

        for (auto it = users.begin(); it != users.end(); ++it)
        {
            if (it->username == usernameToDelete)
            {
                users.erase(it);
                break;
            }
        }

        showAdminPage();
    }
}

void handleLogout()
{
    loggedIn = false;
    loggedInUser = {"", ""};

    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");

    showMainPage();
}

void checkDebugStatus()
{
    Serial.println("\n=== Debug Status Check ===");
    Serial.printf("DEBUG_REGISTRATION: %s\n", DEBUG_REGISTRATION ? "true" : "false");
    Serial.printf("registrationActive: %s\n", registrationActive ? "true" : "false");
    Serial.printf("currentStep: %d\n", currentStep);
    Serial.printf("Time since last debug: %lu ms\n", millis() - lastRegistrationDebug);
}

void handleRegistrationDebug()
{
    if (registrationActive)
    {
        logRegistrationStatus("State Change Check");
    }
}

void scanI2CBuses()
{
    Serial.println("\nScanning primary I2C bus (Wire)...");
    for (byte address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();
        if (error == 0)
        {
            Serial.printf("Device found on Wire at address 0x%02X\n", address);
        }
    }

    Serial.println("\nScanning secondary I2C bus (Wire1)...");
    for (byte address = 1; address < 127; address++)
    {
        Wire1.beginTransmission(address);
        byte error = Wire1.endTransmission();
        if (error == 0)
        {
            Serial.printf("Device found on Wire1 at address 0x%02X\n", address);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    randomSeed(analogRead(0));

    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);

    Wire1.begin(VOICE_SDA, VOICE_SCL);
    Wire1.setClock(100000);

    scanI2CBuses();

    mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);
    finger.begin(57600);

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);

    fingerprintMutex = xSemaphoreCreateMutex();

    if (!SPIFFS.begin(true))
    {
        Serial.println("Greška pri montiranju SPIFFS");
        return;
    }

    initializeUserFile();
    loadUsersFromFile();

    pinMode(ledPin, OUTPUT);
    pinMode(blueLedPin, OUTPUT);
    pinMode(pirPin, INPUT);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        Serial.println(F("OLED failed to initialize"));
        for (;;)
            ;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("Waiting for motion...");
    display.display();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi povezan. IP adresa: ");
    Serial.println(WiFi.localIP());

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    webSocket.enableHeartbeat(15000, 3000, 2);

    server.begin();

    initializeRegistrationState();

    Serial.print("WebSocket server running on: ws:
    Serial.print(WiFi.localIP());
    Serial.println(":81");

    
    server.enableCORS(true); 

    
    server.on("/", HTTP_OPTIONS, []()
              {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        server.send(204); });

    
    server.on("/", showMainPage);
    server.on("/loginPage", showLoginPage);
    server.on("/addUserPage", HTTP_GET, showAddUserPage);
    server.on("/addUser", HTTP_POST, handleAddUser);
    server.on("/resetFingerprintStatus", HTTP_POST, handleResetFingerprintStatus);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/delete", HTTP_GET, handleUserDeletion);
    server.on("/logout", HTTP_GET, handleLogout);

    
    server.on("/ws-info", HTTP_GET, []()
              {
        String info = "WebSocket Server Info:\n";
        info += "Server IP: " + WiFi.localIP().toString() + "\n";
        info += "Server Port: 81\n";
        info += "Connected Clients: " + String(connectedClients) + "\n";
        info += "Client Status:\n";
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            info += "Client " + String(i) + ": " + (clientConnected[i] ? "Connected" : "Disconnected") + "\n";
        }
        info += "WiFi Status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\n";
        info += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
        server.send(200, "text/plain", info); });

    server.on("/test-ws", HTTP_GET, []()
              {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String html = R"html(
            <html>
            <head>
                <title>WebSocket Test</title>
                <style>
                    body { font-family: Arial, sans-serif; margin: 20px; }
                    .status { margin: 10px 0; }
                    .messages { border: 1px solid #ccc; padding: 10px; margin-top: 10px; }
                </style>
            </head>
            <body>
                <h2>WebSocket Test Page</h2>
                <div class="status" id="status">Status: Disconnected</div>
                <button onclick="sendTest()">Send Test Message</button>
                <div class="messages" id="messages"></div>
                <script>
                    let ws;
                    function connect() {
                        ws = new WebSocket('ws:
                        ws.onopen = () => {
                            document.getElementById('status').innerHTML = 'Status: Connected';
                            console.log('WebSocket Connected');
                        };
                        ws.onclose = () => {
                            document.getElementById('status').innerHTML = 'Status: Disconnected';
                            console.log('WebSocket Disconnected');
                            setTimeout(connect, 2000);
                        };
                        ws.onmessage = (e) => {
                            console.log('Message received:', e.data);
                            document.getElementById('messages').innerHTML += '<br>Received: ' + e.data;
                        };
                        ws.onerror = (e) => {
                            console.error('WebSocket error:', e);
                            document.getElementById('messages').innerHTML += '<br>Error: ' + e;
                        };
                    }
                    function sendTest() {
                        if (ws && ws.readyState === WebSocket.OPEN) {
                            ws.send('test');
                            document.getElementById('messages').innerHTML += '<br>Sent: test';
                        }
                    }
                    connect();
                </script>
            </body>
            </html>
        )html";
        server.send(200, "text/html", html); });

    
    ESP32PWM::allocateTimer(0);
    myservo.setPeriodHertz(50);
    myservo.attach(servoPin, 1000, 2000);
    Serial.println("\n=== System Setup Complete ===");
}

void testVoiceSensor()
{
    static unsigned long lastCheck = 0;

    if (millis() - lastCheck > 100)
    {
        uint8_t CMDID = asr.getCMDID();
        if (CMDID != 0)
        {
            Serial.println("\nVoice Command Test:");
            Serial.printf("Command ID received: %d\n", CMDID);

            switch (CMDID)
            {
            case 1:
                Serial.println("Wake word 'probudi se' detected");
                break;
            case 2:
                Serial.println("Wake word 'hello robot' detected");
                break;
            case 5:
                Serial.println("Command 'otvori se' detected");
                break;
            default:
                Serial.printf("Unknown command ID: %d\n", CMDID);
            }
        }
        lastCheck = millis();
    }
}

void loop()
{
    unsigned long currentMillis = millis();
    static bool firstScanMessage = true;
    static bool secondScanMessage = true;

    server.handleClient();
    handleRegistrationDebug();
    webSocket.loop();

    if (DEBUG_REGISTRATION)
    {
        if (registrationActive)
        {
            if (currentMillis - lastRegistrationDebug >= REGISTRATION_DEBUG_INTERVAL)
            {
                Serial.println("\n=== Registration Process Debug ===");
                Serial.printf("Current Step: %d\n", currentStep);
                Serial.printf("ID Assigned: %s\n", idAssigned ? "Yes" : "No");
                Serial.printf("Fingerprint Added: %s\n", fingerprintAdded ? "Yes" : "No");
                Serial.printf("Registration Active: %s\n", registrationActive ? "Yes" : "No");
                Serial.printf("Time since last debug: %lu ms\n", currentMillis - lastRegistrationDebug);
                lastRegistrationDebug = currentMillis;
            }
        }
        else if (currentMillis - lastRegistrationDebug >= 5000)
        {
            Serial.println("Registration is not active. Current status:");
            checkDebugStatus();
            lastRegistrationDebug = currentMillis;
        }
    }

    if (DEBUG_WEBSOCKET && currentMillis - lastDebugPrint > DEBUG_INTERVAL)
    {
        Serial.println("\n=== WebSocket Server Status ===");
        Serial.printf("Connected clients: %d\n", webSocket.connectedClients());
        Serial.printf("WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        lastDebugPrint = currentMillis;
    }

    if (currentMillis - lastPing > 15000)
    {
        webSocket.broadcastPing();
        lastPing = currentMillis;
    }

    handlePIRSensor();
    if (!firstStepVerified)
    {
        handlePasswordInput();
        getFingerprintID();
    }

    if (firstStepVerified && authenticatedUser != nullptr)
    {
        uint8_t CMDID = asr.getCMDID();
        static bool wakeWordDetected = false;

        if (CMDID != 0)
        {
            Serial.printf("\nCommand received - CMDID: %d\n", CMDID);
            Serial.printf("Wake Word State: %s\n", wakeWordDetected ? "Active" : "Inactive");
            Serial.printf("Expected Command: %s\n", expectedVoiceCommand.c_str());
        }

        if (CMDID == 2)
        {
            Serial.println("Wake word detected!");
            wakeWordDetected = true;

            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("Wake word OK!");
            display.println("Say your command:");
            display.display();
            delay(500);
        }

        else if (wakeWordDetected && CMDID != 0)
        {
            bool validCommand = false;

            if (CMDID == expectedVoiceCommand.toInt())
            {
                validCommand = true;
            }

            switch (CMDID)
            {
            case 5:
                if (validCommand)
                {
                    Serial.println("Otvori se command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            case 6:
                if (validCommand)
                {
                    Serial.println("Otkljucaj command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            case 141:
                if (validCommand)
                {
                    Serial.println("Open the door opening command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            case 7:
                if (validCommand)
                {
                    Serial.println("Zatvori command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            case 142:
                if (validCommand)
                {
                    Serial.println("Close the door command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            case 82:
                if (validCommand)
                {
                    Serial.println("Reset command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            case 130:
                if (validCommand)
                {
                    Serial.println("Auto mode command received!");
                    display.clearDisplay();
                    displayWelcomeMessage(authenticatedUser->username);
                    moveServo();
                }
                break;

            default:
                Serial.println("Unrecognized command");
                validCommand = false;
                break;
            }

            if (validCommand)
            {

                firstStepVerified = false;
                authenticatedUser = nullptr;
                wakeWordDetected = false;
                voiceAttempts = 0;
                resetPIRDetection();
            }
            else
            {
                Serial.printf("Wrong command! Got %d, expected %s\n", CMDID, expectedVoiceCommand.c_str());
                voiceAttempts++;

                if (voiceAttempts >= maxVoiceAttempts)
                {
                    Serial.println("Too many failed voice command attempts!");
                    digitalWrite(blueLedPin, HIGH);
                    alarmState = true;
                    broadcastState("alarm", true);
                    sendTelegramMessage("Warning: Multiple failed voice command attempts detected!");

                    firstStepVerified = false;
                    authenticatedUser = nullptr;
                    wakeWordDetected = false;
                    voiceAttempts = 0;
                    resetPIRDetection();
                }
            }
        }
    }

    checkRegistrationTimeout();

    if (registrationActive)
    {
        if (xSemaphoreTake(fingerprintMutex, portMAX_DELAY))
        {
            bool stepCompleted = false;

            switch (currentStep)
            {
            case 0:
                if (!idAssigned)
                {
                    Serial.println("Step 0: Assigning Fingerprint ID");
                    assignFingerprintID();
                    if (idAssigned)
                    {
                        Serial.printf("ID assigned successfully: %d\n", fingerprintID);
                        sendProgressUpdate(0, "Place your finger on the sensor");
                        currentStep = 1;
                        stepCompleted = true;
                    }
                }
                break;

            case 1:
                if (firstScanMessage)
                {
                    Serial.println("Step 1: Waiting for first fingerprint scan");
                    firstScanMessage = false;
                }

                if (getFingerprintImage())
                {
                    firstScanMessage = true;
                    Serial.println("First scan successful");
                    sendProgressUpdate(1, "Remove finger and wait...");
                    currentStep = 2;
                    stepCompleted = true;
                    delay(1000);
                }
                break;

            case 2:
                if (secondScanMessage)
                {
                    Serial.println("Step 2: Waiting for second fingerprint scan");
                    secondScanMessage = false;
                }

                if (confirmSecondScan())
                {
                    secondScanMessage = true;
                    Serial.println("Second scan successful");
                    sendProgressUpdate(2, "Processing scans...");
                    currentStep = 3;
                    stepCompleted = true;
                }
                break;

            case 3:
                Serial.println("Step 3: Saving fingerprint");
                if (saveFingerprint())
                {
                    Serial.println("Fingerprint saved successfully");
                    sendProgressUpdate(3, "Registration complete!");
                    fingerprintAdded = true;
                    currentRegistration.registrationComplete = true;
                    saveFingerprintAdded(true);
                    registrationActive = false;
                    stepCompleted = true;
                }
                else
                {
                    Serial.println("Failed to save fingerprint");
                    sendProgressUpdate(3, "Failed to save. Please try again.");
                    resetRegistrationProcess();
                }
                break;

            default:
                Serial.printf("Invalid step encountered: %d\n", currentStep);
                resetRegistrationProcess();
                break;
            }

            if (stepCompleted)
            {
                Serial.printf("Completed step %d successfully\n", currentStep);
            }

            xSemaphoreGive(fingerprintMutex);
            delay(100);
        }
        else
        {
            Serial.println("Failed to acquire fingerprint mutex");
        }
    }

    if (startRegistrationPending && !registrationActive)
    {
        Serial.println("Starting registration from pending state");
        startFingerprintRegistration();
    }

    if (loggedInClientNum != -1)
    {
        sendAllStatusesToClient(loggedInClientNum);
    }

    static unsigned long lastStatus = 0;
    if (currentMillis - lastStatus > 5000)
    {
        Serial.printf("Connected WebSocket clients: %d\n", webSocket.connectedClients());
        lastStatus = currentMillis;
    }
}
