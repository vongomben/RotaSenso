#include <WiFiManager.h> // WiFiManager for network configuration
#include <SPIFFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

#define TRIGGER_PIN D6

// WiFi & MQTT Configuration Variables
String ssid;
const char* password = "password";
String mqtt_broker = "";
String mqtt_publish_channel = "";
String mqtt_receive_channel = "";

// Available MQTT Brokers
const char* mqtt_broker_options[] = {
  "public-mqtt-broker.bevywise.com",
  "broker.hivemq.com",
  "broker.emqx.io"
};

// Connection tracking
unsigned long lastConnectionAttempt = 0;
const unsigned long reconnectInterval = 5000;

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Servo & Potentiometer
static const int servoPin = 1;
static const int potentiometerPin = A5;

Servo servo1;
int lastMappedValue = -1;
int stableCount = 0;
const int stabilityThreshold = 10;

// Servo 8 middle positions
float medianValues[] = {11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75};

// WiFiManager Parameters
WiFiManager wm;
WiFiManagerParameter *custom_broker, *publish_channel, *receive_channel;

void setup() {
    Serial.begin(115200);
    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    servo1.attach(servoPin);

    wm.resetSettings(); // wipe settings

    if (!SPIFFS.begin(true)) {
        Serial.println("[ERROR] SPIFFS initialization failed!");
    }

    ssid = "RotaSenso" + String(random(1000, 9999));
    Serial.println("Generated SSID: " + ssid);

    loadConfig();

    // Set WiFiManager Title
    wm.setTitle("RotaSenso");

    // Create MQTT broker selection HTML radio buttons
    String mqtt_radio_str = "<h3>Select MQTT Broker:</h3>";
    for (int i = 0; i < 3; i++) {
        mqtt_radio_str += "<input type='radio' name='mqtt_broker' value='" + String(mqtt_broker_options[i]) + "'";
        if (mqtt_broker == mqtt_broker_options[i]) mqtt_radio_str += " checked";
        mqtt_radio_str += "> " + String(mqtt_broker_options[i]) + "<br>";
    }
    custom_broker = new WiFiManagerParameter(mqtt_radio_str.c_str());
    wm.addParameter(custom_broker);

    // Add Text Inputs for MQTT Channels
    publish_channel = new WiFiManagerParameter("mqtt_publish", "MQTT Publish Channel", mqtt_publish_channel.c_str(), 40);
    receive_channel = new WiFiManagerParameter("mqtt_receive", "MQTT Receive Channel", mqtt_receive_channel.c_str(), 40);

    wm.addParameter(publish_channel);
    wm.addParameter(receive_channel);

    // Customize WiFiManager menu
    std::vector<const char *> menu = {"wifi", "info", "restart", "exit"};
    wm.setMenu(menu);

    // Change "Configure WiFi" button text to "Configure"
    wm.setCustomMenuHTML("<script>document.addEventListener('DOMContentLoaded', function() { document.querySelector('.btn.wm-btn-save').innerText = 'Configure'; });</script>");

    // Start WiFiManager with auto-connect
    bool res = wm.autoConnect(ssid.c_str(), password);

    if (!res) {
        Serial.println("Failed to connect");
    } else {
        Serial.println("Connected to WiFi!");
        saveConfig();
    }

    // Connect to WiFi
    WiFi.begin(ssid.c_str(), password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi!");

    // Set up MQTT server and callback
    client.setServer(mqtt_broker.c_str(), 1883);
    client.setCallback(callback);
    reconnect();
}

void loop() {
    checkButton();

    int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 0, 180);
    servo1.write(servoPosition);

    int mappedValue = mapTo8Intervals(servoPosition);

    if (mappedValue != lastMappedValue) {
        stableCount = 0;
        lastMappedValue = mappedValue;
    } else {
        stableCount++;
    }

    if (stableCount == stabilityThreshold) {
        Serial.print("[Mapped Value: ");
        Serial.print(mappedValue);
        Serial.println("] Sent via MQTT!");

        if (client.connected()) {
            client.publish(mqtt_publish_channel.c_str(), String(mappedValue).c_str(), true);
        } else {
            reconnect();
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi Lost! Reconnecting...]");
        WiFi.begin(ssid.c_str(), password);
        delay(5000);
    }

    if (!client.connected()) {
        reconnect();
    }

    client.loop();
}

// Handle Button Press for Reset
void checkButton(){
    static unsigned long buttonPressStart = 0;
    
    if (digitalRead(TRIGGER_PIN) == LOW) {
        delay(50);
        if (digitalRead(TRIGGER_PIN) == LOW) {
            Serial.println("Button Pressed");
            if (buttonPressStart == 0) {
                buttonPressStart = millis();
            }
            if (millis() - buttonPressStart >= 3000) {
                Serial.println("Button Held for 3 sec - Erasing Config & Restarting");
                wm.resetSettings();
                ESP.restart();
            }
        }
    } else {
        buttonPressStart = 0;
    }
}

// Function to handle received MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("[Received on: ");
    Serial.print(topic);
    Serial.println("]");

    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println("[Received Data: " + message + "]");

    if (String(topic) == mqtt_receive_channel) {
        int index = message.toInt();
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

// Function for MQTT reconnection
void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientID = "ESP32-" + String(random(1000, 9999));
        if (client.connect(clientID.c_str())) {
            Serial.println("[Connected to MQTT broker: " + mqtt_broker + "]");
            client.subscribe(mqtt_receive_channel.c_str());
        } else {
            Serial.print("[MQTT Connection Failed. Retrying in 5 seconds...]");
            delay(5000);
        }
    }
}

// Save MQTT broker & channels to SPIFFS
void saveConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to save MQTT configuration.");
        return;
    }

    mqtt_broker = wm.server->arg("mqtt_broker");
    mqtt_publish_channel = wm.server->arg("mqtt_publish");
    mqtt_receive_channel = wm.server->arg("mqtt_receive");

    file.println(mqtt_broker);
    file.println(mqtt_publish_channel);
    file.println(mqtt_receive_channel);
    file.close();
}

// Load MQTT settings from SPIFFS
void loadConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_READ);
    if (!file) return;
    mqtt_broker = file.readStringUntil('\n');
    mqtt_publish_channel = file.readStringUntil('\n');
    mqtt_receive_channel = file.readStringUntil('\n');
    file.close();
}

// Map the servo value in 8 intervals
int mapTo8Intervals(int value) {
    return value / (180 / 8);
}
