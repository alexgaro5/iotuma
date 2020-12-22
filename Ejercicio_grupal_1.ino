#include <ArduinoJson.h>
#include <Arduino_JSON.h>

#include "DHTesp.h"

#ifdef ESP32
#pragma message(THIS EXAMPLE IS FOR ESP8266 ONLY!)
#error Select ESP8266 board.
#endif

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <ESP8266httpUpdate.h>

// Librería para los botones
#include <Button2.h>
#define BUTTON_PIN  0

// datos para actualización   >>>> SUSTITUIR IP <<<<<
#define HTTP_OTA_ADDRESS      F("192.168.1.114")       // Address of OTA update server
//#define HTTP_OTA_ADDRESS      F("192.168.0.17")        // Address of OTA update server
#define HTTP_OTA_PATH         F("/esp8266-ota/update") // Path to update firmware
#define HTTP_OTA_PORT         1880                     // Port of update server
                                                       // Name of firmware
#define HTTP_OTA_VERSION      String(__FILE__).substring(String(__FILE__).lastIndexOf('\\')+1) + ".nodemcu" 

// Update these with values suitable for your network.

const char* ssid = "WLAN_2G_COLETO";
const char* password = "4eddec6e8465458a4096";
const char* mqtt_server = "192.168.1.114";
//const char* ssid = "vodafone7C70";
//const char* password = "XYGTSS2FMRLC7B";
//const char* mqtt_server = "192.168.0.17";
//const char* mqtt_server = "iot.ac.uma.es";

float led, ledControl;
int flag = 0;   // Flag para ver el número de pulsaciones

int boton_flash = 0;       // GPIO0 = boton flash
// int estado_polling = HIGH; // por defecto HIGH (PULLUP). Cuando se pulsa se pone a LOW
int estado_int = HIGH;     // por defecto HIGH (PULLUP). Cuando se pulsa se pone a LOW

unsigned long inicio_pulso_polling = 0;
unsigned long ultima_int = 0;

// Comunica la interrupción con el loop
volatile int inicio_pulsacion = 0;
volatile int fin_pulsacion = 0;
volatile long inicio = 0;
volatile long fin = 0;

int encendido = 1;

Button2 button = Button2(BUTTON_PIN);

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (128)
char msg[MSG_BUFFER_SIZE];

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

ADC_MODE(ADC_VCC);

/*------------------  RTI  --------------------*/
/*
// Rutina de Tratamiento de la Interrupcion (RTI)
ICACHE_RAM_ATTR void RTI() {
  int lectura = digitalRead(boton_flash);
  unsigned long ahora = millis();
  // descomentar para eliminar rebotes
  if (lectura == estado_int || ahora-ultima_int < 50) return; // filtro rebotes 50ms
  if (lectura == LOW)
  { 
   estado_int = LOW;
   inicio_pulsacion = 1;
   inicio = millis();
   // Serial.print("Int en: ");
   // Serial.println(ahora);
  }
  else
  {
   estado_int = HIGH;
   fin_pulsacion = 1;
   fin = millis();
   // Serial.print("Int dura: ");
   // Serial.println(ahora-ultima_int);
  }
  // ultima_int = ahora;
}
*/
/*------------------  RTI  --------------------*/

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {

  char *mensaje=(char *)malloc(length+1); // reservo memoria para copia del mensaje
  strncpy(mensaje,(char*)payload,length); // copio el mensaje en cadena de caracteres
  
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Configure the intensity
  // compruebo que es el topic adecuado
  if(strcmp(topic,"infind/GRUPO2/led/cmd")==0)
  {
    StaticJsonDocument<512> root; // el tamaño tiene que ser adecuado para el mensaje
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(root, mensaje);

    // Compruebo si no hubo error
    if (error) {
      Serial.print("Error deserializeJson() failed: ");
      Serial.println(error.c_str());
    }
    else
    if(root.containsKey("level"))  // comprobar si existe el campo/clave que estamos buscando
    {
     float valor = root["level"];
     Serial.print("Mensaje OK, level = ");
     Serial.println(valor);

     // Escribimos el valor en el led
     ledControl= led;
     led= 100 - valor;
     
     snprintf (msg, MSG_BUFFER_SIZE, "{\"led\":%f} ", valor);
     client.publish("infind/GRUPO2/led/status", msg);

     // Aumentamos o disminuimos la intensidad del led gradualmente
     while(ledControl != led){
   
      if (ledControl < led) ledControl++;
      else ledControl--;
      analogWrite(BUILTIN_LED, ledControl);
      if (ledControl == 100) encendido = 0;
      else encendido = 1;
            
      delay(10);
     }
    }
    else
    {
      Serial.print("Error : ");
      Serial.println("\"level\" key not found in JSON");
    }
  } // if topic
  else
  {
    Serial.println("Error: Topic desconocido");
  }

  free(mensaje); // libero memoria

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(ESP.getChipId());

    //last will
    byte willQoS = 2;
    const char* willTopic = "infind/GRUPO2/conexion";
    const char* willMessage = "{\"online\":false}";
    boolean willRetain = true;
    boolean cleanSession = true;
    
    // Attempt to connect
    if (client.connect(clientId.c_str(),"infind","zancudo",willTopic, willQoS, willRetain, willMessage, cleanSession)) {

      client.publish("infind/GRUPO2/conexion", "{\"online\":true}",true);
      
      Serial.println("connected");

      //char* msg = "{\"online\":true}"
      
      client.subscribe("infind/GRUPO2/led/cmd");
      
    } else {
      
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      
    }
  }
}

