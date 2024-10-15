#include <Keypad.h>

const byte ROWS = 4; // četiri reda
const byte COLS = 4; // četiri kolone

// Mapa tastature
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// Pokušaj sa izmenom rasporeda pinova
byte rowPins2[ROWS] = {13, 14, 27, 26}; // Kolone povezane na GPIO pinove
byte colPins2[COLS] = {25, 33, 32, 22}; // Redovi povezani na GPIO pinove

byte rowPins3[ROWS] = {13, 26, 14, 27}; // Kolone povezane na GPIO pinove
byte colPins3[COLS] = {32, 25, 33, 22}; // Redovi povezani na GPIO pinove

byte rowPins[ROWS] = {13, 12, 14, 27}; // Kolone povezane na GPIO pinove
byte colPins[COLS] = {26, 25, 33, 32}; // Redovi povezani na GPIO pinove

// Kreiramo Keypad objekat
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(115200);
}

void loop() {
  char key = keypad.getKey();  // Čitamo taster

  if (key) {
    Serial.print("Key Pressed : ");
    Serial.println(key);
  }
}
