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
#define TOKEN "cloudflare-api-token" // API token from https://dash.cloudflare.com/profile/api-tokens [All zones - Zone:Read, DNS:Edit]
#define DOMAIN "example.com" // domain name (without subdomain)
#define SUBDOMAIN "hello" // subdomain in: hello.example.com
#define IFTTT_URL "http://maker.ifttt.com/trigger/ip_changed/with/key/xxxxxxxxxxxxx" // IFTTT webhook url
#endif

/* Configurable variables */
#define SERVER_PORT 80
#define OTA_HOSTNAME "DDNSClient"
const int DST = 0;
const int LED_PIN = 0;
const int TIMEZONE = 5;
const int INTERVAL = 1000 * 60; // 60 seconds
const int INTERVAL_INIT = 1000 * 5; // 5 seconds
// const String IP_URL = "http://ipv4.icanhazip.com"; // URL to get public IP address
const String IP_URL = "http://api.ip.sb/ip"; // URL to get public IP address

/* Do not change these unless you know what you are doing */
String newIP;
String oldIP;
String recID;
String zoneID;
String logMsg;
String apiURL;
String logTime;
uint apiPort = 443;
int errorCount = 0;
uint eepromAddr = 0;
bool apiIsTLS = true;
bool lineFeed = true;
unsigned long lastMillis = 0;
unsigned long lastMillisInit = 0;
String apiHost = "api.cloudflare.com";

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
    server.send(200, "text/html", \
      "<a href=\"/log\">/log</a><br>" \
      "<a href=\"/checkdns\">/checkdns</a><br>" \
      "<a href=\"/reboot\">/reboot</a><br>" \
    );
    log("I/server: served / to " + server.client().remoteIP().toString());
  });
  server.on("/log", []() {
    server.send(200, "text/plain", logMsg);
  });
  server.on("/checkdns", []() {
    server.send(200, "text/plain", "running DNS check");
    runProc();
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
  if (zoneID == "") {
    if (millis() > lastMillisInit + INTERVAL_INIT) {
      lastMillisInit = millis();
      lastMillis = millis();
      if (!getZoneID()) {
        log("E/checkr: unable to obtain ZONE_ID");
        errorCount++;
      } else {
        errorCount = 0;
      }
    }
  } else {
    if (recID == "") {
      if (millis() > lastMillisInit + INTERVAL_INIT) {
        lastMillisInit = millis();
        lastMillis = millis();
        if (!getRecID()) {
          log("E/checkr: unable to obtain REC_ID");
          errorCount++;
        } else {
          errorCount = 0;
          runProc();
        }
      }
    }
  }

  if (millis() > lastMillis + INTERVAL) {
    runProc();
  }

  if (errorCount > 5) {
    ESP.restart();
  }
  server.handleClient();
  ArduinoOTA.handle();
}


void runProc() {
  lastMillis = millis();
  if (zoneID != "" && recID != "" && oldIP != "") {
    apiURL = "/client/v4/zones/" + zoneID + "/dns_records/" + recID;
    if (WiFi.status() == WL_CONNECTED) {
      checkDNS();
    } else {
      setupWifi();
      checkDNS();
    }
  } else {
    log("E/system: something went wrong! zoneID => " + zoneID + " recID => " + recID + " oldIP => " + oldIP);
  }
}


void log(String msg) {
  if (!lineFeed) {
    lineFeed = true;
    logMsg = logMsg + "\n";
    Serial.println();
  }
  time_t now = time(0);
  logTime = ctime(&now);
  logTime.trim();
  logMsg = logMsg + "[" + logTime + "] ";
  logMsg = logMsg + msg + "\n";
  Serial.println(msg);
}


void logContinuous(String msg, String progressChar) {
  if (lineFeed) {
    lineFeed = false;
    time_t now = time(0);
    logTime = ctime(&now);
    logTime.trim();
    logMsg = logMsg + "[" + logTime + "] ";
    logMsg = logMsg + msg + progressChar;
    Serial.print(msg + progressChar);
  } else {
    logMsg = logMsg + progressChar;
    Serial.print(progressChar);
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
  newIP.replace("<", "&lt;");
  newIP.replace(">", "&gt;");
  newIP.replace("\n", "");
  newIP.replace("\r", "");
  if (httpCode == HTTP_CODE_OK) {
    errorCount = 0;
    if (newIP == oldIP) {
      logContinuous("I/checkr: IP unchanged", ".");
    } else {
      log("I/checkr: IP changed! current IP => " + newIP + ". updating DNS...");
      updateDNS();
    }
  } else {
    errorCount++;
    if (httpCode < 0) {
      log("E/checkr: HTTP error code => " + String(httpCode) + ". HTTP error => " + http.errorToString(httpCode));
    } else {
      log("E/checkr: HTTP status code => " + String(httpCode) + ". HTTP response => " + newIP);
    }
  }
  http.end();
}


void updateDNS() {
  String reqData = "{\"type\":\"A\",\"name\":\"" + String(SUBDOMAIN) + "\",\"content\":\"" + newIP + "\",\"proxied\":false}";
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, apiHost, apiPort, apiURL, apiIsTLS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(TOKEN));
  http.addHeader("Content-Length", String(reqData.length()));
  int httpCode = http.PUT(reqData);
  String httpResponse = http.getString();
  http.end();
  if (httpCode == HTTP_CODE_OK) {
    log("I/updatr: HTTP status code => " + String(httpCode) + ". HTTP response => " + httpResponse);
    oldIP = newIP;
    notify();
  } else {
    if (httpCode < 0) {
      log("E/checkr: HTTP error code => " + String(httpCode) + ". HTTP error => " + http.errorToString(httpCode));
    } else {
      log("E/checkr: HTTP status code => " + String(httpCode) + ". HTTP response => " + newIP);
    }
  }
}


bool getZoneID() {
  String url = "/client/v4/zones?name=" + String(DOMAIN);
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, apiHost, apiPort, url, apiIsTLS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(TOKEN));
  int httpCode = http.GET();
  String httpResponse = http.getString();
  http.end();
  if (httpCode == HTTP_CODE_OK) {
    int startIndex = httpResponse.indexOf("id") + 5;
    int endIndex = httpResponse.indexOf("\"", startIndex);
    zoneID = httpResponse.substring(startIndex, endIndex);
    return true;
  } else {
    return false;
  }
}


bool getRecID() {
  String url = "/client/v4/zones/" + zoneID + "/dns_records";
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, apiHost, apiPort, url, apiIsTLS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(TOKEN));
  int httpCode = http.GET();
  String httpResponse = http.getString();
  http.end();
  if (httpCode == HTTP_CODE_OK) {
    int recIndex = httpResponse.indexOf("type\":\"A\",\"name\":\"" + String(SUBDOMAIN) + "." + String(DOMAIN) + "\"");
    int startIndex = httpResponse.substring(0, recIndex).lastIndexOf("id") - 2;
    int endIndex = httpResponse.indexOf("}", recIndex) + 2;
    httpResponse = httpResponse.substring(startIndex, endIndex);
    startIndex = httpResponse.indexOf("\"content\"") + 11;
    endIndex = httpResponse.indexOf("\"", httpResponse.indexOf("\"content\"") + 11);
    oldIP = httpResponse.substring(startIndex, endIndex);
    log("I/system: old IP obtained from clouflare API => " + oldIP);
    startIndex = httpResponse.indexOf("id") + 5;
    endIndex = httpResponse.indexOf("\"", startIndex);
    recID = httpResponse.substring(startIndex, endIndex);
    return true;
  } else {
    return false;
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
