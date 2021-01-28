
/* ============================ Librerías ============================ */

#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>

#include <ArduinoJson.h>
#include <Arduino_JSON.h>

#include <PubSubClient.h>

#include "DHTesp.h"

#include <Button2.h>

#include <WiFiManager.h>
#include <EEPROM.h>

/* ============================= Configuración ============================= */

ADC_MODE(ADC_VCC);

struct registro_datos {
  float temp;
  float hum;
  unsigned long tiempo;
  String ssid;
  IPAddress ip;
  String rssi;
  float led;
  float vcc;
  };

struct registro_datos datos;

DHTesp dht;

int datosTime; //Tiempo del ciclo de envio de datos

/* ---------------------------------- OTA ---------------------------------- */

// datos para actualización   >>>> SUSTITUIR IP <<<<<
//#define HTTP_OTA_ADDRESS      F("192.168.0.193")       // Address of OTA update server
#define HTTP_OTA_ADDRESS      F("iot.ac.uma.es")       // Address of uma
#define HTTP_OTA_PATH         F("/esp8266-ota/update") // Path to update firmware
#define HTTP_OTA_PORT         1880                     // Port of update server
// Name of firmware
#define OTA_URL "https://iot.ac.uma.es:1880/esp8266-ota/update"// Address of OTA update server
//#define OTA_URL "http://192.168.0.193:1880/esp8266-ota/update"// Address of OTA update server

#define HTTP_OTA_VERSION      String(__FILE__).substring(String(__FILE__).lastIndexOf('\\') + 1) + ".nodemcu"
String OTAfingerprint("5d 56 09 5c 5f 7b a4 3f 01 b7 22 31 d3 a7 da a3 6e 10 2e 60"); // sustituir valor

//MAC D8:F1:5B:11:21:3C

/*
  Modos de OTA:
  0 -> OTA al iniciar el dispositivo
  1 -> OTA al recibir mensaje MQTT / pulsando boton en la placa
  2 -> OTA cada X ms
*/

int otaMode = 0;
float otaTime = 0;
float lastTime = 0;
unsigned long lastMsg = 0;

/* ---------------------------------- WIFI ---------------------------------- */

WiFiClient espClient;

// Creamos una instancia de la clase WiFiManager
// Ampliacion
WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field; // global param ( for non blocking w params )

int wifiStatus = 0;

/* ---------------------------------- MQTT ---------------------------------- */

PubSubClient client(espClient);
const char* mqtt_server = "iot.ac.uma.es";
// const char* mqtt_server = "192.168.0.193";

/* ------------------------------- Pulsaciones ------------------------------ */
#define BUTTON_PIN  0
Button2 button = Button2(BUTTON_PIN);

/* ----------------------------------- LED ---------------------------------- */

float led, ledControl, ledTime;
int encendido = 1;

/* ------------------------------- MSG mostrar por pantalla ------------------------------ */

#define MSG_BUFFER_SIZE  (128)
char msg[MSG_BUFFER_SIZE];

/* =============================== Funciones ================================ */

/* ---------------------------------- OTA ----------------------------------- */

void progreso_OTA(int, int);
void final_OTA();
void inicio_OTA();
void error_OTA(int);
void checkOTA();

void final_OTA()
{
  Serial.println("Fin OTA. Reiniciando...");
}

void inicio_OTA()
{
  Serial.println("Nuevo Firmware encontrado. Actualizando...");
}

void error_OTA(int e)
{
  char cadena[64];
  snprintf(cadena, 64, "ERROR: %d", e);
  Serial.println(cadena);
}

void progreso_OTA(int x, int todo)
{
  char cadena[256];
  int progress = (int)((x * 100) / todo);
  if (progress % 10 == 0)
  {
    snprintf(cadena, 256, "Progreso: %d%% - %dK de %dK", progress, x / 1024, todo / 1024);
    Serial.println(cadena);
  }
}

