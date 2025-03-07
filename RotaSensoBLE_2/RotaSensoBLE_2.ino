#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <SPIFFS.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ESP32Ping.h> // Per i test di connettività

// Debug flag - set to true for enhanced debugging
#define DEBUG_MODE true
// Enable to see verbose WiFi diagnostics
#define WIFI_DEBUG true
// Enable to see verbose BLE diagnostics
#define BLE_DEBUG true
// Enable to see verbose MQTT diagnostics
#define MQTT_DEBUG true

// UUIDs per servizio e caratteristiche BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DEVICE_NAME_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a7"
#define WIFI_SSID_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define WIFI_PASS_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define MQTT_BROKER_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define MQTT_PUB_CHAN_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define MQTT_SUB_CHAN_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26ac"
#define CONFIG_STATUS_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26ad"

// Pin e configurazione hardware
#define TRIGGER_PIN D6
#define LED_PIN LED_BUILTIN
#define SERVO_PIN 1
#define POT_PIN A5

// Configurazione
String deviceName = "RotaSenso";
String ssid, password, mqtt_broker;
String mqtt_publish_channel, mqtt_receive_channel;
const char* default_mqtt_broker = "broker.hivemq.com";
const char* default_publish_channel = "test/SV02";
const char* default_receive_channel = "test/SV01";
float medianValues[] = {11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75};

// Variabili globali
Servo servo1;
WiFiClient espClient;
PubSubClient client(espClient);
bool wifiConnected = false;
bool mqttConnected = false;
bool bleConfigured = false;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool isPulsing = false;
int lastMappedValue = -1;
int stableCount = 0;
unsigned long pulseStartTime = 0;
const int stabilityThreshold = 10;
const int pulseDuration = 2000;
unsigned long lastWifiCheckTime = 0;
const int wifiCheckInterval = 5000; // Check WiFi status every 5 seconds

// Oggetti BLE
BLEServer *pServer = NULL;
BLEService *pService = NULL;
BLECharacteristic *pDeviceNameCharacteristic;
BLECharacteristic *pWifiSSIDCharacteristic;
BLECharacteristic *pWifiPassCharacteristic;
BLECharacteristic *pMQTTBrokerCharacteristic;
BLECharacteristic *pMQTTPubChannelCharacteristic;
BLECharacteristic *pMQTTSubChannelCharacteristic;
BLECharacteristic *pConfigStatusCharacteristic;

// Prototipi di funzione
void setupBLE();
void connectToWiFi();
bool testWiFiConnection();
bool testMQTTConnection();
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void checkButton();
void updateLED();
void startPulsing();
void blinkLED(int times, int duration);
int mapTo8Intervals(int value);
void saveConfig();
void loadConfig();
void applyConfig();
void sendStatusNotification(const String& message);

// Funzioni di debug migliorate
void DEBUG_LOG(const String& module, const String& message, bool forceOutput = false) {
  if (DEBUG_MODE || forceOutput) {
    String timestamp = String(millis());
    String logMessage = "[" + timestamp + "][" + module + "] " + message;
    Serial.println(logMessage);
  }
}

void WIFI_LOG(const String& message) {
  if (WIFI_DEBUG) {
    DEBUG_LOG("WiFi", message);
  }
}

void BLE_LOG(const String& message) {
  if (BLE_DEBUG) {
    DEBUG_LOG("BLE", message);
  }
}

void MQTT_LOG(const String& message) {
  if (MQTT_DEBUG) {
    DEBUG_LOG("MQTT", message);
  }
}

// Log dettaglio errore WiFi
String wifiStatusToString(int status) {
  switch (status) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN_STATUS(" + String(status) + ")";
  }
}

// Log dettaglio errore MQTT
String mqttStateToString(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "CONNECTION_TIMEOUT";
    case MQTT_CONNECTION_LOST: return "CONNECTION_LOST";
    case MQTT_CONNECT_FAILED: return "CONNECT_FAILED";
    case MQTT_DISCONNECTED: return "DISCONNECTED";
    case MQTT_CONNECTED: return "CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL: return "CONNECT_BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "CONNECT_BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE: return "CONNECT_UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "CONNECT_BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED: return "CONNECT_UNAUTHORIZED";
    default: return "UNKNOWN_STATE(" + String(state) + ")";
  }
}

