#include <Arduino.h>

#define DMX_RX_PIN 10
#define DMX_DE_PIN 4
#define DMX_RE_PIN 5
#define DMX_CHANNELS 512 
#define DMX_FORWARD_CHANNELS 4 // number of channels to forward by espnow

volatile uint8_t dmxRxData[DMX_CHANNELS + 1]; // slot 0 = start code
volatile bool dmxFrameReady = false;
void receiveDMX();

bool serialAvailable = false;
uint8_t dmxStartChannel = 1; // starting channel to forward

// struct that holds the DMX data to be sent via ESP-NOW
struct DMXDataPacket {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
} dmxPacket;

void setup() {
  // initialize DMX frame
  for (int i = 0; i <= DMX_CHANNELS; i++) dmxRxData[i] = 0;

  // Setup DMX receiver
  pinMode(DMX_DE_PIN, OUTPUT);
  pinMode(DMX_RE_PIN, OUTPUT);
  digitalWrite(DMX_DE_PIN, HIGH);
  digitalWrite(DMX_RE_PIN, LOW);
  Serial1.begin(250000, SERIAL_8N2, DMX_RX_PIN, -1); 

  // check if a serial data is available
  if (Serial.available()) 
  {
    Serial.begin(115200);
    Serial.println("DMX Receiver Started");
    serialAvailable = true;
  }
}

void loop() {
  unsigned long now = millis();
  receiveDMX();

  if (dmxFrameReady) {
    dmxFrameReady = false;
    dmxPacket.red = dmxRxData[dmxStartChannel];
    dmxPacket.green = dmxRxData[dmxStartChannel + 1];
    dmxPacket.blue = dmxRxData[dmxStartChannel + 2];
    dmxPacket.white = dmxRxData[dmxStartChannel + 3];
  }

  // Do something every second
  if (now % 1000 == 0 && serialAvailable) {
    Serial.println("DMX Data:");
    for (int i = 0; i < DMX_FORWARD_CHANNELS; i++) {
      Serial.print(dmxRxData[dmxStartChannel + i]);
      Serial.print(" ");
    }
    Serial.println();
  }
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
