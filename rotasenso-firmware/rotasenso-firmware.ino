// Rotasenso V7 - ESP Web Tools configuration support
// Non-blocking approach with minimal delays
// Improved responsiveness for servo control

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Pin for WiFi settings reset button
const int resetButtonPin = D6;  // pushbutton
const int ledPin = D7;          // the number of the LED pin

unsigned long buttonPressStartTime = 0;
const unsigned long resetPressTime = 5000;  // 5 seconds
bool resetButtonPressed = false;

// Servo and Potentiometer Management
static const int servoPin = D1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                // Count how many readings are stable
const int stabilityThreshold = 10;  // Number of stable readings before updating

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// Preferences for saving configuration
Preferences preferences;

// Configuration variables
char wifi_ssid[64] = "";
char wifi_password[64] = "";
char mqtt_server[64] = "test.mosquitto.org";  // Default MQTT broker
int mqtt_port = 1883;                         // Default MQTT port
char mqtt_user[32] = "";                      // MQTT username (optional)
char mqtt_password[32] = "";                  // MQTT password (optional)
char mqtt_topic_prefix[32] = "rotasenso/";    // Default topic prefix

// Available MQTT Brokers (for fallback)
const char* mqtt_broker_options[] = {
  "test.mosquitto.org",
  "broker.hivemq.com",
  "broker.emqx.io",
  "public-mqtt-broker.bevywise.com"
};

// Derived topics for communication
char outboundTopic[64]; // Will be set to mqtt_topic_prefix + "SV01"
char inboundTopic[64];  // Will be set to mqtt_topic_prefix + "SV02"

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

// Non-blocking LED control
unsigned long ledBlinkStartTime = 0;
bool ledBlinkActive = false;
int ledBlinkDuration = 0;

// Non-blocking message sending
bool isSendingMessage = false;
int messageRepeatCount = 0;
unsigned long lastMessagePartSent = 0;

// Non-blocking WiFi reconnection
unsigned long lastWiFiCheckTime = 0;
const long wifiCheckInterval = 5000;  // Check WiFi every 5 seconds

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
      // Calculate servo position based on received index
      float servoPos = medianValues[receivedServoIndex];

      // Move servo - brief detach/reattach cycle to ensure reliable movement
      servo1.detach();
      servo1.attach(servoPin);
      servo1.write(servoPos);
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

// Function to check for configuration data from ESP Web Tools
bool loadConfigFromManifest() {
  // Check if there's a "config.json" file in the flash
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return false;
  }

  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      Serial.println("Reading config file");
      size_t size = configFile.size();
      
      // Allocate a buffer to store contents of the file
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, buf.get());
      
      if (!error) {
        Serial.println("Parsed config file");
        
        // Get WiFi credentials
        if (doc.containsKey("ssid") && doc["ssid"].as<String>() != "") {
          strlcpy(wifi_ssid, doc["ssid"], sizeof(wifi_ssid));
          Serial.print("WiFi SSID from config: ");
          Serial.println(wifi_ssid);
        }
        
        if (doc.containsKey("password")) {
          strlcpy(wifi_password, doc["password"], sizeof(wifi_password));
          Serial.println("WiFi password loaded from config");
        }
        
        // Get MQTT settings
        if (doc.containsKey("mqtt_broker") && doc["mqtt_broker"].as<String>() != "") {
          strlcpy(mqtt_server, doc["mqtt_broker"], sizeof(mqtt_server));
          Serial.print("MQTT broker from config: ");
          Serial.println(mqtt_server);
        }
        
        if (doc.containsKey("mqtt_port")) {
          mqtt_port = doc["mqtt_port"];
          Serial.print("MQTT port from config: ");
          Serial.println(mqtt_port);
        }
        
        if (doc.containsKey("mqtt_user")) {
          strlcpy(mqtt_user, doc["mqtt_user"], sizeof(mqtt_user));
        }
        
        if (doc.containsKey("mqtt_password")) {
          strlcpy(mqtt_password, doc["mqtt_password"], sizeof(mqtt_password));
        }
        
        if (doc.containsKey("mqtt_topic")) {
          strlcpy(mqtt_topic_prefix, doc["mqtt_topic"], sizeof(mqtt_topic_prefix));
          Serial.print("MQTT topic prefix from config: ");
          Serial.println(mqtt_topic_prefix);
        }
        
        // Save the config to preferences
        saveConfig();
        
        // Delete the file after reading
        SPIFFS.remove("/config.json");
        
        return true;
      } else {
        Serial.print("Failed to parse config file: ");
        Serial.println(error.f_str());
      }
    }
  }
  
  return false;
}