// Funzione migliorata per inviare notifiche di stato
void sendStatusNotification(const String& message) {
  if (deviceConnected && pConfigStatusCharacteristic != NULL) {
    BLE_LOG("Sending notification: " + message);
    pConfigStatusCharacteristic->setValue(message.c_str());
    pConfigStatusCharacteristic->notify();
    delay(100); // Piccolo ritardo per garantire l'invio
  } else {
    DEBUG_LOG("STATUS", message + " (BLE non connesso)", true);
  }
}

// Callback server BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      BLE_LOG("*********************************************");
      BLE_LOG("DEVICE CONNECTED VIA BLE");
      BLE_LOG("*********************************************");
      digitalWrite(LED_PIN, LOW);
      
      // Invia un messaggio di stato all'app
      sendStatusNotification("Device connected. Ready for configuration.");
      
      // Invia subito lo stato attuale della connessione
      if (wifiConnected) {
        BLE_LOG("Notifying about existing WiFi connection");
        sendStatusNotification("WiFi: Already connected to " + ssid + ", IP: " + WiFi.localIP().toString());
        
        if (mqttConnected) {
          BLE_LOG("Notifying about existing MQTT connection");
          sendStatusNotification("MQTT: Already connected to " + mqtt_broker);
        }
      }
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      BLE_LOG("*********************************************");
      BLE_LOG("DEVICE DISCONNECTED FROM BLE");
      BLE_LOG("*********************************************");
      
      if (bleConfigured) {
        digitalWrite(LED_PIN, HIGH);
        delay(500);
        applyConfig();
        
        // Questo blocco non invierà più notifiche BLE perché la connessione è chiusa,
        // ma è utile per il debug seriale
        if (wifiConnected && mqttConnected) {
          DEBUG_LOG("STATUS", "Device fully configured and connected!", true);
          DEBUG_LOG("STATUS", "WiFi: Connected to " + ssid, true);
          DEBUG_LOG("STATUS", "MQTT: Connected to " + mqtt_broker, true);
          DEBUG_LOG("STATUS", "Publishing on: " + mqtt_publish_channel, true);
          DEBUG_LOG("STATUS", "Subscribed to: " + mqtt_receive_channel, true);
        } else if (wifiConnected) {
          DEBUG_LOG("STATUS", "WiFi connected but MQTT failed", true);
        } else {
          DEBUG_LOG("STATUS", "Failed to connect to WiFi and MQTT", true);
        }
      } else {
        DEBUG_LOG("STATUS", "BLE disconnected without complete configuration", true);
      }
    }
};

// Callbacks per caratteristiche BLE
class DeviceNameCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      deviceName = pCharacteristic->getValue().c_str();
      BLE_LOG("Received Device Name: " + deviceName);
    }
};

class WifiSSIDCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      ssid = pCharacteristic->getValue().c_str();
      BLE_LOG("Received WiFi SSID: " + ssid);
    }
};

class WifiPassCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      password = pCharacteristic->getValue().c_str();
      BLE_LOG("Received WiFi password, length: " + String(password.length()) + " chars");
    }
};

class MQTTBrokerCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      mqtt_broker = pCharacteristic->getValue().c_str();
      BLE_LOG("Received MQTT Broker: " + mqtt_broker);
    }
};

class MQTTPubChannelCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      mqtt_publish_channel = pCharacteristic->getValue().c_str();
      BLE_LOG("Received MQTT Publish Channel: " + mqtt_publish_channel);
    }
};

class MQTTSubChannelCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      mqtt_receive_channel = pCharacteristic->getValue().c_str();
      BLE_LOG("Received MQTT Subscribe Channel: " + mqtt_receive_channel);

      // Stampa lo stato attuale di tutte le variabili di configurazione
      BLE_LOG("*********************************************");
      BLE_LOG("CURRENT CONFIGURATION STATE:");
      BLE_LOG("Device Name: '" + deviceName + "' (Length: " + deviceName.length() + ")");
      BLE_LOG("SSID: '" + ssid + "' (Length: " + ssid.length() + ")");
      BLE_LOG("Password length: " + String(password.length()));
      BLE_LOG("MQTT Broker: '" + mqtt_broker + "' (Length: " + mqtt_broker.length() + ")");
      BLE_LOG("MQTT Publish: '" + mqtt_publish_channel + "' (Length: " + mqtt_publish_channel.length() + ")");
      BLE_LOG("MQTT Subscribe: '" + mqtt_receive_channel + "' (Length: " + mqtt_receive_channel.length() + ")");
      BLE_LOG("*********************************************");

      // Verifica se tutte le configurazioni sono presenti
      bool allConfigsPresent = (
        ssid.length() > 0 && 
        password.length() > 0 && 
        mqtt_broker.length() > 0 && 
        mqtt_publish_channel.length() > 0 && 
        mqtt_receive_channel.length() > 0
      );

      BLE_LOG("All configs present? " + String(allConfigsPresent ? "YES" : "NO"));

      if (allConfigsPresent) {
        BLE_LOG("CONFIGURATION COMPLETE - TESTING CONNECTIONS");
        bleConfigured = true;
        
        sendStatusNotification("Configuration received, testing connection...");
        
        // Test WiFi connection
        BLE_LOG("CALLING testWiFiConnection()");
        bool wifiSuccess = testWiFiConnection();
        
        bool mqttSuccess = false;
        if (wifiSuccess) {
          BLE_LOG("CALLING testMQTTConnection()");
          mqttSuccess = testMQTTConnection();
        }
        
        String statusMsg;
        if (wifiSuccess && mqttSuccess) {
          statusMsg = "All connections successful! Ready to use.";
        } else if (wifiSuccess) {
          statusMsg = "WiFi connected, but MQTT connection failed.";
        } else {
          statusMsg = "Failed to connect to WiFi. Check credentials.";
        }
        
        BLE_LOG("FINAL STATUS: " + statusMsg);
        sendStatusNotification(statusMsg);
      } else {
        BLE_LOG("WAITING FOR REMAINING CONFIGURATION PARAMETERS");
      }
    }
};

