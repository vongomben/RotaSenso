#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// WiFi & MQTT Configuration (Loaded from SPIFFS)
String ssid = "";
String password = "";
String mqtt_broker = "";
String mqtt_send_channel = "";
String mqtt_receive_channel = "";

WiFiClient espClient;
PubSubClient client(espClient);

// Servo & Potentiometer Management
static const int servoPin = 1;           // Servo pin
static const int potentiometerPin = A5;  // Potentiometer pin

Servo servo1;
int lastMappedValue = -1;
int stableCount = 0;
const int stabilityThreshold = 10;  // Stable readings before sending MQTT

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// Function Prototypes
void loadConfig();
void connectToWiFi();
void connectToMQTT();
void sendMQTTMessage(String msg);
void reconnectMQTT();
void clearConfig();
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
    Serial.begin(115200);
    servo1.attach(servoPin);

    if (!SPIFFS.begin(true)) {
        Serial.println("[ERROR] SPIFFS initialization failed!");
        return;
    }

    // Load saved WiFi & MQTT settings
    loadConfig();
    
    // Connect to WiFi
    connectToWiFi();

    // Set up MQTT
    client.setServer(mqtt_broker.c_str(), 1883);
    client.setCallback(callback);

    // Connect to MQTT
    connectToMQTT();

    // Inform the user that the device is ready
    Serial.println("[Device Ready: Turn the knob to send data]");
}

void loop() {
    // Check for Serial input to clear config
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.equalsIgnoreCase("CLEAR")) {
            clearConfig();
            Serial.println("[Configuration cleared. Restarting ESP...]");
            delay(2000);
            ESP.restart(); // Restart ESP32 to apply changes
        }
    }

    int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 0, 180);
    servo1.write(servoPosition);

    // Map the value from 0 to 7
    int mappedValue = mapTo8Intervals(servoPosition);

    // Stability check
    if (mappedValue != lastMappedValue) {
        stableCount = 0;
        lastMappedValue = mappedValue;
    } else {
        stableCount++;
    }

    // Send MQTT only if the value is stable
    if (stableCount == stabilityThreshold) {
        Serial.print("[Mapped Value: ");
        Serial.print(mappedValue);
        Serial.println("] Sent via MQTT!");

        // Send data via MQTT
        if (client.connected()) {
            sendMQTTMessage(String(mappedValue));
        } else {
            reconnectMQTT();  // Reconnect if disconnected
        }
    }

    // Maintain WiFi & MQTT connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi Lost! Reconnecting...]");
        connectToWiFi();
    }
    
    if (!client.connected()) {
        reconnectMQTT();
    }

    client.loop();  // Maintain MQTT connection
    delay(20);
}

// Function to read configuration from SPIFFS
void loadConfig() {
    File file = SPIFFS.open("/config.txt");
    if (!file) {
        Serial.println("[No configuration file found.]");
        return;
    }
    
    ssid = file.readStringUntil('\n');
    password = file.readStringUntil('\n');
    mqtt_broker = file.readStringUntil('\n');
    mqtt_send_channel = file.readStringUntil('\n');
    mqtt_receive_channel = file.readStringUntil('\n');
    file.close();

    ssid.trim();
    password.trim();
    mqtt_broker.trim();
    mqtt_send_channel.trim();
    mqtt_receive_channel.trim();

    Serial.println("[Configuration Loaded]");
}

// Function to connect to WiFi
void connectToWiFi() {
    Serial.println("[Connecting to WiFi...]");
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {  
        delay(1000);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi Connected to: " + ssid + "]");
        Serial.print("[IP Address: ");
        Serial.print(WiFi.localIP());
        Serial.println("]");
    } else {
        Serial.println("\n[Failed to connect to WiFi]");
    }
}

// Function to connect to MQTT Broker
void connectToMQTT() {
    Serial.println("[Connecting to MQTT Broker...]");
    while (!client.connected()) {
        String clientID = "ESP32-" + String(random(1000, 9999));
        if (client.connect(clientID.c_str())) {
            Serial.println("[MQTT Connected to Broker: " + mqtt_broker + "]");

            // Subscribe to receive channel
            client.subscribe(mqtt_receive_channel.c_str());
        } else {
            Serial.print("[MQTT Connection Failed. Retrying in 5 seconds...]");
            delay(5000);
        }
    }
}

// Function to send data via MQTT
void sendMQTTMessage(String msg) {
    client.publish(mqtt_send_channel.c_str(), msg.c_str());
}

// Function for MQTT reconnection
void reconnectMQTT() {
    if (!client.connected()) {
        Serial.println("[Reconnecting to MQTT...]");
        connectToMQTT();
    }
}

// Function to clear configuration from SPIFFS
void clearConfig() {
    if (SPIFFS.exists("/config.txt")) {
        SPIFFS.remove("/config.txt");
        Serial.println("[Configuration file deleted]");
    } else {
        Serial.println("[No configuration file to delete]");
    }
}

// Function for handling received MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("[Message received on: ");
    Serial.print(topic);
    Serial.println("]");

    // Convert payload to string
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println("[Received Data: " + message + "]");

    // Process received MQTT message
    if (String(topic) == mqtt_receive_channel) {
        int index = message.toInt();

        // Check if index is valid (between 0 and 7)
        if (index >= 0 && index < 8) {
            float servoPos = medianValues[index];
            servo1.write(servoPos);
            Serial.print("[Servo Moved to: ");
            Serial.print(servoPos);
            Serial.println("]");
        } else {
            Serial.println("[Error: Value out of range]");
        }
    }
}

// Function to map the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
    return value / (180 / 8);
}
