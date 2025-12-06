#include <Arduino.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
//#include <WiFiManager.h>

#define DMX_TX_PIN 10
#define DMX_DE_PIN 4
#define DMX_RE_PIN 5

#define DMX_CHANNELS 16
#define DMX_INTERVAL 30  // milliseconds

uint8_t dmxData[DMX_CHANNELS + 1];  // +1 for start code
unsigned long lastDMXTime = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// slider values
int sliders[DMX_CHANNELS] = {};  // R,G,B,W

// wave mode variables
bool waveActive = false;
bool waveFading = false;   // true when wave is deactivated but fading out
unsigned long waveLastTime = 0;
unsigned long waveFadeLastTime = 0;
const int FADE_INTERVAL = 30; // ms per fade step
const int FADE_STEP = 15;     // how fast values fade
int waveStep = 0;
int waveInterval = 100;  // default ms
float waveValues[DMX_CHANNELS] = {}; // use float for smooth fading
const float WAVE_STEP = 15.0; // fade speed per step

// ===== Chaser variables =====
bool chaserActive = false;
bool chaserFading = false;
unsigned long chaserLastTime = 0;
const int CHASER_INTERVAL_DEFAULT = 100;  // ms
int chaserInterval = CHASER_INTERVAL_DEFAULT;
int chaserStep = 0;
float chaserValues[DMX_CHANNELS] = {}; // use float for smooth fading
const float CHASER_STEP = 15.0; // how fast channels fade

// ===== Breath Effect Variables =====
bool breathActive = false;
bool breathFading = false;
unsigned long breathLastTime = 0;
const int BREATH_INTERVAL_DEFAULT = 30;  // update interval in ms
int breathInterval = BREATH_INTERVAL_DEFAULT;
float breathPhase = 0.0; // phase of sine wave
float breathSpeed = 0.05; // speed of the breathing
int breathMin = 10;
int breathMax = 255;
bool breathIncreasing = true;
int breathChannels[512] = {}; // store whether each channel is active


bool ledState = false;

// ===== Function Declarations =====
void connectWifi(const char* ssid, const char* password);
void setupWebServerRoutes();
String processor(const String& var);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void notifyClients();
void sendSliderUpdate(int index, int value);

void updateDMXFromSliders();
void sendDMX();
void handleWave();
void handleDMXUpdate();
void handleWaveFade();
void handleChaserFade();
void handleChaser();
void handleBreathFade();
void handleBreath();

// ===== Setup =====
void setup() {
    Serial.begin(115200);
    Serial.println("DMX controller starting...");

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }

    connectWifi("5-Broertjes", "Waterm0len!3%");
    setupWebServerRoutes();

    pinMode(DMX_DE_PIN, OUTPUT);
    pinMode(DMX_RE_PIN, OUTPUT);
    digitalWrite(DMX_DE_PIN, LOW);
    digitalWrite(DMX_RE_PIN, LOW);

    Serial1.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);

    // Initialize DMX frame
    for (int i = 0; i <= DMX_CHANNELS; i++) dmxData[i] = 0;

    Serial.println("Setup complete!");
}

// ===== Loop =====
void loop() {
    handleDMXUpdate();
}

// ===== DMX Update Logic =====
void handleDMXUpdate() {
    unsigned long now = millis();

    if (now - lastDMXTime >= DMX_INTERVAL) {
        lastDMXTime = now;

        if (waveActive) {
            waveFading = false;  // stop fading if wave is active
            handleWave();
        } 
        else if (waveFading) {
            handleWaveFade();
        }

        if (chaserActive) {
            chaserFading = false;
            handleChaser();
        }
        else if (chaserFading) {
            handleChaserFade();
        }

        if (breathActive) {
            breathFading = false;
            handleBreath();
        }
        else if (breathFading) {
            handleBreathFade();
        }

        updateDMXFromSliders();
        sendDMX();
    }
}