void setup() {
  Serial.begin(115200);
  
  // Attendi che il terminale seriale si stabilizzi
  delay(2000);
  
  DEBUG_LOG("SYSTEM", "*********************************************", true);
  DEBUG_LOG("SYSTEM", "RotaSenso Initializing... Version 1.2", true);
  DEBUG_LOG("SYSTEM", "DEBUG MODE: " + String(DEBUG_MODE ? "ON" : "OFF"), true);
  DEBUG_LOG("SYSTEM", "WIFI DEBUG: " + String(WIFI_DEBUG ? "ON" : "OFF"), true);
  DEBUG_LOG("SYSTEM", "BLE DEBUG: " + String(BLE_DEBUG ? "ON" : "OFF"), true);
  DEBUG_LOG("SYSTEM", "MQTT DEBUG: " + String(MQTT_DEBUG ? "ON" : "OFF"), true);
  DEBUG_LOG("SYSTEM", "*********************************************", true);
  
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  servo1.attach(SERVO_PIN);
  
  if (!SPIFFS.begin(true)) {
    DEBUG_LOG("SYSTEM", "Failed to mount SPIFFS filesystem", true);
  }

  loadConfig();

  if (mqtt_broker.length() == 0) mqtt_broker = default_mqtt_broker;
  if (mqtt_publish_channel.length() == 0) mqtt_publish_channel = default_publish_channel;
  if (mqtt_receive_channel.length() == 0) mqtt_receive_channel = default_receive_channel;

  DEBUG_LOG("SYSTEM", "ESP32 Chip information:", true);
  DEBUG_LOG("SYSTEM", "Model: " + String(ESP.getChipModel()), true);
  DEBUG_LOG("SYSTEM", "Revision: " + String(ESP.getChipRevision()), true);
  DEBUG_LOG("SYSTEM", "Cores: " + String(ESP.getChipCores()), true);
  DEBUG_LOG("SYSTEM", "CPU Freq: " + String(ESP.getCpuFreqMHz()) + " MHz", true);
  DEBUG_LOG("SYSTEM", "Free heap: " + String(ESP.getFreeHeap()) + " bytes", true);
  DEBUG_LOG("SYSTEM", "Flash size: " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB", true);
  DEBUG_LOG("SYSTEM", "MAC Address: " + WiFi.macAddress(), true);
  
  setupBLE();

  if (ssid.length() > 0 && password.length() > 0) {
    WIFI_LOG("Found saved WiFi credentials, attempting to connect...");
    connectToWiFi();
  } else {
    WIFI_LOG("No WiFi credentials found. Please configure via BLE.");
  }
}

void loop() {
  checkButton();
  updateLED();

  // Gestione connessione BLE
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  
  // Check WiFi status at regular intervals
  unsigned long currentMillis = millis();
  if (currentMillis - lastWifiCheckTime > wifiCheckInterval) {
    lastWifiCheckTime = currentMillis;
    
    // Controlla lo stato WiFi periodicamente
    if (wifiConnected && WiFi.status() != WL_CONNECTED) {
      WIFI_LOG("Connection lost, status: " + wifiStatusToString(WiFi.status()));
      WIFI_LOG("Attempting to reconnect...");
      connectToWiFi();
    } 
    else if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
      // Caso in cui il WiFi si è connesso ma la variabile di stato non è aggiornata
      WIFI_LOG("WiFi is connected but state variable is not updated! Fixing...");
      wifiConnected = true;
    }
    
    // Check signal strength periodically
    if (wifiConnected) {
      int rssi = WiFi.RSSI();
      String signalStrength;
      if (rssi > -50) signalStrength = "Excellent";
      else if (rssi > -60) signalStrength = "Very Good";
      else if (rssi > -70) signalStrength = "Good";
      else if (rssi > -80) signalStrength = "Fair";
      else signalStrength = "Weak";
      
      WIFI_LOG("Signal strength: " + String(rssi) + " dBm (" + signalStrength + ")");
    }
  }

  // Gestione potentiometro e MQTT
  if (wifiConnected) {
    // Solo se siamo connessi, procedi con il resto delle operazioni
    if (WiFi.status() == WL_CONNECTED) {
      int potValue = analogRead(POT_PIN);
      int mappedValue = mapTo8Intervals(potValue);
  
      if (mappedValue != lastMappedValue) {
        stableCount = 0;
        lastMappedValue = mappedValue;
      } else {
        stableCount++;
      }
  
      if (stableCount == stabilityThreshold) {
        MQTT_LOG("Sending value: " + String(mappedValue) + 
                 " to Broker: " + mqtt_broker + 
                 " on Channel: " + mqtt_publish_channel);
  
        if (client.connected()) {
          client.publish(mqtt_publish_channel.c_str(), String(mappedValue).c_str(), true);
          MQTT_LOG("Publish successful");
          startPulsing();
        } else {
          MQTT_LOG("Client disconnected, attempting to reconnect...");
          mqttConnected = false;
          reconnect();
        }
      }
  
      if (!client.connected()) {
        MQTT_LOG("MQTT connection state: " + mqttStateToString(client.state()));
        MQTT_LOG("Attempting to reconnect...");
        mqttConnected = false;
        reconnect();
      }
  
      client.loop();
    }
  }
}

