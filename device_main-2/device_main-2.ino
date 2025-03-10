// 2025 Gomba San Valentine's Special RotaSenso project
// Calibrating the sheet: pot movement to servo movements

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiManager.h>

// MQTT broker
const char* mqtt_server = "xfa0fff6.ala.eu-central-1.emqxsl.com";  
const char* mqtt_username = "rotasenso01";
const char* mqtt_password = "rotasenso01";

// WiFi and MQTT client creation
WiFiClient espClient;
PubSubClient client(espClient);

// Servo and Potentiometer Management
static const int servoPin = 1;           // servo pin
static const int potentiometerPin = A5;  // pot pin

const int resetButtonPin = D6;  // the number of the pushbutton pin
const int ledPin = D7;          // the number of the LED pin

unsigned long buttonPressStartTime = 0;
const unsigned long resetPressTime = 5000;  // 5 secondi
bool resetButtonPressed = false;

Servo servo1;
int lastMappedValue = -1;           // Stores the last recorded value
int stableCount = 0;                // Count how many readings are stable
const int stabilityThreshold = 10;  // Number of stable readings before updating

// Servo 8 middle positions
float medianValues[] = { 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };

// MQTT communication topics
const char* outboundTopic = "test/SV02"; // Cambia a SV01 sull'altro dispositivo
const char* inboundTopic = "test/ACK02"; // Cambia a ACK01 sull'altro dispositivo

// Variabili per il sistema ACK
unsigned long lastSentTime = 0;
bool waitingForAck = false;
const unsigned long ackTimeout = 2000; // Timeout di 2 secondi per l'ACK
int lastSentValue = -1;

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

  // Se il messaggio è un ACK
  if (String(topic) == inboundTopic) {
    int ackValue = message.toInt();
    if (ackValue == lastSentValue) {
      waitingForAck = false;
      Serial.print("ACK ricevuto per il valore: ");
      Serial.println(ackValue);
    }
    return;
  }

  // Se il messaggio è sul topic di controllo del servo
  if (String(topic) == "test/SV01") {
    int index = message.toInt();  // convert message to number
    Serial.println(index);
    // Checks that the index is valid (between 0 and 7)
    if (index >= 0 && index < 8) {
      float servoPos = medianValues[index];
      servo1.write(servoPos);  // Moves the servo to the required position
      Serial.print("Moved the servo to: ");
      Serial.println(servoPos);
      
      // Invia ACK per confermare la ricezione
      client.publish("test/ACK01", message.c_str(), true);
      Serial.print("ACK inviato per il valore: ");
      Serial.println(index);
    } else {
      Serial.println("Error: Value out of range");
    }
  }
}

void setup() {
  Serial.begin(115200);
  servo1.attach(servoPin);  // Initialise the servo
  
  // Inizializza il pin LED
  pinMode(ledPin, OUTPUT);
  
  // Configura il pin del pulsante come input con resistenza pull-up
  pinMode(resetButtonPin, INPUT_PULLUP);
  
  // Reset WiFi mode first
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  
  // Disable power saving mode
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  // Generate a random number between 1000-9999 for the AP name
  randomSeed(analogRead(A0));
  String apName = "rotasenso-" + String(random(1000, 9999));
  
  // WiFiManager
  WiFiManager wifiManager;
  
  // Imposta un timeout di 180 secondi
  wifiManager.setTimeout(180);
  
  Serial.println("Creating access point named: " + apName);
  
  // Tenta di connettersi con le credenziali salvate
  // Se fallisce, apre un access point con il nome generato
  if(!wifiManager.autoConnect(apName.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    // Riavvia e prova di nuovo
    ESP.restart();
  }
  
  // Verifica lo stato della connessione
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Set up MQTT server and callback for receiving messages
    client.setServer(mqtt_server, 8883);
    client.setCallback(callback);

    // Initial connection to MQTT
    reconnect();
  } else {
    Serial.println("WiFi connection issue detected even after autoConnect success");
    delay(3000);
    ESP.restart();
  }
}

