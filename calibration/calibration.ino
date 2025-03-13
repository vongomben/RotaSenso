#include <ESP32Servo.h>

// 2025 Gomba San Valentine's Special RotaSenso project
// Calibrating the sheet: pot movement to servo movements
// updated for v2 of Rotasenso

static const int servoPin =  D1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

const int buttonPin = D6;  // the number of the pushbutton pin
const int ledPin = D7;     // the number of the LED pin

// variables will change:
int buttonState = 0;  // variable for reading the pushbutton status

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                //Count how many cycles the value is stable
const int stabilityThreshold = 10;  // Number of stable readings before printing

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };


void setup() {
  Serial.begin(115200);
  servo1.attach(servoPin);  // Connetti il servo al pin 1
                            // initialize the LED pin as an output:
  pinMode(ledPin, OUTPUT);
  // initialize the pushbutton pin as an input:
  pinMode(buttonPin, INPUT);

  for (int i = 0; i < 300; i++) {
    servo1.write(i);
    delay(10);
  }
}

void loop() {
  int servoPosition = map(analogRead(potentiometerPin), 0, 4095, 180, 0);
  // Serial.prinln(servoPosition);


  // read the state of the pushbutton value:
  buttonState = digitalRead(buttonPin);


  // Get the value from 0 to 7
  int mappedValue = mapTo8Intervals(servoPosition);

  // If the value has changed, reset the counter
  if (mappedValue != lastMappedValue) {
    stableCount = 0;  // Resetta il contatore di stabilità
    lastMappedValue = mappedValue;
  } else {
    stableCount++;  // Incrementa il contatore se il valore è stabile
  }

  // Print only if value is stable for X cycles
  if (stableCount == stabilityThreshold) {
    Serial.print("Pot Position: ");
    Serial.print(mappedValue);
    Serial.print(" Servo Position: ");
    Serial.print(servoPosition);
    Serial.print(" -> Mapped Value: ");
    Serial.println(medianValues[mappedValue]);
    servo1.write(medianValues[mappedValue]);
  }


  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  if (buttonState == HIGH) {
    // turn LED on:
    digitalWrite(ledPin, LOW);
  } else {
    // turn LED off:
    digitalWrite(ledPin, HIGH);
  }

  // servo1.write(servoPosition);

  delay(20);
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
 // return value / (180 / 8);  // 180° divided by 8 intervals

  // Map from 0-180 to 0-7
  return constrain(map(value, 0, 181, 0, 8), 0, 7);
}
