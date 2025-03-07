#include <WiFiManager.h>  // WiFiManager for network configuration
#include <SPIFFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

#define TRIGGER_PIN D6
#define LED_PIN LED_BUILTIN  // Use the built-in LED

// Function Prototypes
void saveConfig();
void loadConfig();
void reconnect();
void checkButton();
void callback(char* topic, byte* payload, unsigned int length);
void blinkLED(int times, int duration);
int mapTo8Intervals(int value);

// WiFi & MQTT Configuration Variables
String ssid;
const char* password = "password";
String mqtt_broker = "";
String mqtt_publish_channel = "";
String mqtt_receive_channel = "";

// Default Values (in case nothing is set)
const char* default_mqtt_broker = "broker.hivemq.com";
const char* default_publish_channel = "test/SV02";
const char* default_receive_channel = "test/SV01";

// Available MQTT Brokers
const char* mqtt_broker_options[] = {
    "public-mqtt-broker.bevywise.com",
    "broker.hivemq.com",
    "broker.emqx.io"
};

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
    pinMode(LED_PIN, OUTPUT);
    servo1.attach(servoPin);

    Serial.println("\n[INFO] Initializing...");

    // Reset stored settings on every upload
    SPIFFS.begin(true);
    SPIFFS.remove("/mqtt_config.txt");
    Serial.println("[INFO] Configuration Reset on Upload!");

    ssid = "RotaSenso" + String(random(1000, 9999));
    Serial.println("Generated SSID: " + ssid);

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

    // Configure WiFiManager Menu
    std::vector<const char *> menu = {"wifi", "info", "restart", "exit"};
    wm.setMenu(menu);

    // Change "Configure WiFi" Button to "Configure"
    wm.setCustomMenuHTML("<script>document.addEventListener('DOMContentLoaded', function() { document.querySelector('.btn.wm-btn-save').innerText = 'Configure'; });</script>");

    // Start WiFiManager
    bool res = wm.autoConnect(ssid.c_str(), password);
    if (!res) {
        Serial.println("[ERROR] Failed to connect to WiFi");
    } else {
        Serial.println("[INFO] Connected to WiFi!");
        saveConfig();  // ✅ Now correctly declared before calling
    }

    // Ensure MQTT values are set, or use defaults
    if (mqtt_broker.length() == 0) mqtt_broker = default_mqtt_broker;
    if (mqtt_publish_channel.length() == 0) mqtt_publish_channel = default_publish_channel;
    if (mqtt_receive_channel.length() == 0) mqtt_receive_channel = default_receive_channel;

    Serial.println("[MQTT Broker]: " + mqtt_broker);
    Serial.println("[MQTT Publish Channel]: " + mqtt_publish_channel);
    Serial.println("[MQTT Receive Channel]: " + mqtt_receive_channel);

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
        Serial.println("[INFO] Sending MQTT Message: " + String(mappedValue));

        if (client.connected()) {
            client.publish(mqtt_publish_channel.c_str(), String(mappedValue).c_str(), true);
        } else {
            reconnect();
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ERROR] WiFi Disconnected! Reconnecting...");
        WiFi.begin(ssid.c_str(), password);
        delay(5000);
    }

    if (!client.connected()) {
        reconnect();
    }

    client.loop();
}

// ✅ **Fixed: Properly Declared & Defined saveConfig()**
void saveConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to save configuration");
        return;
    }
    file.println(mqtt_broker);
    file.println(mqtt_publish_channel);
    file.println(mqtt_receive_channel);
    file.close();
    Serial.println("[INFO] Configuration saved");
}

// ✅ **Fixed: Properly Declared & Defined loadConfig()**
void loadConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_READ);
    if (!file) {
        Serial.println("[INFO] No previous configuration found");
        return;
    }
    mqtt_broker = file.readStringUntil('\n');
    mqtt_publish_channel = file.readStringUntil('\n');
    mqtt_receive_channel = file.readStringUntil('\n');
    file.close();
}

// Function for MQTT reconnection
void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientID = "ESP32-" + String(random(1000, 9999));
        if (client.connect(clientID.c_str())) {
            Serial.println("[INFO] Connected to MQTT Broker: " + mqtt_broker);
            client.subscribe(mqtt_receive_channel.c_str());
        } else {
            Serial.println("[ERROR] MQTT Connection Failed. Retrying in 5 seconds...");
            blinkLED(3, 500);  // Blink LED to indicate failure
            delay(5000);
        }
    }
}

// ✅ **Fixed: Properly Declared & Defined callback()**
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.println("[INFO] Received MQTT Message!");
}

// Blink LED for error notification
void blinkLED(int times, int duration) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(duration);
        digitalWrite(LED_PIN, LOW);
        delay(duration);
    }
}

// Maps the servo value in 8 intervals
int mapTo8Intervals(int value) {
    return value / (180 / 8);
}
