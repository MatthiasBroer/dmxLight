/*
  ESP32S3DMX.cpp - DMX512 receiver library for ESP32S3 with Arduino Core 3.0+
  
  Copyright (c) 2024
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
*/

#include "ESP32S3DMX.h"

// Static instance pointer for callbacks
ESP32S3DMX* ESP32S3DMX::instance = nullptr;

ESP32S3DMX::ESP32S3DMX() :
    dmxSerial(nullptr),
    uartNum(0),
    rxPin(-1),
    txPin(-1),
    enablePin(-1),
    inPacket(false),
    bufferIndex(0),
    lastPacketSize(0),
    lastPacketTime(0),
    packetCount(0),
    errorCount(0),
    lastPacketRateTime(0),
    lastPacketCountForRate(0),
    debugMode(false),
    initialized(false)
{
    // Clear buffers
    memset((void*)dmxData, 0, DMX_PACKET_SIZE);
    memset((void*)dmxBuffer, 0, DMX_BUFFER_SIZE);
}

ESP32S3DMX::~ESP32S3DMX() {
    end();
}

void ESP32S3DMX::begin(uint8_t uart_num, int rx_pin, int tx_pin, int enable_pin) {
    if (initialized) {
        end();
    }
    
    // Store configuration
    uartNum = uart_num;
    rxPin = rx_pin;
    txPin = tx_pin;
    enablePin = enable_pin;
    
    // Set instance pointer for callbacks
    instance = this;
    
    // Configure RS485 direction pin (receive mode)
    if (enablePin >= 0) {
        pinMode(enablePin, OUTPUT);
        digitalWrite(enablePin, LOW);  // Set to receive mode
    }
    
    // Create HardwareSerial instance for the specified UART
    dmxSerial = new HardwareSerial(uartNum);
    
    if (!dmxSerial) {
        return;
    }
    
    // Configure and start UART
    dmxSerial->begin(DMX_BAUDRATE, DMX_SERIAL_CONFIG, rxPin, txPin);
    dmxSerial->setRxBufferSize(DMX_BUFFER_SIZE);
    
    // Set up error callback for break detection
    dmxSerial->onReceiveError([](hardwareSerial_error_t error) {
        if (instance && error == UART_BREAK_ERROR) {
            instance->processBreak();
        }
    });
    
    // Set up data received callback
    dmxSerial->onReceive([]() {
        if (instance) {
            instance->processData();
        }
    });
    
    // Initialize timing
    lastPacketTime = millis();
    lastPacketRateTime = millis();
    
    // Clear UART buffer before starting
    delay(10);
    while (dmxSerial->available()) {
        dmxSerial->read();
    }
    
    initialized = true;
}

void ESP32S3DMX::end() {
    if (!initialized) {
        return;
    }
    
    if (dmxSerial) {
        dmxSerial->end();
        delete dmxSerial;
        dmxSerial = nullptr;
    }
    
    initialized = false;
    instance = nullptr;
}

void ESP32S3DMX::processBreak() {
    // Break detected - process previous packet if we have data
    if (bufferIndex > 0) {
        // ESP32S3 UART typically captures 1 extra byte at the start of break
        // Pattern: [break_artifact, start_code(0), ch1, ch2, ...]
        // Skip the first byte which is a break artifact
        int dataStart = (bufferIndex >= 2) ? 1 : 0;
        
        // Calculate size and copy data
        uint16_t dmxDataSize = bufferIndex - dataStart;
        uint16_t copySize = (dmxDataSize > DMX_PACKET_SIZE) ? DMX_PACKET_SIZE : dmxDataSize;
        
        if (copySize > 0) {
            noInterrupts();
            memcpy((void*)dmxData, (void*)&dmxBuffer[dataStart], copySize);
            lastPacketSize = copySize;
            lastPacketTime = millis();
            packetCount++;
            interrupts();
        }
    }
    
    // Reset buffer for next packet
    bufferIndex = 0;
}

void ESP32S3DMX::processData() {
    if (!dmxSerial) {
        return;
    }
    
    // Read all available data
    while (dmxSerial->available() && bufferIndex < DMX_BUFFER_SIZE) {
        dmxBuffer[bufferIndex++] = dmxSerial->read();
    }
}

uint8_t ESP32S3DMX::read(uint16_t channel) {
    if (channel == 0 || channel > DMX_CHANNELS || !initialized) {
        return 0;
    }
    
    // Check if we have recent data
    if (!isConnected()) {
        return 0;
    }
    
    // Bounds check against actual packet size
    if (channel >= lastPacketSize) {
        return 0;
    }
    
    return dmxData[channel];  // dmxData[0] is start code, dmxData[1] is channel 1
}

uint16_t ESP32S3DMX::readChannels(uint8_t* buffer, uint16_t start_channel, uint16_t count) {
    if (!buffer || start_channel == 0 || start_channel > DMX_CHANNELS || !initialized) {
        return 0;
    }
    
    // Check if we have recent data
    if (!isConnected()) {
        return 0;
    }
    
    // Calculate how many channels we can actually read
    uint16_t available = 0;
    if (lastPacketSize > start_channel) {
        available = lastPacketSize - start_channel;
    } else {
        return 0;
    }
    
    uint16_t toRead = (count < available) ? count : available;
    
    // Copy data
    noInterrupts();
    memcpy(buffer, (void*)&dmxData[start_channel], toRead);
    interrupts();
    
    return toRead;
}

bool ESP32S3DMX::isConnected() const {
    if (!initialized) {
        return false;
    }
    
    // Consider connected if we've received a packet within the timeout period
    return (millis() - lastPacketTime) < DMX_TIMEOUT_MS;
}

uint32_t ESP32S3DMX::timeSinceLastPacket() const {
    if (!initialized || lastPacketTime == 0) {
        return 0xFFFFFFFF;  // Max value indicates no packets received
    }
    
    return millis() - lastPacketTime;
}

float ESP32S3DMX::getPacketRate() const {
    if (!initialized || packetCount == 0) {
        return 0.0;
    }
    
    uint32_t now = millis();
    uint32_t timeDiff = now - lastPacketRateTime;
    
    if (timeDiff >= 1000) {  // Update rate every second
        // Calculate rate based on packets received in the last period
        uint32_t packetDiff = packetCount - lastPacketCountForRate;
        float rate = (packetDiff * 1000.0f) / timeDiff;
        
        // Update for next calculation
        const_cast<ESP32S3DMX*>(this)->lastPacketRateTime = now;
        const_cast<ESP32S3DMX*>(this)->lastPacketCountForRate = packetCount;
        
        return rate;
    } else if (lastPacketCountForRate == 0) {
        // First calculation
        float rate = (packetCount * 1000.0f) / (now - 0);
        const_cast<ESP32S3DMX*>(this)->lastPacketRateTime = now;
        const_cast<ESP32S3DMX*>(this)->lastPacketCountForRate = packetCount;
        return rate;
    }
    
    // Return last calculated rate
    uint32_t packetDiff = packetCount - lastPacketCountForRate;
    return (packetDiff * 1000.0f) / timeDiff;
}