void checkOTA() {
  Serial.println( "--------------------------" );
  Serial.println( "Comprobando actualización:" );
  //Serial.print(HTTP_OTA_ADDRESS);Serial.print(":");Serial.print(HTTP_OTA_PORT);Serial.println(HTTP_OTA_PATH);
  Serial.println( "--------------------------" );
  ESPhttpUpdate.setLedPin(16, LOW);
  ESPhttpUpdate.onStart(inicio_OTA);
  ESPhttpUpdate.onError(error_OTA);
  ESPhttpUpdate.onProgress(progreso_OTA);
  ESPhttpUpdate.onEnd(final_OTA);
  switch(ESPhttpUpdate.update(OTA_URL, HTTP_OTA_VERSION, OTAfingerprint)) {
  //switch (ESPhttpUpdate.update(OTA_URL, HTTP_OTA_VERSION)) {
    case HTTP_UPDATE_FAILED:
      Serial.printf(" HTTP update failed: Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println(F(" El dispositivo ya está actualizado"));
      break;
    case HTTP_UPDATE_OK:
      Serial.println(F(" OK"));
      break;
  }
}

/* ---------------------------------- WIFI ---------------------------------- */

void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WiFi.SSID());

  WiFi.mode(WIFI_STA);

  if (wifiStatus==0){
    const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
    new (&custom_field) WiFiManagerParameter(custom_radio_str);
  
    wm.addParameter(&custom_field);
    wm.setSaveParamsCallback(saveParamCallback);
  
    std::vector<const char *> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
    wm.setMenu(menu);
    wm.setClass("invert");
  
    Serial.println("Starting config portal");
    wm.setConfigPortalTimeout(120);
    
    if (!wm.startConfigPortal("ConfiguracionWifiESP", "password")) {
      Serial.println("Failed to connect or hit timeout");
      delay(3000);
    } else {
      Serial.println("Connected.");
      String ssidStored = WiFi.SSID();
      EEPROM.write(0, 1);
      EEPROM.commit();
    }
  }else{
    WiFi.begin();
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  
}

void saveParamCallback(){
  Serial.println("[CALLBACK] saveParamCallback fired");
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}

