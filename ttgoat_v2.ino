#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <string.h>
#include <stdlib.h>

// Définition des pins
#define LED_PIN 12
#define LIGHT_SENSOR_PIN  36  // Broche GPIO36 (ADC0) pour le capteur de lumière
#define ANALOG_THRESHOLD  500 // Seuil pour la LED automatique
const int buzzerPin = 13;     // Pin du buzzer

// Variables pour le mode AP
const char* apSSID = "ESP32_AP";
const char* apPassword = "12345678";

// Création du serveur web
WebServer server(80);

// Variables pour stocker le choix du mode
String connectionMode = "";

// Variables pour le mode STA
String staSSID = "";
String staPassword = "";

// Variables pour le BLE
BLEServer *pBLEServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;

// UUID pour le service BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Variables pour le capteur de lumière et la LED automatique
bool ledAutoMode = false;     // Mode LED automatique activé/désactivé
bool isPhotoresistorPageActive = false; // Flag pour savoir si la page du capteur est active

// Prototypes des fonctions
void startBLEServer();
void stopBLEServer();
void connectToWiFiSTA();
void handleRoot();
void handleModeSelection();

// Fonctions supplémentaires
void handleGPIOControl();
void handlePlaySong();
void playSong(char* notes, int* beats, int songLength, int tempo);
int frequency(char note);
void updatePhotoresistor();
void handleGetPhotoresistorValue();
void handlePhotoresistorExit();
void handleToggleLedAuto();
void handleGetLedAutoState();
void handlePhotoresistorPage();

// Classe de rappel pour le BLE
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();

    if (value.length() > 0) {
      Serial.println("*********");
      Serial.print("Valeur reçue: ");
      for (size_t i = 0; i < value.length(); i++) {
        Serial.print(value[i]);

        if (value[i] == '1') {
          digitalWrite(LED_PIN, HIGH);
        }
        if (value[i] == '0') {
          digitalWrite(LED_PIN, LOW);
        }
      }
      Serial.println();
      Serial.println("*********");
    }
  }
};

// Page HTML stockée en mémoire flash pour économiser la RAM
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Sélection du mode de connexion</title></head><body>
<h2>Sélectionnez le mode de connexion</h2>
<form action='/select_mode' method='POST'>
<input type='radio' name='mode' value='AP' checked> Mode AP<br>
<input type='radio' name='mode' value='STA'> Mode STA<br>
SSID: <input type='text' name='ssid'><br>
Mot de passe: <input type='password' name='password'><br>
<input type='radio' name='mode' value='BLE'> Mode BLE<br><br>
<input type='submit' value='Valider'>
</form></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Éteindre la LED au démarrage

  pinMode(LIGHT_SENSOR_PIN, INPUT);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Démarrer en mode AP
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(apSSID, apPassword);
  Serial.println("ESP32 démarré en mode AP");
  Serial.print("SSID: ");
  Serial.println(apSSID);
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.softAPIP());

  // Configurer les routes du serveur web
  server.on("/", handleRoot);
  server.on("/select_mode", HTTP_POST, handleModeSelection);

  // Routes pour les nouvelles fonctionnalités
  server.on("/control_gpio", handleGPIOControl);
  server.on("/play_song", handlePlaySong);
  server.on("/photoresistor", handlePhotoresistorPage);  // Page pour afficher la luminosité
  server.on("/get_photoresistor", handleGetPhotoresistorValue); // Obtenir les valeurs du capteur
  server.on("/exit_photoresistor", handlePhotoresistorExit);  // Route pour désactiver la lecture
  server.on("/toggle_led_auto", handleToggleLedAuto);
  server.on("/get_led_auto_state", handleGetLedAutoState);

  server.onNotFound([](){
    server.send(404, "text/plain", "Not Found");
  });

  // Démarrer le serveur web
  server.begin();
  Serial.println("Serveur web démarré");
}

void loop() {
  server.handleClient(); // Traiter les requêtes entrantes

  // Si le mode de connexion a été défini, basculer vers ce mode
  if (connectionMode == "STA") {
    Serial.println("Bascule vers le mode STA");
    server.close();
    WiFi.softAPdisconnect(true);
    connectToWiFiSTA();
    connectionMode = ""; // Réinitialiser pour éviter de reboucler
  } else if (connectionMode == "BLE") {
    Serial.println("Bascule vers le mode BLE");
    server.close();
    WiFi.softAPdisconnect(true);
    startBLEServer();
    connectionMode = ""; // Réinitialiser pour éviter de reboucler
  }

  // Mettre à jour le capteur de lumière si la page est active
  if (isPhotoresistorPageActive) {
    updatePhotoresistor();   // Lire le capteur et gérer la LED
  }

  delay(100); // Petit délai pour éviter de monopoliser le CPU
}

// Fonction pour gérer la page d'accueil
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

