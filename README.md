# TTGOAT_Arduino

tools->partition schemes->huge app pour compiler

Modes de connexion :

BLE : L'ESP32 démarre en mode BLE par défaut. Votre application Flutter peut se connecter à l'ESP32 via BLE pour envoyer des commandes et configurer le mode de connexion souhaité.
STA : Permet à l'ESP32 de se connecter à un réseau Wi-Fi existant en utilisant le SSID et le mot de passe fournis.
AP : L'ESP32 crée son propre réseau Wi-Fi (point d'accès) avec le SSID et le mot de passe fournis.
Communication via BLE :

Les commandes pour changer de mode sont envoyées sous la forme : SET_MODE:<MODE>:<SSID>:<PASSWORD>.
L'application Flutter envoie ces commandes à l'ESP32 via la caractéristique BLE.
Communication via Wi-Fi (AP ou STA) :

Les routes HTTP sont définies pour permettre à l'application Flutter de communiquer avec l'ESP32.
On peut envoyer des requêtes HTTP POST ou GET à l'ESP32 pour contrôler les GPIO, le buzzer, etc.
Gestion des modes dans loop():

Le code vérifie si connectionMode a été défini et bascule vers le mode approprié.
Si le mode est STA, il se connecte au réseau Wi-Fi spécifié et démarre le serveur web.
Si le mode est AP, il démarre un point d'accès Wi-Fi avec les paramètres spécifiés et démarre le serveur web.
Si le mode est BLE, il reste en mode BLE.
Initialisation des broches :

Les broches pour la LED, le capteur de lumière et le buzzer sont initialisées dans la fonction setup().
