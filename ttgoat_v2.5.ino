#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <ESPmDNS.h> // Pour mDNS
#include <math.h>    // Pour les calculs du thermistor

// Définition des pins
#define LED_PIN 2
#define LIGHT_SENSOR_PIN  36  // Broche GPIO36 (ADC0) pour le capteur de lumière
#define ANALOG_THRESHOLD  500 // Seuil pour la LED automatique
const int buzzerPin = 13;     // Pin du buzzer
#define THERMISTOR_PIN 34     // Broche pour le thermistor (ajustez si nécessaire)

// Constantes pour le thermistor
const float THERMISTOR_NOMINAL = 10000;   // Résistance à 25°C
const float TEMPERATURE_NOMINAL = 25.0;   // Température nominale en Celsius
const float B_COEFFICIENT = 3950;         // Coefficient B du thermistor
const float SERIES_RESISTOR = 10000;      // Résistance en série (en ohms)

// Variables pour le mode AP
String apSSID = "ESP32_AP";
String apPassword = "12345678";

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
bool isBLEServerRunning = false;

// UUID pour le service BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Variables pour le capteur de lumière et la LED automatique
bool ledAutoMode = false;     // Mode LED automatique activé/désactivé
bool isPhotoresistorPageActive = false; // Flag pour savoir si la page du capteur est active

// Variable pour savoir si le serveur est en cours d'exécution
bool isServerRunning = false;

// Prototypes des fonctions
void startBLEServer();
void stopBLEServer();
void stopWiFi();
void connectToWiFiSTA();
void handleRoot();
void handleModeSelection();
void setupServerRoutes();

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
void handleGetTemperature();

// Classe de rappel pour le BLE
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValueStd = pCharacteristic->getValue();
    String rxValue = String(rxValueStd.c_str());

    if (rxValue.length() > 0) {
      Serial.print("Valeur reçue via BLE: ");
      Serial.println(rxValue);

      if (rxValue.startsWith("SET_MODE")) {
        // Commande pour changer de mode
        int colon1 = rxValue.indexOf(':');
        int colon2 = rxValue.indexOf(':', colon1 + 1);
        int colon3 = rxValue.indexOf(':', colon2 + 1);

        if (colon1 != -1 && colon2 != -1) {
          String mode = rxValue.substring(colon1 + 1, colon2);
          String ssid = "";
          String password = "";

          if (colon3 != -1) {
            ssid = rxValue.substring(colon2 + 1, colon3);
            password = rxValue.substring(colon3 + 1);
          } else {
            ssid = rxValue.substring(colon2 + 1);
          }

          Serial.print("Mode reçu via BLE: ");
          Serial.println(mode);
          Serial.print("SSID reçu via BLE: ");
          Serial.println(ssid);
          Serial.print("Password reçu via BLE: ");
          Serial.println(password);

          if (mode == "AP") {
            apSSID = ssid;
            apPassword = password;
            connectionMode = "AP";
            Serial.println("Demande de passage en mode AP via BLE");
          } else if (mode == "STA") {
            staSSID = ssid;
            staPassword = password;
            connectionMode = "STA";
            Serial.println("Demande de passage en mode STA via BLE");
          } else if (mode == "BLE") {
            connectionMode = "BLE";
            Serial.println("Demande de rester en mode BLE via BLE");
          } else {
            Serial.println("Mode inconnu reçu via BLE");
          }
        } else {
          Serial.println("Commande SET_MODE mal formatée");
        }
      } else if (rxValue == "LED_ON") {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED allumée via BLE");
      } else if (rxValue == "LED_OFF") {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED éteinte via BLE");
      }
      // Ajoutez d'autres commandes si nécessaire
    }
  }
};

// Classe de rappel pour le serveur BLE
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    Serial.println("Client BLE connecté");
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("Client BLE déconnecté");
    pServer->getAdvertising()->start();
    Serial.println("Reprise de l'advertising BLE");
  }
};

// Page HTML stockée en mémoire flash pour économiser la RAM
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Sélection du mode de connexion</title></head><body>
<h2>Sélectionnez le mode de connexion</h2>
<form action='/select_mode' method='POST'>
  <input type='radio' name='mode' value='AP'> Mode AP<br>
  SSID AP: <input type='text' name='ap_ssid'><br>
  Mot de passe AP: <input type='password' name='ap_password'><br>
  <input type='radio' name='mode' value='STA'> Mode STA<br>
  SSID: <input type='text' name='ssid'><br>
  Mot de passe: <input type='password' name='password'><br>
  <input type='radio' name='mode' value='BLE' checked> Mode BLE<br><br>
  <input type='submit' value='Valider'>