void loop() {
  // Gestione del pulsante di reset
  if (digitalRead(resetButtonPin) == LOW) {  // Pulsante premuto (LOW con pull-up)
    if (!resetButtonPressed) {
      resetButtonPressed = true;
      buttonPressStartTime = millis();
      digitalWrite(ledPin, HIGH); // Accendi il LED per indicare che il pulsante è premuto
      Serial.println("Reset button pressed, holding for 5 seconds will reset WiFi settings...");
    } else {
      // Controlla se il pulsante è stato tenuto premuto abbastanza a lungo
      if (millis() - buttonPressStartTime >= resetPressTime) {
        Serial.println("Resetting WiFi settings and restarting...");
        
        // Lampeggia velocemente il LED per indicare il reset
        for (int i = 0; i < 10; i++) {
          digitalWrite(ledPin, !digitalRead(ledPin));
          delay(100);
        }
        
        // Resetta le impostazioni WiFi utilizzando WiFiManager
        WiFiManager wifiManager;
        wifiManager.resetSettings();
        
        // Attendi un momento e poi riavvia
        delay(1000);
        ESP.restart();
      }
    }
  } else {
    // Il pulsante è stato rilasciato prima dei 5 secondi
    if (resetButtonPressed) {
      resetButtonPressed = false;
      digitalWrite(ledPin, LOW); // Spegni il LED
      Serial.println("Reset button released before timeout");
    }
  }

  // read pot
  int servoPosition = map(analogRead(potentiometerPin), 0, 4096, 180, 0);
  
  // Get the value from 0 to 7
  int mappedValue = mapTo8Intervals(servoPosition);

  // Se il valore è cambiato, reset del contatore
  if (mappedValue != lastMappedValue) {
    stableCount = 0;
    lastMappedValue = mappedValue;
  } else {
    stableCount++;
  }
  
  // Verifica se dobbiamo inviare un nuovo messaggio
  unsigned long currentTime = millis();
  
  // Controllo timeout ACK: se stiamo aspettando un ACK ma è passato troppo tempo, 
  // reimpostiamo il flag per permettere un nuovo invio
  if (waitingForAck && (currentTime - lastSentTime > ackTimeout)) {
    Serial.println("ACK timeout, permettendo un nuovo invio");
    waitingForAck = false;
  }
  
  // Invia il messaggio solo se:
  // 1. Il valore è stabile per un certo numero di cicli
  // 2. Non stiamo già aspettando un ACK da un invio precedente
  // 3. Il valore da inviare è diverso dall'ultimo inviato con successo
  if (stableCount == stabilityThreshold && !waitingForAck && mappedValue != lastSentValue) {
    Serial.print("Servo Position: ");
    Serial.print(servoPosition);
    Serial.print(" -> Mapped Value: ");
    Serial.println(mappedValue);

    // Invia il messaggio una sola volta e attendi l'ACK
    client.publish(outboundTopic, String(mappedValue).c_str(), true);
    lastSentTime = currentTime;
    lastSentValue = mappedValue;
    waitingForAck = true;
    
    digitalWrite(ledPin, HIGH); // Accendi il LED quando viene inviato un messaggio
    Serial.print("Messaggio inviato, attesa di ACK per il valore: ");
    Serial.println(mappedValue);
  }

  // Spegni il LED se abbiamo ricevuto l'ACK
  if (!waitingForAck) {
    digitalWrite(ledPin, LOW);
  }

  // Mantieni la connessione WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.begin(); // Usa le credenziali salvate da WiFiManager
    delay(5000);
  }

  // Maintain MQTT connection
  if (!client.connected()) {
    reconnect();
  }

  // Keep MQTT active
  client.loop();
}

// Function for MQTT reconnection
void reconnect() {
  int retry_count = 0;
  const int max_retries = 5;
  
  while (!client.connected() && retry_count < max_retries) {
    Serial.print("Attempting MQTT connection...");

    String clientID = "rotasenso";
    // Tenta di connettersi con username e password
    if (client.connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Connected to MQTT broker");
      client.subscribe("test/SV01");
      client.subscribe("test/SV02");
      client.subscribe(inboundTopic);
    } else {
      retry_count++;
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.print(" Retry ");
      Serial.print(retry_count);
      Serial.print("/");
      Serial.print(max_retries);
      Serial.println("...");
      delay(2000);
    }
  }
  
  if (!client.connected()) {
    Serial.println("Failed to connect to MQTT broker after multiple attempts");
    Serial.println("Will try again in the next loop iteration");
  }
}

// Function that maps the servo value in 8 intervals from 0 to 7
int mapTo8Intervals(int value) {
  return value / (180 / 8);  // 180° divided by 8 intervals
}