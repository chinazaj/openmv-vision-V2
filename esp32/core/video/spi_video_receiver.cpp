#include "spi_video_receiver.h"

#include <esp_heap_caps.h>
#include <string.h>

namespace {
constexpr int SPI_SCK = 4;
constexpr int SPI_MISO = 5;
constexpr int SPI_MOSI = 6;
constexpr int SPI_CS = 7;
constexpr int FRAME_BUFFER_COUNT = 2;

#if VIDEO_CORE_DEVELOPMENT
constexpr size_t DIAG_HEAD_BYTES = 32;
constexpr size_t DIAG_TAIL_BYTES = 18;
portMUX_TYPE g_diagMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_lastGoodHeader[SpiVideoReceiver::SPI_HEADER_SIZE] = {};
uint8_t g_lastBadHeader[SpiVideoReceiver::SPI_HEADER_SIZE] = {};
uint8_t g_lastGoodHead[DIAG_HEAD_BYTES] = {};
uint8_t g_lastBadHead[DIAG_HEAD_BYTES] = {};
uint8_t g_lastGoodTail[DIAG_TAIL_BYTES] = {};
uint8_t g_lastBadTail[DIAG_TAIL_BYTES] = {};
uint32_t g_lastGoodHeaderLen = 0;
uint32_t g_lastBadHeaderLen = 0;
uint32_t g_lastGoodHeaderTx = 0;
uint32_t g_lastBadHeaderTx = 0;
uint32_t g_lastRxMicros = 0;
uint32_t g_lastQueueMicros = 0;
int32_t g_lastQueueResult = 0;
#endif

esp_err_t queueTransaction(spi_slave_transaction_t* transaction, TickType_t timeout) {
    const esp_err_t result = spi_slave_queue_trans(SPI2_HOST, transaction, timeout);
#if VIDEO_CORE_DEVELOPMENT
    g_lastQueueMicros = micros();
    g_lastQueueResult = static_cast<int32_t>(result);
#endif
    return result;
}
}

SpiVideoReceiver::SpiVideoReceiver() = default;

SpiVideoReceiver::~SpiVideoReceiver() {
    end();
}

bool SpiVideoReceiver::begin() {
    if (_running) return true;

    for (size_t i = 0; i < SPI_RX_TRANSACTION_BUFFER_COUNT; ++i) {
        _dmaRxBuffers[i] = static_cast<uint8_t*>(heap_caps_malloc(SPI_PACKET_SIZE, MALLOC_CAP_DMA));
        if (_dmaRxBuffers[i] == nullptr) {
            end();
            return false;
        }
    }

    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        _frameBuffers[i] = static_cast<uint8_t*>(heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_8BIT));
        if (_frameBuffers[i] == nullptr) {
            end();
            return false;
        }
        _bufferState[i] = 0;
        _bufferSize[i] = 0;
        _bufferFrameId[i] = 0;
    }

    spi_bus_config_t buscfg{};
    buscfg.mosi_io_num = SPI_MOSI;
    buscfg.miso_io_num = SPI_MISO;
    buscfg.sclk_io_num = SPI_SCK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SPI_PACKET_SIZE;

    spi_slave_interface_config_t slvcfg{};
    slvcfg.spics_io_num = SPI_CS;
    slvcfg.flags = 0;
    slvcfg.queue_size = SPI_RX_TRANSACTION_BUFFER_COUNT;
    slvcfg.mode = 1;

    const esp_err_t initResult = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    if (initResult != ESP_OK) {
        end();
        return false;
    }
    _spiInitialized = true;

    for (size_t i = 0; i < SPI_RX_TRANSACTION_BUFFER_COUNT; ++i) {
        _transactions[i] = spi_slave_transaction_t{};
        _transactions[i].length = SPI_PACKET_SIZE * 8;
        _transactions[i].rx_buffer = _dmaRxBuffers[i];

        const esp_err_t queueResult = queueTransaction(
            &_transactions[i], pdMS_TO_TICKS(1000));
        if (queueResult != ESP_OK) {
            end();
            return false;
        }
    }

