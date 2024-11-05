#include "DFRobot_DF2301Q.h"

#define Led 2  // Na ESP32, GPIO2 je često korišćen za LED diode

// I2C komunikacija
DFRobot_DF2301Q_I2C asr;

void setup() {
  Serial.begin(115200);

  pinMode(Led, OUTPUT);    // Inicijalizacija LED pina kao izlaz
  digitalWrite(Led, LOW);  // Postavi LED pin na LOW

  // Inicijalizacija senzora
  while (!(asr.begin())) {
    Serial.println("Neuspešna komunikacija sa uređajem, proverite konekciju");
    delay(3000);
  }
  Serial.println("Početna inicijalizacija je uspešna!");

  asr.setVolume(7);  // Postavljanje glasnoće

  asr.setMuteMode(0);  // Postavi režim za zvuk (0: zvuk uključen, 1: isključen)

  asr.setWakeTime(20);  // Postavi trajanje stanja budnosti

  uint8_t wakeTime = asr.getWakeTime();
  Serial.print("wakeTime = ");
  Serial.println(wakeTime);
}

void loop() {
  uint8_t CMDID = asr.getCMDID();
  switch (CMDID) {
    case 103:                                                  // Ako je komanda "Upali svetlo"
      digitalWrite(Led, HIGH);                                 // Upali LED diodu
      Serial.println("Primljena komanda 'Upali svetlo', oznaka komande '103'"); 
      break;

    case 104:                                                  // Ako je komanda "Ugasi svetlo"
      digitalWrite(Led, LOW);                                  // Ugasi LED diodu
      Serial.println("Primljena komanda 'Ugasi svetlo', oznaka komande '104'");  
      break;

    default:
      if (CMDID != 0) {
        Serial.print("CMDID = ");  // Ispis ID komande
        Serial.println(CMDID);
      }
  }
  delay(300);
}
