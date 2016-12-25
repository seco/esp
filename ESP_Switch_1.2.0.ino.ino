#include <FS.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include <ArduinoJson.h>
#include <DHT.h>

ESP8266WebServer server(80);
WiFiManager wifiManager;
WiFiClient client;
PubSubClient mqClient(client);

String content;

//Vcc measurement
ADC_MODE(ADC_VCC);

//APP
String FIRM_VER = "1.1.2";
String SENSOR = "PIR"; //BMP180, HTU21, DHT11

String app_id;
float vcc;

//CONF
String MODE = "AUTO";
int timeOut = 5000;

//mqtt config
char mqttAddress[200] = "http:mymqtt.com";
int mqttPort = 13666;
char mqttTopic[200] = "iot/sensor";

//REST API CONFIG
char rest_server[40];
boolean rest_ssl = false;
char rest_path[200];
int rest_port;
char api_token[100];
char api_payload[400];

boolean buttonPressed = false;
int lastTime = millis();

StaticJsonBuffer<500> jsonBuffer;


int BUILTINLED = 13;
int RELEY = 12;
int GPIO_IN = 14;
#define BUTTON 0

//DHT
#define DHTPIN 3
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

void setup() { //------------------------------------------------
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  pinMode(BUTTON, INPUT);
  pinMode(BUILTINLED, OUTPUT);

  digitalWrite(RELEY, LOW);
  digitalWrite(BUILTINLED, LOW);

  blink(1, 500);
  //dht.begin();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonConfig = jsonBuffer.parseObject(buf.get());
        jsonConfig.printTo(Serial);
        if (jsonConfig.success()) {
          Serial.println("\nparsed json");

          //config parameters
          String timeOut1 = jsonConfig["timeOut"];
          timeOut = timeOut1.toInt();
          String relley1 = jsonConfig["relleyPin"];
          RELEY = relley1.toInt();
          String gpioIn1 = jsonConfig["sensorInPin"];
          GPIO_IN = gpioIn1.toInt();
          String builtInLed1 = jsonConfig["statusLed"];
          BUILTINLED = builtInLed1.toInt();

          String mqttAddress1 = jsonConfig["mqttAddress"].asString();
          mqttAddress1.toCharArray(mqttAddress, 200, 0);
          String mqttPort1 = jsonConfig["mqttPort"];
          mqttPort = mqttPort1.toInt();
          String mqttTopic1 = jsonConfig["mqttTopic"].asString();
          mqttTopic1.toCharArray(mqttTopic, 200, 0);

          String rest_server1 = jsonConfig["restApiServer"].asString();
          rest_server1.toCharArray(rest_server, 40, 0);

          String rest_ssl1 = jsonConfig["restApiSSL"];
          if (rest_ssl1 == "true")
            rest_ssl = true;
          else
            rest_ssl = false;

          String rest_path1 = jsonConfig["restApiPath"].asString();
          rest_path1.toCharArray(rest_path, 200, 0);
          String rest_port1 = jsonConfig["restApiPort"];
          rest_port = rest_port1.toInt();
          String api_token1 = jsonConfig["restApiToken"].asString();
          api_token1.toCharArray(api_token, 200, 0);
          String api_payload1 = jsonConfig["restApiPayload"].asString();
          api_payload1.toCharArray(api_payload, 400, 0);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
    blink(10, 50, 20);
  }
  //end read

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ESP_config", "espjezakon")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...");
  blink(3, 500);

  pinMode(RELEY, OUTPUT);
  pinMode(GPIO_IN, INPUT);
  pinMode(BUILTINLED, OUTPUT);

  Serial.println(" ");
  Serial.print("local ip: ");
  Serial.println(WiFi.localIP());

  createWebServer();

  //MQTT
  mqClient.setServer(mqttAddress, mqttPort);
  mqClient.setCallback(mqCallback);
}

//loop ----------------------------------------------------------
void loop() {
  delay(10);
  server.handleClient();

  int inputState = digitalRead(GPIO_IN);
  int buttonState = digitalRead(BUTTON);
  vcc = ESP.getVcc() / 1000.00;

  if (MODE == "AUTO") {
    if (inputState == HIGH ) {
      Serial.println("Sensor high...");
      digitalWrite(RELEY, HIGH);
      digitalWrite(BUILTINLED, LOW);
      Serial.print("O");
      lastTime = millis();
      buttonPressed = false;
    }
    if (millis() > lastTime + timeOut && !buttonPressed && inputState != HIGH) {
      digitalWrite(RELEY, LOW);
      digitalWrite(BUILTINLED, HIGH);
    }
  }

  //button pressed
  if (buttonState == LOW) {
    Serial.println("Button pressed...");
    buttonPressed = true;
    if (digitalRead(RELEY) == HIGH) {
      digitalWrite(RELEY, LOW);
      digitalWrite(BUILTINLED, HIGH);
    } else {
      digitalWrite(RELEY, HIGH);
      digitalWrite(BUILTINLED, LOW);
    }
    delay(300);
  }
  //  mqClient.subscribe(String(mqttTopic).c_str());
  //  mqPublish("Hi there!");
  delay(100);


}//---------------------------------------------------------------

