// Rotasenso V5 - Servo position controlled in main loop
// Final tests: servo still not working 

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiManager.h>

// Pin for WiFi settings reset button
const int resetButtonPin = D6;  // pushbutton
const int ledPin = D7;          // the number of the LED pin

unsigned long buttonPressStartTime = 0;
const unsigned long resetPressTime = 5000;  // 5 seconds
bool resetButtonPressed = false;

// Servo and Potentiometer Management
static const int servoPin = 1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                // Count how many readings are stable
const int stabilityThreshold = 10;  // Number of stable readings before updating

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// Available MQTT Brokers
const char* mqtt_broker_options[] = {
  "test.mosquitto.org",
  "broker.hivemq.com",
  "broker.emqx.io",
  "public-mqtt-broker.bevywise.com"
};
int selected_broker_index = 0;                // Default to first broker
char mqtt_server[40] = "test.mosquitto.org";  // Buffer to store the selected broker

// Topics for communication
const char* outboundTopic = "test/SV02";  // Topic to send messages
const char* inboundTopic = "test/SV01";   // Topic to receive messages

// WiFi and MQTT client creation
WiFiClient espClient;
PubSubClient client(espClient);

// Interval for sending MQTT messages
const long messageInterval = 2000;  // 2 seconds
unsigned long lastMessageTime = 0;

// Variables for servo control from callback to loop
int receivedServoIndex = -1;  // -1 means no new value
bool newServoValueReceived = false;
unsigned long lastServoMoveTime = 0;

