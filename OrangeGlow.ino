

// Import required libraries
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include <SPI.h>
#include <NTPClient.h>
//#include <timezone.h>
#include <WiFiUdp.h>
#include <Update.h>
#include <ArduinoJson.h>
//calculation constants
#define MILLIS_IN_MIN 60000
#define SEC_IN_HR 3600


struct Config {
  char ssid [60] = "YOUR SSID HERE";// Replace with your network credentials
  char password [60] = "YOUR PW HERE";
  char fallback_ssid [60]     = "Nixie-Access-Point";
  char fallback_password [60] = "123456789";
  int NTPupdatefreq_minutes = 5;
  int timezone = -6;
  char NTPServerPool[60] = "time.nist.gov";
  bool pulseLEDs = false;
  bool twelveHourMode = false;
  bool LeadingZero = true;
  int brightness = 255;
  int LEDBright = 0;
};

String BsliderValue = "255";
String LsliderValue = "0";
String TimezoneValue = "-6";
const char* PARAM_INPUT = "value";
bool shouldReboot = false;
bool saveConfigFlag = false;
//Pin Definitions
const int hvPin = 26;
const int ledPin = 2;
const int led2Pin = 15;
const int reset_n = 17;
static const int spiClk = 1000000;
//led PWM vars
const int freq = 5000;
const int ledChannel = 0;
const int resolution = 8;

//changeable time/NTP settings

//BCD ouput variables for display message
int curr_hour = 0;
int hours0 = 0x00;
int minutes0 = 0x00;
int seconds0 = 0x00;
int hours1 = 0x00;
int minutes1 = 0x00;
int seconds1 = 0x00;
int brightness = 0xFF;
//actual time integers
int hours = 0;
int minutes = 0;
int seconds = 0;
//cstring for sending webpage time display
char message[25];

bool NTPUD = false;
//pointer to spi object
SPIClass * vspi = NULL;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events"); // event source for updating time on webpage
WiFiUDP ntpUDP; 
NTPClient timeClient(ntpUDP);
Config config;
//function prototypes
void HardwareSetup();
void WifiSetup();
void NTPClientSetup();
void WebServerSetup();
void LoadConfig();
void SaveConfig();

void setup(){
  
  HardwareSetup();
  LoadConfig();
  WifiSetup();
  NTPClientSetup();
  WebServerSetup();

}

