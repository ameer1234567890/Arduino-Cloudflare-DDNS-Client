#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <time.h>
#include "Secrets.h"

/* Secrets.h file should contain data as below: */
#ifndef WIFI_SSID
#define WIFI_SSID "xxxxxxxxxx"
#define WIFI_PASSWORD "xxxxxxxxxx"
#define EMAIL "your.email@gmail.com" // email address registered at Cloudflare
#define TOKEN "cloudflare-api-token" // Global API key from https://dash.cloudflare.com/profile
#define SUBDOMAIN "hello" // subdomain in: hello.example.com
#define ZONE_ID "" // refer README on how to obtain this value
#define REC_ID "" // refer README on how to obtain this value
#define IFTTT_URL "http://maker.ifttt.com/trigger/ip_changed/with/key/xxxxxxxxxxxxx" // IFTTT webhook url
#endif

/* Configurable variables */
#define SERVER_PORT 80
#define OTA_HOSTNAME "DDNSClient"
const int DST = 0;
const int LED_PIN = 0;
const int TIMEZONE = 5;
const int INTERVAL = 1000 * 60; // 60 seconds
const String IP_URL = "http://ipv4.icanhazip.com"; // URL to get public IP address

/* Do not change these unless you know what you are doing */
String newIP;
String oldIP;
String logMsg;
String logTime;
uint apiPort = 443;
int errorCount = 0;
uint eepromAddr = 0;
bool apiIsTLS = true;
bool lineFeed = true;
unsigned long lastMillis = 0;
String apiHost = "api.cloudflare.com";
String apiURL = "/client/v4/zones/" + String(ZONE_ID) + "/dns_records/" + String(REC_ID);
typedef struct { 
  uint firstOctet;
  uint secondOctet;
  uint thirdOctet;
  uint fourthOctet;
} eepromData;
eepromData IPData;

HTTPClient http;
WiFiClient wClient;
WiFiClientSecure wClientSecure;
ESP8266WebServer server(SERVER_PORT);


void setup() {
  Serial.begin(115200);
  log("I/system: startup");
  pinMode(LED_PIN, OUTPUT);
  setupWifi();
  setupTime();
  server.on("/", []() {
    server.send(200, "text/html", "\
      <a href=\"/log\">/log</a><br>\
      <a href=\"/reboot\">/reboot</a><br>\
    ");
    log("I/server: served / to " + server.client().remoteIP().toString());
  });
  server.on("/log", []() {
    server.send(200, "text/plain", logMsg);
  });
  server.on("/reboot", []() {
    server.send(200, "text/plain", "rebooting");
    delay(1000);
    ESP.restart();
  });
  server.begin();
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();
  EEPROM.begin(512);
  EEPROM.get(eepromAddr, IPData);
  oldIP = String(IPData.firstOctet) + "." + String(IPData.secondOctet) + "." + String(IPData.thirdOctet) + "." + String(IPData.fourthOctet);
  log("I/system: old IP address read from EEPROM => " + oldIP);
  checkDNS();
  lastMillis = millis();
}


void loop() {
  if (millis() > lastMillis + INTERVAL) {
    lastMillis = millis();
    if(WiFi.status() == WL_CONNECTED) {
      checkDNS();
    } else {
      setupWifi();
      checkDNS();
    }
  }
  if (errorCount > 3) {
    ESP.restart();
  }
  server.handleClient();
  ArduinoOTA.handle();
}

void log(String msg) {
  if (!lineFeed) {
    lineFeed = true;
    logMsg = logMsg + "\n";
  }
  time_t now = time(0);
  logTime = ctime(&now);
  logTime.trim();
  logMsg = logMsg + "[" + logTime + "] ";
  logMsg = logMsg + msg + "\n";
  Serial.println(msg);
}


void logContinuous(String msg) {
  if (lineFeed) {
    lineFeed = false;
    time_t now = time(0);
    logTime = ctime(&now);
    logTime.trim();
    logMsg = logMsg + "[" + logTime + "] ";
    logMsg = logMsg + "I/checkr: IP unchanged" + msg;
    Serial.print("I/checkr: IP unchanged" + msg);
  } else {
    logMsg = logMsg + msg;
    Serial.print(msg);
  }
}


void setupTime() {
  configTime(TIMEZONE * 3600, DST, "pool.ntp.org", "time.nist.gov");
  log("I/time  : waiting for time");
  while (!time(nullptr)) {
    delay(100);
  }
  delay(100);
  time_t now = time(0);
  logTime = ctime(&now);
  logTime.trim();
  log("I/time  : time obtained via NTP => " + logTime);
}


void checkDNS() {
  http.begin(wClient, IP_URL);
  int httpCode = http.GET();
  newIP = http.getString();
  newIP.trim();
  if (httpCode == HTTP_CODE_OK) {
    if (newIP == oldIP) {
      logContinuous(".");
    } else {
      log("I/checkr: IP changed! current IP => " + newIP + ". updating DNS...");
      updateDNS();
    }
  } else {
    errorCount++;
    log("E/checkr: HTTP status code => " + String(httpCode));
    log("E/checkr: HTTP response => " + newIP);
  }
  http.end();
}


void updateDNS() {
  String reqData = "{\"type\":\"A\",\"name\":\"" + String(SUBDOMAIN) + "\",\"content\":\"" + newIP + "\",\"proxied\":false}";
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, apiHost, apiPort, apiURL, apiIsTLS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Auth-Key", TOKEN);
  http.addHeader("X-Auth-Email", EMAIL);
  http.addHeader("Content-Length", String(reqData.length()));
  int httpCode = http.PUT(reqData);
  String httpResponse = http.getString();
  http.end();
  if (httpCode == HTTP_CODE_OK) {
    log("I/updatr: HTTP status code => " + String(httpCode));
    log("I/updatr: HTTP response => " + httpResponse);
    oldIP = newIP;
    int firstDot = newIP.indexOf(".");
    int secondDot = newIP.indexOf(".", firstDot + 1);
    int thirdDot = newIP.indexOf(".", secondDot + 1);
    IPData.firstOctet = newIP.substring(0, firstDot).toInt();
    IPData.secondOctet = newIP.substring(firstDot + 1, secondDot).toInt();
    IPData.thirdOctet = newIP.substring(secondDot + 1, thirdDot).toInt();
    IPData.fourthOctet = newIP.substring(thirdDot + 1, newIP.length()).toInt();
    EEPROM.put(eepromAddr, IPData);
    EEPROM.commit();
    notify();
  } else {
    log("E/updatr: HTTP status code => " + String(httpCode));
    log("E/updatr: HTTP response => " + httpResponse);
  }
}


void notify() {
  http.begin(wClient, IFTTT_URL);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    log("I/notify: notified via IFTTT");
  } else {
    log("E/notify: notifying via IFTTT failed");
  }
  http.end();
}


void setupWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
  Serial.println("");
  log("I/wifi  : WiFi connected. IP address: " + WiFi.localIP().toString());
  delay(700);
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
}
