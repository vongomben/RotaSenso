// 2025 Gomba San Valentine's Special RotaSenso project
// Calibrating the sheet: pot movement to servo movements
//
//
/**
 * WiFiManager advanced demo, contains advanced configuration options
 * Implements TRIGGER_PIN button press, press for ondemand configportal, hold for 3 seconds for reset settings.
 */
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#define TRIGGER_PIN D6

// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = false; // change to true to use non blocking

WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field; // global param (for non blocking w params)
WiFiManagerParameter mqtt_broker_param; // Parameter for MQTT broker selection
WiFiManagerParameter mqtt_channel_send_param; // Parameter for MQTT send channel
WiFiManagerParameter mqtt_channel_receive_param; // Parameter for MQTT receive channel

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// MQTT broker options
const char* mqtt_broker_options[] = {
  "public-mqtt-broker.bevywise.com",
  "broker.hivemq.com",
  "broker.emqx.io",
  "test.mosquitto.org"
};

// Default broker (will be updated based on user selection)
const char* mqtt_server = mqtt_broker_options[0];  // Changed from char* to const char*

// Default MQTT channels
char mqtt_channel_send[30] = "test/SV02";       // Channel to publish messages to
char mqtt_channel_receive[30] = "test/SV01";    // Channel to receive messages from
char mqtt_channel_ack_send[30] = "test/SV02_ACK";   // Channel to send ACKs to
char mqtt_channel_ack_receive[30] = "test/SV01_ACK"; // Channel to receive ACKs from

// Connection tracking
unsigned long lastConnectionAttempt = 0;
const unsigned long reconnectInterval = 5000;  // 5 seconds between connection attempts

// WiFi and MQTT client creation
WiFiClient espClient;
PubSubClient client(espClient);

// Interval for sending MQTT messages (we'll send based on the pot position though)
unsigned long lastMsgTime = 0;
const long msgInterval = 5000;

// Handshake management
unsigned long messageTimestamp = 0;
unsigned long ackTimeout = 3000;  // 3 seconds timeout for ACK
bool waitingForAck = false;
String lastMessageSent = "";
int messageRetries = 0;
const int maxRetries = 3;

// Servo and Potentiometer Management
static const int servoPin = 1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                // Count how many readings are stable
const int stabilityThreshold = 10;  // Number of stable readings before updating

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// Forward declarations of functions (this is important!)
String getParam(String name);
void saveParamCallback();
void reconnect();
void checkButton();
int mapTo8Intervals(int value);

