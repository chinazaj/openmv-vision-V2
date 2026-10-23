#include "spi_video_receiver.h"

#include <driver/spi_slave.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
constexpr int SPI_SCK = 4;
constexpr int SPI_MISO = 5;
constexpr int SPI_MOSI = 6;
constexpr int SPI_CS = 7;
constexpr int FRAME_BUFFER_COUNT = 2;

// 0 = FREE, 1 = RECEIVING, 2 = READY, 3 = SENDING
}

SpiVideoReceiver::SpiVideoReceiver() = default;

SpiVideoReceiver::~SpiVideoReceiver() {
    end();
}

bool SpiVideoReceiver::begin() {
    if (_running) {
        return true;
    }

    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return false;
    }

    _dmaRxBuffer = static_cast<uint8_t*>(
        heap_caps_malloc(SPI_PACKET_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (_dmaRxBuffer == nullptr) {
        end();
        return false;
    }

    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        _frameBuffers[i] = static_cast<uint8_t*>(
            heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_8BIT));
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
    slvcfg.queue_size = 1;
    slvcfg.mode = 0;

    const esp_err_t initResult = spi_slave_initialize(
        SPI2_HOST,
        &buscfg,
        &slvcfg,
        SPI_DMA_CH_AUTO);
    if (initResult != ESP_OK) {
        end();
        return false;
    }

    _running = true;
    BaseType_t taskResult = xTaskCreate(
        &SpiVideoReceiver::receiveTaskEntry,
        "spi_video_rx",
        4096,
        this,
        3,
        &_task);
    if (taskResult != pdPASS) {
        _running = false;
        spi_slave_free(SPI2_HOST);
        end();
        return false;
    }

    return true;
}

void SpiVideoReceiver::end() {
    _running = false;

    // spi_slave_transmit() is intentionally blocking while waiting for the
    // OpenMV master. The product normally keeps the receiver alive for its
    // lifetime, so cleanup is best-effort here.
    if (_task != nullptr && xTaskGetCurrentTaskHandle() != _task) {
        vTaskDelay(pdMS_TO_TICKS(10));
        _task = nullptr;
    }

    spi_slave_free(SPI2_HOST);

    if (_dmaRxBuffer != nullptr) {
        heap_caps_free(_dmaRxBuffer);
        _dmaRxBuffer = nullptr;
    }

    for (auto& buffer : _frameBuffers) {
        if (buffer != nullptr) {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
    }

    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
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
    header.magic = static_cast<uint16_t>(packet[0]) |
                   static_cast<uint16_t>(packet[1]) << 8;
    header.version = packet[2];
    header.type = packet[3];
    header.frame_id = static_cast<uint16_t>(packet[4]) |
                      static_cast<uint16_t>(packet[5]) << 8;
    header.packet_idx = static_cast<uint16_t>(packet[6]) |
                        static_cast<uint16_t>(packet[7]) << 8;
    header.total_packets = static_cast<uint16_t>(packet[8]) |
                           static_cast<uint16_t>(packet[9]) << 8;
    header.payload_len = static_cast<uint16_t>(packet[10]) |
                         static_cast<uint16_t>(packet[11]) << 8;
    header.frame_size = static_cast<uint32_t>(packet[12]) |
                        static_cast<uint32_t>(packet[13]) << 8 |
                        static_cast<uint32_t>(packet[14]) << 16 |
                        static_cast<uint32_t>(packet[15]) << 24;
    header.frame_crc16 = static_cast<uint16_t>(packet[16]) |
                         static_cast<uint16_t>(packet[17]) << 8;

    // Match the original firmware's header-error counter semantics:
    // this function validates the fixed signature/version/type only.
    // Length and packet-field validation is handled by receiveTask().
    return header.magic == SPI_MAGIC &&
           header.version == SPI_VERSION &&
           header.type == SPI_TYPE_FRAME;
}

int SpiVideoReceiver::findReceiveBuffer() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }

    int index = -1;
    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        if (_bufferState[i] == 0) {
            index = i;
            break;
        }
    }

    // If both buffers are occupied, sacrifice an unpublished READY frame.
    // Never overwrite a frame currently being sent to an HTTP client.
    if (index < 0 && _readyBuffer >= 0 && _bufferState[_readyBuffer] == 2) {
        index = _readyBuffer;
        _readyBuffer = -1;
        ++_stats.dropped_frames;
    }

    if (index >= 0) {
        _bufferState[index] = 1;
        _bufferSize[index] = 0;
    }

    xSemaphoreGive(_mutex);
    return index;
}