bool testWiFiConnection() {
  WIFI_LOG("*********************************************");
  WIFI_LOG("Testing connection to SSID: " + ssid);
  
  // Print available networks to help diagnose SSID issues
  WIFI_LOG("Scanning for available networks...");
  int networksFound = WiFi.scanNetworks();
  WIFI_LOG("Found " + String(networksFound) + " networks");
  
  bool ssidFound = false;
  String networksStr = "";
  for (int i = 0; i < networksFound && i < 10; i++) {
    networksStr += "  " + String(i+1) + ". " + WiFi.SSID(i) + " (Signal: " + WiFi.RSSI(i) + " dBm)";
    if (i < networksFound-1) networksStr += "\n";
    
    if (WiFi.SSID(i) == ssid) {
      ssidFound = true;
      WIFI_LOG("Target SSID '" + ssid + "' found with signal strength: " + String(WiFi.RSSI(i)) + " dBm");
    }
  }
  WIFI_LOG("Networks:\n" + networksStr);
  
  if (!ssidFound) {
    WIFI_LOG("WARNING: Target SSID '" + ssid + "' not found in scan results!");
  }
  
  // Disconnect properly first
  WIFI_LOG("Disconnecting from previous connection...");
  WiFi.disconnect(true);
  delay(1000);
  
  // Set the hostname before connecting
  String hostname = "RotaSenso-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
  WiFi.setHostname(hostname.c_str());
  WIFI_LOG("Setting hostname to: " + hostname);
  
  // Start connection attempt
  WIFI_LOG("Beginning connection attempt...");
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // Log connection process
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    if (attempts % 5 == 0) {
      WIFI_LOG("Status: " + wifiStatusToString(WiFi.status()) + " (attempt " + String(attempts) + ")");
    }
    attempts++;
  }
  
  // Check final result
  if (WiFi.status() == WL_CONNECTED) {
    WIFI_LOG("TEST SUCCESS! Connected to IP: " + WiFi.localIP().toString());
    WIFI_LOG("Gateway IP: " + WiFi.gatewayIP().toString());
    WIFI_LOG("DNS IP: " + WiFi.dnsIP().toString());
    WIFI_LOG("Subnet mask: " + WiFi.subnetMask().toString());
    WIFI_LOG("Signal strength: " + String(WiFi.RSSI()) + " dBm");
    return true;
  } else {
    WIFI_LOG("TEST FAILED. Status: " + wifiStatusToString(WiFi.status()));
    WIFI_LOG("Common reasons for failure:");
    WIFI_LOG("- Incorrect password");
    WIFI_LOG("- SSID out of range or interference");
    WIFI_LOG("- Router rejecting connection (MAC filtering, etc)");
    return false;
  }
}

bool testMQTTConnection() {
  MQTT_LOG("*********************************************");
  MQTT_LOG("Testing connection to broker: " + mqtt_broker);
  
  // Test DNS resolution first
  MQTT_LOG("Resolving hostname...");
  IPAddress mqttIP;
  bool dnsResolved = WiFi.hostByName(mqtt_broker.c_str(), mqttIP);
  
  if (dnsResolved) {
    MQTT_LOG("Resolved broker IP: " + mqttIP.toString());
  } else {
    MQTT_LOG("ERROR: Failed to resolve broker hostname. DNS failure!");
    return false;
  }
  
  PubSubClient testClient(espClient);
  testClient.setServer(mqtt_broker.c_str(), 1883);
  
  int attempts = 0;
  bool connected = false;
  while (!connected && attempts < 3) {
    attempts++;
    MQTT_LOG("Attempt " + String(attempts) + " to connect to MQTT");
    
    String clientID = "ESP32Test-" + String(random(1000, 9999));
    MQTT_LOG("Using client ID: " + clientID);
    
    if (testClient.connect(clientID.c_str())) {
      MQTT_LOG("Connection successful!");
      connected = true;
    } else {
      int state = testClient.state();
      MQTT_LOG("Connection failed: " + mqttStateToString(state));
      MQTT_LOG("Trying again in 2 seconds");
      delay(2000);
    }
  }
  
  if (connected) {
    // Try to publish a test message
    bool publishSuccess = testClient.publish("rotasenso/test", "Test connection");
    if (publishSuccess) {
      MQTT_LOG("Test publish successful");
    } else {
      MQTT_LOG("Test publish failed");
    }
    
    testClient.disconnect();
    MQTT_LOG("Test connection successful and disconnected");
  } else {
    MQTT_LOG("Test connection failed after all attempts");
  }
  
  return connected;
}

