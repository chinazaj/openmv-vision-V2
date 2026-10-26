#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/spi_slave.h>

#include "video_build_config.h"

class SpiVideoReceiver {
public:
    static constexpr uint16_t SPI_MAGIC = 0x55AA;
    static constexpr uint8_t SPI_VERSION = 2;
    static constexpr uint8_t SPI_TYPE_FRAME = 1;
    static constexpr uint8_t SPI_TYPE_TEST = 2; // Development diagnostic only.
    static constexpr size_t SPI_HEADER_SIZE = 18;
    static constexpr size_t SPI_PAYLOAD_SIZE = 4000;
    static constexpr size_t MAX_FRAME_SIZE = 48 * 1024;
    static constexpr size_t SPI_PACKET_SIZE = (SPI_HEADER_SIZE + SPI_PAYLOAD_SIZE + 3) & ~static_cast<size_t>(3);
    // Keep several DMA transactions armed so the slave does not need to
    // re-arm the peripheral in software between consecutive CS assertions.
    static constexpr size_t SPI_RX_TRANSACTION_BUFFER_COUNT = 4;

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

#if VIDEO_CORE_DEVELOPMENT
        // Development-only SPI transaction diagnostics.
        uint32_t transactions = 0;
        uint32_t full_transactions = 0;
        uint32_t short_transactions = 0;
        uint32_t zero_transactions = 0;
        uint32_t bad_magic = 0;
        uint32_t bad_version = 0;
        uint32_t bad_type = 0;
        uint32_t bad_payload = 0;
        uint32_t bad_packet_range = 0;
        uint32_t bad_frame_meta = 0;
        uint32_t bad_sequence = 0;
        uint32_t bad_jpeg = 0;
        uint32_t bad_crc = 0;
        uint32_t no_receive_buffer = 0;
        uint32_t stale_receive_frame = 0;
        uint32_t queue_errors = 0;
        uint32_t trans_len_0_31 = 0;
        uint32_t trans_len_32_4095 = 0;
        uint32_t trans_len_4096_8191 = 0;
        uint32_t trans_len_8192_plus = 0;
        uint32_t bad_header_len_0_31 = 0;
        uint32_t bad_header_len_32_4095 = 0;
        uint32_t bad_header_len_4096_8191 = 0;
        uint32_t bad_header_len_8192_plus = 0;
        uint32_t last_trans_len = 0;
        uint32_t last_bad_header_len = 0;
        uint32_t last_good_frame_ms = 0;
        uint32_t consecutive_bad_transactions = 0;
        uint32_t max_consecutive_bad_transactions = 0;
        uint32_t bad_burst_start_tx = 0;
        uint32_t bad_burst_end_tx = 0;
        uint32_t bad_burst_frame_id = 0;
        uint32_t bad_burst_packet_idx = 0;
        uint32_t recovered_after_bad_burst = 0;
        uint32_t test_packets_ok = 0;
        uint32_t test_packets_bad = 0;
        uint32_t test_crc_errors = 0;
        uint32_t test_pattern_errors = 0;
#endif
    };

    SpiVideoReceiver();
    ~SpiVideoReceiver();

    bool begin();
    void end();

    bool acquireLatestFrame(FrameView& frame);
    void releaseFrame(FrameView& frame);

    Stats getStats() const;
    void resetStats();

#if VIDEO_CORE_DEVELOPMENT
    void printDiagnostics() const;
#endif

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

    uint8_t* _dmaRxBuffers[SPI_RX_TRANSACTION_BUFFER_COUNT] = {nullptr, nullptr, nullptr, nullptr};
    spi_slave_transaction_t _transactions[SPI_RX_TRANSACTION_BUFFER_COUNT] = {};
    uint8_t* _frameBuffers[2] = {nullptr, nullptr};

    volatile uint8_t _bufferState[2] = {0, 0};
    volatile size_t _bufferSize[2] = {0, 0};
    volatile uint16_t _bufferFrameId[2] = {0, 0};
    volatile int _readyBuffer = -1;

    portMUX_TYPE _frameMux = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _taskExitSemaphore = nullptr;
    volatile bool _running = false;
    bool _spiInitialized = false;

    Stats _stats;
    uint16_t _lastFrameId = 0;
    bool _haveLastFrameId = false;
};
