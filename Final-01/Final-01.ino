// TODO LED behaviour

#include <WiFiManager.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

#define TRIGGER_PIN D6
#define LED_PIN LED_BUILTIN

// WiFi & MQTT Configuration
String ssid;
const char* password = "password";
String mqtt_broker;
String mqtt_publish_channel;
String mqtt_receive_channel;

// Default MQTT Values
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
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// WiFiManager Parameters
WiFiManager wm;
WiFiManagerParameter *custom_broker, *publish_channel, *receive_channel;

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // Ensure LED starts OFF

  servo1.attach(servoPin);

  Serial.println("\n[INFO] Initializing...");

  // Reset Configurations on Each Upload
  SPIFFS.begin(true);
  SPIFFS.remove("/mqtt_config.txt");

  ssid = "RotaSenso" + String(random(1000, 9999));
  Serial.println("Generated SSID: " + ssid);

  // Load previous MQTT config
  loadConfig();

  // Ensure values are set; otherwise, use defaults
  if (mqtt_broker.length() == 0) mqtt_broker = default_mqtt_broker;
  if (mqtt_publish_channel.length() == 0) mqtt_publish_channel = default_publish_channel;
  if (mqtt_receive_channel.length() == 0) mqtt_receive_channel = default_receive_channel;

  Serial.println("[MQTT Broker]: " + mqtt_broker);
  Serial.println("[MQTT Publish Channel]: " + mqtt_publish_channel);
  Serial.println("[MQTT Receive Channel]: " + mqtt_receive_channel);

  // Set WiFiManager Title
  wm.setTitle("RotaSenso");

  // Create MQTT broker selection HTML radio buttons
  String mqtt_radio_str = "<h3>Select MQTT Broker:</h3>";
  for (int i = 0; i < 3; i++) {
    mqtt_radio_str += "<input type='radio' name='mqtt_broker' value='" + String(mqtt_broker_options[i]) + "' ";
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

  // Start WiFiManager with auto-connect
  bool res = wm.autoConnect(ssid.c_str(), password);
  if (!res) {
    Serial.println("[ERROR] Failed to connect to WiFi");
  } else {
    Serial.println("[INFO] Connected to WiFi!");
    saveConfig();
  }

  // Connect to MQTT Broker
  client.setServer(mqtt_broker.c_str(), 1883);
  client.setCallback(callback);
  reconnect();
}

void loop() {
  checkButton();

  // Read potentiometer value
  int potValue = analogRead(potentiometerPin);
  int mappedValue = mapTo8Intervals(potValue);

  if (mappedValue != lastMappedValue) {
    stableCount = 0;
    lastMappedValue = mappedValue;
  } else {
    stableCount++;
  }

  // Send pot value via MQTT when stable
  if (stableCount == stabilityThreshold) {
    Serial.print("[INFO] Sending: ");
    Serial.print(mappedValue);
    Serial.print(" to Broker: ");
    Serial.print(mqtt_broker);
    Serial.print(" on Channel: ");
    Serial.println(mqtt_publish_channel);

    if (client.connected()) {
      client.publish(mqtt_publish_channel.c_str(), String(mappedValue).c_str(), true);
    } else {
      reconnect();
    }
  }

  if (!client.connected()) {
    reconnect();
  }

  client.loop();
}

// Function to handle button press for reset
void checkButton() {
  static unsigned long buttonPressStart = 0;

  if (digitalRead(TRIGGER_PIN) == LOW) {
    delay(50);
    if (digitalRead(TRIGGER_PIN) == LOW) {
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

// Function for MQTT reconnection
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientID = "ESP32-" + String(random(1000, 9999));
    if (client.connect(clientID.c_str())) {
      Serial.println("[Connected to MQTT broker: " + mqtt_broker + "]");
      client.subscribe(mqtt_receive_channel.c_str());
    } else {
      Serial.println("[ERROR] MQTT Connection Failed. Retrying in 5 seconds...]");
      blinkLED(3, 500);
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

// Blink LED on error
void blinkLED(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    delay(duration);
  }
}

// Handle received MQTT messages and move servo
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("[INFO] Received MQTT Message!");

    digitalWrite(LED_PIN, LOW);  // Turn LED ON when message is received

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  int index = message.toInt();
  if (index >= 0 && index < 8) {
    float servoPos = medianValues[index];
    servo1.write(servoPos);
    Serial.print("[Servo Moved to: ");
    Serial.print(servoPos);
    Serial.println("]");
  }

    delay(500);  // Keep LED on for visibility
    digitalWrite(LED_PIN, HIGH);  // Turn LED OFF


}

// Map the pot value in 8 intervals
int mapTo8Intervals(int value) {
  return value / (4096 / 8);
}
