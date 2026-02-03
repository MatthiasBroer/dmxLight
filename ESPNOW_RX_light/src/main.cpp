#include <Arduino.h>

// struct that holds the DMX data to be sent via ESP-NOW
struct DMXDataPacket {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
} dmxPacket;

void setup() {
  // put your setup code here, to run once:
}

void loop() {
  // put your main code here, to run repeatedly:
}