// Function for handling received MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Messaggio ricevuto su topic: ");
  Serial.println(topic);

  // Convert the payload in a string
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Contenuto: " + message);

  // Check if this is an ACK message
  if (String(topic) == mqtt_channel_ack_receive) {
    if (waitingForAck && message == lastMessageSent) {
      Serial.println("ACK received for message: " + message);
      waitingForAck = false;
      messageRetries = 0;
    }
    return;
  }

  // If the message arrives at our receive channel, check the value and send ACK
  if (String(topic) == mqtt_channel_receive) {
    int index = message.toInt();  // convert message to number
    Serial.println(index);
    // Checks that the index is valid (between 0 and 7)
    if (index >= 0 && index < 8) {
      float servoPos = medianValues[index];
      servo1.write(servoPos);  // Moves the servo to the required position
      Serial.print("Moved the servo to: ");
      Serial.println(servoPos);

      // Send acknowledgment
      client.publish(mqtt_channel_ack_send, String(index).c_str(), true);
      Serial.println("Sent ACK for message: " + String(index));
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

String getParam(String name) {
  //read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback() {
  Serial.println("[CALLBACK] saveParamCallback fired");
  
  // Get MQTT broker selection
  String broker_selection = getParam("mqtt_broker");
  if (broker_selection.length() > 0) {
    int index = broker_selection.toInt();
    if (index >= 0 && index < 4) {
      // Fix for the type conversion error - use const_cast to safely convert const char* to char*
      mqtt_server = mqtt_broker_options[index];
      Serial.println("Selected MQTT broker: " + String(mqtt_server));
    }
  }
  
  // Get MQTT channel values
  String send_channel = getParam("mqtt_send");
  if (send_channel.length() > 0) {
    strcpy(mqtt_channel_send, send_channel.c_str());
    // Update ACK channel based on send channel
    strcpy(mqtt_channel_ack_send, (send_channel + "_ACK").c_str());
    Serial.println("MQTT Send Channel: " + send_channel);
    Serial.println("MQTT Send ACK Channel: " + String(mqtt_channel_ack_send));
  }
  
  String receive_channel = getParam("mqtt_receive");
  if (receive_channel.length() > 0) {
    strcpy(mqtt_channel_receive, receive_channel.c_str());
    // Update ACK channel based on receive channel
    strcpy(mqtt_channel_ack_receive, (receive_channel + "_ACK").c_str());
    Serial.println("MQTT Receive Channel: " + receive_channel);
    Serial.println("MQTT Receive ACK Channel: " + String(mqtt_channel_ack_receive));
  }
  
  // Legacy custom field (can be removed if not needed)
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}

// Function for MQTT reconnection
void reconnect() {
  // Only attempt reconnection if enough time has passed since last attempt
  if (!client.connected() && (millis() - lastConnectionAttempt > reconnectInterval)) {
    lastConnectionAttempt = millis();
    Serial.print("Attempting MQTT connection to broker: ");
    Serial.print(mqtt_server);
    Serial.print("...");

    String clientID = "ESP32-" + String(random(1000, 9999));  // random client id

    if (client.connect(clientID.c_str())) {
      Serial.println("Connected to MQTT broker!");

      // Subscribe to topics
      client.subscribe(mqtt_channel_receive);      // subscribe to receive channel
      client.subscribe(mqtt_channel_send);         // subscribe to send channel (for monitoring)
      client.subscribe(mqtt_channel_ack_receive);  // subscribe to receive ACK channel
      client.subscribe(mqtt_channel_ack_send);     // subscribe to send ACK channel (for monitoring)
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Will try again in 5 seconds");
    }
  }
}

void checkButton() {
  // check for button press
  if (digitalRead(TRIGGER_PIN) == LOW) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if (digitalRead(TRIGGER_PIN) == LOW) {
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings, code not ideal for production
      delay(3000); // reset delay hold
      if (digitalRead(TRIGGER_PIN) == LOW) {
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);
      
      if (!wm.startConfigPortal("OnDemandAP", "password")) {
        Serial.println("failed to connect or hit timeout");
        delay(3000);
        // ESP.restart();
      } else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  servo1.attach(servoPin);  // Initialize the servo

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP  

  Serial.setDebugOutput(true);  
  delay(3000);
  Serial.println("\n Starting");

  pinMode(TRIGGER_PIN, INPUT);

  wm.resetSettings(); // wipe settings

  if (wm_nonblocking) wm.setConfigPortalBlocking(false);

  // add a custom input field
  int customFieldLength = 40;

  // Create MQTT broker selection radio buttons
  String mqtt_radio_str = "<br/><label for='mqtt_broker'><b>Select MQTT Broker:</b></label>";
  for (int i = 0; i < 4; i++) {
    mqtt_radio_str += "<br><input type='radio' name='mqtt_broker' value='" + String(i) + "'";
    if (i == 0) mqtt_radio_str += " checked";
    mqtt_radio_str += "> " + String(mqtt_broker_options[i]);
  }
  
  new (&mqtt_broker_param) WiFiManagerParameter(mqtt_radio_str.c_str()); // custom html input for broker selection
  
  // Create MQTT channel input fields
  new (&mqtt_channel_send_param) WiFiManagerParameter("mqtt_send", "<br/><b>MQTT Channel to Send Messages:</b>", mqtt_channel_send, 30);
  new (&mqtt_channel_receive_param) WiFiManagerParameter("mqtt_receive", "<br/><b>MQTT Channel to Receive Messages:</b>", mqtt_channel_receive, 30);
  
  // Legacy custom field (can be removed if not needed)
  const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input
  
  wm.addParameter(&mqtt_broker_param);
  wm.addParameter(&mqtt_channel_send_param);
  wm.addParameter(&mqtt_channel_receive_param);
  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  std::vector<const char *> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  wm.setConfigPortalTimeout(30); // auto close configportal after n seconds

  bool res;
  res = wm.autoConnect("Rotasenso", "password"); // password protected ap

  if (!res) {
    Serial.println("Failed to connect or hit timeout");
    // ESP.restart();
  } 
  else {
    // if you get here you have connected to the WiFi    
    Serial.println("connected...yeey :)");
  
    // Set up MQTT server and callback for receiving messages
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    // Initial connection to MQTT
    reconnect();
  }
}

void loop() {
  if (wm_nonblocking) wm.process();  // avoid delays() in loop when non-blocking and other long running code
  checkButton();

  // read pot
  int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 0, 180);
  //servo1.write(servoPosition);

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
        client.publish(mqtt_channel_send, lastMessageSent.c_str(), true);
        Serial.println("Sent message: " + lastMessageSent);

        waitingForAck = true;
        messageTimestamp = millis();
      }
    }
  }

  // Check for ACK timeout
  if (waitingForAck && millis() - messageTimestamp > ackTimeout) {
    // Timeout will be handled on next stable reading
  }

  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    // Try to reconnect using saved credentials
    WiFi.begin();
    
    // If not connected after 5 seconds, open the config portal
    delay(5000);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Failed to reconnect. Starting config portal...");
      wm.autoConnect("Rotasenso", "password");
    }
  }

  // Check and maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }

  // Keep MQTT active
  client.loop();
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
  return value / (180 / 8);  // 180° divided by 8 intervals
}