// Fonction pour gérer la sélection du mode
void handleModeSelection() {
  if (server.hasArg("mode")) {
    connectionMode = server.arg("mode");
    Serial.print("Mode sélectionné: ");
    Serial.println(connectionMode);

    if (connectionMode == "STA") {
      if (server.hasArg("ssid") && server.hasArg("password")) {
        staSSID = server.arg("ssid");
        staPassword = server.arg("password");
        server.send(200, "text/plain", "Bascule vers le mode STA...");
      } else {
        server.send(400, "text/plain", "SSID et mot de passe requis pour le mode STA");
        connectionMode = ""; // Réinitialiser le mode car les infos sont manquantes
      }
    } else if (connectionMode == "BLE") {
      server.send(200, "text/plain", "Bascule vers le mode BLE...");
    } else if (connectionMode == "AP") {
      server.send(200, "text/plain", "Vous êtes déjà en mode AP");
      connectionMode = ""; // Pas besoin de changer de mode
    } else {
      server.send(400, "text/plain", "Mode inconnu");
      connectionMode = ""; // Réinitialiser le mode car invalide
    }
  } else {
    server.send(400, "text/plain", "Aucun mode sélectionné");
  }
}

// Fonction pour se connecter en mode STA
void connectToWiFiSTA() {
  Serial.print("Connexion au réseau Wi-Fi: ");
  Serial.println(staSSID);
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(staSSID.c_str(), staPassword.c_str());

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnecté en mode STA");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
    // Démarrer le serveur web en mode STA si nécessaire
    server.begin();
    Serial.println("Serveur web démarré en mode STA");
  } else {
    Serial.println("\nImpossible de se connecter au réseau Wi-Fi");
    // Revenir en mode AP pour réessayer
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(apSSID, apPassword);
    server.begin();
    Serial.println("Revenu en mode AP");
  }
}

// Fonctions pour le BLE
void startBLEServer() {
  BLEDevice::init("ESP32_BLE");
  pBLEServer = BLEDevice::createServer();

  BLEService *pService = pBLEServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("Hello BLE");
  pService->start();

  BLEAdvertising *pAdvertising = pBLEServer->getAdvertising();
  pAdvertising->start();

  Serial.println("Serveur BLE démarré");
}

void stopBLEServer() {
  if (pBLEServer != nullptr) {
    pBLEServer->getAdvertising()->stop();
    pBLEServer->removeService(pBLEServer->getServiceByUUID(SERVICE_UUID));
    BLEDevice::deinit(true);
    Serial.println("Serveur BLE arrêté");
  }
}

// Fonction pour gérer les requêtes de contrôle GPIO
void handleGPIOControl() {
  Serial.println("Requête reçue pour /control_gpio");

  if (server.hasArg("pin") && server.hasArg("state")) {
    int pin = server.arg("pin").toInt();
    String state = server.arg("state");

    Serial.printf("Paramètres reçus - pin: %d, state: %s\n", pin, state.c_str());

    // Valider le numéro de pin (ajustez selon vos besoins)
    if (pin >= 0 && pin <= 39) {
      pinMode(pin, OUTPUT); // S'assurer que la broche est configurée en sortie

      if (state == "HIGH") {
        digitalWrite(pin, HIGH);
        server.send(200, "text/plain", "Pin " + String(pin) + " mise à HIGH");
        Serial.printf("Pin %d mise à HIGH\n", pin);
      } else if (state == "LOW") {
        digitalWrite(pin, LOW);
        server.send(200, "text/plain", "Pin " + String(pin) + " mise à LOW");
        Serial.printf("Pin %d mise à LOW\n", pin);
      } else {
        server.send(400, "text/plain", "État invalide. Utilisez 'HIGH' ou 'LOW'.");
        Serial.println("État invalide reçu");
      }
    } else {
      server.send(400, "text/plain", "Numéro de pin invalide.");
      Serial.println("Numéro de pin invalide");
    }
  } else {
    server.send(400, "text/plain", "Paramètres 'pin' ou 'state' manquants.");
    Serial.println("Paramètres 'pin' ou 'state' manquants");
  }
}

// Fonction pour gérer les requêtes de lecture du buzzer
void handlePlaySong() {
  Serial.println("Requête reçue pour /play_song");

  if (server.hasArg("notes") && server.hasArg("beats") && server.hasArg("tempo")) {
    String notesStr = server.arg("notes");
    String beatsStr = server.arg("beats");
    int tempo = server.arg("tempo").toInt();

    Serial.printf("Paramètres reçus - notes: %s, beats: %s, tempo: %d\n", notesStr.c_str(), beatsStr.c_str(), tempo);

    // Convertir les chaînes en tableaux
    int songLength = notesStr.length();
    char notes[songLength + 1];
    notesStr.toCharArray(notes, songLength + 1);

    // Compter le nombre de notes (caractères non espaces)
    int numNotes = 0;
    for (int i = 0; i < songLength; i++) {
      if (notes[i] != ' ') {
        numNotes++;
      }
    }

    // Extraire les beats (supposons qu'ils sont séparés par des virgules)
    int beatsArray[numNotes];
    int beatIndex = 0;
    char* beatsCStr = strdup(beatsStr.c_str());
    char* token = strtok(beatsCStr, ",");
    while (token != NULL && beatIndex < numNotes) {
      beatsArray[beatIndex++] = atoi(token);
      token = strtok(NULL, ",");
    }
    free(beatsCStr);

    // Vérifier que le nombre de beats correspond au nombre de notes
    if (beatIndex != numNotes) {
      server.send(400, "text/plain", "Le nombre de beats doit correspondre au nombre de notes (hors espaces).");
      Serial.println("Nombre de beats incorrect");
      return;
    }

    // Jouer la chanson
    playSong(notes, beatsArray, songLength, tempo);

    server.send(200, "text/plain", "Chanson jouée avec succès");
  } else {
    server.send(400, "text/plain", "Paramètres 'notes', 'beats' ou 'tempo' manquants.");
    Serial.println("Paramètres 'notes', 'beats' ou 'tempo' manquants");
  }
}

