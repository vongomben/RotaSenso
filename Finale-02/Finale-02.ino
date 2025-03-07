#include <WiFiManager.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

#define TRIGGER_PIN D6
#define LED_PIN LED_BUILTIN

// WiFi & MQTT Configuration
String ssid;
// No password for captive portal
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

// LED pulse timing variables
unsigned long lastPulseTime = 0;
const int pulseDuration = 2000; // 2 seconds for a complete pulse cycle
bool isPulsing = false;
unsigned long pulseStartTime = 0;

// Connection status
bool wifiConnected = false;
bool mqttConnected = false;

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED ON (indicating not connected)

  servo1.attach(servoPin);

  Serial.println("\n[INFO] Initializing...");

  // Reset Configurations on Each Upload
  SPIFFS.begin(true);
  SPIFFS.remove("/mqtt_config.txt");

  ssid = "RotaSensos" + String(random(1000, 9999));
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

  // Start WiFiManager with auto-connect (no password)
  bool res = wm.autoConnect(ssid.c_str());
  if (!res) {
    Serial.println("[ERROR] Failed to connect to WiFi");
    wifiConnected = false;
  } else {
    Serial.println("[INFO] Connected to WiFi!");
    wifiConnected = true;
    saveConfig();
  }

  // Connect to MQTT Broker
  client.setServer(mqtt_broker.c_str(), 1883);
  client.setCallback(callback);
  reconnect();
  
  // Turn off LED if successfully connected to both WiFi and MQTT
  if (wifiConnected && mqttConnected) {
    digitalWrite(LED_PIN, HIGH); // LED OFF when connected
  }
}

void loop() {
  // Check button for reset or send position 0
  checkButton();

  // Update LED based on connection status
  updateLED();

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
      // Start pulsing LED for message confirmation
      startPulsing();
    } else {
      mqttConnected = false;
      reconnect();
    }
  }

  if (!client.connected()) {
    mqttConnected = false;
    reconnect();
  }

  client.loop();
}

// Function to handle button press for reset or send position 0
void checkButton() {
  static unsigned long buttonPressStart = 0;
  static bool buttonWasPressed = false;

  if (digitalRead(TRIGGER_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(TRIGGER_PIN) == LOW) {
      if (buttonPressStart == 0) {
        buttonPressStart = millis();
        buttonWasPressed = true;
      }
      
      // If button held for 3+ seconds, reset config and change SSID
      if (millis() - buttonPressStart >= 3000) {
        Serial.println("Button Held for 3 sec - Changing SSID & Restarting Captive Portal");
        wm.resetSettings();
        
        // Generate a new SSID
        ssid = "RotaSenso" + String(random(1000, 9999));
        Serial.println("New SSID: " + ssid);
        
        // Restart the captive portal with the new SSID
        wm.startConfigPortal(ssid.c_str());
      }
    }
  } else {
    // Button was released
    if (buttonWasPressed && buttonPressStart > 0 && millis() - buttonPressStart < 3000) {
      // Button pressed but not held for 3 seconds - send position 0
      Serial.println("[INFO] Button pressed - Sending position 0");
      
      // Move servo to position 0 for 1 second
      servo1.write(medianValues[0]);
      Serial.println("[Servo temporarily moved to position 0]");
      
      // Send position 0 via MQTT
      if (client.connected()) {
        client.publish(mqtt_publish_channel.c_str(), "0", true);
        startPulsing(); // Start pulsing LED for confirmation
      }
      
      // Wait 1 second
      delay(1000);
      
      // Return to previous position based on pot value
      int potValue = analogRead(potentiometerPin);
      int mappedValue = mapTo8Intervals(potValue);
      servo1.write(medianValues[mappedValue]);
    }
    
    buttonPressStart = 0;
    buttonWasPressed = false;
  }
}

// Start pulsing the LED
void startPulsing() {
  isPulsing = true;
  pulseStartTime = millis();
}

// Update LED based on connection status and pulse state
void updateLED() {
  // If not connected to WiFi or MQTT, keep LED ON (LOW)
  if (!wifiConnected || !mqttConnected) {
    digitalWrite(LED_PIN, LOW); // LED ON
    return;
  }
  
  // If pulsing is active (message sent confirmation)
  if (isPulsing) {
    // Pulse for 5 seconds then stop
    if (millis() - pulseStartTime > 5000) {
      isPulsing = false;
      digitalWrite(LED_PIN, HIGH); // LED OFF when done pulsing
      return;
    }
    
    // Create slow pulsing effect
    float progress = (millis() - pulseStartTime) % pulseDuration;
    float brightness = sin((progress / pulseDuration) * PI) * 127.5 + 127.5;
    
    // For digital LED, we'll just toggle on/off at a threshold
    if (brightness > 127) {
      digitalWrite(LED_PIN, LOW); // LED ON
    } else {
      digitalWrite(LED_PIN, HIGH); // LED OFF
    }
  } else {
    // Normal connected state - LED is OFF
    digitalWrite(LED_PIN, HIGH);
  }
}

// Function for MQTT reconnection
void reconnect() {
  int attempts = 0;
  while (!client.connected() && attempts < 3) {
    attempts++;
    Serial.print("Attempting MQTT connection...");
    String clientID = "ESP32-" + String(random(1000, 9999));
    if (client.connect(clientID.c_str())) {
      Serial.println("[Connected to MQTT broker: " + mqtt_broker + "]");
      client.subscribe(mqtt_receive_channel.c_str());
      mqttConnected = true;
    } else {
      Serial.println("[ERROR] MQTT Connection Failed. Retrying in 5 seconds...]");
      mqttConnected = false;
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
    digitalWrite(LED_PIN, LOW);  // LED ON
    delay(duration);
    digitalWrite(LED_PIN, HIGH); // LED OFF
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