void setupBLE() {
  // Use MAC address to make name unique and persistent between restarts
  String bleName = "RotaSenso-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
  BLE_LOG("*********************************************");
  BLE_LOG("Initializing with name: " + bleName);
  BLEDevice::init(bleName.c_str());
  
  // Set transmit power to maximum for better range
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
  
  BLE_LOG("Transmit power set to maximum");
  
  // Create server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  pService = pServer->createService(SERVICE_UUID);
  BLE_LOG("Created service with UUID: " + String(SERVICE_UUID));
  
  // Device Name
  pDeviceNameCharacteristic = pService->createCharacteristic(
                               DEVICE_NAME_UUID,
                               BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE);
  pDeviceNameCharacteristic->setCallbacks(new DeviceNameCallbacks());
  pDeviceNameCharacteristic->setValue(deviceName.c_str());
  pDeviceNameCharacteristic->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2901)));
  BLE_LOG("Added characteristic: Device Name");

  // WiFi SSID
  pWifiSSIDCharacteristic = pService->createCharacteristic(
                               WIFI_SSID_UUID,
                               BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE);
  pWifiSSIDCharacteristic->setCallbacks(new WifiSSIDCallbacks());
  pWifiSSIDCharacteristic->setValue(ssid.c_str());
  pWifiSSIDCharacteristic->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2901)));
  BLE_LOG("Added characteristic: WiFi SSID");

  // WiFi Password
  pWifiPassCharacteristic = pService->createCharacteristic(
                               WIFI_PASS_UUID,
                               BLECharacteristic::PROPERTY_WRITE);
  pWifiPassCharacteristic->setCallbacks(new WifiPassCallbacks());
  pWifiPassCharacteristic->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2901)));
  BLE_LOG("Added characteristic: WiFi Password");

  // MQTT Broker
  pMQTTBrokerCharacteristic = pService->createCharacteristic(
                               MQTT_BROKER_UUID,
                               BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE);
  pMQTTBrokerCharacteristic->setCallbacks(new MQTTBrokerCallbacks());
  pMQTTBrokerCharacteristic->setValue(mqtt_broker.c_str());
  pMQTTBrokerCharacteristic->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2901)));
  BLE_LOG("Added characteristic: MQTT Broker");

  // MQTT Publish Channel
  pMQTTPubChannelCharacteristic = pService->createCharacteristic(
                               MQTT_PUB_CHAN_UUID,
                               BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE);
  pMQTTPubChannelCharacteristic->setCallbacks(new MQTTPubChannelCallbacks());
  pMQTTPubChannelCharacteristic->setValue(mqtt_publish_channel.c_str());
  pMQTTPubChannelCharacteristic->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2901)));
  BLE_LOG("Added characteristic: MQTT Publish Channel");

  // MQTT Subscribe Channel
  pMQTTSubChannelCharacteristic = pService->createCharacteristic(
                               MQTT_SUB_CHAN_UUID,
                               BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_WRITE);
  pMQTTSubChannelCharacteristic->setCallbacks(new MQTTSubChannelCallbacks());
  pMQTTSubChannelCharacteristic->setValue(mqtt_receive_channel.c_str());
  pMQTTSubChannelCharacteristic->addDescriptor(new BLEDescriptor(BLEUUID((uint16_t)0x2901)));
  BLE_LOG("Added characteristic: MQTT Subscribe Channel");

  // Configuration Status
  pConfigStatusCharacteristic = pService->createCharacteristic(
                               CONFIG_STATUS_UUID,
                               BLECharacteristic::PROPERTY_READ |
                               BLECharacteristic::PROPERTY_NOTIFY);
  pConfigStatusCharacteristic->addDescriptor(new BLE2902());
  pConfigStatusCharacteristic->setValue("Waiting for configuration...");
  BLE_LOG("Added characteristic: Configuration Status (with notifications)");

  // Start service
  pService->start();
  BLE_LOG("Service started");
  
  // Configure advertising
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  
  BLEAdvertisementData advData;
  advData.setFlags(0x06); // BR_EDR_NOT_SUPPORTED | LE General Discoverable Mode
  advData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advData.setName(bleName.c_str());
  pAdvertising->setAdvertisementData(advData);
  
  // Start advertising
  BLEDevice::startAdvertising();
  
  BLE_LOG("Started advertising as '" + bleName + "'");
  BLE_LOG("Service UUID: " + String(SERVICE_UUID));
  BLE_LOG("*********************************************");
}

