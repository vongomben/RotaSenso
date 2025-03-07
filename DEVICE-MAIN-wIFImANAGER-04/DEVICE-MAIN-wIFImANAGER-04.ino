#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <SPIFFS.h>

#define TRIGGER_PIN D6

// Variables for configuration storage
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

WiFiManager wm;
WiFiManagerParameter *custom_broker, *publish_channel, *receive_channel; // MQTT broker & channels

void setup() {
    Serial.begin(115200);
    pinMode(TRIGGER_PIN, INPUT_PULLUP); // Use internal pull-up resistor

    // Start SPIFFS for storing broker & channels
    if (!SPIFFS.begin(true)) {
        Serial.println("[ERROR] SPIFFS initialization failed!");
    }

    // Generate WiFi SSID with a random number
    ssid = "RotaSenso" + String(random(1000, 9999));
    Serial.println("Generated SSID: " + ssid);

    // Load previous broker & channel selection from SPIFFS
    loadConfig();

    // Set custom page title
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

    // Create text input fields for MQTT channels
    publish_channel = new WiFiManagerParameter("mqtt_publish", "MQTT Publish Channel", mqtt_publish_channel.c_str(), 40);
    receive_channel = new WiFiManagerParameter("mqtt_receive", "MQTT Receive Channel", mqtt_receive_channel.c_str(), 40);

    wm.addParameter(publish_channel);
    wm.addParameter(receive_channel);

    // Modify button text and move MQTT settings under "Configure WiFi"
    wm.setCustomHeadElement("<style>.btn.wm-btn-save { background-color: #4CAF50; color: white; }</style>");
    wm.setSaveConfigCallback([]() {
        Serial.println("WiFi Configuration Saved!");
    });

    // Configure WiFiManager settings menu
    std::vector<const char *> menu = {"wifi", "info", "restart", "exit"}; // "param" removed since settings are now inside "Configure WiFi"
    wm.setMenu(menu);

    // Change "Configure WiFi" button text to "Configure"
    wm.setCustomMenuHTML("<script>document.addEventListener('DOMContentLoaded', function() { document.querySelector('.btn.wm-btn-save').innerText = 'Configure'; });</script>");

    // Start WiFiManager with auto-connect
    bool res = wm.autoConnect(ssid.c_str(), password);

    if (!res) {
        Serial.println("Failed to connect");
    } else {
        Serial.println("Connected to WiFi!");
        saveConfig(); // Save broker & channels
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

// Function to save MQTT broker & channels to SPIFFS
void saveConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to save MQTT configuration.");
        return;
    }

    // Retrieve broker selection from WiFiManager
    if (wm.server->hasArg("mqtt_broker")) {
        mqtt_broker = wm.server->arg("mqtt_broker");
    }
    if (wm.server->hasArg("mqtt_publish")) {
        mqtt_publish_channel = wm.server->arg("mqtt_publish");
    }
    if (wm.server->hasArg("mqtt_receive")) {
        mqtt_receive_channel = wm.server->arg("mqtt_receive");
    }
    
    file.println(mqtt_broker);
    file.println(mqtt_publish_channel);
    file.println(mqtt_receive_channel);
    file.close();

    Serial.println("[Saved MQTT Broker: " + mqtt_broker + "]");
    Serial.println("[Saved Publish Channel: " + mqtt_publish_channel + "]");
    Serial.println("[Saved Receive Channel: " + mqtt_receive_channel + "]");
}

// Function to load the last selected MQTT broker & channels from SPIFFS
void loadConfig() {
    File file = SPIFFS.open("/mqtt_config.txt", FILE_READ);
    if (!file) {
        Serial.println("[No previous MQTT configuration found]");
        return;
    }
    mqtt_broker = file.readStringUntil('\n');
    mqtt_publish_channel = file.readStringUntil('\n');
    mqtt_receive_channel = file.readStringUntil('\n');

    mqtt_broker.trim();
    mqtt_publish_channel.trim();
    mqtt_receive_channel.trim();
    
    file.close();

    Serial.println("[Loaded MQTT Broker: " + mqtt_broker + "]");
    Serial.println("[Loaded Publish Channel: " + mqtt_publish_channel + "]");
    Serial.println("[Loaded Receive Channel: " + mqtt_receive_channel + "]");
}