void SpiVideoReceiver::publishFrame(int index, size_t size, uint16_t frameId) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ++_stats.dropped_frames;
        _bufferState[index] = 0;
        return;
    }

    _bufferSize[index] = size;
    _bufferFrameId[index] = frameId;
    _bufferState[index] = 2;

    const int oldReady = _readyBuffer;
    _readyBuffer = index;

    if (oldReady >= 0 && oldReady != index && _bufferState[oldReady] == 2) {
        _bufferState[oldReady] = 0;
        ++_stats.dropped_frames;
    }

    xSemaphoreGive(_mutex);
}

bool SpiVideoReceiver::acquireLatestFrame(FrameView& frame) {
    frame = FrameView{};
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }

    if (_readyBuffer >= 0 && _bufferState[_readyBuffer] == 2) {
        const int index = _readyBuffer;
        _readyBuffer = -1;
        _bufferState[index] = 3;
        frame.data = _frameBuffers[index];
        frame.size = _bufferSize[index];
        frame.buffer_index = index;
        frame.frame_id = _bufferFrameId[index];
    }

    xSemaphoreGive(_mutex);
    return frame.buffer_index >= 0;
}

void SpiVideoReceiver::releaseFrame(FrameView& frame) {
    if (frame.buffer_index < 0 || frame.buffer_index >= FRAME_BUFFER_COUNT || _mutex == nullptr) {
        frame = FrameView{};
        return;
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_bufferState[frame.buffer_index] == 3) {
            _bufferState[frame.buffer_index] = 0;
        }
        xSemaphoreGive(_mutex);
    }
    frame = FrameView{};
}

SpiVideoReceiver::Stats SpiVideoReceiver::getStats() const {
    return _stats;
}

