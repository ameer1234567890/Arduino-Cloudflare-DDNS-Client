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
#define IFTTT_NOTIFY_URL "http://maker.ifttt.com/trigger/ip_changed/with/key/xxxxxxxxxxxxx" // IFTTT webhook notify url
#define IFTTT_ERROR_URL "http://maker.ifttt.com/trigger/ddns_error/with/key/xxxxxxxxxxxxx" // IFTTT webhook error url
#endif

/* Configurable variables */
#define SERVER_PORT 80
#define OTA_HOSTNAME "DDNSClient"
const int DST = 0;
const int LED_PIN = 0;
const int TIMEZONE = 5;
const int INTERVAL = 1000 * 60; // 60 seconds
const int INTERVAL_INIT = 1000 * 5; // 5 seconds
const String IP_URL = "http://ifconfig.me/ip"; // URL to get public IP address

/* Do not change these unless you know what you are doing */
String newIP;
String oldIP;
String recID;
String zoneID;
String logMsg;
String logTime;
String lastError;
int errorCount = 0;
uint eepromAddr = 0;
bool lineFeed = true;
bool errorNotified = false;
unsigned long lastMillis = 0;
unsigned long lastMillisInit = 0;

HTTPClient http;
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
        lastError = "unable to obtain ZONE_ID";
        log("E/checkr: " + lastError);
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
          lastError = "unable to obtain REC_ID";
          log("E/checkr: " + lastError);
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

  if (errorCount > 5 && !errorNotified) {
    errorNotified = true;
    error(lastError);
  }
  if (errorCount > 20) {
    ESP.restart();
  }
  server.handleClient();
  ArduinoOTA.handle();
}


void runProc() {
  lastMillis = millis();
  if (zoneID != "" && recID != "" && oldIP != "") {
    if (WiFi.status() == WL_CONNECTED) {
      checkDNS();
    } else {
      setupWifi();
      checkDNS();
    }
  } else {
    lastError = "something went wrong! zoneID => " + zoneID + " recID => " + recID + " oldIP => " + oldIP;
    log("E/system: " + lastError);
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
  WiFiClient wClient;
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
      lastError = "HTTP error code => " + String(httpCode) + ". HTTP error => " + http.errorToString(httpCode);
      log("E/checkr: " + lastError);
    } else {
      lastError = "HTTP status code => " + String(httpCode) + ". HTTP response => " + newIP;
      log("E/checkr: " + lastError);
    }
  }
  http.end();
}


void updateDNS() {
  String reqData = "{\"type\":\"A\",\"name\":\"" + String(SUBDOMAIN) + "\",\"content\":\"" + newIP + "\",\"proxied\":false}";
  String url = "https://api.cloudflare.com/client/v4/zones/" + zoneID + "/dns_records/" + recID;
  String host = url.substring(url.indexOf("https://") + 8, url.indexOf("/", url.indexOf("https://") + 8));
  String path = url.substring(url.indexOf(host) + host.length(), url.length());
  WiFiClientSecure wClientSecure;
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, host, 443, path, true);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(TOKEN));
  http.addHeader("Content-Length", String(reqData.length()));
  int httpCode = http.PUT(reqData);
  String httpResponse = http.getString();
  http.end();
  httpResponse.replace("\n", "");
  if (httpCode == HTTP_CODE_OK) {
    log("I/updatr: HTTP status code => " + String(httpCode) + ". HTTP response => " + httpResponse);
    oldIP = newIP;
    notify();
  } else {
    if (httpCode < 0) {
      lastError = "HTTP error code => " + String(httpCode) + ". HTTP error => " + http.errorToString(httpCode);
      log("E/checkr: " + lastError);
    } else {
      lastError = "HTTP status code => " + String(httpCode) + ". HTTP response => " + httpResponse;
      log("E/checkr: " + lastError);
    }
  }
}


bool getZoneID() {
  String url = "https://api.cloudflare.com/client/v4/zones?name=" + String(DOMAIN);
  String host = url.substring(url.indexOf("https://") + 8, url.indexOf("/", url.indexOf("https://") + 8));
  String path = url.substring(url.indexOf(host) + host.length(), url.length());
  WiFiClientSecure wClientSecure;
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, host, 443, path, true);
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
  String url = "https://api.cloudflare.com/client/v4/zones/" + zoneID + "/dns_records";
  String host = url.substring(url.indexOf("https://") + 8, url.indexOf("/", url.indexOf("https://") + 8));
  String path = url.substring(url.indexOf(host) + host.length(), url.length());
  WiFiClientSecure wClientSecure;
  wClientSecure.setInsecure(); // until we have better handling of a trust chain on small devices
  http.begin(wClientSecure, host, 443, path, true);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(TOKEN));
  int httpCode = http.GET();
  String httpResponse = http.getString();
  http.end();
  if (httpCode == HTTP_CODE_OK) {
    int recIndex = httpResponse.indexOf("\"name\": \"" + String(SUBDOMAIN) +"." + String(DOMAIN) + "\",\n      \"type\": \"A\",");
    int startIndex = httpResponse.substring(0, recIndex).lastIndexOf("\"id\"") - 8;
    int endIndex = httpResponse.indexOf("}", recIndex) + 2;
    httpResponse = httpResponse.substring(startIndex, endIndex);
    startIndex = httpResponse.indexOf("\"content\"") + 12;
    endIndex = httpResponse.indexOf("\"", startIndex);
    oldIP = httpResponse.substring(startIndex, endIndex);
    log("I/system: old IP obtained from clouflare API => " + oldIP);
    startIndex = httpResponse.indexOf("\"id\"") + 7;
    endIndex = httpResponse.indexOf("\"", startIndex);
    recID = httpResponse.substring(startIndex, endIndex);
    return true;
  } else {
    return false;
  }
}


void notify() {
  WiFiClient wClient;
  http.begin(wClient, String(IFTTT_NOTIFY_URL) + "?value1=" + newIP);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    log("I/notify: notified via IFTTT");
  } else {
    log("E/notify: notification via IFTTT failed. HTTP status code => " + String(httpCode));
  }
  http.end();
}


void error(String message) {
  message.replace(" ", "+");
  WiFiClient wClient;
  http.begin(wClient, String(IFTTT_ERROR_URL) + "?value1=" + message);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    log("I/error : error notified via IFTTT");
  } else {
    log("E/notify: notification via IFTTT failed. HTTP status code => " + String(httpCode));
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
