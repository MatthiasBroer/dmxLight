#include <Arduino.h>
#include <NeoPixelBus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// struct that holds the DMX data to be sent via ESP-NOW
struct DMXDataPacket {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
};;// dmxPacket;

DMXDataPacket dmx; 
 
uint8_t broadcastAddress[] = {0x32, 0xAE, 0xA4, 0x07, 0x0D, 0x66};

#define LED_PIN 4
#define NUM_LEDS 20

// NeoPixelBus<NeoGrbwFeature, NeoEsp32Rmt0800KbpsMethod> strip(NUM_LEDS, LED_PIN);
// NeoPixelBus<NeoGrbwFeature, NeoEsp32BitBang800KbpsMethod> strip(NUM_LEDS, LED_PIN);
NeoPixelBus<NeoGrbwFeature, NeoEsp32Rmt0800KbpsMethod> strip(NUM_LEDS, LED_PIN);

// Base color (full intensity)
RgbwColor WW_Color(0, 255, 0, 0);

// functions
void breathe(RgbwColor baseColor, byte period = 128, byte lowValue = 0, byte highValue = 255);
bool startupChase(RgbwColor color, unsigned long speedMs = 100);
void setLightOnStrip(RgbwColor color);
void onDataRecv(const uint8_t* mac, const uint8_t *incomingData, int len);

// Breathing state variables
float brightness = 0.0;   // 0.0 - 1.0
float step = 0.0;         // calculated based on period
int direction = 1;         // 1 = increasing, -1 = decreasing
unsigned long lastUpdate = 0;

int state = 0;


void setup() {
  Serial.begin(115200);
  strip.Begin();
  strip.Show();

  WiFi.mode(WIFI_STA);
  
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, &broadcastAddress[0]);
  if (err == ESP_OK) {
    Serial.println("setting MAC address SUCCESS");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));

  while (!startupChase(WW_Color, 100)) {
    // wait for startup chase to finish
  }
}

void loop() {
  static unsigned long lastPrint = 0;
  static unsigned long lastLightUpdate = 0;
  unsigned long now = millis();

  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.printf("Current DMX data: R=%d G=%d B=%d W=%d\n", 
                  dmx.red, dmx.green, dmx.blue, dmx.white);
  }

  if (now - lastLightUpdate >= 10) { // update light every 50ms for smooth breathing
    lastLightUpdate = now;
    setLightOnStrip(RgbwColor(dmx.red, dmx.white, dmx.green, dmx.blue));
  }  
}

// Non-blocking breathing function
// period: 0=off, 255=fastest
// lowValue: 0=off, 255=max brightness
// highValue: 0=off, 255=max brightness
void breathe(RgbwColor baseColor, byte period, byte lowValue, byte highValue) {
  static float brightness = 0.0;
  static int direction = 1;
  static unsigned long lastUpdate = 0;

  unsigned long now = millis();
  const unsigned long interval = 20; // update every 20ms for smoothness

  // map period (0–255) to actual breathing speed
  // period=0 -> no breathing, period=255 -> fastest
  unsigned long mappedPeriod = (period == 0) ? 0 : map(period, 1, 255, 2000, 100); // ms full cycle

  if (mappedPeriod == 0) {
    // constant brightness at highValue
    RgbwColor scaledColor(
      (uint8_t)(baseColor.R * highValue / 255),
      (uint8_t)(baseColor.G * highValue / 255),
      (uint8_t)(baseColor.B * highValue / 255),
      (uint8_t)(baseColor.W * highValue / 255)
    );
    for (uint16_t i = 0; i < NUM_LEDS; i++) strip.SetPixelColor(i, scaledColor);
    strip.Show();
    return;
  }

  // calculate step based on mappedPeriod and interval
  float step = (float)interval / (mappedPeriod / 2.0);

  if (now - lastUpdate >= interval) {
    lastUpdate = now;

    // update brightness
    brightness += step * direction;
    if (brightness >= 1.0) {
      brightness = 1.0;
      direction = -1;
    } else if (brightness <= 0.0) {
      brightness = 0.0;
      direction = 1;
    }

    // scale brightness to low/high range
    float scaledBrightness = lowValue + brightness * (highValue - lowValue);
    scaledBrightness = constrain(scaledBrightness, 0, 255);

    // scale base color by scaledBrightness
    RgbwColor scaledColor(
      (uint8_t)(baseColor.R * scaledBrightness / 255),
      (uint8_t)(baseColor.G * scaledBrightness / 255),
      (uint8_t)(baseColor.B * scaledBrightness / 255),
      (uint8_t)(baseColor.W * scaledBrightness / 255)
    );

    // apply to all LEDs
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
      strip.SetPixelColor(i, scaledColor);
    }
    strip.Show();
  }
}

void setLightOnStrip(RgbwColor color) {
  RgbwColor scaledColor(
      (uint8_t)(color.R),
      (uint8_t)(color.G),
      (uint8_t)(color.B),
      (uint8_t)(color.W)
    );
    for (uint16_t i = 0; i < NUM_LEDS; i++) strip.SetPixelColor(i, scaledColor);
    strip.Show();
    return;
}

bool startupChase(RgbwColor color, unsigned long speedMs) {
  static uint16_t pos = 0;
  static unsigned long lastUpdate = 0;
  static bool finished = false;

  const uint8_t fadeAmount = 40; // higher = faster fade

  if (finished) return true; // already done

  unsigned long now = millis();
  if (now - lastUpdate >= speedMs) {
    lastUpdate = now;

    // fade all pixels a bit
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
      RgbwColor c = strip.GetPixelColor(i);

      c.R = (c.R > fadeAmount) ? c.R - fadeAmount : 0;
      c.G = (c.G > fadeAmount) ? c.G - fadeAmount : 0;
      c.B = (c.B > fadeAmount) ? c.B - fadeAmount : 0;
      c.W = (c.W > fadeAmount) ? c.W - fadeAmount : 0;

      strip.SetPixelColor(i, c);
    }

    // set current pixel to full color
    strip.SetPixelColor(pos, color);
    strip.Show();

    pos++;
    if (pos >= NUM_LEDS) {
      finished = true;
    }
  }
  return false;
}

void onDataRecv(const uint8_t* mac, const uint8_t *incomingData, int len) {
  memcpy(&dmx, incomingData, sizeof(DMXDataPacket));
  Serial.printf("Received DMX data via ESP-NOW: R=%d G=%d B=%d W=%d\n", 
                dmx.red, dmx.green, dmx.blue, dmx.white);
}