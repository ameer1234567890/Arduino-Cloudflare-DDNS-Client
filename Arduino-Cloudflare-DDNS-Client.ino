#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
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
bool apiIsTLS = true;
unsigned long lastMillis = 0;
String apiHost = "api.cloudflare.com";
String apiURL = "/client/v4/zones/" + String(ZONE_ID) + "/dns_records/" + String(REC_ID);

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
    log("I/server: served /log to " + server.client().remoteIP().toString());
  });
  server.on("/reboot", []() {
    server.send(200, "text/plain", "rebooting");
    delay(1000);
    ESP.restart();
  });
  server.begin();
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();
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
  server.handleClient();
  ArduinoOTA.handle();
}


void log(String msg) {
  time_t now = time(0);
  logTime = ctime(&now);
  logTime.trim();
  logMsg = logMsg + "[" + logTime + "] ";
  logMsg = logMsg + msg + "\n";
  Serial.println(msg);
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
  if (httpCode == HTTP_CODE_OK) {
    newIP = http.getString();
    newIP.trim();
    if (newIP == oldIP) {
      log("I/checkr: IP unchanged! current IP => " + newIP);
    } else {
      log("I/checkr: IP changed! current IP => " + newIP + ". updating DNS...");
      updateDNS();
    }
  } else {
    log("E/checkr: " + http.getString());
    http.end();
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
  } else {
    log("E/updatr: HTTP status code => " + String(httpCode));
    log("E/updatr: HTTP response => " + httpResponse);
    log("E/updatr: Error!");
  }
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