// Function to save config to Preferences
void saveConfig() {
  preferences.begin("rotasenso", false);
  
  preferences.putString("mqtt_server", mqtt_server);
  preferences.putInt("mqtt_port", mqtt_port);
  preferences.putString("mqtt_user", mqtt_user);
  preferences.putString("mqtt_pass", mqtt_password);
  preferences.putString("mqtt_topic", mqtt_topic_prefix);
  
  preferences.end();
  Serial.println("Configuration saved to flash");
}

// Function to load config from Preferences
void loadConfig() {
  preferences.begin("rotasenso", true);
  
  String saved_broker = preferences.getString("mqtt_server", "");
  if (saved_broker != "") {
    strlcpy(mqtt_server, saved_broker.c_str(), sizeof(mqtt_server));
  }
  
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  
  String saved_user = preferences.getString("mqtt_user", "");
  if (saved_user != "") {
    strlcpy(mqtt_user, saved_user.c_str(), sizeof(mqtt_user));
  }
  
  String saved_pass = preferences.getString("mqtt_pass", "");
  if (saved_pass != "") {
    strlcpy(mqtt_password, saved_pass.c_str(), sizeof(mqtt_password));
  }
  
  String saved_topic = preferences.getString("mqtt_topic", "");
  if (saved_topic != "") {
    strlcpy(mqtt_topic_prefix, saved_topic.c_str(), sizeof(mqtt_topic_prefix));
  }
  
  preferences.end();
  Serial.println("Configuration loaded from flash");
}