#if VIDEO_CORE_DEVELOPMENT
    Serial.printf("[SPI DIAG BOOT] side=ESP32 diag_rev=20261025A spi_host=SPI2 mode=1 cpol=0 cpha=1 sclk=%d mosi=%d miso=%d cs=%d master_clock_hz=not_reported_by_slave_driver payload_max=%u header_size=%u packet_size=%u dma_queue_depth=%u\n",
                  SPI_SCK, SPI_MOSI, SPI_MISO, SPI_CS,
                  static_cast<unsigned>(SPI_PAYLOAD_SIZE),
                  static_cast<unsigned>(SPI_HEADER_SIZE),
                  static_cast<unsigned>(SPI_PACKET_SIZE),
                  static_cast<unsigned>(SPI_RX_TRANSACTION_BUFFER_COUNT));
#endif
    _running = true;
    const BaseType_t taskResult = xTaskCreate(
        &SpiVideoReceiver::receiveTaskEntry, "spi_rx", 6144, this, 3, &_task);
    if (taskResult != pdPASS) {
        _running = false;
        end();
        return false;
    }
    return true;
}

void SpiVideoReceiver::end() {
    _running = false;

    if (_task != nullptr && xTaskGetCurrentTaskHandle() != _task) {
        vTaskDelay(pdMS_TO_TICKS(10));
        _task = nullptr;
    }

    if (_spiInitialized) {
        spi_slave_free(SPI2_HOST);
        _spiInitialized = false;
    }

    for (auto& buffer : _dmaRxBuffers) {
        if (buffer != nullptr) {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
    }

    for (auto& buffer : _frameBuffers) {
        if (buffer != nullptr) {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
    }
    _readyBuffer = -1;
}

uint16_t SpiVideoReceiver::crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= static_cast<uint16_t>(*data++) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool SpiVideoReceiver::parseHeader(const uint8_t* packet, PacketHeader& header) {
    header.magic = static_cast<uint16_t>(packet[0]) | static_cast<uint16_t>(packet[1]) << 8;
    header.version = packet[2];
    header.type = packet[3];
    header.frame_id = static_cast<uint16_t>(packet[4]) | static_cast<uint16_t>(packet[5]) << 8;
    header.packet_idx = static_cast<uint16_t>(packet[6]) | static_cast<uint16_t>(packet[7]) << 8;
    header.total_packets = static_cast<uint16_t>(packet[8]) | static_cast<uint16_t>(packet[9]) << 8;
    header.payload_len = static_cast<uint16_t>(packet[10]) | static_cast<uint16_t>(packet[11]) << 8;
    header.frame_size = static_cast<uint32_t>(packet[12]) |
                        static_cast<uint32_t>(packet[13]) << 8 |
                        static_cast<uint32_t>(packet[14]) << 16 |
                        static_cast<uint32_t>(packet[15]) << 24;
    header.frame_crc16 = static_cast<uint16_t>(packet[16]) | static_cast<uint16_t>(packet[17]) << 8;

    return header.magic == SPI_MAGIC && header.version == SPI_VERSION && header.type == SPI_TYPE_FRAME;
}

int SpiVideoReceiver::findReceiveBuffer() {
    int index = -1;
    portENTER_CRITICAL(&_frameMux);
    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        if (_bufferState[i] == 0) {
            index = i;
            break;
        }
    }
    if (index < 0 && _readyBuffer >= 0 && _bufferState[_readyBuffer] == 2) {
        index = _readyBuffer;
        _readyBuffer = -1;
        ++_stats.dropped_frames;
    }
    if (index >= 0) {
        _bufferState[index] = 1;
        _bufferSize[index] = 0;
    }
    portEXIT_CRITICAL(&_frameMux);
    return index;
}

void SpiVideoReceiver::publishFrame(int index, size_t size, uint16_t frameId) {
    portENTER_CRITICAL(&_frameMux);
    _bufferSize[index] = size;
    _bufferFrameId[index] = frameId;
    _bufferState[index] = 2;

    const int oldReady = _readyBuffer;
    _readyBuffer = index;
    if (oldReady >= 0 && oldReady != index && _bufferState[oldReady] == 2) {
        _bufferState[oldReady] = 0;
        ++_stats.dropped_frames;
    }
    portEXIT_CRITICAL(&_frameMux);
}

bool SpiVideoReceiver::acquireLatestFrame(FrameView& frame) {
    frame = FrameView{};
    portENTER_CRITICAL(&_frameMux);
    if (_readyBuffer >= 0) {
        const int index = _readyBuffer;
        _readyBuffer = -1;
        if (_bufferState[index] == 2) {
            _bufferState[index] = 3;
            frame.data = _frameBuffers[index];
            frame.size = _bufferSize[index];
            frame.buffer_index = index;
            frame.frame_id = _bufferFrameId[index];
        }
    }
    portEXIT_CRITICAL(&_frameMux);
    return frame.buffer_index >= 0;
}

void SpiVideoReceiver::releaseFrame(FrameView& frame) {
    if (frame.buffer_index < 0 || frame.buffer_index >= FRAME_BUFFER_COUNT) {
        frame = FrameView{};
        return;
    }
    portENTER_CRITICAL(&_frameMux);
    if (_bufferState[frame.buffer_index] == 3) _bufferState[frame.buffer_index] = 0;
    portEXIT_CRITICAL(&_frameMux);
    frame = FrameView{};
}

SpiVideoReceiver::Stats SpiVideoReceiver::getStats() const {
#if VIDEO_CORE_DEVELOPMENT
    static uint32_t lastDiagPrintMs = 0;
    const uint32_t now = millis();
    if (now - lastDiagPrintMs >= 5000) {
        lastDiagPrintMs = now;
        printDiagnostics();
    }
#endif
    return _stats;
}

void SpiVideoReceiver::resetStats() {
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&_frameMux));
    _stats = Stats{};
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&_frameMux));
}

