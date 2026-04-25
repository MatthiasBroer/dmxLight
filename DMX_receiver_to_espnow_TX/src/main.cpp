#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebserver.h>
#include <arduinojson.h>
#include "ESP32S3DMX.h"
#include <Adafruit_NeoPixel.h>


#define DMX_RX_PIN 10
#define DMX_DE_PIN 4
#define DMX_RE_PIN 5
#define DMX_CHANNELS 512

#define PIN_NEO_PIXEL 48
#define NUM_LEDS 1

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
// struct DMXDataPacket {
//   uint8_t red;
//   uint8_t green;
//   uint8_t blue;
//   uint8_t white;
// } dmxPacket;

struct DMXDataPacket {
  uint8_t data[49]; // 1+(6*8)=49 channels there is 1 mode selection, and then max of 8 segments and every segment has 6 channels (start led, end led, r, g, b, w)
  uint8_t count;
} dmxPacket;

void receiveDMX();
void setupWebServerRoutes();
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
config readJSONFile(const char* path);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
uint32_t Wheel(byte WheelPos);

bool serialAvailable = false;
uint8_t dmxStartChannel = 1; // starting channel to forward
uint8_t dmxForwardChannel = 32; // number of channels to forward by espnow

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

esp_now_peer_info_t peerInfo;

ESP32S3DMX dmx;

// create NeoPixel strip object (1 LED, connected to PIN_NEO_PIXEL), RGB 
Adafruit_NeoPixel led = Adafruit_NeoPixel(NUM_LEDS, PIN_NEO_PIXEL, NEO_GRB + NEO_KHZ800);

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

  // make a network a client can connect to for provisioning
  WiFi.softAP("DMX_Receiver", "1234567890");

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
  
  setupWebServerRoutes();

  dmx.begin();

  led.begin();
  led.show();
  led.setBrightness(255);

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

   led.setPixelColor(1, led.Color(125, 0, 0)); // Red for no signal
  led.show();
}

void loop() {
  unsigned long now = millis();
  static unsigned long lastSend = 0;

  if (dmx.isConnected()) {
    
    // send update to esp-now every 300 ms or when a new frame is ready
    if (now - lastSend >= 10) {
      lastSend = now;
      dmxFrameReady = true;
      led.fill(led.Color(0, 125, 0)); // Green for DMX signal
      led.show();
    }

    if (dmxFrameReady) {
      dmxFrameReady = false;
      for (uint8_t i = 0; i < dmxForwardChannel && (dmxStartChannel + i) <= DMX_CHANNELS; i++) {
        dmxPacket.data[i] = dmx.read(dmxStartChannel + i);
      }
      // dmxPacket.red = dmx.read(dmxStartChannel);
      // dmxPacket.green = dmx.read(dmxStartChannel + 1);  
      // dmxPacket.blue = dmx.read(dmxStartChannel + 2);
      // dmxPacket.white = dmx.read(dmxStartChannel + 3);

      Serial.printf("Forwarding DMX channels %d-%d\n", 
                    dmxStartChannel, dmxStartChannel + dmxForwardChannel - 1);

      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &dmxPacket, sizeof(dmxPacket));
      if (result == ESP_OK) {
        if (serialAvailable) Serial.println("DMX data sent via ESP-NOW");
      } else {
        if (serialAvailable) Serial.println("Error sending DMX data via ESP-NOW");
      }
    }
  } else {
      // Serial.print("No DMX signal");
      // uint32_t lastPacket = dmx.timeSinceLastPacket();
      // if (lastPacket != 0xFFFFFFFF) {
      //     Serial.print(" (last seen ");
      //     Serial.print(lastPacket / 1000.0, 1);
      //     Serial.print("s ago)");
      // }
      // Serial.println();
      led.fill(led.Color(125, 0, 0)); // Red for no signal
      led.show();
      delay(10);
  }
}

uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return led.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return led.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return led.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
          {
            Serial.printf("WebSocket client #%u connected\n", client->id());

            DynamicJsonDocument doc(256);
            // JsonDocument doc;
            doc["start"] = dmxStartChannel;
            doc["count"] = dmxForwardChannel;

            String msg;
            serializeJson(doc, msg);
            client->text(msg);
          }
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

    // data[len] = 0;
    // String msg = (char*)data;
    DynamicJsonDocument doc(256);
    // JsonDocument doc;
    deserializeJson(doc, data);

    if(doc.containsKey("start")){
        dmxStartChannel = doc["start"];
    }

    if(doc.containsKey("count")){
        dmxForwardChannel = doc["count"];
    }

    // if(doc["start"]){
    //     dmxStartChannel = doc["start"];
    // }

    // if(doc["count"]){
    //     dmxForwardChannel = doc["count"];
    // }

    Serial.printf("New config: start=%d count=%d\n", dmxStartChannel, dmxForwardChannel);

    // save to SPIFFS
    DynamicJsonDocument saveDoc(256);
    // JsonDocument saveDoc;
    saveDoc["dmx_start_channel"] = dmxStartChannel;
    saveDoc["dmx_forward_channels"] = dmxForwardChannel;

    File file = SPIFFS.open("/config.json","w");
    serializeJson(saveDoc,file);
    file.close();
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

  // favicon route
  server.on("/image/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/image/favicon.ico", "image/favicon.ico");
  });

  // DOWNLOAD CONFIG
  server.on("/downloadConfig", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/config.json", "application/json", true);
  });

  // UPLOAD CONFIG
  server.on(
      "/uploadConfig",
      HTTP_POST,
      [](AsyncWebServerRequest *request){
          request->send(200, "text/plain", "Upload OK");
          ESP.restart();   // optional but recommended after config change
      },
      [](AsyncWebServerRequest *request,
        String filename,
        size_t index,
        uint8_t *data,
        size_t len,
        bool final)
      {
          static File uploadFile;

          if(index == 0) {
              uploadFile = SPIFFS.open("/config.json", "w");
          }

          if(uploadFile) {
              uploadFile.write(data, len);
          }

          if(final) {
              if(uploadFile) uploadFile.close();
          }
      }
  );

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    String ssid, pass;

    if (request->hasParam("ssid", true)) ssid = request->getParam("ssid", true)->value();
    if (request->hasParam("pass", true)) pass = request->getParam("pass", true)->value();

    if (ssid.length() > 0 && pass.length() > 0) {
        // Save to SPIFFS
        DynamicJsonDocument doc(1024);
        // JsonDocument doc;
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
  // static JsonDocument doc;
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