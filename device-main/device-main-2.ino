// 2025 Gomba San Valentine's Special RotaSenso project
// Enhanced version with WiFiManager and broker selection

#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <EEPROM.h>
#include <ArduinoJson.h>  // For handling JSON configuration

// EEPROM config
#define EEPROM_SIZE 512
#define CONFIG_ADDR 0

// MQTT brokers available for selection
const char* mqtt_brokers[] = {
  "public-mqtt-broker.bevywise.com",
  "broker.hivemq.com",
  "broker.emqx.io",
  "test.mosquitto.org"
};
const int numBrokers = 4;

// Default settings
char mqtt_server[80] = "public-mqtt-broker.bevywise.com";
int selectedBrokerIndex = 0; // Default broker index

// MQTT channel settings
char subscribeChannel[32] = "test/SV01";  // Default channel to subscribe to
char publishChannel[32] = "test/SV02";    // Default channel to publish to
char ackSubscribeChannel[36] = "test/SV01_ACK";  // ACK channel to subscribe to
char ackPublishChannel[36] = "test/SV02_ACK";    // ACK channel to publish to

// WiFi and MQTT client creation
WiFiClient espClient;
PubSubClient client(espClient);

// Handshake management
unsigned long messageTimestamp = 0;
unsigned long ackTimeout = 3000; // 3 seconds timeout for ACK
bool waitingForAck = false;
String lastMessageSent = "";
int messageRetries = 0;
const int maxRetries = 3;

// Connection tracking
unsigned long lastConnectionAttempt = 0;
const unsigned long reconnectInterval = 5000; // 5 seconds between connection attempts

// Servo and Potentiometer Management
static const int servoPin = 1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                // Count how many readings are stable
const int stabilityThreshold = 10;  // Number of stable readings before updating

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// WiFiManager configuration parameters
WiFiManagerParameter custom_mqtt_broker_dropdown("broker", "MQTT Broker", mqtt_server, 80);
WiFiManagerParameter custom_subscribe_channel("subscribe", "Subscribe Channel", subscribeChannel, 32);
WiFiManagerParameter custom_publish_channel("publish", "Publish Channel", publishChannel, 32);
WiFiManager wifiManager;
bool shouldSaveConfig = false;

// Config save callback
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// Generate the HTML for broker dropdown
String getBrokerDropdownHTML() {
  String options = "";
  
  for (int i = 0; i < numBrokers; i++) {
    options += "<option value='";
    options += mqtt_brokers[i];
    options += "'";
    
    if (i == selectedBrokerIndex) {
      options += " selected";
    }
    
    options += ">";
    options += mqtt_brokers[i];
    options += "</option>";
  }
  
  String customHTML = ""
    "<label for='broker'>MQTT Broker</label>"
    "<select id='broker' name='broker'>"
    + options +
    "</select>";
    
  return customHTML;
}

// Load configuration from EEPROM
bool loadConfiguration() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Read if config has been saved before
  if (EEPROM.read(CONFIG_ADDR) != 0xFF) {
    char config[EEPROM_SIZE - 1];
    for (int i = 0; i < EEPROM_SIZE - 1; i++) {
      config[i] = EEPROM.read(CONFIG_ADDR + 1 + i);
    }
    
    StaticJsonDocument<400> doc;
    DeserializationError error = deserializeJson(doc, config);
    
    if (!error) {
      strncpy(mqtt_server, doc["mqtt_server"], sizeof(mqtt_server));
      
      // Find the broker index
      selectedBrokerIndex = 0; // Default to first
      for (int i = 0; i < numBrokers; i++) {
        if (strcmp(mqtt_server, mqtt_brokers[i]) == 0) {
          selectedBrokerIndex = i;
          break;
        }
      }
      
      // Load channel configuration if available
      if (doc.containsKey("subscribe_channel")) {
        strncpy(subscribeChannel, doc["subscribe_channel"], sizeof(subscribeChannel));
      }
      if (doc.containsKey("publish_channel")) {
        strncpy(publishChannel, doc["publish_channel"], sizeof(publishChannel));
      }
      
      // Update ACK channels based on main channels
      snprintf(ackSubscribeChannel, sizeof(ackSubscribeChannel), "%s_ACK", subscribeChannel);
      snprintf(ackPublishChannel, sizeof(ackPublishChannel), "%s_ACK", publishChannel);
      
      Serial.println("Loaded configuration:");
      Serial.print("MQTT Broker: ");
      Serial.println(mqtt_server);
      Serial.print("Subscribe Channel: ");
      Serial.println(subscribeChannel);
      Serial.print("Publish Channel: ");
      Serial.println(publishChannel);
      Serial.print("ACK Subscribe Channel: ");
      Serial.println(ackSubscribeChannel);
      Serial.print("ACK Publish Channel: ");
      Serial.println(ackPublishChannel);
      
      return true;
    } else {
      Serial.println("Failed to parse config");
    }
  }
  
  return false;
}