</form></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println("Démarrage de l'ESP32...");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Éteindre la LED au démarrage

  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(THERMISTOR_PIN, INPUT);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Démarrer en mode AP par défaut
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(apSSID.c_str(), apPassword.c_str());
  Serial.println("Mode AP démarré par défaut");
  Serial.print("SSID: ");
  Serial.println(apSSID);
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.softAPIP());

  // Démarrer le serveur web
  setupServerRoutes();
  server.begin();
  isServerRunning = true;
  Serial.println("Serveur web démarré en mode AP");

  // Démarrer mDNS
  if (!MDNS.begin("esp32")) {
    Serial.println("Erreur lors de la configuration du mDNS");
  } else {
    Serial.println("mDNS démarré. Nom d'hôte : esp32.local");
  }
}

void loop() {
  // Si le mode de connexion a été défini, basculer vers ce mode
  if (connectionMode == "STA") {
    Serial.println("Bascule vers le mode STA");
    stopBLEServer();
    stopWiFi();
    connectToWiFiSTA();
    connectionMode = "";
  } else if (connectionMode == "AP") {
    Serial.println("Bascule vers le mode AP avec les nouveaux paramètres");
    stopBLEServer();
    stopWiFi();
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    Serial.println("Mode AP démarré avec les nouveaux paramètres");
    Serial.print("SSID: ");
    Serial.println(apSSID);
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.softAPIP());
    setupServerRoutes();
    server.begin();
    isServerRunning = true;
    Serial.println("Serveur web redémarré en mode AP");

    // Redémarrer mDNS en mode AP
    if (!MDNS.begin("esp32")) {
      Serial.println("Erreur lors de la configuration du mDNS");
    } else {
      Serial.println("mDNS redémarré en mode AP");
    }

    connectionMode = "";
  } else if (connectionMode == "BLE") {
    Serial.println("Bascule vers le mode BLE");
    stopWiFi();
    stopBLEServer();
    startBLEServer();
    connectionMode = "";
  }

  // Traiter les requêtes du serveur web si actif
  if (isServerRunning) {
    server.handleClient();
  }

  // Mettre à jour le capteur de lumière si la page est active
  if (isPhotoresistorPageActive) {
    updatePhotoresistor();   // Lire le capteur et gérer la LED
  }

  delay(10); // Petit délai pour éviter de monopoliser le CPU
}

// Fonction pour arrêter le Wi-Fi proprement
void stopWiFi() {
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    Serial.println("Arrêt du Wi-Fi");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_MODE_NULL);
    isServerRunning = false;
    server.stop();
  }
}

// Fonction pour configurer les routes du serveur web
void setupServerRoutes() {
  // Configurer les routes du serveur web
  server.on("/", handleRoot);
  server.on("/select_mode", HTTP_POST, handleModeSelection);

  // Routes pour les fonctionnalités
  server.on("/control_gpio", HTTP_GET, handleGPIOControl);
  server.on("/play_song", HTTP_GET, handlePlaySong);
  server.on("/get_photoresistor", HTTP_GET, handleGetPhotoresistorValue);
  server.on("/toggle_led_auto", HTTP_GET, handleToggleLedAuto);
  server.on("/get_led_auto_state", HTTP_GET, handleGetLedAutoState);
  server.on("/get_temperature", HTTP_GET, handleGetTemperature);
  server.on("/photoresistor_page", HTTP_GET, handlePhotoresistorPage);

  server.onNotFound([](){
    server.send(404, "text/plain", "Not Found");
  });
}

// Fonction pour gérer la page d'accueil
void handleRoot() {
  Serial.println("Requête GET reçue pour /");
  server.send_P(200, "text/html", index_html);
}

// Fonction pour gérer la sélection du mode
void handleModeSelection() {
  Serial.println("Requête POST reçue pour /select_mode");
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
        Serial.println("SSID et mot de passe manquants pour le mode STA");
        connectionMode = ""; // Réinitialiser le mode car les infos sont manquantes
      }
    } else if (connectionMode == "AP") {
      if (server.hasArg("ap_ssid") && server.hasArg("ap_password")) {
        apSSID = server.arg("ap_ssid");
        apPassword = server.arg("ap_password");
        server.send(200, "text/plain", "Bascule vers le mode AP avec les nouveaux paramètres...");
      } else {
        server.send(400, "text/plain", "SSID et mot de passe requis pour le mode AP");
        Serial.println("SSID et mot de passe manquants pour le mode AP");
        connectionMode = ""; // Réinitialiser le mode car les infos sont manquantes
      }
    } else if (connectionMode == "BLE") {
      server.send(200, "text/plain", "Bascule vers le mode BLE...");
    } else {
      server.send(400, "text/plain", "Mode inconnu");
      Serial.println("Mode inconnu sélectionné");
      connectionMode = ""; // Réinitialiser le mode car invalide
    }
  } else {
    server.send(400, "text/plain", "Aucun mode sélectionné");
    Serial.println("Aucun mode sélectionné dans la requête");
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
    // Démarrer le serveur web en mode STA
    setupServerRoutes();
    server.begin();
    isServerRunning = true;
    Serial.println("Serveur web démarré en mode STA");

    // Démarrer mDNS en mode STA
    if (!MDNS.begin("esp32")) {
      Serial.println("Erreur lors de la configuration du mDNS");
    } else {
      Serial.println("mDNS démarré en mode STA");
    }
  } else {
    Serial.println("\nImpossible de se connecter au réseau Wi-Fi");
    // Revenir en mode AP pour réessayer
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    Serial.println("Revenu en mode AP par défaut");
    Serial.print("SSID: ");
    Serial.println(apSSID);
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.softAPIP());
    setupServerRoutes();
    server.begin();
    isServerRunning = true;
    Serial.println("Serveur web redémarré en mode AP");
  }
}