void handleWaveFade() {
    unsigned long now = millis();
    if (now - waveFadeLastTime >= FADE_INTERVAL) {
        waveFadeLastTime = now;
        bool stillFading = false;

        for (int i = 0; i < DMX_CHANNELS; i++) {
            if (sliders[i] > 0) {
                sliders[i] -= FADE_STEP;
                if (sliders[i] < 0) sliders[i] = 0;
                stillFading = true;
            }
        }

        if (!stillFading) waveFading = false; // finished fading
    }
}

// ===== Update DMX Array from Slider Values =====
void updateDMXFromSliders() {
  dmxData[0] = 0;
  for (int i = 0; i < DMX_CHANNELS; i++) {
      dmxData[i + 1] = sliders[i];
  }
}

// ===== Send DMX Frame =====
void sendDMX() {
    digitalWrite(DMX_RE_PIN, HIGH);  // disable receiver
    digitalWrite(DMX_DE_PIN, HIGH);  // enable driver

    Serial1.end();
    pinMode(DMX_TX_PIN, OUTPUT);
    digitalWrite(DMX_TX_PIN, LOW);
    delayMicroseconds(120);  // break
    digitalWrite(DMX_TX_PIN, HIGH);
    delayMicroseconds(12);   // mark after break

    Serial1.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);
    Serial1.write(dmxData, DMX_CHANNELS + 1);
    Serial1.flush();

    digitalWrite(DMX_DE_PIN, LOW);
    digitalWrite(DMX_RE_PIN, LOW);
}

// ===== Handle Wave Mode =====
void handleWave() {
    unsigned long now = millis();
    if (now - waveLastTime >= waveInterval) {
        waveLastTime = now;
        waveStep = (waveStep + 1) % DMX_CHANNELS;

        // Reset all channels
        for (int i = 0; i < DMX_CHANNELS; i++) sliders[i] = 0;

        // Activate the current channel fully
        sliders[waveStep] = 255;

        Serial.printf("Wave step: %d\n", waveStep);
    }
}

void handleChaser() {
    unsigned long now = millis();
    if (now - chaserLastTime >= chaserInterval) {
        chaserLastTime = now;
        chaserStep = (chaserStep + 1) % DMX_CHANNELS;
        Serial.printf("Chaser step: %d\n", chaserStep);
    }

    // Fade channels smoothly
    for (int i = 0; i < DMX_CHANNELS; i++) {
        if (i == chaserStep) {
            chaserValues[i] += CHASER_STEP;
            if (chaserValues[i] > 255) chaserValues[i] = 255;
        } else {
            chaserValues[i] -= CHASER_STEP;
            if (chaserValues[i] < 0) chaserValues[i] = 0;
        }
        sliders[i] = (int)chaserValues[i];
    }
}

void handleChaserFade() {
    unsigned long now = millis();
    if (now - waveFadeLastTime >= FADE_INTERVAL) {
        waveFadeLastTime = now;
        bool stillFading = false;

        for (int i = 0; i < DMX_CHANNELS; i++) {
            if (chaserValues[i] > 0) {
                chaserValues[i] -= CHASER_STEP;
                if (chaserValues[i] < 0) chaserValues[i] = 0;
                sliders[i] = (int)chaserValues[i];
                stillFading = true;
            }
        }

        if (!stillFading) chaserFading = false;
    }
}

void handleBreath() {
    unsigned long now = millis();
    if (now - breathLastTime >= breathInterval) {
        breathLastTime = now;

        // Sine wave calculation between min and max
        float intensity = (sin(breathPhase) + 1.0) / 2.0; // 0 → 1
        int value = breathMin + intensity * (breathMax - breathMin);

        for (int i = 0; i < DMX_CHANNELS; i++) {
            if (breathChannels[i]) {
                sliders[i] = value;
            }
        }

        breathPhase += breathSpeed;
        if (breathPhase > 2 * PI) breathPhase -= 2 * PI;
    }
}