//web server
void createWebServer()
{
  Serial.println("Starting server...");
  app_id = getMac();
  Serial.print("App ID: ");
  Serial.println(app_id);

  Serial.println("REST handlers init...");
  server.on("/", []() {
    blink();
    IPAddress ip = WiFi.localIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    content = "<!DOCTYPE HTML>\r\n<html>";
    content += "<h1>ESP web server</h1>";
    content += "<p>IP: " + ipStr + "</p>";
    content += "<p>MAC/AppId: " + app_id + "</p>";
    content += "<p>Version: " + FIRM_VER + "</p>";
    content += "<p>Voltage: " + String(vcc) + " V</p>";
    content += "</html>";
    server.send(200, "text/html", content);
  });

  server.on("/switch/auto", []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    if (digitalRead(RELEY) == HIGH) {
      root["status"] = "on";
      digitalWrite(BUILTINLED, LOW);
    } else {
      root["status"] = "off";
      digitalWrite(BUILTINLED, HIGH);
    }
    MODE = "AUTO";
    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/switch/on", []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    root["status"] = "on";
    MODE = "MANUAL";
    String content;
    root.printTo(content);
    digitalWrite(RELEY, HIGH);
    digitalWrite(BUILTINLED, LOW);
    server.send(200, "application/json", content);
  });

  server.on("/switch/off", []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    root["status"] = "off";
    MODE = "MANUAL";
    String content;
    root.printTo(content);
    digitalWrite(RELEY, LOW);
    digitalWrite(BUILTINLED, HIGH);
    server.send(200, "application/json", content);
  });

  server.on("/switch/status", HTTP_GET, []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    root["mode"] = MODE;
    if (digitalRead(RELEY) == HIGH) {
      root["status"] = "on";
      digitalWrite(BUILTINLED, LOW);
    } else {
      root["status"] = "off";
      digitalWrite(BUILTINLED, HIGH);
    }
    JsonObject& meta = root.createNestedObject("meta");
    meta["version"] = FIRM_VER;
    meta["sensor"] = SENSOR;
    meta["voltage"] = vcc;
    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/update", HTTP_GET, []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["url"] = "Enter url to bin file here and POST json object to ESP.";

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/update", HTTP_POST, []() {
    blink();
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(server.arg("plain"));
    Serial.println("Updating firmware...");
    String message = "";
    //    for (uint8_t i = 0; i < server.args(); i++) {
    //      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    //    }

    String url = root["url"];
    Serial.println("");
    Serial.print("Update url: ");
    Serial.println(url);

    blink(10, 80);
    String content;

    t_httpUpdate_return ret = ESPhttpUpdate.update(url);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        root["rc"] = ESPhttpUpdate.getLastError();
        message += "HTTP_UPDATE_FAILD Error (";
        message += ESPhttpUpdate.getLastError();
        message += "): ";
        message += ESPhttpUpdate.getLastErrorString().c_str();
        root["msg"] = message;
        root.printTo(content);
        server.send ( 200, "application/json", content );
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        root["rc"] = 1;
        root["msg"] = "HTTP_UPDATE_NO_UPDATES";
        root.printTo(content);
        server.send ( 200, "application/json", content );
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        root["rc"] = 0;
        root["msg"] = "HTTP_UPDATE_OK";
        root.printTo(content);
        server.send ( 200, "application/json", content );
        break;
    }

  });

  server.on("/config", HTTP_GET, []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["rc"] = 0;
    root["timeOut"] = timeOut;
    root["relleyPin"] = RELEY;
    root["sensorInPin"] = GPIO_IN;
    root["statusLed"] = BUILTINLED;

    root["mqttAddress"] = mqttAddress;
    root["mqttPort"] = mqttPort;
    root["mqttTopic"] = mqttTopic;

    root["restApiServer"] = rest_server;
    root["restApiSSL"] = rest_ssl;
    root["restApiPath"] = rest_path;
    root["restApiPort"] = rest_port;
    root["restApiToken"] = api_token;
    root["restApiPayload"] = api_payload;

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/config", HTTP_POST, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(server.arg("plain"));
    Serial.println("\nSaving config...");

    String timeOut1 = root["timeOut"];
    timeOut = timeOut1.toInt();
    String relley1 = root["relleyPin"];
    RELEY = relley1.toInt();
    String gpioIn1 = root["sensorInPin"];
    GPIO_IN = gpioIn1.toInt();
    String builtInLed1 = root["statusLed"];
    BUILTINLED = builtInLed1.toInt();
    String mqttAddress1 = root["mqttAddress"].asString();
    mqttAddress1.toCharArray(mqttAddress, 200, 0);
    String mqttPort1 = root["mqttPort"];
    mqttPort = mqttPort1.toInt();
    String mqttTopic1 = root["mqttTopic"].asString();
    mqttTopic1.toCharArray(mqttTopic, 200, 0);

    String rest_server1 = root["restApiServer"].asString();
    rest_server1.toCharArray(rest_server, 40, 0);

    String rest_ssl1 = root["restApiSSL"];
    if (rest_ssl1 == "true")
      rest_ssl = true;
    else
      rest_ssl = false;

    String rest_path1 = root["restApiPath"].asString();
    rest_path1.toCharArray(rest_path, 200, 0);
    String rest_port1 = root["restApiPort"];
    rest_port = rest_port1.toInt();
    String api_token1 = root["restApiToken"].asString();
    api_token1.toCharArray(api_token, 200, 0);
    String api_payload1 = root["restApiPayload"].asString();
    api_payload1.toCharArray(api_payload, 400, 0);

    root.printTo(Serial);

    saveConfig(root);
    pinMode(RELEY, OUTPUT);
    pinMode(GPIO_IN, INPUT);

    Serial.println("\nConfiguration is saved.");
    root["rc"] = 0;
    root["msg"] = "Configuration is saved.";

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  //---------------------------------------------------------

  server.on("/reset", HTTP_DELETE, []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["rc"] = 0;
    root["msg"] = "Reseting ESP config...";
    Serial.println("\nReseting ESP config...");

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);

    //clean FS, for testing
    SPIFFS.format();

    //reset settings - for testing
    wifiManager.resetSettings();

    ESP.reset();
    delay(5000);
  });

  server.on("/reset", HTTP_GET, []() {
    blink();
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["rc"] = 0;
    root["msg"] = "Reseting ESP...";
    Serial.println("\nReseting ESP...");

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);

    ESP.reset();
    delay(5000);
  });

  server.begin();
  Serial.println("Server started");

}//--

