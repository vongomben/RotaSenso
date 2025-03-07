#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <SPIFFS.h>

#define TRIGGER_PIN D6

// Variables for configuration storage
String ssid;
const char* password = "password";
String mqtt_broker = "";

// Available MQTT Brokers
const char* mqtt_broker_options[] = {
  "public-mqtt-broker.bevywise.com",
  "broker.hivemq.com",
  "broker.emqx.io"
};

WiFiManager wm;
WiFiManagerParameter* custom_broker; // MQTT broker selection

void setup() {
    Serial.begin(115200);

    pinMode(TRIGGER_PIN, INPUT_PULLUP); // Use internal pull-up resistor

    // Start SPIFFS for storing broker selection
    if (!SPIFFS.begin(true)) {
        Serial.println("[ERROR] SPIFFS initialization failed!");
    }

    // Generate WiFi SSID with a random number
    ssid = "RotaSenso" + String(random(1000, 9999));
    Serial.println("Generated SSID: " + ssid);

    // Load previous broker selection from SPIFFS
    loadConfig();

    // Create MQTT broker selection HTML radio buttons
    String mqtt_radio_str = "<h3>Select MQTT Broker:</h3>";
    for (int i = 0; i < 3; i++) {
        mqtt_radio_str += "<input type='radio' name='mqtt_broker' value='" + String(mqtt_broker_options[i]) + "'";
        if (mqtt_broker == mqtt_broker_options[i]) mqtt_radio_str += " checked";
        mqtt_radio_str += "> " + String(mqtt_broker_options[i]) + "<br>";
    }

    // Create a WiFiManagerParameter for broker selection
    custom_broker = new WiFiManagerParameter(mqtt_radio_str.c_str());
    wm.addParameter(custom_broker);

    // Configure WiFiManager settings menu
    std::vector<const char *> menu = {"wifi", "param", "info", "restart", "exit"};
    wm.setMenu(menu);

    // Start WiFiManager with auto-connect
    bool res = wm.autoConnect(ssid.c_str(), password);

    if (!res) {
        Serial.println("Failed to connect");
    } else {
        Serial.println("Connected to WiFi!");
        saveConfig(); // Save selected broker
    }
}

void loop() {
    checkButton(); // Check for button press continuously
}

// Improved Non-Blocking Button Check
void checkButton(){
    static unsigned long buttonPressStart = 0;
    
    if (digitalRead(TRIGGER_PIN) == LOW) {
        delay(50); // Basic debounce
        if (digitalRead(TRIGGER_PIN) == LOW) {
            Serial.println("Button Pressed");
            
            // Start counting the hold time
            if (buttonPressStart == 0) {
                buttonPressStart = millis();
            }

            // If button is held for 3 seconds, reset settings
            if (millis() - buttonPressStart >= 3000) {
                Serial.println("Button Held for 3 sec - Erasing Config & Restarting");
                wm.resetSettings();
                ESP.restart();
            }
        }
    } else {
        buttonPressStart = 0; // Reset if button is released
    }
}

// Function to save the selected MQTT broker to SPIFFS
void saveConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to save MQTT broker.");
        return;
    }

    // Retrieve broker selection from WiFiManager
    if (wm.server->hasArg("mqtt_broker")) {
        mqtt_broker = wm.server->arg("mqtt_broker");
    }
    
    file.println(mqtt_broker);
    file.close();
    Serial.println("[Saved MQTT Broker: " + mqtt_broker + "]");
}

// Function to load the last selected MQTT broker from SPIFFS
void loadConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_READ);
    if (!file) {
        Serial.println("[No previous MQTT configuration found]");
        return;
    }
    mqtt_broker = file.readStringUntil('\n');
    mqtt_broker.trim();
    file.close();
    Serial.println("[Loaded MQTT Broker: " + mqtt_broker + "]");
}
