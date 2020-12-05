#include <WiFi.h>
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"
#include "secrets.h"

// wifi network
const char* wifiSSID = SECRET_SSID;
const char* wifiPwd = SECRET_PASS;

AsyncWebServer server(80);

bool requireAuth(AsyncWebServerRequest *request) {
  if (!request->authenticate("admin", "admin")) {
    request->requestAuthentication();
    return false;
  }
  return true;
}

void setup() {
  // Initialize serial and wait for port to open
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  connectToWiFiNetwork(wifiSSID, wifiPwd);

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse(404, "image/x-icon");
    request->send(response);
  });

  server.on("/echo", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!requireAuth(request)) return;

    AsyncResponseStream *response = request->beginResponseStream("text/html");

    response->printf("<!DOCTYPE html><html><head><title>Echo Webpage at %s</title></head><body>", request->url().c_str());

    response->print("<h2>Client: ");
    response->print(request->client()->remoteIP());
    response->print("</h2>");

    response->print("<h3>General</h3>");
    response->print("<ul>");
    response->printf("<li>Version: HTTP/1.%u</li>", request->version());
    response->printf("<li>Method: %s</li>", request->methodToString());
    response->printf("<li>URL: %s</li>", request->url().c_str());
    response->printf("<li>Host: %s</li>", request->host().c_str());
    response->printf("<li>ContentType: %s</li>", request->contentType().c_str());
    response->printf("<li>ContentLength: %u</li>", request->contentLength());
    response->printf("<li>Multipart: %s</li>", request->multipart() ? "true" : "false");
    response->print("</ul>");

    response->print("<h3>Headers</h3>");
    response->print("<ul>");
    int headers = request->headers();
    for (int i = 0; i < headers; i++) {
      AsyncWebHeader* h = request->getHeader(i);
      response->printf("<li>%s: %s</li>", h->name().c_str(), h->value().c_str());
    }
    response->print("</ul>");

    response->print("<h3>Parameters</h3>");
    response->print("<ul>");
    int params = request->params();
    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isFile()) {
        response->printf("<li>FILE[%s]: %s, size: %u</li>", p->name().c_str(), p->value().c_str(), p->size());
      } else if (p->isPost()) {
        response->printf("<li>POST[%s]: %s</li>", p->name().c_str(), p->value().c_str());
      } else {
        response->printf("<li>GET[%s]: %s</li>", p->name().c_str(), p->value().c_str());
      }
    }
    response->print("</ul>");
    response->print("</body></html>");

    request->send(response);
  }).setAuthentication("user", "pass");

  server.begin();
}

void connectToWiFiNetwork(const char * ssid, const char * pwd)
{
  // Station mode
  WiFi.begin(ssid, pwd);
  while ( WiFi.status() != WL_CONNECTED ) {
    delay(500);
  }

  // wifi network debug info
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {}
