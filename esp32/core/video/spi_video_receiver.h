#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class SpiVideoReceiver {
public:
    static constexpr uint16_t SPI_MAGIC = 0x55AA;
    static constexpr uint8_t SPI_VERSION = 2;
    static constexpr uint8_t SPI_TYPE_FRAME = 1;
    static constexpr size_t SPI_HEADER_SIZE = 18;
    static constexpr size_t SPI_PAYLOAD_SIZE = 4096;
    static constexpr size_t MAX_FRAME_SIZE = 48 * 1024;
    static constexpr size_t SPI_PACKET_SIZE = (SPI_HEADER_SIZE + SPI_PAYLOAD_SIZE + 3) & ~static_cast<size_t>(3);

    struct FrameView {
        const uint8_t* data = nullptr;
        size_t size = 0;
        int buffer_index = -1;
        uint16_t frame_id = 0;
    };

    struct Stats {
        uint32_t ok_frames = 0;
        uint32_t bad_frames = 0;
        uint32_t bad_headers = 0;
        uint32_t dropped_frames = 0;
        uint32_t timeouts = 0;
        uint32_t oversize = 0;
        uint32_t frame_id_gaps = 0;
    };

    SpiVideoReceiver();
    ~SpiVideoReceiver();

    bool begin();
    void end();

    bool acquireLatestFrame(FrameView& frame);
    void releaseFrame(FrameView& frame);

    Stats getStats() const;
    void resetStats();

private:
    struct PacketHeader {
        uint16_t magic;
        uint8_t version;
        uint8_t type;
        uint16_t frame_id;
        uint16_t packet_idx;
        uint16_t total_packets;
        uint16_t payload_len;
        uint32_t frame_size;
        uint16_t frame_crc16;
    };

    static uint16_t crc16Ccitt(const uint8_t* data, size_t len);
    static bool parseHeader(const uint8_t* packet, PacketHeader& header);

    int findReceiveBuffer();
    void publishFrame(int index, size_t size, uint16_t frameId);
    void receiveTask();
    static void receiveTaskEntry(void* arg);

    uint8_t* _dmaRxBuffer = nullptr;
    uint8_t* _frameBuffers[2] = {nullptr, nullptr};

    volatile uint8_t _bufferState[2] = {0, 0};
    volatile size_t _bufferSize[2] = {0, 0};
    volatile uint16_t _bufferFrameId[2] = {0, 0};
    volatile int _readyBuffer = -1;

    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    volatile bool _running = false;
    bool _spiInitialized = false;

    Stats _stats;
    uint16_t _lastFrameId = 0;
    bool _haveLastFrameId = false;
};
