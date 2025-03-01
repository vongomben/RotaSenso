#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>

String ssid = "";
String password = "";
String mqtt_broker = "";
String mqtt_send_channel = "";
String mqtt_receive_channel = "";

void setup() {
    Serial.begin(115200);
    
    if (!SPIFFS.begin(true)) {
        Serial.println("Errore nella inizializzazione di SPIFFS");
        return;
    }

    loadConfig();
    
    Serial.println("Pronto per ricevere configurazioni via Web Serial.");
}

void loop() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');  
        input.trim();
        
        int space1 = input.indexOf(' ');
        int space2 = input.indexOf(' ', space1 + 1);
        int space3 = input.indexOf(' ', space2 + 1);
        int space4 = input.indexOf(' ', space3 + 1);

        if (space1 != -1 && space2 != -1 && space3 != -1 && space4 != -1) {
            ssid = input.substring(0, space1);
            password = input.substring(space1 + 1, space2);
            mqtt_broker = input.substring(space2 + 1, space3);
            mqtt_send_channel = input.substring(space3 + 1, space4);
            mqtt_receive_channel = input.substring(space4 + 1);

            saveConfig();
            Serial.println("Configurazioni salvate!");
        } else {
            Serial.println("Formato errato! Usa: SSID PASS BROKER SEND_CHANNEL RECV_CHANNEL");
        }
    }
}

void saveConfig() {
    File file = SPIFFS.open("/config.txt", FILE_WRITE);
    if (!file) {
        Serial.println("Errore nel salvataggio della configurazione");
        return;
    }
    file.println(ssid);
    file.println(password);
    file.println(mqtt_broker);
    file.println(mqtt_send_channel);
    file.println(mqtt_receive_channel);
    file.close();
}

void loadConfig() {
    File file = SPIFFS.open("/config.txt");
    if (!file) {
        Serial.println("Nessun file di configurazione trovato.");
        return;
    }
    ssid = file.readStringUntil('\n');
    password = file.readStringUntil('\n');
    mqtt_broker = file.readStringUntil('\n');
    mqtt_send_channel = file.readStringUntil('\n');
    mqtt_receive_channel = file.readStringUntil('\n');
    file.close();

    Serial.println("Configurazione caricata:");
    Serial.println("SSID: " + ssid);
    Serial.println("Broker: " + mqtt_broker);
}
