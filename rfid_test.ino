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

enum HttpRequestStatus {
  HttpRequestSuccess,
  HttpRequestFailed,
  HttpRequestNoData
};

const int MAX_CHAR = 50;
volatile bool rfidReaderActive = false;
volatile bool rfidReady = false;
/*
SSID = CAT
PASSWORD = 0ammj742660a
SERVERIP = 192.168.1.10
*/
String ssid;
String password;
String serverIP;

const char *ssidEsp32 = "RFID-Config";
const char *passwordEsp32 = "rfidconfig";

MFRC522 mfrc522(SS_PIN, RST_PIN);
AsyncWebServer server(80);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  WiFi.softAP(ssidEsp32, passwordEsp32);
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head>"
                  "<style>"
                  "  body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 20px; }"
                  "  h1 { color: #333; }"
                  "  .container { max-width: 600px; margin: auto; background-color: #fff; padding: 20px; border-radius: 5px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
                  "  label { display: block; margin-bottom: 8px; }"
                  "  input { width: 100%; padding: 8px; margin-bottom: 16px; box-sizing: border-box; }"
                  "  button { background-color: #007bff; color: #fff; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; }"
                  "</style>"
                  "</head><body>"
                  "<div class='container'>"
                  "<h1>Configuración ESP32</h1>"
                  "<form>"
                  "  <label for='ssid'>SSID:</label>"
                  "  <input type='text' id='ssid'>"
                  "  <label for='password'>Contraseña:</label>"
                  "  <input type='password' id='password'>"
                  "  <label for='serverIP'>Servidor IP:</label>"
                  "  <input type='text' id='serverIP'>"
                  "  <button type='button' onclick='guardarConfiguracion()'>Guardar</button>"
                  "</form>"
                  "<script>"
                  "function guardarConfiguracion() {"
                  "  var ssid = document.getElementById('ssid').value;"
                  "  var password = document.getElementById('password').value;"
                  "  var serverIP = document.getElementById('serverIP').value;"
                  "  window.location = '/config?ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password) + '&serverIP=' + encodeURIComponent(serverIP);"
                  "}"
                  "</script>"
                  "</div>"
                  "</body></html>";

    request->send(200, "text/html", html);
  });


  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Lee los parámetros del formulario
    if (request->hasParam("ssid") && request->hasParam("password") && request->hasParam("serverIP")) {
      ssid = request->getParam("ssid")->value();
      password = request->getParam("password")->value();
      serverIP = request->getParam("serverIP")->value();

      request->send(200, "text/html", "Configuracion guardada.");
      WiFi.softAPdisconnect(true);
      connectToWiFi();
      //inicializa rfid status con informacion de base de datos desde el servidor
      String readerStatus;
      HttpRequestStatus status = makeHttpRequest("rfid/status", "GET", "", readerStatus);

      if (status == HttpRequestSuccess) {
        if (readerStatus == "true") {
          rfidReaderActive = true;
        } else {
          rfidReaderActive = false;
        }
      }

      rfidReady = true;
    } else {
      request->send(400, "text/plain", "Parámetros incompletos.");
    }
  });

  server.on("/status/refresh", HTTP_GET, [](AsyncWebServerRequest *request) {
    String readerStatus;
    HttpRequestStatus status = makeHttpRequest("rfid/status", "GET", "", readerStatus);

    if (status == HttpRequestSuccess) {
      if (readerStatus == "true") {
        rfidReaderActive = true;
      } else {
        rfidReaderActive = false;
        request->send(500, "text/html", "Error al obtener estado de lector desde base de datos.");
      }
    }
    request->send(200, "text/html", "Estado del lector actulizada con base de datos.");
  });

  server.on("/status/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    String response;
    HttpRequestStatus status = makeHttpRequest("rfid/status/update", "PUT", "", response);
    if (status == HttpRequestSuccess) {
      rfidReaderActive = !rfidReaderActive;
      request->send(200, "text/plain", String("RFID status actualizado: ") + (rfidReaderActive ? "activo" : "inactivo"));
    } else {
      request->send(500, "text/plain", String("Ocurrio un erro al intentar actualizar el estado del lector RFID"));
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
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

  if (rfidReady) {
    readCard();
  }
}

void readCard() {
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    digitalWrite(LED_PIN, HIGH);
    String cardUID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      cardUID += (mfrc522.uid.uidByte[i] < 0x10 ? " 0" : "_");
      cardUID += String(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println("UID de la targeta: " + cardUID);
    mfrc522.PICC_HaltA();
    processUid(cardUID);
    delay(1000);
    digitalWrite(LED_PIN, LOW);
  }
}

void processUid(String uid) {
  String response;
  HttpRequestStatus status = makeHttpRequest("rfid/" + uid + "/exists", "GET", "", response);
  if (status == HttpRequestSuccess && rfidReaderActive) {
    registryTime(uid);
  } else {
    registryCardUid(uid);
  }
}

void registryTime(String uid) {
  String registryResponse;
  HttpRequestStatus registryStatus = makeHttpRequest("rfid/registry_time/" + uid, "POST", "", registryResponse);
  if (registryStatus == HttpRequestSuccess) {
    Serial.println("Registro de tiempo exitoso para UID: " + uid);
  } else {
    Serial.println("Error al registrar el tiempo para UID: " + uid);
  }
}

void registryCardUid(String uid) {
  String registryResponse;
  HttpRequestStatus registryStatus = makeHttpRequest("rfid/" + uid, "POST", "", registryResponse);
  if (registryStatus == HttpRequestSuccess) {
    Serial.println("Registro de tarjeta exitoso para UID: " + uid);
  } else {
    Serial.println("Error al registrar la tarjeta para UID: " + uid);
  }
}

void connectToWiFi() {
  Serial.println("Conectando a la red WiFi...");
  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.println("Intentando conectar a la red WiFi...");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conetado a la red WiFi.");
    Serial.println("Direccion IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("Error al conectar a la red WiFi.");
  }
}

HttpRequestStatus makeHttpRequest(String path, String method, String data, String &responseData) {
  HTTPClient http;
  String url = "http://" + String(serverIP) + ":8080" + "/api/v1/" + path;
  http.begin(url);
  int httpCode;

  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(data);
  } else if (method == "PUT") {
    httpCode = http.PUT(data);
  } else {
    Serial.println("Metodo HTTP no válido");
    http.end();
    return HttpRequestFailed;
  }

  if (httpCode > 0) {
    String payload = http.getString();

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    if (doc.containsKey("code") && doc.containsKey("message")) {

      const char *code = doc["code"];
      const char *message = doc["message"];
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      Serial.println(String(code) + " - " + String(message));

      if (doc.containsKey("data")) {
        serializeJson(doc["data"], responseData);
        Serial.println(responseData);
        http.end();
        return HttpRequestSuccess;
      } else {
        Serial.println("No se encontró la propiedad 'data' en la respuesta JSON");
        http.end();
        return HttpRequestNoData;
      }

    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      http.end();
      return HttpRequestFailed;
    }
  } else {
    Serial.printf("[HTTP] %s... failed, error: %s\n", method.c_str(), http.errorToString(httpCode).c_str());
    http.end();
    return HttpRequestFailed;
  }
}

void processCommand(String command) {
  command.trim();
  command.toLowerCase();

  if (command == "readcard") {
    String uid = "12abc";
    String response;
    HttpRequestStatus status = makeHttpRequest("rfid/" + uid + "/exists", "GET", "", response);
    if (status == HttpRequestSuccess) {
      if (response == "true") {
        String registryResponse;
        HttpRequestStatus registryStatus = makeHttpRequest("rfid/registry_time/" + uid, "POST", "", registryResponse);
      }
    }
  } else if (command == "desactivar") {
    Serial.println("Comando recibido: Desactivar RFID");
  } else if (command == "ipaddress") {
    Serial.println("IP: " + WiFi.localIP().toString());
  } else if (command == "rfidstatus") {
    Serial.println(String("Estado del lector RFID: ") + (rfidReaderActive ? "activo" : "inactivo"));
  } else {
    Serial.println("Comando no reconocido");
  }
}