void handleBreathFade() {
    unsigned long now = millis();
    if (now - waveFadeLastTime >= FADE_INTERVAL) {
        waveFadeLastTime = now;
        bool stillFading = false;

        for (int i = 0; i < DMX_CHANNELS; i++) {
            if (sliders[i] > 0) {
                sliders[i] -= FADE_STEP;
                if (sliders[i] < 0) sliders[i] = 0;
                stillFading = true;
            }
        }

        if (!stillFading) breathFading = false;
    }
}

// ===== Wi-Fi =====
void connectWifi(const char* ssid, const char* password) {
    Serial.print("Connecting to WiFi ..");
  
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
    Serial.println(WiFi.localIP());
}

// ===== Web Server =====
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

    server.on("/setup.html", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(
            SPIFFS, "/setup.html", "text/html"
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

    server.begin();
}


// ===== Template Processor =====
String processor(const String& var) {
    if(var == "STATE") return ledState ? "ON" : "OFF";
    return String();
}

// ===== WebSocket Events =====
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

    if (msg == "toggle") {
        ledState = !ledState;
        notifyClients();
    }
    else if (msg.startsWith("wave:")) {
        if (msg == "wave:toggle") {
            if (waveActive) {
                // turning off wave -> start fade
                waveActive = false;
                waveFading = true;
                Serial.println("Wave stopped, fading out...");
            } else {
                // start wave
                waveActive = true;
                Serial.println("Wave started");
            }
        }
        else if (msg.startsWith("wave:speed:")) {
            waveInterval = msg.substring(11).toInt();
            Serial.printf("Wave speed set to %d ms\n", waveInterval);
        }
    }
    else if (msg.startsWith("chaser:")) {
        if (msg == "chaser:toggle") {
            if (chaserActive) {
                chaserActive = false;
                chaserFading = true;
                Serial.println("Chaser stopped, fading out...");
            } else {
                chaserActive = true;
                Serial.println("Chaser started");
            }
        }
        else if (msg.startsWith("chaser:speed:")) {
            chaserInterval = msg.substring(13).toInt();
            Serial.printf("Chaser speed set to %d ms\n", chaserInterval);
        }
    }
    else if (msg.startsWith("breath:")) {
        if (msg == "breath:toggle") {
            if (breathActive) {
                breathActive = false;
                breathFading = true;
                Serial.println("Breath effect stopped, fading out...");
            } else {
                breathActive = true;
                Serial.println("Breath effect started");
            }
        }
        else if (msg.startsWith("breath:speed:")) {
            breathSpeed = msg.substring(13).toFloat() / 100.0;
            Serial.printf("Breath speed set to %.2f\n", breathSpeed);
        }
        else if (msg.startsWith("breath:min:")) {
            breathMin = msg.substring(11).toInt();
            Serial.printf("Breath min set to %d\n", breathMin);
        }
        else if (msg.startsWith("breath:max:")) {
            breathMax = msg.substring(11).toInt();
            Serial.printf("Breath max set to %d\n", breathMax);
        }
        else if (msg.startsWith("breath:channels:")) {
            String list = msg.substring(16);
            for (int i = 0; i < 512; i++) breathChannels[i] = 0;
            int start = 0;
            while (start >= 0) {
                int comma = list.indexOf(',', start);
                String token = (comma == -1) ? list.substring(start) : list.substring(start, comma);
                int ch = token.toInt();
                if (ch >= 1 && ch <= DMX_CHANNELS) breathChannels[ch - 1] = 1;
                if (comma == -1) break;
                start = comma + 1;
            }
            Serial.print("Breath channels updated: ");
            Serial.println(list);
        }
    }
    else {
        int colonIndex = msg.indexOf(':');
        if (colonIndex > 0) {
            int sliderNum = msg.substring(0, colonIndex).toInt();
            int value = msg.substring(colonIndex + 1).toInt();

            if (sliderNum >= 1 && sliderNum <= DMX_CHANNELS) {
                sliders[sliderNum - 1] = value;
                Serial.printf("Slider %d -> %d\n", sliderNum, value);
            }
        }
    }
}

// ===== Notify Clients =====
void notifyClients() {
    ws.textAll(String(ledState));
}