void HardwareSetup()
{
   // Serial port for debugging purposes
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(led2Pin, OUTPUT);
  pinMode(hvPin, OUTPUT);
  pinMode(reset_n, OUTPUT);
  
  digitalWrite(led2Pin,LOW); //turn off bright LEDs
  digitalWrite(hvPin,LOW); //turn on HV
  digitalWrite(reset_n, HIGH); //take FPGA out of reset
   
   // configure LED PWM functionalitites
  ledcSetup(ledChannel, freq, resolution);
  
  // attach the channel to the GPIO to be controlled
  ledcAttachPin(led2Pin, ledChannel);
  
  ledcWrite(ledChannel, LsliderValue.toInt());
   //init spi
   vspi = new SPIClass(VSPI);
   //initialise vspi with default pins
  //SCLK = 18, MISO = 19, MOSI = 23, SS = 5
   vspi->begin();
    pinMode(5, OUTPUT); //VSPI SS


  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
}
void LoadConfig()
{
  File file = SPIFFS.open("/config.json", FILE_READ);
 
  if (!file) {
    Serial.println("There was an error opening the config file for reading");
    return;
  }
  StaticJsonDocument<512> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
    if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  strlcpy(config.ssid, doc["ssid"], sizeof(config.ssid));
  strlcpy(config.password, doc["password"], sizeof(config.password));
  strlcpy(config.fallback_ssid, doc["fallback_ssid"], sizeof(config.fallback_ssid));
  strlcpy(config.fallback_password, doc["fallback_password"], sizeof(config.fallback_password));
  config.timezone = doc["timezone"];
  config.NTPupdatefreq_minutes = doc["NTPupdatefreq_minutes"];
  strlcpy(config.NTPServerPool, doc["NTPServerPool"],sizeof(config.NTPServerPool));
  config.twelveHourMode = doc["twelveHourMode"];
  config.LeadingZero = doc["LeadingZero"];
  config.pulseLEDs = doc["pulseLEDs"];
  file.close();
  Serial.println("Config.json loaded successfully");
  Serial.println(config.ssid);
  Serial.println(config.password);
  Serial.println(config.fallback_ssid);
  Serial.println(config.fallback_password);
  Serial.println(config.timezone);
  Serial.println(config.NTPupdatefreq_minutes);
  Serial.println(config.NTPServerPool);
  Serial.println(config.twelveHourMode);
  Serial.println(config.LeadingZero);
  Serial.println(config.pulseLEDs);
}
void SaveConfig()
{
  File file = SPIFFS.open("/config.json", FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create file"));
    return;
  }
  StaticJsonDocument<512> doc;

  //build doc
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["fallback_ssid"] = config.fallback_ssid;
  doc["fallback_password"] = config.fallback_password;
  doc["timezone"] = config.timezone;
  doc["NTPupdatefreq_minutes"] = config.NTPupdatefreq_minutes;
  doc["NTPServerPool"] = config.NTPServerPool;
  doc["twelveHourMode"] = config.twelveHourMode;
  doc["LeadingZero"] = config.LeadingZero;
  doc["pulseLEDs"] = config.pulseLEDs;


  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  file.close();
  Serial.println("Config.json saved successfully");
}
void WifiSetup()
{
  // Connect to Wi-Fi
  WiFi.begin(config.ssid, config.password);
  int i = 0;
  for(i=0;(i<20&&(WiFi.status() != WL_CONNECTED));i++){ // wait for wifi to connect for up to 10 seconds
    delay(500);
    Serial.print(i);
  }
  if(WiFi.status() != WL_CONNECTED){
     //Setup Fallback AP
    Serial.print("Setting AP (Access Point)…");
    // Remove the password parameter, if you want the AP (Access Point) to be open
    WiFi.softAP(config.fallback_ssid, config.fallback_password);
  
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
  }
  else{
    // Print local IP address and start web server
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
  }
  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());
}

void NTPClientSetup()
{
  timeClient.begin();
  timeClient.setTimeOffset(config.timezone*SEC_IN_HR);
  timeClient.setUpdateInterval(config.NTPupdatefreq_minutes*MILLIS_IN_MIN);
  timeClient.setPoolServerName(config.NTPServerPool);
}

void WebServerSetup()
{
  events.onConnect([](AsyncEventSourceClient *client){
  if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it gat is: %u\n", client->lastId());
    }
    //send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!",NULL,millis(),1000);
  });
  //HTTP Basic authentication
  events.setAuthentication("user", "pass");
  server.addHandler(&events);
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });
  // Route to set GPIO to HIGH
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    config.pulseLEDs = false; 
    ledcWrite(ledChannel, 255);  
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  // Route to set GPIO to LOW
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    config.pulseLEDs = false;
    ledcWrite(ledChannel, 0);
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/pulse", HTTP_GET, [](AsyncWebServerRequest *request){
    config.pulseLEDs = true;
    //TODO Later, make magic fading happen with shit LEDC arduino libs
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/HVon", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(hvPin, LOW);    
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/HVoff", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(hvPin, HIGH);    
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/sync", HTTP_GET, [](AsyncWebServerRequest *request){
    timeClient.forceUpdate();    
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/mil", HTTP_GET, [](AsyncWebServerRequest *request){
    config.twelveHourMode = !config.twelveHourMode; 
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/zero", HTTP_GET, [](AsyncWebServerRequest *request){
    config.LeadingZero = !config.LeadingZero;
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
    SaveConfig();
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/setTZ", HTTP_GET, [](AsyncWebServerRequest *request){
    String inputMessage;
    // GET input1 value on <ESP_IP>/slider?value=<inputMessage>
    if (request->hasParam(PARAM_INPUT)) {
      inputMessage = request->getParam(PARAM_INPUT)->value();
      TimezoneValue = inputMessage;
      config.timezone = TimezoneValue.toInt();
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    timeClient.setTimeOffset(config.timezone*SEC_IN_HR); //refresh timeclient settings (saves a reboot)
    request->send(200, "text/plain", "OK");
  });