//send request
void sendRequest(float pre, float temp, float hum, String msg)
{
  WiFiClientSecure client;
  //WiFiClient client;
  int i = 0;
  String url = rest_path;
  Serial.println("");
  Serial.print("Connecting for request... ");
  Serial.print(String(rest_server));
  Serial.print(":");
  Serial.print(String(rest_port));

  while (!client.connect(rest_server, rest_port)) {
    Serial.print("Try ");
    Serial.print(i);

    if (i == 4) {
      blink(5, 20);
      return;
    }
    i++;
  }

  Serial.print("POST data to URL: ");
  Serial.println(url);

  String api_payload_s = String(api_payload);
  if (api_payload_s != "")
    api_payload_s = api_payload_s + ",";
  String data = "{" + api_payload_s + " \"sensor\":{\"sensor_type\":\"" + SENSOR +
                "\", \"data\":{\"vcc\":" + String(vcc) +
                ", \"event\":\"" + msg + "\"" +
                ", \"temp\":" + String(temp) +
                ", \"hum\":" + String(hum) +
                ", \"ver\":\"" + FIRM_VER +
                "\"}}}";

  String req = String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + String(rest_server) + "\r\n" +
               "User-Agent: ESP/1.0\r\n" +
               "Content-Type: application/json\r\n" +
               "Cache-Control: no-cache\r\n" +
               "Sensor-Id: " + String(app_id) + "\r\n" +
               "Token: " + String(api_token) + "\r\n" +
               "Content-Type: application/x-www-form-urlencoded;\r\n" +
               "Content-Length: " + data.length() + "\r\n" +
               "\r\n" + data;
  Serial.print("Request: ");
  Serial.println(req);
  client.print(req);

  delay(100);

  Serial.println("Response: ");
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  blink(2, 40);

  Serial.println();
  Serial.println("Connection closed");
} //--

void saveConfig(JsonObject& json) {

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}

//MQTT
void mqCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  blink(3, 50);
}

boolean mqReconnect() {
  // Loop until we're reconnected
  while (!mqClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP";
    clientId += String(app_id);
    // Attempt to connect
    if (mqClient.connect(clientId.c_str())) {
      Serial.println("connected ");
      return true;
    } else {
      Serial.print("failed, rc=");
      return false;
    }
  }
}

void mqPublish(String msg) {
  if (!mqClient.connected()) {
    mqReconnect();
  }
  mqClient.loop();

  Serial.print("Publish message: ");
  Serial.println(msg);
  mqClient.publish(String(mqttTopic).c_str(), msg.c_str());

}

//blink
void blink () {
  blink(1, 30, 30);
}

void blink (int times) {
  blink(times, 40, 40);
}

void blink (int times, int milisec) {
  blink(times, milisec, milisec);
}
void blink (int times, int himilisec, int lowmilisec) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUILTINLED, LOW);
    delay(lowmilisec);
    digitalWrite(BUILTINLED, HIGH);
    delay(himilisec);
  }
}

String getMac() {
  unsigned char mac[6];
  WiFi.macAddress(mac);
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
  }
  return result;
}
