// 2025 Gomba San Valentine's Special RotaSenso project
// Calibrating the sheet: pot movement to servo movements
//
//
/**
 * WiFiManager advanced demo, contains advanced configurartion options
 * Implements TRIGGEN_PIN button press, press for ondemand configportal, hold for 3 seconds for reset settings.
 */
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#define TRIGGER_PIN D6


// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = false; // change to true to use non blocking

WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field; // global param ( for non blocking w params )

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// Credenziali WiFi
const char* ssid = "YourSSID";
const char* password = "Your-Wifi";

// MQTT broker - we use only one broker to ensure both devices can communicate
const char* mqtt_server = "public-mqtt-broker.bevywise.com";

// Alternative brokers for manual configuration if needed
// const char* alt_brokers[] = {
//   "broker.hivemq.com",
//   "broker.emqx.io",
//   "test.mosquitto.org"
// };

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
  if (String(topic) == "test/SV01_ACK") {
    if (waitingForAck && message == lastMessageSent) {
      Serial.println("ACK received for message: " + message);
      waitingForAck = false;
      messageRetries = 0;
    }
    return;
  }

  // If the message arrives at '/SV01', check the value received and send ACK
  // IMPORTANT: CHANGE '/SV01' to '/SV02' for the other device
  if (String(topic) == "test/SV01") {
    int index = message.toInt();  // convert message to number
    Serial.println(index);
    // Checks that the index is valid (between 0 and 7)
    if (index >= 0 && index < 8) {
      float servoPos = medianValues[index];
      servo1.write(servoPos);  // Moves the servo to the required position
      Serial.print("Moved the servo to: ");
      Serial.println(servoPos);

      // Send acknowledgment
      // IMPORTANT: CHANGE 'SV01_ACK' to 'SV02_ACK' for the other device
      client.publish("test/SV02_ACK", String(index).c_str(), true);
      Serial.println("Sent ACK for message: " + String(index));
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

void setup() {
  Serial.begin(115200);
  servo1.attach(servoPin);  //  Initialise the servo

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP  

  Serial.setDebugOutput(true);  
  delay(3000);
  Serial.println("\n Starting");

  pinMode(TRIGGER_PIN, INPUT);

  //wm.resetSettings(); // wipe settings

  if(wm_nonblocking) wm.setConfigPortalBlocking(false)

// add a custom input field
  int customFieldLength = 40;


  // new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\"");
  
  // test custom html input type(checkbox)
  // new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\" type=\"checkbox\""); // custom html type
  
  // test custom html(radio)
  const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input
  
  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  // 
  // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
  // const char* menu[] = {"wifi","info","param","sep","restart","exit"}; 
  // wm.setMenu(menu,6);
  std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");


  //set static ip
  // wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
  // wm.setShowStaticFields(true); // force show static ip fields
  // wm.setShowDnsFields(true);    // force show dns field always

  // wm.setConnectTimeout(20); // how long to try to connect for before continuing
  wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
  // wm.setCaptivePortalEnable(false); // disable captive portal redirection
  // wm.setAPClientCheck(true); // avoid timeout if client connected to softap

  // wifi scan settings
  // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
  // wm.setMinimumSignalQuality(20);  // set min RSSI (percentage) to show in scans, null = 8%
  // wm.setShowInfoErase(false);      // do not show erase button on info page
  // wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons
  
  // wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect("Rotasenso","password"); // password protected ap

  if(!res) {
    Serial.println("Failed to connect or hit timeout");
    // ESP.restart();
  } 
  else {
    //if you get here you have connected to the WiFi    
    Serial.println("connected...yeey :)");
  }


  //  WiFi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Set up MQTT server and callback for receiving messages
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Initial connection to MQTT
  reconnect();
}



void checkButton(){
  // check for button press
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if( digitalRead(TRIGGER_PIN) == LOW ){
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings, code not ideaa for production
      delay(3000); // reset delay hold
      if( digitalRead(TRIGGER_PIN) == LOW ){
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);
      
      if (!wm.startConfigPortal("OnDemandAP","password")) {
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


String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  Serial.println("[CALLBACK] saveParamCallback fired");
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}



void loop() {

  if (wm_nonblocking) wm.process();  // avoid delays() in loop when non-blocking and other long running code
  checkButton();
  // put your main code here, to run repeatedly:

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
        // IMPORTANT: CHANGE '/SV02' to '/SV01' for allowing the two devices to talk to each other
        client.publish("test/SV02", lastMessageSent.c_str(), true);
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

  // Mantieni la connessione WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.begin(ssid, password);
    delay(5000);
  }

  // Check and maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }

  // Keep MQTT active
  client.loop();
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
      client.subscribe("test/SV01");      // subscribe to /SV01
      client.subscribe("test/SV02");      // subscribe to /SV02
      client.subscribe("test/SV01_ACK");  // subscribe to /SV01_ACK
      client.subscribe("test/SV02_ACK");  // subscribe to /SV02_ACK
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