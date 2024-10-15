#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Définition des pins
#define LED_PIN 12

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

// Prototypes des fonctions
void startBLEServer();
void stopBLEServer();
void connectToWiFiSTA();
void handleRoot();
void handleModeSelection();

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
    // ...
  } else {
    Serial.println("\nImpossible de se connecter au réseau Wi-Fi");
    // Revenir en mode AP pour réessayer
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(apSSID, apPassword);
    server.begin();
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