void SpiVideoReceiver::resetStats() {
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _stats = Stats{};
        xSemaphoreGive(_mutex);
    } else {
        _stats = Stats{};
    }
}

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
        spi_slave_transaction_t transaction{};
        transaction.length = SPI_PACKET_SIZE * 8;
        transaction.rx_buffer = _dmaRxBuffer;

        const esp_err_t result = spi_slave_transmit(SPI2_HOST, &transaction, portMAX_DELAY);
        if (!_running) {
            break;
        }
        if (result != ESP_OK) {
            ++_stats.timeouts;
            continue;
        }

        const size_t receivedBytesOnWire = transaction.trans_len / 8;
        if (receivedBytesOnWire < SPI_HEADER_SIZE) {
            ++_stats.bad_headers;
            ++_stats.bad_frames;
            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
            }
            continue;
        }

        PacketHeader header{};
        if (!parseHeader(_dmaRxBuffer, header)) {
            ++_stats.bad_headers;
            ++_stats.bad_frames;

            // Log the first complete invalid header so wire-level protocol
            // mismatches can be diagnosed without flooding the serial port.
            static bool firstBadHeaderLogged = false;
            if (!firstBadHeaderLogged) {
                firstBadHeaderLogged = true;
                Serial.println("[SPI DIAG] First invalid header bytes:");
                for (size_t i = 0; i < SPI_HEADER_SIZE; ++i) {
                    Serial.printf("%02X%s", _dmaRxBuffer[i],
                                  (i + 1 == SPI_HEADER_SIZE) ? "\n" : " ");
                }
                Serial.printf(
                    "[SPI DIAG] bytes=%u magic=0x%04X version=%u type=%u "
                    "frame=%u packet=%u/%u payload=%u frame_size=%lu crc=0x%04X\n",
                    static_cast<unsigned>(receivedBytesOnWire),
                    header.magic, header.version, header.type, header.frame_id,
                    header.packet_idx, header.total_packets, header.payload_len,
                    static_cast<unsigned long>(header.frame_size), header.frame_crc16);
            }

            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
            }
            continue;
        }

        const size_t maxPacketCount =
            (MAX_FRAME_SIZE + SPI_PAYLOAD_SIZE - 1) / SPI_PAYLOAD_SIZE;
        if (header.total_packets == 0 ||
            header.total_packets > maxPacketCount ||
            header.payload_len == 0 ||
            header.payload_len > SPI_PAYLOAD_SIZE ||
            header.frame_size < 4 ||
            header.frame_size > MAX_FRAME_SIZE) {
            ++_stats.bad_frames;
            if (header.frame_size < 4 || header.frame_size > MAX_FRAME_SIZE) {
                ++_stats.oversize;
            }
            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
            }
            continue;
        }

        // trans_len is the number of bits actually clocked by the master.
        // Never copy payload bytes that were not received in this transaction.
        if (SPI_HEADER_SIZE + header.payload_len > receivedBytesOnWire) {
            ++_stats.bad_headers;
            ++_stats.bad_frames;
            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
            }
            continue;
        }

        const size_t expectedTotalPackets =
            (header.frame_size + SPI_PAYLOAD_SIZE - 1) / SPI_PAYLOAD_SIZE;
        if (expectedTotalPackets != header.total_packets ||
            header.packet_idx >= header.total_packets ||
            (header.packet_idx + 1 < header.total_packets && header.payload_len != SPI_PAYLOAD_SIZE) ||
            (header.packet_idx + 1 == header.total_packets &&
             header.payload_len != header.frame_size -
                 static_cast<uint32_t>(header.packet_idx) * SPI_PAYLOAD_SIZE)) {
            ++_stats.bad_headers;
            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
            }
            ++_stats.bad_frames;
            continue;
        }

        if (header.packet_idx == 0) {
            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
            }
            rxBufferIndex = findReceiveBuffer();
            if (rxBufferIndex < 0) {
                ++_stats.dropped_frames;
                continue;
            }
            rxFrameId = header.frame_id;
            expectedPackets = header.total_packets;
            expectedFrameSize = header.frame_size;
            expectedFrameCrc = header.frame_crc16;
            receivedBytes = 0;
            nextPacket = 0;
        }

        if (rxBufferIndex < 0 ||
            header.frame_id != rxFrameId ||
            header.total_packets != expectedPackets ||
            header.frame_size != expectedFrameSize ||
            header.frame_crc16 != expectedFrameCrc ||
            header.packet_idx != nextPacket) {
            ++_stats.bad_frames;
            if (rxBufferIndex >= 0) {
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
            }
            continue;
        }

        const size_t offset = static_cast<size_t>(header.packet_idx) * SPI_PAYLOAD_SIZE;
        if (offset + header.payload_len > MAX_FRAME_SIZE ||
            offset + header.payload_len > expectedFrameSize) {
            ++_stats.oversize;
            ++_stats.bad_frames;
            _bufferState[rxBufferIndex] = 0;
            rxBufferIndex = -1;
            continue;
        }

        memcpy(_frameBuffers[rxBufferIndex] + offset, _dmaRxBuffer + SPI_HEADER_SIZE, header.payload_len);
        receivedBytes += header.payload_len;
        ++nextPacket;

        if (nextPacket == expectedPackets) {
            const uint8_t* jpeg = _frameBuffers[rxBufferIndex];
            if (receivedBytes != expectedFrameSize ||
                expectedFrameSize < 4 ||
                jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
                jpeg[expectedFrameSize - 2] != 0xFF ||
                jpeg[expectedFrameSize - 1] != 0xD9 ||
                crc16Ccitt(jpeg, expectedFrameSize) != expectedFrameCrc) {
                ++_stats.bad_frames;
                _bufferState[rxBufferIndex] = 0;
                rxBufferIndex = -1;
                continue;
            }

            if (_haveLastFrameId) {
                const uint16_t frameIdDelta = static_cast<uint16_t>(rxFrameId - _lastFrameId);
                if (frameIdDelta > 1) {
                    // Preserve the original firmware's metric: count missing
                    // frame IDs, not merely the number of gap events.
                    _stats.frame_id_gaps += static_cast<uint32_t>(frameIdDelta - 1);
                }
            }
            _lastFrameId = rxFrameId;
            _haveLastFrameId = true;

            publishFrame(rxBufferIndex, expectedFrameSize, rxFrameId);
            ++_stats.ok_frames;
            rxBufferIndex = -1;
        }
    }
}