// Function to build the MQTT topics
void buildMqttTopics() {
  snprintf(outboundTopic, sizeof(outboundTopic), "%sSV01", mqtt_topic_prefix);
  snprintf(inboundTopic, sizeof(inboundTopic), "%sSV02", mqtt_topic_prefix);
  
  Serial.print("Outbound topic: ");
  Serial.println(outboundTopic);
  Serial.print("Inbound topic: ");
  Serial.println(inboundTopic);
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Initial delay is acceptable for setup
  Serial.println("\nRotasenso V7 Starting...");

  servo1.attach(servoPin);  // Initialize the servo

  // Configure the button pin as input with pull-up resistor
  pinMode(resetButtonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);  // LED initially off

  // Test the servo at startup
  Serial.println("Testing servo movement...");
  for (int i = 0; i < 8; i++) {
    servo1.write(medianValues[i]);
    delay(100);  // Short delays during setup are acceptable
  }
  servo1.write(medianValues[7]);  // Initial position

  // Initialize SPIFFS for config file
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS - will use default or saved settings");
  }

  // Try to load config from ESP Web Tools manifest
  bool configFromManifest = loadConfigFromManifest();
  
  if (!configFromManifest) {
    // If no manifest config, load from saved preferences
    loadConfig();
  }
  
  // Build the MQTT topics based on prefix
  buildMqttTopics();

  // Reset WiFi mode first
  WiFi.mode(WIFI_OFF);
  delay(1000);  // Short delay during setup is acceptable
  WiFi.mode(WIFI_STA);

  // Disable power saving mode
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Try to connect using stored credentials if available
  if (strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
    Serial.print("Connecting to WiFi with stored credentials for SSID: ");
    Serial.println(wifi_ssid);
    
    WiFi.begin(wifi_ssid, wifi_password);
    
    int connectionAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && connectionAttempts < 20) {
      delay(500);
      Serial.print(".");
      connectionAttempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi using stored credentials");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Failed to connect with stored credentials, falling back to WiFiManager");
    }
  }
  
  // If not connected, use WiFiManager
  if (WiFi.status() != WL_CONNECTED) {
    // Generate a random number between 1000-9999 for the AP name
    randomSeed(analogRead(A0));
    String apName = "Rotasenso-" + String(random(1000, 9999));
    
    // WiFiManager
    WiFiManager wifiManager;
    
    // Add custom parameters to WiFiManager
    WiFiManagerParameter mqtt_broker_param("broker", "MQTT Broker", mqtt_server, sizeof(mqtt_server)-1);
    WiFiManagerParameter mqtt_port_param("port", "MQTT Port", String(mqtt_port).c_str(), 6);
    WiFiManagerParameter mqtt_user_param("user", "MQTT Username (optional)", mqtt_user, sizeof(mqtt_user)-1);
    WiFiManagerParameter mqtt_pass_param("pass", "MQTT Password (optional)", mqtt_password, sizeof(mqtt_password)-1);
    WiFiManagerParameter mqtt_topic_param("topic", "MQTT Topic Prefix", mqtt_topic_prefix, sizeof(mqtt_topic_prefix)-1);
    
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
    wifiManager.addParameter(&mqtt_port_param);
    wifiManager.addParameter(&mqtt_user_param);
    wifiManager.addParameter(&mqtt_pass_param);
    wifiManager.addParameter(&mqtt_topic_param);
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
    
    // Get the values from WiFiManager
    strlcpy(mqtt_server, mqtt_broker_param.getValue(), sizeof(mqtt_server));
    mqtt_port = String(mqtt_port_param.getValue()).toInt();
    strlcpy(mqtt_user, mqtt_user_param.getValue(), sizeof(mqtt_user));
    strlcpy(mqtt_password, mqtt_pass_param.getValue(), sizeof(mqtt_password));
    strlcpy(mqtt_topic_prefix, mqtt_topic_param.getValue(), sizeof(mqtt_topic_prefix));
    
    // Save the new configuration
    saveConfig();
    
    // Build the MQTT topics with the new prefix
    buildMqttTopics();
  }
  
  // Check connection status
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Set up MQTT server and callback for receiving messages
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    
    // Initial connection to MQTT
    reconnect();
  } else {
    Serial.println("WiFi connection issue detected");
    delay(3000);
    ESP.restart();
  }
}

