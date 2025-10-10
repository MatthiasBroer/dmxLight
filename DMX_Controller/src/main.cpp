#include <Arduino.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#define DMX_TX_PIN 10
#define DMX_DE_PIN 4
#define DMX_RE_PIN 5

#define DMX_CHANNELS 4  // Lamp has 4 channels
#define DMX_INTERVAL 30  // milliseconds between DMX frames

uint8_t dmxData[DMX_CHANNELS + 1];  // +1 for start code
unsigned long lastDMXTime = 0;

// function declarations
void connectWifi(const char* ssid, const char* password);
void setupWebServerRoutes();
String processor(const String& var);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void notifyClients();
void sendDMX();
void updateDMX();

// create async web server object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool ledState = false;

// store slider values
int sliders[6] = {0, 0, 0, 0, 0, 0};


void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // wait for USB CDC connection
  }
  Serial.println("DMX controller starting...");
  // initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // connect to Wi-Fi
  connectWifi("5-Broertjes", "Waterm0len!3%");

  // initialize web server routes
  setupWebServerRoutes();

  pinMode(DMX_DE_PIN, OUTPUT);
  pinMode(DMX_RE_PIN, OUTPUT);

  digitalWrite(DMX_DE_PIN, LOW);
  digitalWrite(DMX_RE_PIN, LOW);

  Serial1.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);

  Serial.println("Setup complete!");
}

void loop() {
  // Non-blocking DMX frame scheduler
  unsigned long now = millis();
  if (now - lastDMXTime >= DMX_INTERVAL) {
    lastDMXTime = now;
    updateDMX();
    sendDMX();
  }
}

void updateDMX() {
  // Update DMX data based on slider values
  dmxData[0] = 0;  // DMX start code

  // Direct assignment from sliders to DMX channels
  dmxData[1] = sliders[0];  // Red
  dmxData[2] = sliders[1];  // Green
  dmxData[3] = sliders[2];  // Blue
  dmxData[4] = sliders[3];  // White
}

void sendDMX() {
  // Enable RS-485 transmit mode
  digitalWrite(DMX_RE_PIN, HIGH);  // disable receiver
  digitalWrite(DMX_DE_PIN, HIGH);  // enable driver

  // Send BREAK (manually drive line low)
  Serial1.end();
  pinMode(DMX_TX_PIN, OUTPUT);
  digitalWrite(DMX_TX_PIN, LOW);
  delayMicroseconds(120);  // Break
  digitalWrite(DMX_TX_PIN, HIGH);
  delayMicroseconds(12);   // Mark after break

  // Re-enable UART and send DMX packet
  Serial1.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);
  Serial1.write(dmxData, DMX_CHANNELS + 1);
  Serial1.flush();

  // Disable transmit mode
  digitalWrite(DMX_DE_PIN, LOW);
  digitalWrite(DMX_RE_PIN, LOW);
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

  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
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
