

// Import required libraries
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include <SPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Update.h>
///calculation constants
#define MILLIS_IN_HR 60000
#define SEC_IN_HR 3600

// Replace with your network credentials
const char* ssid = "ScoopNet";
const char* password = "lockontractorbeam";
const char* fallbackssid     = "Nixie-Access-Point";
const char* fallbackpassword = "123456789";

String sliderValue = "0";
const char* PARAM_INPUT = "value";
bool shouldReboot = false;
//Pin Definitions
const int hvPin = 26;
const int ledPin = 2;
const int led2Pin = 15;
const int reset_n = 17;
static const int spiClk = 1000000;
//changeable time/NTP settings
int NTPupdateMinutes = 5;
int timezone = -6;
//BCD ouput variables for display message
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

// Stores LED state
String ledState;

//pointer to spi object
SPIClass * vspi = NULL;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncEventSource events("/events"); // event source for updating time on webpage
WiFiUDP ntpUDP; 
NTPClient timeClient(ntpUDP);
//function prototypes
void HardwareSetup();
void WifiSetup();
void NTPClientSetup();
void WebServerSetup();

void setup(){
  
  HardwareSetup();
  WifiSetup();
  NTPClientSetup();
  WebServerSetup();

}
 
void HardwareSetup(){
   // Serial port for debugging purposes
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(led2Pin, OUTPUT);
  pinMode(hvPin, OUTPUT);
  pinMode(reset_n, OUTPUT);
  
  digitalWrite(led2Pin,LOW); //turn off bright LEDs
  digitalWrite(hvPin,LOW); //turn on HV
  digitalWrite(reset_n, HIGH); //take FPGA out of reset
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

void WifiSetup()
{
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  int i = 0;
  for(i=0;(i<20&&(WiFi.status() != WL_CONNECTED));i++){ // wait for wifi to connect for up to 10 seconds
    delay(500);
    Serial.print(i);
  }
  if(WiFi.status() != WL_CONNECTED){
     //Setup Fallback AP
    Serial.print("Setting AP (Access Point)…");
    // Remove the password parameter, if you want the AP (Access Point) to be open
    WiFi.softAP(ssid, password);
  
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
  timeClient.setTimeOffset(SEC_IN_HR*timezone);
  timeClient.setUpdateInterval(MILLIS_IN_HR*NTPupdateMinutes);
  timeClient.setPoolServerName("time.nist.gov");
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
    digitalWrite(ledPin, HIGH);
    digitalWrite(hvPin, HIGH);    
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  // Route to set GPIO to LOW
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, LOW);
    digitalWrite(hvPin, LOW);    
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
// Send a GET request to <ESP_IP>/slider?value=<inputMessage>
  server.on("/slider", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    // GET input1 value on <ESP_IP>/slider?value=<inputMessage>
    if (request->hasParam(PARAM_INPUT)) {
      inputMessage = request->getParam(PARAM_INPUT)->value();
      sliderValue = inputMessage;
      brightness = sliderValue.toInt();
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

void loop(){

  sprintf(message, "%i%i:%i%i:%i%i", hours1, hours0, minutes1, minutes0, seconds1, seconds0);
  events.send(message,"time",millis());
  seconds = timeClient.getSeconds();
  seconds0 = seconds % 10;
  seconds1 = seconds / 10;
  minutes = timeClient.getMinutes();
  minutes0 = minutes % 10;
  minutes1 = minutes / 10;
  hours = timeClient.getHours();
  hours0 = hours % 10;
  hours1 = hours / 10;
  writeDisplay((hours1 << 4 | hours0), (minutes1 << 4 | minutes0),(seconds1 << 4 | seconds0), brightness);
  timeClient.update();
  // char debug[25];
  // sprintf(debug, "%i|%i::%i|%i::%i|%", hours1, hours0, minutes1, minutes0, seconds1, seconds0);
  // Serial.println(debug);
  delay(1000);

}

// Replaces placeholder with LED state value
String processor(const String& var){
  Serial.println(var);
  if (var == "SLIDERVALUE"){
    return sliderValue;
  }
  else if(var == "STATE"){
    if(digitalRead(hvPin)){
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