// Save configuration to EEPROM
void saveConfiguration() {
  StaticJsonDocument<400> doc;
  
  doc["mqtt_server"] = mqtt_server;
  doc["subscribe_channel"] = subscribeChannel;
  doc["publish_channel"] = publishChannel;
  
  char buffer[EEPROM_SIZE - 1];
  serializeJson(doc, buffer, sizeof(buffer));
  
  EEPROM.write(CONFIG_ADDR, 0x42); // Config flag
  
  for (int i = 0; i < EEPROM_SIZE - 1; i++) {
    EEPROM.write(CONFIG_ADDR + 1 + i, buffer[i]);
  }
  
  EEPROM.commit();
  Serial.println("Configuration saved");
}

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

  // Check if this is an ACK message
  if (String(topic) == String(ackSubscribeChannel)) {
    if (waitingForAck && message == lastMessageSent) {
      Serial.println("ACK received for message: " + message);
      waitingForAck = false;
      messageRetries = 0;
    }
    return;
  }

  // If the message arrives at subscription channel, check the value received and send ACK
  if (String(topic) == String(subscribeChannel)) {
    int index = message.toInt();  // convert message to number
    Serial.println(index);
    // Checks that the index is valid (between 0 and 7)
    if (index >= 0 && index < 8) {
      float servoPos = medianValues[index];
      servo1.write(servoPos);  // Moves the servo to the required position
      Serial.print("Moved the servo to: ");
      Serial.println(servoPos);
      
      // Send acknowledgment on the ACK publish channel
      client.publish(ackPublishChannel, String(index).c_str(), true);
      Serial.println("Sent ACK for message: " + String(index));
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting RotaSenso with WiFiManager");
  
  servo1.attach(servoPin);  // Initialize the servo
  
  // Load saved configuration if it exists
  if (loadConfiguration()) {
    Serial.println("Using saved configuration");
  } else {
    Serial.println("Using default configuration");
  }
  
  // Setup WiFiManager configuration portal
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  // Set custom parameters for WiFiManager
  // We'll use a custom HTML to create a dropdown
  WiFiManagerParameter custom_mqtt_dropdown(
    "mqtt_dropdown", 
    getBrokerDropdownHTML().c_str(), 
    "", 
    0
  );
  
  wifiManager.addParameter(&custom_mqtt_broker_dropdown);
  wifiManager.addParameter(&custom_mqtt_dropdown);
  wifiManager.addParameter(&custom_subscribe_channel);
  wifiManager.addParameter(&custom_publish_channel);
  
  // Configure WiFiManager to show a configuration portal
  // Give it a name and password (empty password = no password)
  // This will block until connected or config timeout (default 3 minutes)
  if (!wifiManager.autoConnect("RotaSenso-AP")) {
    Serial.println("Failed to connect and hit timeout");
    // Reset and try again
    ESP.restart();
    delay(5000);
  }
  
  // Connected to WiFi
  Serial.println("Connected to WiFi");
  
  // Get custom parameters after connection
  strncpy(mqtt_server, custom_mqtt_broker_dropdown.getValue(), sizeof(mqtt_server));
  strncpy(subscribeChannel, custom_subscribe_channel.getValue(), sizeof(subscribeChannel));
  strncpy(publishChannel, custom_publish_channel.getValue(), sizeof(publishChannel));
  
  // Generate ACK channels based on the main channels
  snprintf(ackSubscribeChannel, sizeof(ackSubscribeChannel), "%s_ACK", subscribeChannel);
  snprintf(ackPublishChannel, sizeof(ackPublishChannel), "%s_ACK", publishChannel);
  
  // Find the broker index for the selected server
  for (int i = 0; i < numBrokers; i++) {
    if (strcmp(mqtt_server, mqtt_brokers[i]) == 0) {
      selectedBrokerIndex = i;
      break;
    }
  }
  
  Serial.println("Current configuration:");
  Serial.print("Subscribe Channel: ");
  Serial.println(subscribeChannel);
  Serial.print("Publish Channel: ");
  Serial.println(publishChannel);
  Serial.print("ACK Subscribe Channel: ");
  Serial.println(ackSubscribeChannel);
  Serial.print("ACK Publish Channel: ");
  Serial.println(ackPublishChannel);
  
  // Save configuration if needed
  if (shouldSaveConfig) {
    saveConfiguration();
  }
  
  // Set up MQTT server and callback
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  // Initial connection to MQTT broker
  reconnect();
  
  Serial.println("Setup complete");
}

void loop() {
  // read pot
  int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 0, 180);
  
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

    // Only send if we're not waiting for an ACK or if retry timeout occurred
    if (!waitingForAck || (waitingForAck && millis() - messageTimestamp > ackTimeout)) {
      // Check if we need to resend the same message (retry)
      if (waitingForAck) {
        if (messageRetries >= maxRetries) {
          Serial.println("Max retries reached, giving up on message: " + lastMessageSent);
          waitingForAck = false;
          messageRetries = 0;
        } else {
          messageRetries++;
          Serial.println("Retry " + String(messageRetries) + " for message: " + lastMessageSent);
        }
      } else {
        // New message to send
        lastMessageSent = String(mappedValue);
        messageRetries = 0;
      }

      if (!waitingForAck || messageRetries > 0) {
        // Publish to the configured publish channel
        client.publish(publishChannel, lastMessageSent.c_str(), true);
        Serial.println("Sent message: " + lastMessageSent + " on " + publishChannel);
        
        waitingForAck = true;
        messageTimestamp = millis();
      }
    }
  }

  // Check for ACK timeout
  if (waitingForAck && millis() - messageTimestamp > ackTimeout) {
    // Timeout will be handled on next stable reading
  }

  // Maintain WiFi connection with WiFiManager
  // WiFiManager handles reconnection automatically

  // Check and maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }

  // Keep MQTT active
  client.loop();
  
  // Reset WiFi configuration button check
  // Uncomment and set a GPIO pin to use as reset button
  /*
  if (digitalRead(RESET_PIN) == LOW) {
    Serial.println("Resetting WiFi configuration...");
    wifiManager.resetSettings();
    ESP.restart();
  }
  */
}

// Function for MQTT reconnection
void reconnect() {
  // Only attempt reconnection if enough time has passed since last attempt
  if (!client.connected() && (millis() - lastConnectionAttempt > reconnectInterval)) {
    lastConnectionAttempt = millis();
    Serial.print("Attempting MQTT connection to broker: ");
    Serial.print(mqtt_server);
    Serial.print("...");
    
    String clientID = "ESP32-" + String(random(1000, 9999)); // random client id
    
    if (client.connect(clientID.c_str())) {
      Serial.println("Connected to MQTT broker!");
      
      // Subscribe to the configured channels
      client.subscribe(subscribeChannel);  // Main channel for receiving messages
      client.subscribe(ackSubscribeChannel);  // ACK channel for receiving acknowledgments
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Will try again in 5 seconds");
    }
  }
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
  return value / (180 / 8);  // 180° divided by 8 intervals
}