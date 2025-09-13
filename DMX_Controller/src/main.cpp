#include <Arduino.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_dmx.h>

// function declarations
void connectWifi(const char* ssid, const char* password);
void setupWebServerRoutes();
String processor(const String& var);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void notifyClients();

// create async web server object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool ledState = false;

// store slider values
int sliders[6] = {0, 0, 0, 0, 0, 0};

void setup() {
  Serial.begin(115200);

  // initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // connect to Wi-Fi
  connectWifi("your_SSID", "your_PASSWORD");

  // initialize web server routes
  setupWebServerRoutes();
}

void loop() {
  // nothing needed here because AsyncWebServer + WebSockets are event-driven
}

// connect to Wi-Fi network
void connectWifi(const char* ssid, const char* password) {
  WiFi.begin(ssid,  password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void setupWebServerRoutes() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  
  // route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html", false, processor);
  });

  server.begin();
}

String processor(const String& var){
  if(var == "STATE"){
    if (ledState){
      return "ON";
    }
    else{
      return "OFF";
    }
  }
  return String();
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(),
                    client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String msg = (char*)data;

    if (msg == "toggle") {
      ledState = !ledState;
      notifyClients();
    } 
    else {
      // expect format "n:value"
      int colonIndex = msg.indexOf(':');
      if (colonIndex > 0) {
        int sliderNum = msg.substring(0, colonIndex).toInt();
        int value = msg.substring(colonIndex + 1).toInt();

        if (sliderNum >= 1 && sliderNum <= 6) {
          sliders[sliderNum - 1] = value;
          Serial.printf("Slider %d -> %d\n", sliderNum, value);
        }
      }
    }
  }
}

void notifyClients() {
  ws.textAll(String(ledState));
}