void loop() {
  unsigned long currentTime = millis();

  // Non-blocking LED control
  if (ledBlinkActive && currentTime - ledBlinkStartTime >= ledBlinkDuration) {
    digitalWrite(ledPin, HIGH);  // Turn off LED
    ledBlinkActive = false;
  }

  // Reset button handling
  if (digitalRead(resetButtonPin) == LOW) {  // Button pressed (LOW with pull-up)
    if (!resetButtonPressed) {
      resetButtonPressed = true;
      buttonPressStartTime = currentTime;
      digitalWrite(ledPin, LOW);  // Turn on LED when button is pressed
      Serial.println("Reset button pressed, holding for 5 seconds will reset WiFi settings...");
    } else {
      // Check if the button has been held long enough
      if (currentTime - buttonPressStartTime >= resetPressTime) {
        Serial.println("Resetting WiFi settings and restarting...");

        // Non-blocking LED flashing for reset indication
        for (int i = 0; i < 10; i++) {
          digitalWrite(ledPin, !digitalRead(ledPin));
          delay(100);  // Short delay acceptable for reset sequence
        }

        // Reset WiFi settings using WiFiManager
        WiFiManager wifiManager;
        wifiManager.resetSettings();

        // Wait a moment and then restart
        delay(1000);  // Acceptable for reset sequence
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

  // Read pot - no delay needed
  int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 180, 0);
  int mappedValue = mapTo8Intervals(servoPosition);

  // If the value has changed, reset the counter
  if (mappedValue != lastMappedValue) {
    stableCount = 0;
    lastMappedValue = mappedValue;
  } else {
    stableCount++;
  }

  // Non-blocking message sending sequence
  if (!isSendingMessage && currentTime - lastMessageTime >= messageInterval && stableCount >= stabilityThreshold) {
    // Start message sending sequence
    isSendingMessage = true;
    messageRepeatCount = 0;
    lastMessagePartSent = currentTime;

    // Log message details
    Serial.print("Servo Position: ");
    Serial.print(servoPosition);
    Serial.print(" -> Mapped Value: ");
    Serial.println(mappedValue);

    // Start LED blink
    digitalWrite(ledPin, LOW);  // Turn on LED
    ledBlinkActive = true;
    ledBlinkStartTime = currentTime;
    ledBlinkDuration = 100;  // 100ms blink
  }

  // Handle message sending in steps without blocking
  if (isSendingMessage) {
    if (messageRepeatCount < 3 && currentTime - lastMessagePartSent >= 50) {
      client.publish(outboundTopic, String(mappedValue).c_str(), true);
      messageRepeatCount++;
      lastMessagePartSent = currentTime;

      if (messageRepeatCount >= 3) {
        // All messages sent
        Serial.print("Message sent on topic: ");
        Serial.print(outboundTopic);
        Serial.print(" with value: ");
        Serial.println(mappedValue);

        lastMessageTime = currentTime;
        isSendingMessage = false;
      }
    }
  }

  // Non-blocking servo handling
  if (newServoValueReceived) {
    // Start brief LED blink for servo command reception
    digitalWrite(ledPin, LOW);  // Turn on LED
    ledBlinkActive = true;
    ledBlinkStartTime = currentTime;
    ledBlinkDuration = 50;  // 50ms blink

    // Calculate servo position based on received index
    float servoPos = medianValues[receivedServoIndex];

    // Move servo - brief detach/reattach cycle to ensure reliable movement
    servo1.detach();
    servo1.attach(servoPin);
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

  // Non-blocking WiFi maintenance
  if (WiFi.status() != WL_CONNECTED) {
    if (currentTime - lastWiFiCheckTime > wifiCheckInterval) {
      Serial.println("WiFi lost! Reconnecting...");
      WiFi.begin();  // Use credentials saved by WiFiManager
      lastWiFiCheckTime = currentTime;
    }
  }

  // Maintain MQTT connection
  if (!client.connected()) {
    reconnect();  // Consider making this non-blocking in future versions
  }

  // Keep MQTT active - this is non-blocking
  client.loop();
}

// Handler to save configuration from ESP Web Tools to a file
void saveConfigFromEspWebTools() {
  // Check if SPIFFS is mounted
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  
  // Create JSON document with config data
  StaticJsonDocument<512> doc;
  
  // Get config data from manifest
  doc["ssid"] = wifi_ssid;
  doc["password"] = wifi_password;
  doc["mqtt_broker"] = mqtt_server;
  doc["mqtt_port"] = mqtt_port;
  doc["mqtt_user"] = mqtt_user;
  doc["mqtt_password"] = mqtt_password;
  doc["mqtt_topic"] = mqtt_topic_prefix;
  
  // Write JSON to file
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  
  // Serialize JSON to file
  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write config to file");
  } else {
    Serial.println("Config saved from ESP Web Tools");
  }
  
  configFile.close();
}

// Function for MQTT reconnection - could be improved to be non-blocking in future
void reconnect() {
  int retry_count = 0;
  const int max_retries = 5;

  while (!client.connected() && retry_count < max_retries) {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.print(mqtt_port);
    Serial.print("...");

    String clientID = "RotaS-" + String(random(1000, 9999));
    
    bool connected = false;
    
    // Connect with credentials if provided
    if (strlen(mqtt_user) > 0) {
      connected = client.connect(clientID.c_str(), mqtt_user, mqtt_password);
      Serial.println("Connecting with credentials");
    } else {
      connected = client.connect(clientID.c_str());
      Serial.println("Connecting without credentials");
    }
    
    if (connected) {
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
      delay(100);  // Acceptable for connection retry sequence
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.print(" Retry ");
      Serial.print(retry_count);
      Serial.print("/");
      Serial.print(max_retries);
      Serial.println("...");
      delay(2000);  // Acceptable for connection retry sequence
      digitalWrite(ledPin, HIGH);
    }
  }
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
 // return value / (180 / 8);  // 180° divided by 8 intervals

  // Map from 0-180 to 0-7
  return constrain(map(value, 0, 181, 0, 8), 0, 7);
}