void connectToWiFi() {
  WIFI_LOG("*********************************************");
  WIFI_LOG("Connecting to network: " + ssid);
  
  // Set hostname before connecting
  String hostname = "RotaSenso-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));
  WiFi.setHostname(hostname.c_str());
  WIFI_LOG("Setting hostname to: " + hostname);
  
  // Set low power mode
  WiFi.setSleep(false);
  
  // Print current WiFi mode
  WIFI_LOG("WiFi mode: " + String(WiFi.getMode()));
  
  // Print MAC address (for troubleshooting MAC filtering)
  WIFI_LOG("MAC address: " + WiFi.macAddress());
  
  // Start connection
  WIFI_LOG("Beginning connection attempt...");
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // Log connection progress
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    if (attempts % 5 == 0) {
      WIFI_LOG("Status: " + wifiStatusToString(WiFi.status()) + " (attempt " + String(attempts) + ")");
    }
    attempts++;
    blinkLED(1, 100);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    WIFI_LOG("CONNECTION SUCCESSFUL!");
    WIFI_LOG("IP address: " + WiFi.localIP().toString());
    WIFI_LOG("Gateway IP: " + WiFi.gatewayIP().toString());
    WIFI_LOG("DNS server: " + WiFi.dnsIP().toString());
    WIFI_LOG("Signal strength: " + String(WiFi.RSSI()) + " dBm");
    WIFI_LOG("Channel: " + String(WiFi.channel()));
    wifiConnected = true;
    
    // Tenta di raggiungere il broker MQTT (verifica connettività)
    WIFI_LOG("Testing MQTT broker DNS resolution");
    IPAddress mqttIP;
    bool dnsResolved = WiFi.hostByName(mqtt_broker.c_str(), mqttIP);
    
    if (dnsResolved) {
      WIFI_LOG("Resolved MQTT broker IP: " + mqttIP.toString());
      
      // Ping test (optional)
      WIFI_LOG("Testing network connectivity to MQTT broker...");
      bool pingSuccess = Ping.ping(mqttIP);
      if (pingSuccess) {
        WIFI_LOG("Ping to MQTT broker successful! Avg time: " + String(Ping.averageTime()) + "ms");
      } else {
        WIFI_LOG("WARNING: Ping to MQTT broker failed. Port might be filtered.");
      }
    } else {
      WIFI_LOG("ERROR: Failed to resolve MQTT broker hostname: " + mqtt_broker);
      WIFI_LOG("Possible DNS issues or broker hostname typo");
    }
    
    // Setup MQTT after WiFi connected
    client.setServer(mqtt_broker.c_str(), 1883);
    client.setCallback(callback);
    reconnect();
    
    // Invia lo stato anche via BLE se la connessione è attiva
    if (deviceConnected) {
      sendStatusNotification("WiFi: Connected to " + ssid + ", IP: " + WiFi.localIP().toString());
    }
    
  } else {
    WIFI_LOG("CONNECTION FAILED! Final status: " + wifiStatusToString(WiFi.status()));
    WIFI_LOG("Common failure reasons:");
    WIFI_LOG("- Incorrect password");
    WIFI_LOG("- Network out of range or interference");
    WIFI_LOG("- MAC address filtering");
    WIFI_LOG("- AP at capacity or rejecting connections");
    wifiConnected = false;
    
    // Invia notifica di fallimento
    if (deviceConnected) {
      sendStatusNotification("WiFi: Connection failed - " + wifiStatusToString(WiFi.status()));
    }
  }
}

void applyConfig() {
  saveConfig();
  connectToWiFi();
  
  if (wifiConnected) {
    if (mqttConnected) {
      Serial.println("[CONFIG] WiFi and MQTT successfully connected!");
    } else {
      Serial.println("[CONFIG] WiFi connected but MQTT failed");
    }
  } else {
    Serial.println("[CONFIG] Failed to connect to WiFi");
  }
}

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
      
      if (millis() - buttonPressStart >= 5000) {
        Serial.println("****************************************");
        Serial.println("[RESET] Button held for 5+ seconds - Resetting configuration");
        Serial.println("****************************************");
        SPIFFS.remove("/config.txt");
        WiFi.disconnect(true);
        delay(1000);
        ESP.restart();
      }
    }
  } else {
    if (buttonWasPressed && buttonPressStart > 0 && millis() - buttonPressStart < 3000) {
      Serial.println("[INFO] Button pressed - Sending position 0");
      servo1.write(medianValues[0]);
      Serial.println("[Servo temporarily moved to position 0]");
      
      if (client.connected()) {
        client.publish(mqtt_publish_channel.c_str(), "0", true);
        startPulsing();
      }
      
      delay(1000);
      
      int potValue = analogRead(POT_PIN);
      int mappedValue = mapTo8Intervals(potValue);
      servo1.write(medianValues[mappedValue]);
    }
    
    buttonPressStart = 0;
    buttonWasPressed = false;
  }
}

