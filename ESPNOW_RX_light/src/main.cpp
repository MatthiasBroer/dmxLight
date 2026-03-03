#include <Arduino.h>
#include <NeoPixelBus.h>

// struct that holds the DMX data to be sent via ESP-NOW
struct DMXDataPacket {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
} dmxPacket;
 

#define LED_PIN 8
#define NUM_LEDS 20

// NeoPixelBus<NeoGrbwFeature, NeoEsp32Rmt0800KbpsMethod> strip(NUM_LEDS, LED_PIN);
NeoPixelBus<NeoGrbwFeature, NeoEsp32BitBang800KbpsMethod> strip(NUM_LEDS, LED_PIN);

// Base color (full intensity)
RgbwColor WW_Color(0, 255, 0, 0);


// functions
void breathe(RgbwColor baseColor, byte period = 128, byte lowValue = 0, byte highValue = 255);

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
}

void loop() {
  static unsigned long lastStateChange = 0;
  const unsigned long stateDuration = 10000; // 10 seconds per state
  // period: 128 (~medium speed), lowValue=50, highValue=200
  switch (state)
  {
  case 0: // white
    // breath with with color
    breathe(WW_Color, 1, 1, 255);
    if (millis() - lastStateChange >= stateDuration) {
      lastStateChange = millis();
      state = 1; // move to next state
      Serial.println("Switching form 0 to 1 [red]");
    }
    break;
  case 1: // red
    // breath with green color
    breathe(RgbwColor(255, 0, 0, 0), 1, 1, 255);
      if (millis() - lastStateChange >= stateDuration) {
        lastStateChange = millis();
        state = 2; // move to next state
        Serial.println("Switching form 1 to 2 [green]");
      }
    break;
  case 2: // greed
    // breath with blue color
    breathe(RgbwColor(0, 0, 255, 0), 1, 1, 255);
    if (millis() - lastStateChange >= stateDuration) {
      lastStateChange = millis();
      state = 3; // move to next state
      Serial.println("Switching form 2 to 3 [blue]");
    }
    break;
  case 3: // blue
    // breath with white color
    breathe(RgbwColor(0, 0, 0, 255), 1, 1, 255);
    if (millis() - lastStateChange >= stateDuration) {
      lastStateChange = millis();
      state = 0; // loop back to first state
      Serial.println("Switching form 3 to 0 [white]");
    }
    break;

  default:
    break;
  }
  
  // breathe with red color
  // breathe(RgbwColor(255, 0, 0, 0), 1, 1, 255);
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