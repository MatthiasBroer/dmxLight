#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebserver.h>
#include <arduinojson.h>
#include "ESP32S3DMX.h"


#define DMX_RX_PIN 10
#define DMX_DE_PIN 4
#define DMX_RE_PIN 5
#define DMX_CHANNELS 512 

uint8_t broadcastAddress[] = {0x32, 0xAE, 0xA4, 0x07, 0x0D, 0x66};

volatile uint8_t dmxRxData[DMX_CHANNELS + 1]; // slot 0 = start code
volatile bool dmxFrameReady = false;

// struct to hold JSON config data
struct config{
  bool valid;
  uint8_t dmx_start_channel;
  uint8_t dmx_forward_channels;
};

// struct that holds the DMX data to be sent via ESP-NOW
struct DMXDataPacket {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
} dmxPacket;

void receiveDMX();
void setupWebServerRoutes();
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
config readJSONFile(const char* path);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

bool serialAvailable = false;
uint8_t dmxStartChannel = 1; // starting channel to forward
uint8_t dmxForwardChannel =4; // number of channels to forward by espnow

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

esp_now_peer_info_t peerInfo;

ESP32S3DMX dmx;

void setup() {
   // check if a serial data is available
  if (Serial.available()) 
  {
    Serial.begin(115200);
    Serial.println("DMX Receiver starting...");
    serialAvailable = true;
  }

  // init of SPIFFS
  if (!SPIFFS.begin(true)) {
    if (serialAvailable) Serial.println("SPIFFS mount failed, formatting...");
    return;
  }

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent));

  // register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0; // use current channel
  peerInfo.encrypt = false;

  // add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  
  // setupWebServerRoutes();

  dmx.begin();

  // Read config from SPIFFS
  config cfg = readJSONFile("/config.json");
  if (cfg.valid) {
    dmxStartChannel = cfg.dmx_start_channel;
    dmxForwardChannel = cfg.dmx_forward_channels;
    if (serialAvailable) Serial.printf("Config loaded: Start Channel=%d, Forward Channels=%d\n", dmxStartChannel, dmxForwardChannel);
  } else {
    if (serialAvailable) Serial.println("Using default config: Start Channel=1, Forward Channels=4");
  }

  if (serialAvailable) Serial.println("DXM Receiver Setup complete!");
}

void loop() {
  unsigned long now = millis();

  if (dmx.isConnected()) {
    // send update to esp-now every 300 ms or when a new frame is ready
    if (now % 300 == 0 ) {
      dmxFrameReady = true;
    }

    if (dmxFrameReady) {
      dmxFrameReady = false;
      dmxPacket.red = dmx.read(dmxStartChannel);
      dmxPacket.green = dmx.read(dmxStartChannel + 1);  
      dmxPacket.blue = dmx.read(dmxStartChannel + 2);
      dmxPacket.white = dmx.read(dmxStartChannel + 3);

      Serial.printf("Forwarding DMX channels %d-%d: R=%d G=%d B=%d W=%d\n", 
                    dmxStartChannel, dmxStartChannel + dmxForwardChannel - 1, 
                    dmxPacket.red, dmxPacket.green, dmxPacket.blue, dmxPacket.white);

      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &dmxPacket, sizeof(dmxPacket));
      if (result == ESP_OK) {
        if (serialAvailable) Serial.println("DMX data sent via ESP-NOW");
      } else {
        if (serialAvailable) Serial.println("Error sending DMX data via ESP-NOW");
      }
    }
  } else {
      Serial.print("No DMX signal");
      uint32_t lastPacket = dmx.timeSinceLastPacket();
      if (lastPacket != 0xFFFFFFFF) {
          Serial.print(" (last seen ");
          Serial.print(lastPacket / 1000.0, 1);
          Serial.print("s ago)");
      }
      Serial.println();
  }

  //receiveDMX();
}

void receiveDMX() {
  static uint16_t channel = 0;
  static unsigned long lastByteTime = 0;
  static bool receiving = false;

  while (Serial1.available()) {
    uint8_t c = Serial1.read();
    unsigned long now = micros();

    // Detect BREAK by long gap (> 88 µs)
    if (now - lastByteTime > 120) {  
      channel = 0;
      receiving = true;
    }

    lastByteTime = now;

    if (!receiving) continue;

    if (channel <= DMX_CHANNELS) {
      dmxRxData[channel++] = c;
    }

    if (channel > DMX_CHANNELS) {
      receiving = false;
      dmxFrameReady = true; // full frame received
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected\n", client->id());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        default: break;
    }
}

// ===== Handle Incoming WebSocket Message =====
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

    data[len] = 0;
    String msg = (char*)data;
}

void setupWebServerRoutes() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      AsyncWebServerResponse *response = request->beginResponse(
          SPIFFS, "/index.html", "text/html"
      );
      response->addHeader("Content-Encoding", "utf-8");
      response->addHeader("Content-Type", "text/html; charset=utf-8");
      request->send(response);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
      AsyncWebServerResponse *response = request->beginResponse(
          SPIFFS, "/style.css", "text/css"
      );
      response->addHeader("Content-Encoding", "utf-8");
      response->addHeader("Content-Type", "text/css; charset=utf-8");
      request->send(response);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    String ssid, pass;

    if (request->hasParam("ssid", true)) ssid = request->getParam("ssid", true)->value();
    if (request->hasParam("pass", true)) pass = request->getParam("pass", true)->value();

    if (ssid.length() > 0 && pass.length() > 0) {
        // Save to SPIFFS
        DynamicJsonDocument doc(1024);
        doc["wifi_ssid"] = ssid;
        doc["wifi_password"] = pass;

        File file = SPIFFS.open("/config.json", "w");
        if (!file) {
            Serial.println("Failed to open config.json for writing");
        } else {
            serializeJson(doc, file);
            file.close();
            Serial.println("WiFi credentials saved!");
        }

        // Send response
        request->send(200, "text/html", "<h2>Settings saved. Rebooting...</h2><script>setTimeout(()=>{location.reload();},2000);</script>");

        // Optionally restart to apply new WiFi
        delay(1000);
        ESP.restart();
        return;
    } else {
        request->send(400, "text/html", "SSID or Password empty!");
    }
  });


  server.begin();
}

config readJSONFile(const char* path) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    if (serialAvailable) Serial.println("Failed to open config file");
    return {false, 1, 4}; // return empty config on failure
  }

  static  DynamicJsonDocument doc(1024);
  deserializeJson(doc, file);
  file.close();

  config cfg;
  cfg.valid = true;
  cfg.dmx_start_channel = doc["dmx_start_channel"] | 1; // default to 1
  cfg.dmx_forward_channels = doc["dmx_forward_channels"] | 4; // default to 4
  return cfg;
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}