String getParam(String name){
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

/* ---------------------------------- MQTT ---------------------------------- */

void callback(char* topic, byte* payload, unsigned int length);
bool checkTopicFOTA(char* topic, char* mensaje);
bool checkTopicLedCmd(char* topic, char* mensaje);
bool checkTopicConfig(char* topic, char* mensaje);

void callback(char* topic, byte* payload, unsigned int length) {

  char *mensaje = (char *)malloc(length + 1); // reservo memoria para copia del mensaje
  strncpy(mensaje, (char*)payload, length); // copio el mensaje en cadena de caracteres

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  bool known = false;

  if(!known)known = checkTopicFOTA(topic, mensaje);
  if(!known)known = checkTopicLedCmd(topic, mensaje);
  if(!known)known = checkTopicConfig(topic, mensaje);
  
  if (!known)
  {
    Serial.println("Error: Uknown topic");
  }

  free(mensaje); // libero memoria

}

/* -- infind/GRUPO2/ESPX/FOTA -- */

bool checkTopicFOTA(char* topic, char* mensaje) {

  // compruebo que es el topic adecuado
  String topicStr = "infind/GRUPO2/ESP";
  topicStr += ESP.getChipId();
  topicStr += "/FOTA";
  
  if (strcmp(topic, topicStr.c_str()) == 0) {
    
    StaticJsonDocument<512> root; // el tamaño tiene que ser adecuado para el mensaje
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(root, mensaje);

    // Compruebo si no hubo error
    if (error) {
      Serial.print("Error deserializeJson() failed: ");
      Serial.println(error.c_str());
    }else{
      if(root.containsKey("actualiza")){
        checkOTA();
      }
    }    
    return true;
  }
  return false;
}


/* -- infind/GRUPO2/ESPX/config -- */

bool checkTopicConfig(char* topic, char* mensaje) {

  String topicStr = "infind/GRUPO2/ESP";
  topicStr += ESP.getChipId();
  topicStr += "/config";
  
  if (strcmp(topic, topicStr.c_str()) == 0) {
    //Se recibe: {envia, actiualiza, velocidad}

    StaticJsonDocument<512> root; // el tamaño tiene que ser adecuado para el mensaje
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(root, mensaje);

    // Compruebo si no hubo error
    if (error) {
      Serial.print("Error deserializeJson() failed: ");
      Serial.println(error.c_str());
    }else{
      int aux;
      //Comprobamos los campos
      if((root.containsKey("envia")) && (!root["envia"].isNull())){
        //Cambiamos el tiempo de envio de los datos (s)
        datosTime = root["envia"];
/*----------------------Guardo en la flash------------------------------------------*/
        EEPROM.write(25, datosTime);
        EEPROM.commit(); 
/*----------------------------------------------------------------------------------*/
        Serial.print("envia: ");
        Serial.println(datosTime);   
      }          

      if((root.containsKey("actualiza")) && (!root["actualiza"].isNull())){
        //Cambiamos el tiempo de comprobacion de actualizacion (min)
        otaTime = root["actualiza"];
/*----------------------Guardo en la flash------------------------------------------*/
        EEPROM.write(30, otaTime);
        EEPROM.commit(); 
/*----------------------------------------------------------------------------------*/
        Serial.print("actualiza: ");
        Serial.println(otaTime);
      }              

      if((root.containsKey("velocidad")) && (!root["velocidad"].isNull())){
        //Cambiamos el tiempo de cambio del LED (ms)
        ledTime = root["velocidad"];
/*----------------------Guardo en la flash------------------------------------------*/
        EEPROM.write(20, ledTime);
        EEPROM.commit(); 
/*----------------------------------------------------------------------------------*/
        Serial.print("velocidad: ");
        Serial.println(ledTime);            
      }
    }    
    return true;
  }
  return false;
}

/* -- infind/GRUPO2/ESPX/led/cmd -- */

bool checkTopicLedCmd(char* topic, char* mensaje) {

  String topicStr = "infind/GRUPO2/ESP";
  topicStr += ESP.getChipId();
  topicStr += "/led/cmd";
  if (strcmp(topic, topicStr.c_str()) == 0) {
    StaticJsonDocument<512> root; // el tamaño tiene que ser adecuado para el mensaje
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(root, mensaje);

    // Compruebo si no hubo error
    if (error) {
      Serial.print("Error deserializeJson() failed: ");
      Serial.println(error.c_str());
    }
    else if(root.containsKey("level"))  // comprobar si existe el campo/clave que estamos buscando
    {
      float valor = root["level"];
      Serial.print("Mensaje OK, level = ");
      Serial.println(valor);

      // Escribimos el valor en el led
      ledControl = led;
      led = 100 - valor;
     
      snprintf (msg, MSG_BUFFER_SIZE, "{\"led\":%f} ", valor);
      client.publish("infind/GRUPO2/led/status", msg);

      // Aumentamos o disminuimos la intensidad del led gradualmente
     
      while(ledControl != led){   
      if (ledControl < led) ledControl++;
      else ledControl--;
      analogWrite(BUILTIN_LED, ledControl);
      if (ledControl == 100) encendido = 0;
      else encendido = 1;            
      delay(ledTime);
      
     }
    }
    else
    {
      Serial.print("Error : ");
      Serial.println("\"level\" key not found in JSON");
    }

    return true;
  } // if topic
  return false;
}


/* -- setup MQTT -- */

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(ESP.getChipId());

    //last will
    byte willQoS = 2;
    String willTopicStr = "infind/GRUPO2/ESP";
    willTopicStr += ESP.getChipId();
    willTopicStr += "/conexion";
    const char* willTopic = willTopicStr.c_str();
    const char* willMessage = "{\"online\":false}";
    boolean willRetain = true;
    boolean cleanSession = true;
    
    // Attempt to connect
    if (client.connect(clientId.c_str(), "infind", "zancudo", willTopic, willQoS, willRetain, willMessage, cleanSession)) {
    //if (client.connect(clientId.c_str(), "", "", willTopic, willQoS, willRetain, willMessage, cleanSession)) {

      String topic = "infind/GRUPO2/ESP";
      topic += ESP.getChipId();
      topic += "/conexion";      
      client.publish(topic.c_str(), "{\"online\":true}", true);
      Serial.println("connected to MQTT broker");

      // Nos suscribimos a los topics necesarios
      topic = "infind/GRUPO2/ESP";
      topic += ESP.getChipId();
      topic += "/config";
      client.subscribe(topic.c_str());
      Serial.println(topic.c_str());
      topic = "infind/GRUPO2/ESP";
      topic += ESP.getChipId();
      topic += "/led/cmd";
      client.subscribe(topic.c_str());
      Serial.println(topic.c_str());
      topic = "infind/GRUPO2/ESP";
      topic += ESP.getChipId();
      topic += "/switch/cmd";
      client.subscribe(topic.c_str());
      Serial.println(topic.c_str());
      topic = "infind/GRUPO2/ESP";
      topic += ESP.getChipId();
      topic += "/FOTA";
      client.subscribe(topic.c_str());
      Serial.println(topic.c_str());
      
      
    } else {
      
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      
    }
  }
}

