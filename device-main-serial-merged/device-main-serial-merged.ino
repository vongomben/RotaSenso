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
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
    Serial.begin(115200);
    servo1.attach(servoPin);

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed!");
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
}

void loop() {
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
        Serial.print("Servo Position: ");
        Serial.print(servoPosition);
        Serial.print(" -> Mapped Value: ");
        Serial.println(mappedValue);

        // Send data via MQTT
        if (client.connected()) {
            sendMQTTMessage(String(mappedValue));
        } else {
            reconnectMQTT();  // Reconnect if disconnected
        }
    }

    // Maintain WiFi & MQTT connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost! Reconnecting...");
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
        Serial.println("No configuration file found.");
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

    Serial.println("Loaded Configuration:");
    Serial.println("SSID: " + ssid);
    Serial.println("Broker: " + mqtt_broker);
    Serial.println("Send Channel: " + mqtt_send_channel);
    Serial.println("Receive Channel: " + mqtt_receive_channel);
}

// Function to connect to WiFi
void connectToWiFi() {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {  
        delay(1000);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect to WiFi.");
    }
}

// Function to connect to MQTT Broker
void connectToMQTT() {
    Serial.println("Connecting to MQTT Broker...");
    while (!client.connected()) {
        String clientID = "ESP32-" + String(random(1000, 9999));
        if (client.connect(clientID.c_str())) {
            Serial.println("Connected to MQTT Broker!");

            // Subscribe to receive channel
            client.subscribe(mqtt_receive_channel.c_str());
        } else {
            Serial.print("Failed. Retry in 5 seconds...");
            delay(5000);
        }
    }
}

// Function for MQTT reconnection
void reconnectMQTT() {
    if (!client.connected()) {
        Serial.println("Reconnecting to MQTT...");
        connectToMQTT();
    }
}

// Function to send data via MQTT
void sendMQTTMessage(String msg) {
    client.publish(mqtt_send_channel.c_str(), msg.c_str());
    Serial.println("MQTT Message Sent: " + msg);
}

// Function for handling received MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message received on topic: ");
    Serial.println(topic);

    // Convert payload to string
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println("Content: " + message);

    // Process received MQTT message
    if (String(topic) == mqtt_receive_channel) {
        int index = message.toInt();
        Serial.println(index);

        // Check if index is valid (between 0 and 7)
        if (index >= 0 && index < 8) {
            float servoPos = medianValues[index];
            servo1.write(servoPos);
            Serial.print("Moved the servo to: ");
            Serial.println(servoPos);
        } else {
            Serial.println("Error: Value out of range");
        }
    }
}

// Function to map the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
    return value / (180 / 8);
}