void startPulsing() {
  isPulsing = true;
  pulseStartTime = millis();
}

void updateLED() {
  if (deviceConnected) {
    digitalWrite(LED_PIN, LOW);
    return;
  } else if (!wifiConnected || !mqttConnected) {
    digitalWrite(LED_PIN, LOW);
    return;
  }
  
  if (isPulsing) {
    if (millis() - pulseStartTime > 5000) {
      isPulsing = false;
      digitalWrite(LED_PIN, HIGH);
      return;
    }
    
    float progress = (millis() - pulseStartTime) % pulseDuration;
    float brightness = sin((progress / pulseDuration) * PI) * 127.5 + 127.5;
    
    if (brightness > 127) {
      digitalWrite(LED_PIN, LOW);
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, HIGH);
  }
}

void reconnect() {
  int attempts = 0;
  while (!client.connected() && attempts < 3) {
    attempts++;
    Serial.print("Attempting MQTT connection...");
    String clientID = "ESP32-" + String(random(1000, 9999));
    if (client.connect(clientID.c_str())) {
      Serial.println("\n****************************************");
      Serial.println("[MQTT] Connected to broker: " + mqtt_broker);
      Serial.println("****************************************");
      client.subscribe(mqtt_receive_channel.c_str());
      mqttConnected = true;
      
      // Invia notifica anche via BLE
      if (deviceConnected) {
        sendStatusNotification("MQTT: Connected to " + mqtt_broker);
      }
    } else {
      Serial.print("\n[ERROR] MQTT Connection Failed (");
      Serial.print(client.state());
      Serial.println("). Retrying...");
      mqttConnected = false;
      delay(2000);
      
      // Invia notifica di errore MQTT
      if (deviceConnected) {
        String errorMsg = "MQTT: Connection failed, rc=" + String(client.state());
        sendStatusNotification(errorMsg);
      }
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("[INFO] Received MQTT Message!");
  digitalWrite(LED_PIN, LOW);

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("[MQTT] Received on topic: ");
  Serial.print(topic);
  Serial.print(" - Message: ");
  Serial.println(message);

  int index = message.toInt();
  if (index >= 0 && index < 8) {
    float servoPos = medianValues[index];
    servo1.write(servoPos);
    Serial.print("[Servo Moved to: ");
    Serial.print(servoPos);
    Serial.println("]");
  }

  delay(500);
  digitalWrite(LED_PIN, HIGH);
}

void saveConfig() {
  File file = SPIFFS.open("/config.txt", FILE_WRITE);
  if (!file) {
    Serial.println("[ERROR] Failed to open config file for writing");
    return;
  }
  
  file.println(deviceName);
  file.println(ssid);
  file.println(password);
  file.println(mqtt_broker);
  file.println(mqtt_publish_channel);
  file.println(mqtt_receive_channel);
  
  file.close();
  Serial.println("[INFO] Configuration saved to SPIFFS");
}

void loadConfig() {
  File file = SPIFFS.open("/config.txt", FILE_READ);
  if (!file) {
    Serial.println("[INFO] No configuration file found");
    return;
  }

  deviceName = file.readStringUntil('\n'); deviceName.trim();
  ssid = file.readStringUntil('\n'); ssid.trim();
  password = file.readStringUntil('\n'); password.trim();
  mqtt_broker = file.readStringUntil('\n'); mqtt_broker.trim();
  mqtt_publish_channel = file.readStringUntil('\n'); mqtt_publish_channel.trim();
  mqtt_receive_channel = file.readStringUntil('\n'); mqtt_receive_channel.trim();
  
  file.close();
  
  Serial.println("****************************************");
  Serial.println("[CONFIG] Configuration loaded from SPIFFS");
  Serial.println("Device Name: " + deviceName);
  Serial.println("SSID: " + ssid);
  Serial.println("MQTT Broker: " + mqtt_broker);
  Serial.println("MQTT Publish Channel: " + mqtt_publish_channel);
  Serial.println("MQTT Subscribe Channel: " + mqtt_receive_channel);
  Serial.println("****************************************");
}

void blinkLED(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(duration);
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
  }
}

int mapTo8Intervals(int value) {
  return value / (4096 / 8);
}