String serializa_JSON1 (struct registro_datos datos)
{
  StaticJsonDocument<300> jsonRoot;
  String jsonString;
 
  jsonRoot["Uptime"]= datos.tiempo;

  jsonRoot["Vcc"]= datos.vcc;
  
  JsonObject DHT11=jsonRoot.createNestedObject("DHT11");
  DHT11["temp"] = datos.temp;
  DHT11["hum"] = datos.hum;

  jsonRoot["LED"]= datos.led;

  JsonObject Wifi=jsonRoot.createNestedObject("Wifi");
  Wifi["SSId"] = datos.ssid;
  Wifi["IP"] = datos.ip.toString();
  Wifi["RSSI"] = datos.rssi;

  serializeJson(jsonRoot,jsonString);
  return jsonString;
}

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
  snprintf(cadena,64,"ERROR: %d",e);
  Serial.println(cadena);
}

void progreso_OTA(int x, int todo)
{
  char cadena[256];
  int progress=(int)((x*100)/todo);
  if(progress%10==0)
  {
    snprintf(cadena,256,"Progreso: %d%% - %dK de %dK",progress,x/1024,todo/1024);
    Serial.println(cadena);
  }
}


void fota() {
  // FOTA
  Serial.println( "--------------------------" );  
  Serial.println( "Comprobando actualización:" );
  Serial.print(HTTP_OTA_ADDRESS);Serial.print(":");Serial.print(HTTP_OTA_PORT);Serial.println(HTTP_OTA_PATH);
  Serial.println( "--------------------------" );  
  ESPhttpUpdate.setLedPin(16,LOW);
  ESPhttpUpdate.onStart(inicio_OTA);
  ESPhttpUpdate.onError(error_OTA);
  ESPhttpUpdate.onProgress(progreso_OTA);
  ESPhttpUpdate.onEnd(final_OTA);
  switch(ESPhttpUpdate.update(HTTP_OTA_ADDRESS, HTTP_OTA_PORT, HTTP_OTA_PATH, HTTP_OTA_VERSION)) {
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

void hacerPulsacionCorta(Button2& btn) {
  if (encendido == 1) {
    analogWrite(BUILTIN_LED, 100);
    led=100;
    encendido = 0;
  } else {
    analogWrite(BUILTIN_LED, ledControl);
    led=ledControl;
    encendido = 1;
  }
}

void hacerPulsacionDoble(Button2& btn) {
  analogWrite(BUILTIN_LED, 0);
  led=0;
  ledControl=0;
}

void hacerPulsacionLarga(Button2& btn) {
  fota();
}

void setup() {

  dht.setup(5, DHTesp::DHT11); // Connect DHT sensor to GPIO 15 Hay que mirar en el esquema que GPIO es
  
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  led=0;
  ledControl=0;
  analogWriteRange(100);
  analogWrite(BUILTIN_LED, led); 
  
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  button.setClickHandler(hacerPulsacionCorta);
  button.setLongClickHandler(hacerPulsacionLarga);
  button.setDoubleClickHandler(hacerPulsacionDoble);

  /*
  // FOTA
  Serial.println( "--------------------------" );  
  Serial.println( "Comprobando actualización:" );
  Serial.print(HTTP_OTA_ADDRESS);Serial.print(":");Serial.print(HTTP_OTA_PORT);Serial.println(HTTP_OTA_PATH);
  Serial.println( "--------------------------" );  
  ESPhttpUpdate.setLedPin(16,LOW);
  ESPhttpUpdate.onStart(inicio_OTA);
  ESPhttpUpdate.onError(error_OTA);
  ESPhttpUpdate.onProgress(progreso_OTA);
  ESPhttpUpdate.onEnd(final_OTA);
  switch(ESPhttpUpdate.update(HTTP_OTA_ADDRESS, HTTP_OTA_PORT, HTTP_OTA_PATH, HTTP_OTA_VERSION)) {
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
  */

  // PULSACIONES
  //pinMode(boton_flash, INPUT_PULLUP);
  // descomentar para activar interrupción
  // attachInterrupt(digitalPinToInterrupt(boton_flash), RTI, CHANGE);

}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  button.loop();

  // Aumentar a cinco minutos.
  unsigned long now = millis();
  if (now - lastMsg > 10000) {

    delay(dht.getMinimumSamplingPeriod());

    float humidity = dht.getHumidity();
    float temperature = dht.getTemperature();

    lastMsg = now;

    datos.tiempo = millis();
    datos.vcc = (float)ESP.getVcc()/(float)1000;
    datos.temp = temperature;
    datos.hum = humidity;
    datos.ssid = WiFi.SSID();
    datos.ip = WiFi.localIP();
    datos.rssi = WiFi.RSSI();
    datos.led = 100 - led; //Lo invertimos para que 0 sea el mínimo y 100 el máximo
    
    Serial.println("JSON generado con ArduinoJson:");
    Serial.println(serializa_JSON1(datos));

    client.publish("infind/GRUPO2/datos", serializa_JSON1(datos).c_str());

    /* ---------------  PULSACIONES  --------------- */
    /*
    if (inicio_pulsacion == 1) {
      Serial.print("Se detectó una pulsación en: ");
      Serial.print(inicio);
      Serial.println(" ms");
      inicio_pulsacion = 0;
      */
      /*
      if (flag == 1) {
        // PULSACION DOBLE: SE enciende nivel maximo
        flag = 0;
        Serial.println("Pulsación corta 2");
      } else {
        flag = 1;
      }
      */
    //}
    /*
    if (fin_pulsacion == 1) {
      Serial.print("Terminó la pulsación en: ");
      Serial.print(fin);
      Serial.print(" ms (duración: ");
      Serial.print(fin - inicio);
      Serial.println(" ms)");
      fin_pulsacion = 0;
    }
    */

    /*
    // Pulsación prolongada
    if ((fin - inicio) > 2000) {
      fota();
      flag = 0;
      Serial.println("Pulsación larga");
    } else {
      if (flag == 1 && (millis() - inicio) > 2000) {
        // APAGAMOS o ENCENDEMOS CON ESTADO ANTERIOR
        flag = 0;
        Serial.println("Pulsación corta 1");
      }
    }
    */
    /* ---------------  PULSACIONES  --------------- */
  }
}
