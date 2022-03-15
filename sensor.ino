#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>  
#include <PubSubClient.h>
#include <DHTesp.h>

struct ClientDetails {
  char deviceName[60];
  char ipAddress[14];
};

struct MqttDetails {
  char serverAddress[30];
  int port;
  char topic[100];
  char username[40];
  char password[40];
};

struct SensorDetails {
  int tempOffset;
  int humidityOffset;
  int refreshDelay;
  bool resetFlag = false;
};

struct LocalConfig {
  ClientDetails clientDetails;
  MqttDetails mqttDetails;
  SensorDetails sensorDetails;
};

const char *configfilename = "/config.txt";
LocalConfig localConfig;

WiFiServer server(80);
String header;
WiFiClient espClient;
bool shouldSaveConfig = true;

WiFiManager wifiManager;

PubSubClient client(espClient);

unsigned long previousMillis = 0;    // will store last time DHT was updated
// Updates DHT readings every 10 seconds
const long interval = 1500;  

//#define DHTTYPE DHT11
//#define DHTPIN 2
//DHT dht = DHT(DHTPIN, DHTTYPE);

DHTesp dht;

// current temperature & humidity, updated in loop()
float t = 0.0;
float h = 0.0;

char buffTemp[200];
char buffHumid[200];
char buffDelay[200];
char buffReset[200];

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


bool readFromFile(const char *configfilename, LocalConfig &localConfig) {

  Serial.print("Start read from file");
  Serial.println(configfilename);

  File file = LittleFS.open(configfilename, "r");
  if (!file) {
    Serial.println("Error: Opening config.txt to write failed");
  }
  byte jsonBuffer[350];
  while (file.available()) {
    file.read(jsonBuffer, 350);
  }
  
  Serial.println();
  StaticJsonDocument<350> doc;
  DeserializationError error = deserializeJson(doc, jsonBuffer);
  if (error) {
    Serial.println(F("Error: Failed to read file."));
    file.close();
    return false;
  }

  Serial.println("Attempt to put config in memory.");
  serializeJsonPretty(doc, Serial);

  strlcpy(localConfig.clientDetails.deviceName,                 // <- destination
          doc["clientDetails"]["deviceName"],                  // <- source
          sizeof(localConfig.clientDetails.deviceName));      // <- destination's capacity

  strlcpy(localConfig.clientDetails.ipAddress,                 // <- destination
          doc["clientDetails"]["ipAddress"],                  // <- source
          sizeof(localConfig.clientDetails.ipAddress));      // <- destination's capacity
  
  strlcpy(localConfig.mqttDetails.serverAddress,                 // <- destination
          doc["mqttDetails"]["serverAddress"],                  // <- source
          sizeof(localConfig.mqttDetails.serverAddress));      // <- destination's capacity

  localConfig.mqttDetails.port = doc["mqttDetails"]["port"];

  strlcpy(localConfig.mqttDetails.topic,                 // <- destination
          doc["mqttDetails"]["topic"],                  // <- source
          sizeof(localConfig.mqttDetails.topic));      // <- destination's capacity

  strlcpy(localConfig.mqttDetails.username,                 // <- destination
          doc["mqttDetails"]["username"],                  // <- source
          sizeof(localConfig.mqttDetails.username));      // <- destination's capacity

  strlcpy(localConfig.mqttDetails.password,                 // <- destination
          doc["mqttDetails"]["password"],                  // <- source
          sizeof(localConfig.mqttDetails.password));      // <- destination's capacity

  localConfig.sensorDetails.tempOffset = doc["sensorDetails"]["tempOffset"];
  localConfig.sensorDetails.humidityOffset = doc["sensorDetails"]["humidityOffset"];
  localConfig.sensorDetails.refreshDelay = doc["sensorDetails"]["refreshDelay"];
  localConfig.sensorDetails.resetFlag = doc["sensorDetails"]["resetFlag"];

  file.close();
  Serial.println();
  Serial.println("Successful read from config file");
  return true;
}

void updateFile(){
  Serial.println("Start of update file.");
  while (!LittleFS.begin()) {
    Serial.println(F("Error: Failed to initialize LittleFS...Trying again."));
    delay(1000);
  }

  if(localConfig.sensorDetails.resetFlag) {
    LittleFS.format(); 
  }

  
//  File file = LittleFS.open(configfilename, "r");
//  if (!file) {
//    Serial.println("Error: Opening config.txt to write failed");
//  }


//  if(!readFromFile(configfilename, localConfig)) {
      readFromFile(configfilename, localConfig);
      writeToFile(configfilename, localConfig);
//  }
  
//  file.close();
  
  LittleFS.end();
}