// Fonction pour jouer la chanson
void playSong(char* notes, int* beats, int songLength, int tempo) {
  int duration;
  int beatIndex = 0; // Nouvel index pour le tableau beats

  for (int i = 0; i < songLength; i++) {
    if (notes[i] == ' ') {        // Si c'est un silence
      duration = tempo; // Durée du silence (ajustez si nécessaire)
      delay(duration);
    } else {
      duration = beats[beatIndex++] * tempo;  // Durée de la note
      int freq = frequency(notes[i]);
      if (freq > 0) {
        tone(buzzerPin, freq, duration);
        delay(duration);
        noTone(buzzerPin); // Arrêter le son
      } else {
        Serial.printf("Note invalide: %c\n", notes[i]);
      }
    }
    // Petite pause entre les notes (optionnel)
    // delay(tempo / 10);
  }
}

// Fonction pour convertir une note en fréquence
int frequency(char note) {
  // Cette fonction prend une note (a-g) et renvoie la fréquence correspondante en Hz
  const int numNotes = 8;
  char names[] = { 'c', 'd', 'e', 'f', 'g', 'a', 'b', 'C' };
  int frequencies[] = {262, 294, 330, 349, 392, 440, 494, 523};

  for (int i = 0; i < numNotes; i++) {
    if (names[i] == note) {
      return frequencies[i];
    }
  }
  return 0; // Note non trouvée
}

// Fonction pour mettre à jour le capteur de lumière et gérer la LED automatique
void updatePhotoresistor() {
  int analogValue = analogRead(LIGHT_SENSOR_PIN); // Lire la valeur du capteur
  int percentage = map(analogValue, 0, 4095, 0, 100); // Convertir en pourcentage

  // Si le mode automatique est activé, gérer la LED
  if (ledAutoMode) {
    if (analogValue < ANALOG_THRESHOLD) {
      digitalWrite(LED_PIN, HIGH); // Allumer la LED si la lumière est faible
    } else {
      digitalWrite(LED_PIN, LOW);  // Éteindre la LED sinon
    }
  }

  Serial.printf("Valeur capteur: %d, Pourcentage: %d%%\n", analogValue, percentage);
}

// Route pour obtenir la valeur du capteur en pourcentage
void handleGetPhotoresistorValue() {
  int analogValue = analogRead(LIGHT_SENSOR_PIN);
  int percentage = map(analogValue, 0, 4095, 0, 100);

  String jsonResponse = "{\"percentage\": " + String(percentage) + "}";
  server.send(200, "application/json", jsonResponse);
}

// Route pour désactiver les mesures lorsque l'utilisateur quitte la page
void handlePhotoresistorExit() {
  isPhotoresistorPageActive = false;  // Désactive les mesures
  server.send(200, "text/plain", "Sortie de la page photoresistor.");
}

// Route pour activer/désactiver le mode automatique de la LED
void handleToggleLedAuto() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "on") {
      ledAutoMode = true;
    } else if (state == "off") {
      ledAutoMode = false;
      digitalWrite(LED_PIN, LOW); // Éteindre la LED si le mode auto est désactivé
    }
    server.send(200, "text/plain", "LED automatique: " + state);
  } else {
    server.send(400, "text/plain", "Paramètre 'state' manquant");
  }
}

// Route pour obtenir l'état actuel de la LED automatique
void handleGetLedAutoState() {
  String jsonResponse = "{\"state\": \"" + String(ledAutoMode ? "on" : "off") + "\"}";
  server.send(200, "application/json", jsonResponse);
}

// Fonction pour gérer la page du photoresistor
void handlePhotoresistorPage() {
  isPhotoresistorPageActive = true;  // Active les mesures du capteur
  const char page[] PROGMEM = R"rawliteral(
  <html>
  <body>
    <h1>Page du Photoresistor</h1>
    <p>Valeur de luminosité: <span id='lightValue'></span>%</p>
    <script>
      setInterval(function(){
        fetch('/get_photoresistor')
        .then(response => response.json())
        .then(data => {
          document.getElementById('lightValue').innerText = data.percentage;
        });
      }, 1000);
    </script>
  </body>
  </html>
  )rawliteral";
  server.send_P(200, "text/html", page);
}