/* ---------------------------------- Pulsaciones ---------------------------------- */

// Pulsación corta
void pulsacionCorta(Button2& btn) {
  if (encendido == 1) {
    analogWrite(BUILTIN_LED, 100);
    led = 100;
    encendido = 0;
  } else {
    analogWrite(BUILTIN_LED, ledControl);
    led = ledControl;
    encendido = 1;
  }
}

void pulsacionDoble(Button2& btn) {
  analogWrite(BUILTIN_LED, 0);
  led = 0;
  ledControl = 0;
}

void pulsacionLarga(Button2& btn) {
    checkOTA();
}

void pulsacionTriple(Button2& btn) {
    // Flag a 0 borrar config
    EEPROM.write(0, 0);
    EEPROM.commit();
    Serial.println("Reseteando configuración de WIFI......");
    wm.resetSettings();
    ESP.restart();
    wm.setConfigPortalTimeout(120);

}

/* ===================================== Setup ===================================== */

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(2, HIGH);   // LED off

  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output

  // Inicializamos EEPROM y actualizamos el valor de LED con el que está en la memoria FLASH
  EEPROM.begin(512);
  led = EEPROM.read(13);
  ledTime = EEPROM.read(20);
  datosTime=EEPROM.read(25);
  otaTime=EEPROM.read(30);
  //switch=EEPROM.read(35);
  
  Serial.println("");
  Serial.print("LED: ");
  Serial.println(led);
  ledControl = 0;
  analogWriteRange(100);
  analogWrite(BUILTIN_LED, led); 
  

  // Leemos el wifiStatus de la memoria FLASH
  wifiStatus = EEPROM.read(0);
  setup_wifi();



  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  if (otaMode == 0) {
    checkOTA();
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  button.setClickHandler(pulsacionCorta);
  button.setDoubleClickHandler(pulsacionDoble);
  button.setLongClickHandler(pulsacionLarga);
  button.setTripleClickHandler(pulsacionTriple);
  
}