void writeToFile(const char *configfilename, LocalConfig &localConfig) {
  Serial.println();
  Serial.println("write to file started.");
  Serial.println(configfilename);
  Serial.println();
  File file = LittleFS.open(configfilename, "w+");
  if (!file) {
    Serial.println("Error: Opening config.txt to write failed");
  }

  StaticJsonDocument<350> configFile;

  JsonObject clientDetails  = configFile.createNestedObject("clientDetails");
  clientDetails["deviceName"] = localConfig.clientDetails.deviceName;
  clientDetails["ipAddress"] = localConfig.clientDetails.ipAddress;

  JsonObject mqttDetails  = configFile.createNestedObject("mqttDetails");
  mqttDetails["serverAddress"] = localConfig.mqttDetails.serverAddress;
  mqttDetails["port"] = localConfig.mqttDetails.port;
  mqttDetails["topic"] = localConfig.mqttDetails.topic;
  mqttDetails["username"] = localConfig.mqttDetails.username;
  mqttDetails["password"] = localConfig.mqttDetails.password;

  JsonObject sensorDetails = configFile.createNestedObject("sensorDetails");
  sensorDetails["tempOffset"] = localConfig.sensorDetails.tempOffset;
  sensorDetails["humidityOffset"] = localConfig.sensorDetails.humidityOffset;
  sensorDetails["refreshDelay"] = localConfig.sensorDetails.refreshDelay;
  sensorDetails["resetFlag"] = localConfig.sensorDetails.resetFlag;


  if (serializeJson(configFile, file) == 0) {
    Serial.println("Error: Failed to write to file");
  }

  serializeJsonPretty(configFile, Serial);

  file.close();
}

void wifiManagerSetup() {
  Serial.println("Start Wifi Manager Setup.");
  
  if(localConfig.sensorDetails.resetFlag) {
    Serial.println("Reset WiFi Manager");
    wifiManager.resetSettings(); 
  }else {
    Serial.println("Don't reset WiFi Manager.");
  }

  // id/name, placeholder/prompt, default, length
  WiFiManagerParameter custom_mqtt_server("mqtt server address", "mqtt server address", "192.168.3.154", 30);
  WiFiManagerParameter custom_mqtt_port("mqtt port number", "mqtt port number", "30077", 6);
  WiFiManagerParameter custom_mqtt_topic("mqtt topic string", "mqtt topic string", "home/office/sensor", 60);
  WiFiManagerParameter custom_mqtt_username("mqtt username", "mqtt username", "username", 40);
  WiFiManagerParameter custom_mqtt_password("mqtt password", "mqtt password", "password", 40);

  WiFiManagerParameter custom_sensor_tempOffset("temp Offset", "temp offset by degree", "-12", 3);
  WiFiManagerParameter custom_sensor_humidityOffset("humidity offset", "humidity offset by percent", "0", 3);
  WiFiManagerParameter custom_sensor_refreshDelay("refresh delay", "refresh delay in seconds", "5", 3);
  WiFiManagerParameter custom_sensor_resetFlag("reset flag", "reset flag", "false", 5);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  wifiManager.addParameter(&custom_sensor_tempOffset);
  wifiManager.addParameter(&custom_sensor_humidityOffset);
  wifiManager.addParameter(&custom_sensor_refreshDelay);
  wifiManager.addParameter(&custom_sensor_resetFlag);

  if(localConfig.sensorDetails.resetFlag) {
    wifiManager.startConfigPortal("Alltop Smart Device");
  }else{
    wifiManager.autoConnect("Alltop Smart Device");
  }

  Serial.println("Connected to wifi.");

  strcpy(localConfig.clientDetails.deviceName, "Office Temperature/Humidity Sensor");
  
  String ip = WiFi.localIP().toString();
  int str_len = ip.length() + 1; 
  char char_buffer[str_len];
  ip.toCharArray(char_buffer, str_len);
  strlcpy(localConfig.clientDetails.ipAddress, char_buffer, sizeof(localConfig.clientDetails.ipAddress));

  strcpy(localConfig.mqttDetails.serverAddress, custom_mqtt_server.getValue());
  localConfig.mqttDetails.port = atoi(custom_mqtt_port.getValue());
  strcpy(localConfig.mqttDetails.topic, custom_mqtt_topic.getValue());
  strcpy(localConfig.mqttDetails.username, custom_mqtt_username.getValue());
  strcpy(localConfig.mqttDetails.password, custom_mqtt_password.getValue());

  localConfig.sensorDetails.tempOffset = atoi(custom_sensor_tempOffset.getValue());
  localConfig.sensorDetails.humidityOffset = atoi(custom_sensor_humidityOffset.getValue());
  localConfig.sensorDetails.refreshDelay = atoi(custom_sensor_refreshDelay.getValue());
  if((char)custom_sensor_resetFlag.getValue()[0] == 't') {
      localConfig.sensorDetails.resetFlag = true;
  } else {
      localConfig.sensorDetails.resetFlag = false;
  }

  Serial.print("value from wifi manager: ");
  Serial.println(custom_sensor_resetFlag.getValue());
  Serial.print("value from local config ");
  Serial.println(localConfig.sensorDetails.resetFlag);
}