#if VIDEO_CORE_DEVELOPMENT
void SpiVideoReceiver::printDiagnostics() const {
    const Stats stats = _stats;
    uint8_t lastGoodHeader[SPI_HEADER_SIZE] = {};
    uint8_t lastBadHeader[SPI_HEADER_SIZE] = {};
    uint8_t lastGoodHead[DIAG_HEAD_BYTES] = {};
    uint8_t lastBadHead[DIAG_HEAD_BYTES] = {};
    uint8_t lastGoodTail[DIAG_TAIL_BYTES] = {};
    uint8_t lastBadTail[DIAG_TAIL_BYTES] = {};
    uint32_t lastGoodHeaderLen = 0;
    uint32_t lastBadHeaderLen = 0;
    uint32_t lastGoodHeaderTx = 0;
    uint32_t lastBadHeaderTx = 0;
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&g_diagMux));
    memcpy(lastGoodHeader, g_lastGoodHeader, SPI_HEADER_SIZE);
    memcpy(lastBadHeader, g_lastBadHeader, SPI_HEADER_SIZE);
    memcpy(lastGoodHead, g_lastGoodHead, DIAG_HEAD_BYTES);
    memcpy(lastBadHead, g_lastBadHead, DIAG_HEAD_BYTES);
    memcpy(lastGoodTail, g_lastGoodTail, DIAG_TAIL_BYTES);
    memcpy(lastBadTail, g_lastBadTail, DIAG_TAIL_BYTES);
    lastGoodHeaderLen = g_lastGoodHeaderLen;
    lastBadHeaderLen = g_lastBadHeaderLen;
    lastGoodHeaderTx = g_lastGoodHeaderTx;
    lastBadHeaderTx = g_lastBadHeaderTx;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&g_diagMux));

    Serial.printf("[SPI DIAG] timing: last_rx_us=%lu last_queue_us=%lu rx_to_queue_us=%lu queue_result=%ld queue_depth=%u\n",
                  static_cast<unsigned long>(g_lastRxMicros),
                  static_cast<unsigned long>(g_lastQueueMicros),
                  static_cast<unsigned long>(g_lastQueueMicros - g_lastRxMicros),
                  static_cast<long>(g_lastQueueResult),
                  static_cast<unsigned>(SPI_RX_TRANSACTION_BUFFER_COUNT));
    Serial.printf(
        "[SPI DIAG] tx=%lu full=%lu short=%lu zero=%lu last_len=%lu "
        "len[0-31]=%lu len[32-4095]=%lu len[4096-8191]=%lu len[8192+]=%lu\n",
        static_cast<unsigned long>(stats.transactions),
        static_cast<unsigned long>(stats.full_transactions),
        static_cast<unsigned long>(stats.short_transactions),
        static_cast<unsigned long>(stats.zero_transactions),
        static_cast<unsigned long>(stats.last_trans_len),
        static_cast<unsigned long>(stats.trans_len_0_31),
        static_cast<unsigned long>(stats.trans_len_32_4095),
        static_cast<unsigned long>(stats.trans_len_4096_8191),
        static_cast<unsigned long>(stats.trans_len_8192_plus));
    Serial.printf(
        "[SPI DIAG] bad: magic=%lu version=%lu type=%lu payload=%lu "
        "packet_range=%lu frame_meta=%lu sequence=%lu jpeg=%lu crc=%lu\n",
        static_cast<unsigned long>(stats.bad_magic),
        static_cast<unsigned long>(stats.bad_version),
        static_cast<unsigned long>(stats.bad_type),
        static_cast<unsigned long>(stats.bad_payload),
        static_cast<unsigned long>(stats.bad_packet_range),
        static_cast<unsigned long>(stats.bad_frame_meta),
        static_cast<unsigned long>(stats.bad_sequence),
        static_cast<unsigned long>(stats.bad_jpeg),
        static_cast<unsigned long>(stats.bad_crc));
    Serial.printf(
        "[SPI DIAG] burst: qerr=%lu bad_len[0-31]=%lu bad_len[32-4095]=%lu "
        "bad_len[4096-8191]=%lu bad_len[8192+]=%lu last_bad_len=%lu "
        "burst_start=%lu burst_end=%lu burst_frame=%lu burst_packet=%lu recovered=%lu\n",
        static_cast<unsigned long>(stats.queue_errors),
        static_cast<unsigned long>(stats.bad_header_len_0_31),
        static_cast<unsigned long>(stats.bad_header_len_32_4095),
        static_cast<unsigned long>(stats.bad_header_len_4096_8191),
        static_cast<unsigned long>(stats.bad_header_len_8192_plus),
        static_cast<unsigned long>(stats.last_bad_header_len),
        static_cast<unsigned long>(stats.bad_burst_start_tx),
        static_cast<unsigned long>(stats.bad_burst_end_tx),
        static_cast<unsigned long>(stats.bad_burst_frame_id),
        static_cast<unsigned long>(stats.bad_burst_packet_idx),
        static_cast<unsigned long>(stats.recovered_after_bad_burst));
    Serial.printf(
        "[SPI DIAG] state: no_buf=%lu stale_frame=%lu consecutive_bad=%lu "
        "max_consecutive_bad=%lu last_good_frame_ms=%lu\n",
        static_cast<unsigned long>(stats.no_receive_buffer),
        static_cast<unsigned long>(stats.stale_receive_frame),
        static_cast<unsigned long>(stats.consecutive_bad_transactions),
        static_cast<unsigned long>(stats.max_consecutive_bad_transactions),
        static_cast<unsigned long>(stats.last_good_frame_ms));

    Serial.printf("[SPI DIAG] raw_good tx=%lu len=%lu:",
                  static_cast<unsigned long>(lastGoodHeaderTx),
                  static_cast<unsigned long>(lastGoodHeaderLen));
    for (size_t i = 0; i < SPI_HEADER_SIZE; ++i) Serial.printf(" %02X", lastGoodHeader[i]);
    Serial.println();

    Serial.printf("[SPI DIAG] raw_bad tx=%lu len=%lu:",
                  static_cast<unsigned long>(lastBadHeaderTx),
                  static_cast<unsigned long>(lastBadHeaderLen));
    for (size_t i = 0; i < SPI_HEADER_SIZE; ++i) Serial.printf(" %02X", lastBadHeader[i]);
    Serial.println();

    Serial.printf("[SPI DIAG] raw_good_head tx=%lu:", static_cast<unsigned long>(lastGoodHeaderTx));
    for (size_t i = 0; i < DIAG_HEAD_BYTES; ++i) Serial.printf(" %02X", lastGoodHead[i]);
    Serial.println();

    Serial.printf("[SPI DIAG] raw_good_tail tx=%lu:", static_cast<unsigned long>(lastGoodHeaderTx));
    for (size_t i = 0; i < DIAG_TAIL_BYTES; ++i) Serial.printf(" %02X", lastGoodTail[i]);
    Serial.println();

    Serial.printf("[SPI DIAG] raw_bad_head tx=%lu:", static_cast<unsigned long>(lastBadHeaderTx));
    for (size_t i = 0; i < DIAG_HEAD_BYTES; ++i) Serial.printf(" %02X", lastBadHead[i]);
    Serial.println();

    Serial.printf("[SPI DIAG] raw_bad_tail tx=%lu:", static_cast<unsigned long>(lastBadHeaderTx));
    for (size_t i = 0; i < DIAG_TAIL_BYTES; ++i) Serial.printf(" %02X", lastBadTail[i]);
    Serial.println();
}
#endif

