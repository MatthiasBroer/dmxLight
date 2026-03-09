/**
 * @file ESP32S3DMX.h
 * @brief DMX512 receiver library for ESP32 with Arduino Core 3.0+
 * @version 1.0.0
 * @date 2024
 * 
 * @copyright Copyright (c) 2024. Licensed under LGPL v2.1
 * 
 * This library provides DMX512 receive functionality for ESP32 boards
 * using the Arduino Core 3.0+ framework. It handles the specific UART
 * behavior of ESP32-S3 chips and provides a simple API for reading
 * DMX channel data.
 * 
 * @see https://github.com/yourusername/ESP32S3DMX
 */

#ifndef ESP32S3DMX_H
#define ESP32S3DMX_H

#include <Arduino.h>
#include <HardwareSerial.h>

// DMX Protocol Constants
#define DMX_BAUDRATE 250000          ///< DMX512 baud rate (250kbps)
#define DMX_SERIAL_CONFIG SERIAL_8N2 ///< 8 data bits, no parity, 2 stop bits
#define DMX_CHANNELS 512             ///< Maximum DMX channels per universe
#define DMX_PACKET_SIZE 513          ///< Start code + 512 channels
#define DMX_BUFFER_SIZE 514          ///< Extra byte for UART break artifact

// DMX Timing Constants
#define DMX_BREAK_MIN 88       ///< Minimum break time in microseconds
#define DMX_MAB_MIN 8          ///< Minimum mark after break in microseconds
#define DMX_TIMEOUT_MS 1000    ///< Connection timeout in milliseconds

// Default Pin Configuration
#define DEFAULT_RX_PIN 6       ///< Default GPIO for UART RX
#define DEFAULT_TX_PIN 4       ///< Default GPIO for UART TX
#define DEFAULT_ENABLE_PIN 5   ///< Default GPIO for RS485 direction control

/**
 * @class ESP32S3DMX
 * @brief DMX512 receiver implementation for ESP32
 * 
 * This class provides a complete DMX512 receiver implementation that is
 * compatible with Arduino Core 3.0+. It uses hardware UART with break
 * detection to receive DMX data efficiently.
 * 
 * @note Only one instance should be created per UART peripheral
 * 
 * Example usage:
 * @code
 * ESP32S3DMX dmx;
 * 
 * void setup() {
 *     dmx.begin();  // Initialize with default pins
 * }
 * 
 * void loop() {
 *     if (dmx.isConnected()) {
 *         uint8_t ch1 = dmx.read(1);
 *         // Use channel data...
 *     }
 * }
 * @endcode
 */
class ESP32S3DMX {
public:
    /**
     * @brief Construct a new ESP32S3DMX object
     */
    ESP32S3DMX();
    
    /**
     * @brief Destroy the ESP32S3DMX object and release resources
     */
    ~ESP32S3DMX();
    
    /**
     * @brief Initialize the DMX receiver
     * 
     * @param uart_num UART peripheral to use (1 or 2, not 0 on ESP32-S3)
     * @param rx_pin GPIO pin connected to RS485 RO (receive)
     * @param tx_pin GPIO pin connected to RS485 DI (transmit) - not used for receive
     * @param enable_pin GPIO pin connected to RS485 DE/RE (direction control)
     * 
     * @note UART0 is not supported on ESP32-S3 when USB CDC is enabled
     */
    void begin(uint8_t uart_num = 2, 
               int rx_pin = DEFAULT_RX_PIN, 
               int tx_pin = DEFAULT_TX_PIN, 
               int enable_pin = DEFAULT_ENABLE_PIN);
    
    /**
     * @brief Stop the DMX receiver and release resources
     */
    void end();
    
    /**
     * @brief Read a single DMX channel value
     * 
     * @param channel DMX channel number (1-512)
     * @return uint8_t Channel value (0-255), or 0 if channel is invalid or no signal
     */
    uint8_t read(uint16_t channel);
    
    /**
     * @brief Read multiple consecutive channels
     * 
     * @param buffer Array to store channel values
     * @param start_channel First channel to read (1-512)
     * @param count Number of channels to read
     * @return uint16_t Number of channels actually read
     * 
     * @note This method is more efficient than multiple read() calls
     */
    uint16_t readChannels(uint8_t* buffer, uint16_t start_channel, uint16_t count);
    
    /**
     * @brief Get direct access to the DMX data buffer
     * 
     * @return const uint8_t* Pointer to 513-byte buffer (start code + 512 channels)
     * @warning The buffer contents may change during access. Use with caution.
     */
    const uint8_t* getBuffer() const { return (const uint8_t*)dmxData; }
    
    /**
     * @brief Check if receiving valid DMX signal
     * 
     * @return true if DMX received within last second
     * @return false if no recent DMX signal
     */
    bool isConnected() const;
    
    /**
     * @brief Get time since last valid DMX packet
     * 
     * @return uint32_t Milliseconds since last packet, or 0xFFFFFFFF if never received
     */
    uint32_t timeSinceLastPacket() const;
    
    /**
     * @brief Get total number of valid packets received
     * 
     * @return uint32_t Packet count since initialization
     */
    uint32_t getPacketCount() const { return packetCount; }
    
    /**
     * @brief Get total number of reception errors
     * 
     * @return uint32_t Error count since initialization
     */
    uint32_t getErrorCount() const { return errorCount; }
    
    /**
     * @brief Get current packet reception rate
     * 
     * @return float Packets per second (typically 44-50 Hz for DMX)
     */
    float getPacketRate() const;
    
    /**
     * @brief Get size of last received packet
     * 
     * @return uint16_t Packet size in bytes (1-513)
     */
    uint16_t getLastPacketSize() const { return lastPacketSize; }
    
private:
    HardwareSerial* dmxSerial;               ///< UART instance
    uint8_t uartNum;                         ///< UART peripheral number
    int rxPin;                               ///< RX pin number
    int txPin;                               ///< TX pin number
    int enablePin;                           ///< RS485 direction control pin
    
    volatile uint8_t dmxData[DMX_PACKET_SIZE];    ///< DMX data buffer
    volatile uint8_t dmxBuffer[DMX_BUFFER_SIZE];  ///< UART receive buffer
    
    volatile bool inPacket;                  ///< Currently receiving packet flag
    volatile uint16_t bufferIndex;           ///< Current buffer position
    volatile uint16_t lastPacketSize;        ///< Size of last complete packet
    volatile uint32_t lastPacketTime;        ///< Timestamp of last packet
    volatile uint32_t packetCount;           ///< Total packets received
    volatile uint32_t errorCount;            ///< Total errors detected
    uint32_t lastPacketRateTime;            ///< Last rate calculation time
    uint32_t lastPacketCountForRate;        ///< Packet count at last rate calc
    
    bool debugMode;                          ///< Debug output enable flag
    bool initialized;                        ///< Initialization status
    
    static ESP32S3DMX* instance;             ///< Singleton instance for callbacks
    
    /**
     * @brief Process UART break detection
     * @private
     */
    void processBreak();
    
    /**
     * @brief Process received UART data
     * @private
     */
    void processData();
};

#endif // ESP32S3DMX_H