// Fonctions pour le BLE
void startBLEServer() {
  if (!isBLEServerRunning) {
    Serial.println("Démarrage du serveur BLE...");
    BLEDevice::init("ESP32_BLE");
    pBLEServer = BLEDevice::createServer();
    pBLEServer->setCallbacks(new MyServerCallbacks());

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

    isBLEServerRunning = true;
    Serial.println("Serveur BLE démarré");
  } else {
    Serial.println("Le serveur BLE est déjà en cours d'exécution");
  }
}

void stopBLEServer() {
  if (isBLEServerRunning && pBLEServer != nullptr) {
    Serial.println("Arrêt du serveur BLE...");
    pBLEServer->getAdvertising()->stop();
    pBLEServer->removeService(pBLEServer->getServiceByUUID(SERVICE_UUID));
    BLEDevice::deinit(true);
    isBLEServerRunning = false;
    Serial.println("Serveur BLE arrêté");
  }
}

// Fonction pour gérer les requêtes de contrôle GPIO
void handleGPIOControl() {
  Serial.println("Requête reçue pour /control_gpio");

  if (server.hasArg("action")) {
    String action = server.arg("action");

    if (action == "on") {
      digitalWrite(LED_PIN, HIGH);
      server.send(200, "text/plain", "LED allumée");
      Serial.println("LED allumée");
    } else if (action == "off") {
      digitalWrite(LED_PIN, LOW);
      server.send(200, "text/plain", "LED éteinte");
      Serial.println("LED éteinte");
    } else {
      server.send(400, "text/plain", "Action invalide. Utilisez 'on' ou 'off'.");
      Serial.println("Action invalide reçue");
    }
  } else {
    server.send(400, "text/plain", "Paramètre 'action' manquant.");
    Serial.println("Paramètre 'action' manquant");
  }
}

// Fonction pour gérer les requêtes de lecture du buzzer
void handlePlaySong() {
  Serial.println("Requête reçue pour /play_song");

  // Pour simplifier, nous jouerons une chanson prédéfinie
  char notes[] = "ccggaagffeeddc ";
  int beats[] =  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
  int songLength = sizeof(notes) - 1;
  int tempo = 300;

  playSong(notes, beats, songLength, tempo);

  server.send(200, "text/plain", "Chanson jouée avec succès");
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
    delay(tempo / 10);
  }
}

// Fonction pour convertir une note en fréquence
int frequency(char note) {
  // Cette fonction prend une note (c, d, e, f, g, a, b, C) et renvoie la fréquence correspondante en Hz
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

  // Affichage pour le débogage
  // Serial.printf("Valeur capteur: %d, Pourcentage: %d%%\n", analogValue, percentage);
}

// Route pour obtenir la valeur du capteur en pourcentage
void handleGetPhotoresistorValue() {
  int analogValue = analogRead(LIGHT_SENSOR_PIN);
  int percentage = map(analogValue, 0, 4095, 0, 100);

  server.send(200, "text/plain", String(percentage));
}

// Route pour activer/désactiver le mode automatique de la LED
void handleToggleLedAuto() {
  ledAutoMode = !ledAutoMode;
  server.send(200, "text/plain", ledAutoMode ? "ON" : "OFF");
}

// Route pour obtenir l'état actuel de la LED automatique
void handleGetLedAutoState() {
  server.send(200, "text/plain", ledAutoMode ? "ON" : "OFF");
}

// Fonction pour obtenir la température depuis le thermistor
void handleGetTemperature() {
  int analogValue = analogRead(THERMISTOR_PIN);
  float resistance = SERIES_RESISTOR / ((4095.0 / analogValue) - 1.0);

  float steinhart;
  steinhart = resistance / THERMISTOR_NOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                      // ln(R/Ro)
  steinhart /= B_COEFFICIENT;                      // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURE_NOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                     // Inverse
  steinhart -= 273.15;                             // Convertir en Celsius

  server.send(200, "text/plain", String(steinhart, 2));
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
        .then(response => response.text())
        .then(data => {
          document.getElementById('lightValue').innerText = data;
        });
      }, 1000);
    </script>
  </body>
  </html>
  )rawliteral";
  server.send_P(200, "text/html", page);
}