void SpiVideoReceiver::receiveTaskEntry(void* arg) {
    static_cast<SpiVideoReceiver*>(arg)->receiveTask();
    vTaskDelete(nullptr);
}

void SpiVideoReceiver::receiveTask() {
    int rxBufferIndex = -1;
    uint16_t rxFrameId = 0;
    uint16_t expectedPackets = 0;
    uint32_t expectedFrameSize = 0;
    uint16_t expectedFrameCrc = 0;
    size_t receivedBytes = 0;
    uint16_t nextPacket = 0;

    while (_running) {
        spi_slave_transaction_t* transaction = nullptr;
        const esp_err_t result = spi_slave_get_trans_result(
            SPI2_HOST, &transaction, pdMS_TO_TICKS(1000));
        if (result != ESP_OK) {
            ++_stats.timeouts;
            continue;
        }
        if (transaction == nullptr || transaction->rx_buffer == nullptr) {
            ++_stats.bad_frames;
            continue;
        }

        const size_t receivedBytesOnWire = transaction->trans_len / 8;
        uint8_t* packetBuffer = static_cast<uint8_t*>(transaction->rx_buffer);
#if VIDEO_CORE_DEVELOPMENT
        g_lastRxMicros = micros();
#endif

#if VIDEO_CORE_DEVELOPMENT
        ++_stats.transactions;
        _stats.last_trans_len = static_cast<uint32_t>(receivedBytesOnWire);
        if (receivedBytesOnWire == 0) ++_stats.zero_transactions;
        else if (receivedBytesOnWire == SPI_PACKET_SIZE) ++_stats.full_transactions;
        else ++_stats.short_transactions;
        if (receivedBytesOnWire < 32) ++_stats.trans_len_0_31;
        else if (receivedBytesOnWire < SPI_HEADER_SIZE + SPI_PAYLOAD_SIZE) ++_stats.trans_len_32_4095;
        else if (receivedBytesOnWire < SPI_PACKET_SIZE * 2) ++_stats.trans_len_4096_8191;
        else ++_stats.trans_len_8192_plus;
#endif

        if (receivedBytesOnWire < SPI_HEADER_SIZE) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            _stats.last_bad_header_len = static_cast<uint32_t>(receivedBytesOnWire);
            if (receivedBytesOnWire < 32) ++_stats.bad_header_len_0_31;
            else if (receivedBytesOnWire < SPI_HEADER_SIZE + SPI_PAYLOAD_SIZE) ++_stats.bad_header_len_32_4095;
            else if (receivedBytesOnWire < SPI_PACKET_SIZE * 2) ++_stats.bad_header_len_4096_8191;
            else ++_stats.bad_header_len_8192_plus;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
            portENTER_CRITICAL(&g_diagMux);
            memset(g_lastBadHeader, 0, SPI_HEADER_SIZE);
            memcpy(g_lastBadHeader, packetBuffer, receivedBytesOnWire);
            memset(g_lastBadHead, 0, DIAG_HEAD_BYTES);
            memcpy(g_lastBadHead, packetBuffer, receivedBytesOnWire < DIAG_HEAD_BYTES ? receivedBytesOnWire : DIAG_HEAD_BYTES);
            memset(g_lastBadTail, 0, DIAG_TAIL_BYTES);
            if (receivedBytesOnWire >= DIAG_TAIL_BYTES) {
                memcpy(g_lastBadTail, packetBuffer + receivedBytesOnWire - DIAG_TAIL_BYTES, DIAG_TAIL_BYTES);
            } else if (receivedBytesOnWire > 0) {
                memcpy(g_lastBadTail, packetBuffer, receivedBytesOnWire);
            }
            g_lastBadHeaderLen = static_cast<uint32_t>(receivedBytesOnWire);
            g_lastBadHeaderTx = _stats.transactions;
            portEXIT_CRITICAL(&g_diagMux);
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        PacketHeader header{};
        parseHeader(packetBuffer, header);
        const bool magicOk = header.magic == SPI_MAGIC;
        const bool versionOk = header.version == SPI_VERSION;
        const bool typeOk = header.type == SPI_TYPE_FRAME;

        if (!magicOk || !versionOk || !typeOk) {
            ++_stats.bad_frames;
            ++_stats.bad_headers;
#if VIDEO_CORE_DEVELOPMENT
            if (!magicOk) ++_stats.bad_magic;
            if (!versionOk) ++_stats.bad_version;
            if (!typeOk) ++_stats.bad_type;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            _stats.last_bad_header_len = static_cast<uint32_t>(receivedBytesOnWire);
            if (receivedBytesOnWire < 32) ++_stats.bad_header_len_0_31;
            else if (receivedBytesOnWire < SPI_HEADER_SIZE + SPI_PAYLOAD_SIZE) ++_stats.bad_header_len_32_4095;
            else if (receivedBytesOnWire < SPI_PACKET_SIZE * 2) ++_stats.bad_header_len_4096_8191;
            else ++_stats.bad_header_len_8192_plus;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
            portENTER_CRITICAL(&g_diagMux);
            memcpy(g_lastBadHeader, packetBuffer, SPI_HEADER_SIZE);
            g_lastBadHeaderLen = static_cast<uint32_t>(receivedBytesOnWire);
            g_lastBadHeaderTx = _stats.transactions;
            memcpy(g_lastBadHead, packetBuffer, DIAG_HEAD_BYTES);
            if (receivedBytesOnWire >= DIAG_TAIL_BYTES) {
                memcpy(g_lastBadTail, packetBuffer + receivedBytesOnWire - DIAG_TAIL_BYTES, DIAG_TAIL_BYTES);
            } else {
                memset(g_lastBadTail, 0, DIAG_TAIL_BYTES);
                memcpy(g_lastBadTail, packetBuffer, receivedBytesOnWire);
            }
            portEXIT_CRITICAL(&g_diagMux);
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

#if VIDEO_CORE_DEVELOPMENT
        portENTER_CRITICAL(&g_diagMux);
        memcpy(g_lastGoodHeader, packetBuffer, SPI_HEADER_SIZE);
        g_lastGoodHeaderLen = static_cast<uint32_t>(receivedBytesOnWire);
        g_lastGoodHeaderTx = _stats.transactions;
        memcpy(g_lastGoodHead, packetBuffer, DIAG_HEAD_BYTES);
        if (receivedBytesOnWire >= DIAG_TAIL_BYTES) {
            memcpy(g_lastGoodTail, packetBuffer + receivedBytesOnWire - DIAG_TAIL_BYTES, DIAG_TAIL_BYTES);
        } else {
            memset(g_lastGoodTail, 0, DIAG_TAIL_BYTES);
            memcpy(g_lastGoodTail, packetBuffer, receivedBytesOnWire);
        }
        portEXIT_CRITICAL(&g_diagMux);
#endif

        if (header.payload_len == 0 || header.payload_len > SPI_PAYLOAD_SIZE) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_payload;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        if (SPI_HEADER_SIZE + header.payload_len > receivedBytesOnWire) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_payload;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        if (header.total_packets == 0 || header.packet_idx >= header.total_packets) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_packet_range;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        if (header.frame_size < 4 || header.frame_size > MAX_FRAME_SIZE) {
            ++_stats.bad_frames;
            ++_stats.oversize;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_frame_meta;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        const uint32_t calculatedPackets = (header.frame_size + SPI_PAYLOAD_SIZE - 1) / SPI_PAYLOAD_SIZE;
        if (header.total_packets != calculatedPackets) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_frame_meta;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        if (header.packet_idx == 0) {
            if (rxBufferIndex >= 0) {
#if VIDEO_CORE_DEVELOPMENT
                ++_stats.stale_receive_frame;
#endif
                portENTER_CRITICAL(&_frameMux);
                _bufferState[rxBufferIndex] = 0;
                portEXIT_CRITICAL(&_frameMux);
                rxBufferIndex = -1;
            }

            rxBufferIndex = findReceiveBuffer();
            if (rxBufferIndex < 0) {
                ++_stats.dropped_frames;
#if VIDEO_CORE_DEVELOPMENT
                ++_stats.no_receive_buffer;
#endif
                const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
                if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
                continue;
            }

            rxFrameId = header.frame_id;
            expectedPackets = header.total_packets;
            expectedFrameSize = header.frame_size;
            expectedFrameCrc = header.frame_crc16;
            receivedBytes = 0;
            nextPacket = 0;
#if VIDEO_CORE_DEVELOPMENT
            if (_stats.consecutive_bad_transactions > 0) {
                _stats.bad_burst_end_tx = _stats.transactions - 1;
                _stats.recovered_after_bad_burst++;
            }
#endif
        }

        if (rxBufferIndex < 0 ||
            header.frame_id != rxFrameId ||
            header.packet_idx != nextPacket ||
            header.total_packets != expectedPackets ||
            header.frame_size != expectedFrameSize ||
            header.frame_crc16 != expectedFrameCrc) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_sequence;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) {
                _stats.bad_burst_start_tx = _stats.transactions;
                _stats.bad_burst_frame_id = header.frame_id;
                _stats.bad_burst_packet_idx = header.packet_idx;
            }
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            if (rxBufferIndex >= 0) {
                portENTER_CRITICAL(&_frameMux);
                _bufferState[rxBufferIndex] = 0;
                portEXIT_CRITICAL(&_frameMux);
                rxBufferIndex = -1;
            }
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        if (receivedBytes + header.payload_len > MAX_FRAME_SIZE) {
            ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
            ++_stats.bad_frame_meta;
            ++_stats.consecutive_bad_transactions;
            if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
            if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
            portENTER_CRITICAL(&_frameMux);
            _bufferState[rxBufferIndex] = 0;
            portEXIT_CRITICAL(&_frameMux);
            rxBufferIndex = -1;
            const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
            if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
            continue;
        }

        memcpy(_frameBuffers[rxBufferIndex] + receivedBytes,
               packetBuffer + SPI_HEADER_SIZE, header.payload_len);
        receivedBytes += header.payload_len;
        ++nextPacket;

        if (header.packet_idx == header.total_packets - 1) {
            if (receivedBytes != expectedFrameSize) {
                ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
                ++_stats.bad_frame_meta;
                ++_stats.consecutive_bad_transactions;
                if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
                if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                    _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
                portENTER_CRITICAL(&_frameMux);
                _bufferState[rxBufferIndex] = 0;
                portEXIT_CRITICAL(&_frameMux);
                rxBufferIndex = -1;
                const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
                if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
                continue;
            }

            const uint8_t* jpeg = _frameBuffers[rxBufferIndex];
            if (jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
                jpeg[receivedBytes - 2] != 0xFF || jpeg[receivedBytes - 1] != 0xD9) {
                ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
                ++_stats.bad_jpeg;
                ++_stats.consecutive_bad_transactions;
                if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
                if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                    _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
                portENTER_CRITICAL(&_frameMux);
                _bufferState[rxBufferIndex] = 0;
                portEXIT_CRITICAL(&_frameMux);
                rxBufferIndex = -1;
                const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
                if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
                continue;
            }

            const uint16_t crc = crc16Ccitt(_frameBuffers[rxBufferIndex], receivedBytes);
            if (crc != expectedFrameCrc) {
                ++_stats.bad_frames;
#if VIDEO_CORE_DEVELOPMENT
                ++_stats.bad_crc;
                ++_stats.consecutive_bad_transactions;
                if (_stats.consecutive_bad_transactions == 1) _stats.bad_burst_start_tx = _stats.transactions;
                if (_stats.consecutive_bad_transactions > _stats.max_consecutive_bad_transactions)
                    _stats.max_consecutive_bad_transactions = _stats.consecutive_bad_transactions;
#endif
                portENTER_CRITICAL(&_frameMux);
                _bufferState[rxBufferIndex] = 0;
                portEXIT_CRITICAL(&_frameMux);
                rxBufferIndex = -1;
                const esp_err_t queueResult = queueTransaction(transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
                if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
                continue;
            }

            if (_haveLastFrameId) {
                const uint16_t frameIdDelta = static_cast<uint16_t>(rxFrameId - _lastFrameId);
                if (frameIdDelta > 1) _stats.frame_id_gaps += static_cast<uint32_t>(frameIdDelta - 1);
            }
            _lastFrameId = rxFrameId;
            _haveLastFrameId = true;

            publishFrame(rxBufferIndex, receivedBytes, rxFrameId);
            ++_stats.ok_frames;
#if VIDEO_CORE_DEVELOPMENT
            _stats.last_good_frame_ms = millis();
            if (_stats.consecutive_bad_transactions > 0) {
                _stats.bad_burst_end_tx = _stats.transactions - 1;
                ++_stats.recovered_after_bad_burst;
            }
            _stats.consecutive_bad_transactions = 0;
#endif
            rxBufferIndex = -1;
        }

        const esp_err_t queueResult = queueTransaction(
            transaction, pdMS_TO_TICKS(1000));
#if VIDEO_CORE_DEVELOPMENT
        if (queueResult != ESP_OK) ++_stats.queue_errors;
#endif
    }
}
