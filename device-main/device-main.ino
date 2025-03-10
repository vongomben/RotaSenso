// 2025 Gomba San Valentine's Special RotaSenso project
// Calibrating the sheet: pot movement to servo movements

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiManager.h>

// Credenziali WiFi
const char* ssid = "Cerbiatto";
const char* password = "parall3l0!";

// MQTT broker
const char* mqtt_server = "public-mqtt-broker.bevywise.com";  // the other brokers if is not responding properly

// WiFi and MQTT client creation
WiFiClient espClient;
PubSubClient client(espClient);

// Interval for sending MQTT messages (we'll send based on the pot position though)
unsigned long lastMsgTime = 0;
const long msgInterval = 5000;

// Servo and Potentiometer Management
static const int servoPin = 1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                // Count how many readings are stable
const int stabilityThreshold = 10;  // Number of stable readings before updating


// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// Function for handling received MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Messaggio ricevuto su topic: ");
  Serial.println(topic);

  // Convert the payload in a string
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Contenut: " + message);

  // If the message arrives at ‘/SV02’, check the value received
  // IMPORTANT: CHANGE ‘/SV02’ to ‘/SV01’ for allowing the two devices to talk to each other
  if (String(topic) == "test/SV01") {
    int index = message.toInt();  // convert message to number
    Serial.println(index);
    // Checks that the index is valid (between 0 and 7)
    if (index >= 0 && index < 8) {
      float servoPos = medianValues[index];
      servo1.write(servoPos);  // Moves the servo to the required position
      Serial.print("Moved the servo to: ");
      Serial.println(servoPos);
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

void setup() {
  Serial.begin(115200);
  servo1.attach(servoPin);  // Initialise the servo

  // Generate a random number between 1000-9999 for the AP name
  String apName = "rotasenso-" + String(random(1000, 9999));
  
  // WiFiManager
  WiFiManager wifiManager;
  
  // Imposta un timeout di 180 secondi
  wifiManager.setTimeout(180);
  
  Serial.println("Creating access point named: " + apName);
  
  // Tenta di connettersi con le credenziali salvate
  // Se fallisce, apre un access point con il nome generato
  if(!wifiManager.autoConnect(apName.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    // Riavvia e prova di nuovo
    ESP.restart();
  }
  
  Serial.println("Connected to WiFi");
  
  // Set up MQTT server and callback for receiving messages
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Initial connection to MQTT
  reconnect();
}

void loop() {

  // read pot
  int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 180, 0);
  //servo1.write(servoPosition);

  // Get the value from 0 to 7
  int mappedValue = mapTo8Intervals(servoPosition);

  // If the value has changed, reset the counter
  if (mappedValue != lastMappedValue) {
    stableCount = 0;  // reset counter for stability
    lastMappedValue = mappedValue;
  } else {
    stableCount++;  //Increases the counter if the value is stable
  }
  // Print only if value is stable for X cycles
  if (stableCount == stabilityThreshold) {
    Serial.print("Servo Position: ");
    Serial.print(servoPosition);
    Serial.print(" -> Mapped Value: ");
    Serial.println(mappedValue);

    // send message 3 times for redundancy
    for (int i = 0; i < 3; i++) {

      // IMPORTANT: CHANGE ‘/SV02’ to ‘/SV01’ for allowing the two devices to talk to each other
      client.publish("test/SV02", String(mappedValue).c_str(), true);
      delay(50);
    }
  }

  // Mantieni la connessione WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.begin(ssid, password);
    delay(5000);
  }


  // Maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }

  // Keep MQTT active
  client.loop();

  // for testing purposes
  /*    // Send messages every 5 seconds without blocking the loop
    if (millis() - lastMsgTime > msgInterval) {
        lastMsgTime = millis();
        Serial.println("Sending MQTT message...");
        client.publish("test/SV01", "4");
    }*/
}

// Function for MQTT reconnection
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    String clientID = "ESP32-" + String(random(1000, 9999));  // we are generating a random client id in order to have it different from the others
    if (client.connect(clientID.c_str())) {
      Serial.println("Connected to MQTT broker");
      client.subscribe("test/SV01");  // subscribe to /SV01
      client.subscribe("test/SV02");  // subscribe to /SV02
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}


// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
  return value / (180 / 8);  // 180° divided by 8 intervals
}