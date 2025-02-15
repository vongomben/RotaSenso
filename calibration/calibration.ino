#include <ESP32Servo.h>

// 2025 Gomba San Valentine's Special RotaSenso project
// Calibrating the sheet: pot movement to servo movements

static const int servoPin = 1;         // servo pin 
static const int potentiometerPin = A5; // pot pin

Servo servo1;
int lastMappedValue = -1;  // Stores the last recorded value
int stableCount = 0;       //Count how many cycles the value is stable
const int stabilityThreshold = 10;  // Number of stable readings before printing

void setup() {
    Serial.begin(115200);
    servo1.attach(servoPin);  // Connetti il servo al pin 1
}

void loop() {
    int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 0, 180);
    servo1.write(servoPosition);

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
        Serial.print("Servo Position: ");
        Serial.print(servoPosition);
        Serial.print(" -> Mapped Value: ");
        Serial.println(mappedValue);
    }

    delay(20);
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
    return value / (180 / 8);  // 180° divided by 8 intervals
}
