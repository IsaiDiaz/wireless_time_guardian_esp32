#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPAsyncWebSrv.h>


#define RST_PIN 22
#define SS_PIN 21
#define LED_PIN 2

const int MAX_CHAR = 50;
volatile bool rfidReaderActive;

/*
SSID = CAT
PASSWORD = 0ammj742660a
SERVERIP = 192.168.1.10
*/
char ssid[MAX_CHAR] = "CAT";
char password[MAX_CHAR] = "0ammj742660a";
char serverIP[MAX_CHAR] = "192.168.1.10";


MFRC522 mfrc522(SS_PIN, RST_PIN);
AsyncWebServer server(80);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  /*Serial.println("Ingrese el nombre de la red WiFi:");
  while(Serial.available() == 0){

  }

  delay (199);
  Serial.readBytesUntil('\n', ssid, MAX_CHAR);
  Serial.println(ssid);

  Serial.println("Ingrese la contraseña de la red WiFi:");
  while (Serial.available() == 0){

  }

  delay(100);
  Serial.readBytesUntil('\n', password, MAX_CHAR);
  Serial.println(password);*/

  connectToWiFi();

  /*Serial.println("Ingrese la direccion IP del servidor");
  while (Serial.available() == 0){
  }
  
  delay(100);
  Serial.readBytesUntil('\n', serverIP, MAX_CHAR);
  Serial.println(serverIP);*/

  String readerStatus = makeHttpRequest("rfid/status", "GET", "");
  if (readerStatus == "true") {
    rfidReaderActive = true;
  } else {
    rfidReaderActive = false;
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    makeHttpRequest("rfid/status/update", "PUT", "");
    rfidReaderActive = !rfidReaderActive;
    request->send(200, "text/plain", String("RFID status actualizado: ") + (rfidReaderActive?"activo":"inactivo"));
  });

  server.begin();

  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println("Listo para leer tarjetas RFID");
}

void loop() {

  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    processCommand(command);
  }

  /*String data = makeHttpRequest("rfid/status", "");

  if (data == "true") {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      Serial.print("UID de la tarjeta: ");
      digitalWrite(LED_PIN, HIGH);
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
      }
      Serial.println();

      mfrc522.PICC_HaltA();
    }
    Serial.println("RFID lector activo");
    //delay(5000);
    digitalWrite(LED_PIN, LOW);
  }else{
    Serial.println("El lector RFID se encuentra desactivado por el administrador");
    //delay(10000);
  }*/
}

void connectToWiFi() {
  Serial.println("Conectando a la red WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.println("Intentando conectar a la red WiFi...");
  }

  Serial.println("Conectado a la red WiFi");
  Serial.println("Direccion IP: " + WiFi.localIP().toString());
}

String makeHttpRequest(String path, String method, String data) {
  HTTPClient http;
  String url = "http://" + String(serverIP) + ":8080" + "/api/v1/" + path;
  http.begin(url);
  int httpCode;
  if(method == "GET"){
    httpCode = http.GET();
  }else if (method == "POST"){
    httpCode = http.POST(data);
  }else if (method == "PUT"){
    httpCode = http.PUT(data);
  }
  

  String dataResult;

  if (httpCode > 0) {
    // file found at server
    String payload = http.getString();
    Serial.println(payload);

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    const char* code = doc["code"];
    const char* message = doc["message"];
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
    Serial.println(String(code) + " - " + String(message));

    if (doc.containsKey("data")) {
      serializeJson(doc["data"], dataResult);
    } else {
      dataResult = "nothing";
      Serial.println("No se encontró la propiedad 'data' en la respuesta JSON");
    }

  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return dataResult;
}

void processCommand(String command) {
  command.trim();
  command.toLowerCase();

  if (command == "readcard") {
    String uid = "12abc";
    makeHttpRequest("rfid/" + uid + "/exists", "GET", "");
  } else if (command == "desactivar") {
    Serial.println("Comando recibido: Desactivar RFID");
  } else if (command == "ipaddress") {
    Serial.println("IP: " + WiFi.localIP().toString());
  } else if (command == "rfidstatus"){
    Serial.println(String("Estado del lector RFID: ") + (rfidReaderActive?"activo":"inactivo"));
  }else {
    Serial.println("Comando no reconocido");
  }
}