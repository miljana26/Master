#include <ESP32Servo.h>
#include <Keypad.h>

// Servo setup
Servo myservo;
int servoPin = 18;  // GPIO pin za servo
int pos = 0;  // Početna pozicija servoa

// Keypad setup
const byte ROWS = 4; // četiri reda
const byte COLS = 4; // četiri kolone

// Mapa tastature
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// Povezivanje redova i kolona sa ESP32 pinovima
byte rowPins[ROWS] = {13, 12, 14, 27}; // Redovi povezani na GPIO pinove
byte colPins[COLS] = {26, 25, 33, 32}; // Kolone povezane na GPIO pinove

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String inputPassword = "";  // Promenljiva za unos sa tastature
String correctPassword = "123A";  // Ispravna lozinka

void setup() {
  Serial.begin(115200);

  // Servo setup
  ESP32PWM::allocateTimer(0);
  myservo.setPeriodHertz(50);  // standardni servo sa 50 Hz
  myservo.attach(servoPin, 1000, 2000); // Povezivanje servoa na pin 18
}

void loop() {
  char key = keypad.getKey();  // Čitanje tastera

  if (key) {
    if (key == '#') {  // Ako je pritisnuto # (enter)
      if (inputPassword == correctPassword) {  // Provera da li se lozinka poklapa
        Serial.println("Ispravna lozinka!");
        moveServo();  // Pokrećemo servo
      } else {
        Serial.println("Neispravna lozinka!");
      }
      inputPassword = "";  // Resetovanje unosa nakon pritiska #
    } else {
      inputPassword += key;  // Dodavanje pritisnutog tastera u string
      Serial.print("Uneseno: ");
      Serial.println(inputPassword);
    }
  }
}

// Funkcija za pomeranje servoa od 0 do 180 stepeni polako
void moveServo() {
  for (pos = 0; pos <= 180; pos += 1) { // Polako pomera servo od 0 do 180 stepeni
    myservo.write(pos);  
    delay(10);  // Čekanje od 10ms da se poveća brzina kretanja
  }
}