// Send a GET request to <ESP_IP>/slider?value=<inputMessage>
  server.on("/Bslider", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    // GET input1 value on <ESP_IP>/slider?value=<inputMessage>
    if (request->hasParam(PARAM_INPUT)) {
      inputMessage = request->getParam(PARAM_INPUT)->value();
      BsliderValue = inputMessage;
      config.brightness = BsliderValue.toInt();
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(200, "text/plain", "OK");
  });
  server.on("/Lslider", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    // GET input1 value on <ESP_IP>/slider?value=<inputMessage>
    if (request->hasParam(PARAM_INPUT)) {
      inputMessage = request->getParam(PARAM_INPUT)->value();
      LsliderValue = inputMessage;
      if(config.pulseLEDs == false){
        ledcWrite(ledChannel, LsliderValue.toInt());
      }
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(200, "text/plain", "OK");
  });
  //wifi scan page
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
  String json = "[";
  int n = WiFi.scanComplete();
  if(n == -2){
    WiFi.scanNetworks(true);
  } else if(n){
    for (int i = 0; i < n; ++i){
      if(i) json += ",";
      json += "{";
      json += "\"rssi\":"+String(WiFi.RSSI(i));
      json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
      json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
      json += ",\"channel\":"+String(WiFi.channel(i));
      json += ",\"secure\":"+String(WiFi.encryptionType(i));
      json += ",\"hidden\":"+String("false");
      json += "}";
    }
    WiFi.scanDelete();
    if(WiFi.scanComplete() == -2){
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  request->send(200, "application/json", json);
  json = String();
});

// Simple Firmware Update Form
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      //Update.runAsync(true);
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  // Start server
  server.begin();
} 

void loop()
  {
    seconds = timeClient.getSeconds();
    minutes = timeClient.getMinutes();
    hours = timeClient.getHours();
    if(config.twelveHourMode){
      if(hours > 12)
        hours = hours - 12;
      
    }
    seconds0 = seconds % 10;
    seconds1 = seconds / 10;
    minutes0 = minutes % 10;
    minutes1 = minutes / 10;
    hours0 = hours % 10;
    hours1 = hours / 10;
    if(config.LeadingZero == false && hours1 == 0){
      hours1 = 0xA; //out of range, will blank digit.
    }
  writeDisplay((hours1 << 4 | hours0), (minutes1 << 4 | minutes0),(seconds1 << 4 | seconds0), brightness);
  sprintf(message, "%i%i:%i%i:%i%i", hours1, hours0, minutes1, minutes0, seconds1, seconds0);
  events.send(message,"time",millis());

  if (hours != curr_hour) {
    NTPUD = timeClient.update();
    curr_hour = hours;

    // char debug[25];
    // sprintf(debug, "%i|%i::%i|%i::%i|%", hours1, hours0, minutes1, minutes0, seconds1, seconds0);
    Serial.print("Time Update Successful? ");
    Serial.println(NTPUD);
  }
  delay(250);
}

// Replaces placeholder with LED state value
String processor(const String& var){
  String ledState;
  String hvState;
  Serial.println(var);
  if (var == "BSLIDERVALUE"){
    return BsliderValue;
  }
  else if (var == "LSLIDERVALUE"){
    return LsliderValue;
  }
  else if(var == "HVSTATE"){
    if(!digitalRead(hvPin)){
      hvState = "ON";
    }
    else{
      hvState = "OFF";
    }
    Serial.print(hvState);
    return hvState;
  }
  else if(var == "LEDSTATE"){
    if(ledcRead(ledChannel) != 0){
      ledState = "ON";
    }
    else{
      ledState = "OFF";
    }
    Serial.print(ledState);
    return ledState;
  }
  return String();
}

//function for writing to display FPGA over SPI
//params:hours, minutes, seconds bytes each split into 4 bit nybble with 0x0-0x9 for each individual display digit.
//params:Brightness value 0-255

 void writeDisplay(byte hours, byte minutes, byte seconds, byte brightness) {
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
  digitalWrite(5, LOW); //pull SS slow to prep other end for transfer
  vspi->transfer(hours);
  vspi->transfer(minutes);
  vspi->transfer(seconds);
  vspi->transfer(brightness);  
  digitalWrite(5, HIGH); //pull ss high to signify end of data transfer
  vspi->endTransaction();
}