// Function for handling received MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("*** CALLBACK TRIGGERED ***");
  Serial.print("Topic received: ");
  Serial.println(topic);
  Serial.print("Comparing with inboundTopic: ");
  Serial.println(inboundTopic);
  Serial.print("Are they equal? ");
  Serial.println(String(topic) == inboundTopic ? "YES" : "NO");

  // Convert the payload to a string
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Content: " + message);

  // If the message is on the servo control topic
  if (String(topic) == inboundTopic) {
    int index = message.toInt();
    // Checks that the index is valid (between 0 and 7)
    if (index >= 0 && index < 8) {
      // Store the received index to use it in the main loop
      receivedServoIndex = index;
      newServoValueReceived = true;
      Serial.print("New servo value received: ");
      Serial.println(index);
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Wait to stabilize the serial
  Serial.println("\nRotasenso V5 Starting...");

  servo1.attach(servoPin);  // Initialize the servo

  // Configure the button pin as input with pull-up resistor
  pinMode(resetButtonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);  // LED initially off

  // Test the servo at startup
  Serial.println("Testing servo movement...");
  for (int i = 0; i < 8; i++) {
    servo1.write(medianValues[i]);
    delay(100);
  }
  servo1.write(medianValues[0]);  // Initial position

  // Reset WiFi mode first
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);

  // Disable power saving mode
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Generate a random number between 1000-9999 for the AP name
  randomSeed(analogRead(A0));
  String apName = "Rotasenso-" + String(random(1000, 9999));

  // WiFiManager
  WiFiManager wifiManager;

  // Add custom parameters to WiFiManager
  WiFiManagerParameter mqtt_broker_param("broker", "MQTT Broker", mqtt_server, 40);

  // Options for broker selection
  String html = "<br/><label for='broker'>MQTT Broker</label><br>";
  html += "<select id='broker' name='broker' onchange='document.getElementById(\"broker\").value=this.value'>";
  for (int i = 0; i < 4; i++) {
    html += "<option value='";
    html += mqtt_broker_options[i];
    html += "'>";
    html += mqtt_broker_options[i];
    html += "</option>";
  }
  html += "</select>";

  WiFiManagerParameter custom_broker_options(html.c_str());

  wifiManager.addParameter(&mqtt_broker_param);
  wifiManager.addParameter(&custom_broker_options);

  // Set a timeout of 180 seconds
  wifiManager.setTimeout(180);

  Serial.println("Creating access point named: " + apName);

  // Try to connect with saved credentials
  // If it fails, open an access point with the generated name
  if (!wifiManager.autoConnect(apName.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  // Get the selected broker
  strncpy(mqtt_server, mqtt_broker_param.getValue(), sizeof(mqtt_server));
  Serial.print("Selected MQTT broker: ");
  Serial.println(mqtt_server);

  // Check connection status
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Set up MQTT server and callback for receiving messages
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    // Initial connection to MQTT
    reconnect();
  } else {
    Serial.println("WiFi connection issue detected even after autoConnect success");
    delay(3000);
    ESP.restart();
  }
}

void loop() {
  // Reset button handling
  if (digitalRead(resetButtonPin) == LOW) {  // Button pressed (LOW with pull-up)
    if (!resetButtonPressed) {
      resetButtonPressed = true;
      buttonPressStartTime = millis();
      digitalWrite(ledPin, LOW);  // Turn on LED when button is pressed
      Serial.println("Reset button pressed, holding for 5 seconds will reset WiFi settings...");
    } else {
      // Check if the button has been held long enough
      if (millis() - buttonPressStartTime >= resetPressTime) {
        Serial.println("Resetting WiFi settings and restarting...");

        // Blink LED rapidly to indicate reset
        for (int i = 0; i < 10; i++) {
          digitalWrite(ledPin, !digitalRead(ledPin));
          delay(100);
        }

        // Reset WiFi settings using WiFiManager
        WiFiManager wifiManager;
        wifiManager.resetSettings();

        // Wait a moment and then restart
        delay(1000);
        ESP.restart();
      }
    }
  } else {
    // Button was released before 5 seconds
    if (resetButtonPressed) {
      resetButtonPressed = false;
      digitalWrite(ledPin, HIGH);  // Turn off LED
      Serial.println("Reset button released before timeout");
    }
  }

  // read pot
  int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 180, 0);

  // Get the value from 0 to 7
  int mappedValue = mapTo8Intervals(servoPosition);

  // If the value has changed, reset the counter
  if (mappedValue != lastMappedValue) {
    stableCount = 0;
    lastMappedValue = mappedValue;
  } else {
    stableCount++;
  }

  // Check if it's time to send a message
  unsigned long currentTime = millis();

  // Send a message every 2 seconds if the value is stable
  if (currentTime - lastMessageTime >= messageInterval && stableCount >= stabilityThreshold) {
    lastMessageTime = currentTime;

    Serial.print("Servo Position: ");
    Serial.print(servoPosition);
    Serial.print(" -> Mapped Value: ");
    Serial.println(mappedValue);

    // Briefly blink LED to indicate message sending
    digitalWrite(ledPin, LOW);

    // Send the message 3 times for redundancy
    for (int i = 0; i < 3; i++) {
      client.publish(outboundTopic, String(mappedValue).c_str(), true);
      delay(50);
    }

    Serial.print("Message sent on topic: ");
    Serial.print(outboundTopic);
    Serial.print(" with value: ");
    Serial.println(mappedValue);

    // Turn off LED after brief blink
    delay(100);
    digitalWrite(ledPin, HIGH);
  }

  // *** SERVO HANDLING IN MAIN LOOP ***
  // Check if there's a new value received from callback
  if (newServoValueReceived) {
    // Blink LED to indicate command reception
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);

    // Detach and reattach servo to reset it
    servo1.detach();
    delay(50);
    servo1.attach(servoPin);

    // Calculate servo position based on received index
    float servoPos = medianValues[receivedServoIndex];

    // Move servo to position
    servo1.write(servoPos);

    // Log
    Serial.print("Moved servo to position: ");
    Serial.print(servoPos);
    Serial.print(" (index: ");
    Serial.print(receivedServoIndex);
    Serial.println(")");

    // Reset flag to avoid repeated movements
    newServoValueReceived = false;
    lastServoMoveTime = currentTime;
  }

  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.begin();  // Use credentials saved by WiFiManager
    delay(5000);
  }

  // Maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }

  // Keep MQTT active
  client.loop();
}

// Function for MQTT reconnection
void reconnect() {
  int retry_count = 0;
  const int max_retries = 5;

  while (!client.connected() && retry_count < max_retries) {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqtt_server);
    Serial.print("...");

    String clientID = "RotaS-" + String(random(1000, 9999));
    if (client.connect(clientID.c_str())) {
      Serial.println("Connected to MQTT broker");

      // Subscribe to topic to receive commands
      bool subscribed = client.subscribe(inboundTopic);
      if (subscribed) {
        Serial.print("Successfully subscribed to topic: ");
        Serial.println(inboundTopic);
      } else {
        Serial.print("Failed to subscribe to topic: ");
        Serial.println(inboundTopic);
      }
    } else {
      retry_count++;
      digitalWrite(ledPin, LOW);
      delay(100);
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.print(" Retry ");
      Serial.print(retry_count);
      Serial.print("/");
      Serial.print(max_retries);
      Serial.println("...");
      delay(2000);
      digitalWrite(ledPin, HIGH);
      delay(100);
    }
  }
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
  return value / (180 / 8);  // 180° divided by 8 intervals
}