void setup() {
  Serial.begin(115200);
  delay(3000); //Temp fix why is the while loop not working????
  while(!Serial);

  Serial.println();
  Serial.println("Start of setup."); 
  while (!LittleFS.begin()) {
    Serial.println(F("Error: Failed to initialize LittleFS...Trying again."));
    delay(1000);
  }

  if(localConfig.sensorDetails.resetFlag) {
    Serial.println("Format LittleFS"); 
    LittleFS.format(); 
  }else {
    Serial.println("Don't need to format LittleFS."); 
  }


//  if(!readFromFile(configfilename, localConfig)) {
//    writeToFile(configfilename, localConfig);
//  }

  readFromFile(configfilename, localConfig);
  
  wifiManagerSetup();
  
  writeToFile(configfilename, localConfig);
  Serial.println("File after wifi setup:");
  readFromFile(configfilename, localConfig);

  LittleFS.end();

  client.setServer(localConfig.mqttDetails.serverAddress, localConfig.mqttDetails.port);
  dht.setup(2, DHTesp::DHT11);
  Serial.println("End of setup.");
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");

  static char msgPayload[129];
  if (length > 128) {
    length = 128;
  }
  strncpy(msgPayload, (char *)message, length);
  msgPayload[length] = '\0';
  
  Serial.printf("message received: %s\n", topic, message);
  Serial.println();
  
  if (String(topic) == String(buffTemp)) {
    localConfig.sensorDetails.tempOffset = atoi(msgPayload);
    Serial.print("tempOffset set to: ");
    Serial.println(localConfig.sensorDetails.tempOffset);
    updateFile();
  }
  
  if (String(topic) == buffHumid) {
    localConfig.sensorDetails.humidityOffset = atoi(msgPayload);
    Serial.print("humidityOffset set to: ");
    Serial.println(localConfig.sensorDetails.humidityOffset);
    updateFile();
  }
  
  if (String(topic) == buffDelay) {
    localConfig.sensorDetails.refreshDelay = atoi(msgPayload);
    Serial.print("refreshDelay set to:");
    Serial.println(localConfig.sensorDetails.refreshDelay);
    updateFile();
  }

   if (String(topic) == buffReset) {
    Serial.print("Reseting device.");
    if((char)message[0] == 't') {
      localConfig.sensorDetails.resetFlag = true;
    } else {
      localConfig.sensorDetails.resetFlag = false;
    }
    updateFile();
    if(localConfig.sensorDetails.resetFlag) {
        wifiManagerSetup();
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(localConfig.clientDetails.deviceName, localConfig.mqttDetails.username, localConfig.mqttDetails.password)) {
      Serial.println("connected to MQTT server.");

      strcpy(buffTemp, localConfig.mqttDetails.topic);
      strcpy(buffHumid, localConfig.mqttDetails.topic);
      strcpy(buffDelay, localConfig.mqttDetails.topic);
      strcpy(buffReset, localConfig.mqttDetails.topic);

      strcat(buffTemp, "/controls/tempOffset");
      strcat(buffHumid, "/controls/humidityOffset");
      strcat(buffDelay, "/controls/refreshDelay");
      strcat(buffReset, "/controls/reset");

      client.subscribe(buffTemp);
      client.subscribe(buffHumid);
      client.subscribe(buffDelay);
      client.subscribe(buffReset);
      
      Serial.print("Subscribed to topic: ");
      Serial.println(buffTemp);
      Serial.print("Subscribed to topic: ");
      Serial.println(buffHumid);
      Serial.print("Subscribed to topic: ");
      Serial.println(buffDelay);
      Serial.print("Subscribed to topic: ");
      Serial.println(buffReset);
      
      client.setCallback(callback);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void loop() {
  client.setServer(localConfig.mqttDetails.serverAddress, localConfig.mqttDetails.port);
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  delay(dht.getMinimumSamplingPeriod() + 1000);

  float humidity = dht.getHumidity() + localConfig.sensorDetails.humidityOffset;
  float temperatureC = dht.getTemperature();
  float temperatureF = dht.toFahrenheit(temperatureC) + localConfig.sensorDetails.tempOffset;

   char buf[70];
   strcpy(buf,localConfig.mqttDetails.topic);
   strcat(buf,"/temperature");
   client.publish(buf, String(temperatureF).c_str(), true);

   char bufHumid[70];
   strcpy(bufHumid,localConfig.mqttDetails.topic);
   strcat(bufHumid,"/humidity");
   client.publish(bufHumid, String(humidity).c_str(), true);

   delay(localConfig.sensorDetails.refreshDelay * 1000);
}