String serializa_JSON1 (struct registro_datos datos)
{
  StaticJsonDocument<300> jsonRoot;
  String jsonString;
 
  jsonRoot["Uptime"] = datos.tiempo;

  jsonRoot["Vcc"] = datos.vcc;
  
  JsonObject DHT11 = jsonRoot.createNestedObject("DHT11");
  DHT11["temp"] = datos.temp;
  DHT11["hum"] = datos.hum;

  jsonRoot["LED"] = datos.led;

  JsonObject Wifi = jsonRoot.createNestedObject("Wifi");
  Wifi["SSId"] = datos.ssid;
  Wifi["IP"] = datos.ip.toString();
  Wifi["RSSI"] = datos.rssi;

  serializeJson(jsonRoot, jsonString);
  return jsonString;
}

/*--------------------------Errores y Alertas----------------------------------*/
String serializa_JSON2 (struct registro_datos datos)
{
  StaticJsonDocument<300> jsonRoot;
  String jsonString;

  jsonRoot["mensaje"]= "Las lecturas del sensor DHT11 son nulas.";
  jsonRoot["ChipID"]= 123;

  serializeJson(jsonRoot,jsonString);
  return jsonString;
}

String serializa_JSON3 (struct registro_datos datos)
{
  StaticJsonDocument<300> jsonRoot;
  String jsonString;

  jsonRoot["mensaje"]= "El led tiene un valor negativo";
  jsonRoot["ChipID"]= 1234;

  serializeJson(jsonRoot,jsonString);
  return jsonString;
}

/*
String serializa_JSON4 (struct registro_datos datos)
{
  StaticJsonDocument<300> jsonRoot;
  String jsonString;

  jsonRoot["mensaje"]= "El switch esta recibiendo valores distintos de 0 o 1";
  jsonRoot["ChipID"]= datos.chipID;

  serializeJson(jsonRoot,jsonString);
  return jsonString;
}
*/
/*----------------------------------------------------------------------*/



// the loop function runs over and over again forever
void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  button.loop();
  // Aumentar a cinco minutos.
  unsigned long now = millis();
  if (now - lastMsg > datosTime * 1000) {

    delay(dht.getMinimumSamplingPeriod());

    float humidity = dht.getHumidity();
    float temperature = dht.getTemperature();

    lastMsg = now;

    datos.tiempo = millis();
    datos.vcc = (float)ESP.getVcc() / (float)1000;
    datos.temp = temperature;
    datos.hum = humidity;
    datos.ssid = WiFi.SSID();
    datos.ip = WiFi.localIP();
    datos.rssi = WiFi.RSSI();
    datos.led = 100 - led; //Lo invertimos para que 0 sea el mínimo y 100 el máximo
    // Falata poner el ChipID
    
/*----------------Errores y alertas--------------------------------------------*/
    if(isnan((double)datos.temp) && isnan((double)datos.hum)){
    client.publish("infind/GRUPOX/ESPX/errores", serializa_JSON2(datos).c_str());
    Serial.println("ERROR EL SENSOR NO FUNCIONA");
      }
    if(datos.led == 0){
    client.publish("infind/GRUPOX/ESPX/avisos", serializa_JSON3(datos).c_str());
    Serial.println("ERROR EL LED NO FUNCIONA");
      }
    /*Falta la alerta del Switch

    if(datos.switch !=0 || datos.switch != 1){
    client.publish("infind/GRUPOX/ESPX/errores", serializa_JSON4(datos).c_str());
    Serial.println("ERROR EL SWITCH NO FUNCIONA");
      }
*/
/*------------------------------------------------------------------------------*/ 
    EEPROM.write(13, led);
    EEPROM.commit();  
    /*
    EEPROM.write(35, switch);
    EEPROM.commit();  
     */

    
    Serial.println("JSON generado con ArduinoJson:");
    Serial.println(serializa_JSON1(datos));

    String topic = "infind/GRUPO2/ESP";
    topic += ESP.getChipId();
    topic += "/datos";

    client.publish(topic.c_str(), serializa_JSON1(datos).c_str());

  
  }

  if (otaMode == 2) {
    if (millis() - lastTime >= (otaTime * 1000 * 60)){
      checkOTA();
      lastTime = millis();
    }
  }
  
}
