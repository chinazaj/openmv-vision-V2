# SPI 一位错位问题：实验记录与排查交接

> 更新日期：2026-10-28  
> 当前分支：`fix/spi-dma-lifecycle-diagnostics`  
> 状态：已记录现象并加入两个诊断性代码改动；**尚未在真实硬件上验证，也未证明根因已修复**。

## 1. 目的与当前结论

ESP32-C3 作为 SPI 从机接收 OpenMV 主机发送的数据时，固定长度诊断包中反复出现整条数据流右移一位的特征。当前最重要的现象不是随机 CRC 错误，而是：

- 预期头部字节 `AA 55 02 02`，接收原始字节常见为 `D5 2A 81 00` 或 `D5 2A 81 01`。
- 对原始接收流按一位边界重建后，头部、确定性 payload、CRC 均可通过验证。
- 已观察到的样本中，错位比例约为 24%–25%。
- ESP32 队列错误计数 `qerr=0`，已记录的 `queue_result=0` 表示对应入队操作返回成功；目前没有证据表明队列耗尽是主要根因。
- 现有日志不足以断言根因一定是 ESP32-C3 芯片采样问题。仍需测量 CS、SCLK、MOSI 的实际边沿时序，并确认逻辑分析仪解码设置。

本轮采用两个相互独立的改动：
1. 修复异常 RX transaction 分支中可能丢失 transaction 回收的问题。
2. 在 OpenMV 将 CS 拉低后增加 3 微秒等待，作为 CS setup 时序实验。

不要把这两项改动视为已经验证的根因修复。

## 2. 硬件、固件与协议配置

### 2.1 SPI 连接

| 信号 | ESP32-C3 GPIO | 说明 |
|---|---:|---|
| CS | GPIO7 | 从机片选 |
| SCLK | GPIO4 | SPI 时钟 |
| MOSI | GPIO6 | OpenMV 主机发送、ESP32 接收 |
| MISO | GPIO5 | ESP32 发送、OpenMV 接收；当前测试主要关注 MOSI |
| GND | GND | 两端共地 |

OpenMV 代码配置：

- SPI bus：2
- CS 引脚：`P3`
- 时钟：**2,000,000 Hz（2 MHz）**
- 模式：Mode 1，CPOL=0、CPHA=1
- payload 默认上限：4000 bytes
- 包头：18 bytes
- 每个 packet 按 4 字节边界补齐

ESP32 代码配置：

- Arduino-ESP32：3.3.11
- ESP-IDF：v5.5.5
- SPI 从机：`SPI2_HOST`
- Mode 1
- DMA RX buffer：4 个
- 每个 RX transaction buffer：4020 bytes（18-byte header + 4000-byte payload，再按 4 字节对齐）
- CS/SCLK/MOSI/MISO：GPIO7/GPIO4/GPIO6/GPIO5

**注意：** 仓库中的 `docs/esp32-video-hardware-test.md` 有旧配置描述，曾写成 4 MHz、4096-byte payload；这与当前 `openmv/core/video/video_core.py` 的 2 MHz、4000-byte payload 不一致。测试以实际烧录/部署的代码为准。

## 3. 已执行的测试与观测

### 3.1 固定长度诊断包

OpenMV 使用 `run_fixed_length_test(payload_size=..., packet_count=..., interval_ms=0)`，发送 type=2 的诊断包，不发送 JPEG 视频。

payload 的每个字节由下式确定：

```text
payload[i] = (i * 37 + seed * 13 + 0x5A) & 0xFF
```

payload 使用 CRC16-CCITT 校验。测试数据可重复生成，便于区分随机损坏和位边界错位。

一次 3500-byte payload 测试的总线 transaction 长度应为：

```text
18-byte header + 3500-byte payload + 2-byte alignment padding = 3520 bytes
```

### 3.2 代表性日志

| 观测记录 | 接收 transaction 数 | 一位错位候选且验证成功 | 估算比例 |
|---|---:|---:|---:|
| 样本 A | 275 | 67 | 24.4% |
| 样本 B | 341 | 84 | 24.6% |
| 样本 C | 408 | 104 | 25.5% |

相关日志中还出现过：

- `[SPI TEST] ok=208 bad=0 crc_errors=0 pattern_errors=0`
- `[SPI TEST ALIGN] shift1_candidates=67 shift1_ok=67 shift1_bad=0 ...`
- `qerr=0`
- `queue_result=0`
- `rx_to_queue_us=3271`
- `last_len=3520`，且 `full=0 short=275`

计数来自不同时间点/日志窗口，不应把不同窗口的数字拼接成同一组总量。应在后续每轮实验中清零统计或记录起止值，确保分母、错位数来自同一轮测试。

### 3.3 对日志的解释

- `AA 55 02 02` 变成 `D5 2A 81 00/01`，并且整包按一位重建后 CRC 与 pattern 均正确，强烈支持“采样到的整条 bitstream 相对字节边界错一位”这一现象。
- CRC/pattern 错误为 0 的正常包，只能说明那些包完整通过了现有验证；不能证明总线所有 transaction 都正确。
- `queue_depth=4` 是配置的队列容量，不等同于每个时刻实际已经 armed 的 transaction 数量。
- `rx_to_queue_us` 是软件完成 transaction 后到再次入队的测量间隔，不是 CS 到首个 SCLK 的硬件时序。
- 逻辑分析仪探头接在 ESP32 侧引脚，有助于观察 ESP32 引脚处的实际信号；但仍需确认采样率、阈值、地线、解码模式，以及 MOSI 相对 SCLK 的 setup/hold 时间。单凭探头位置不能唯一确定是芯片内部采样、CS 时序、引脚路由还是信号完整性问题。
- 目前这些固定长度测试是在视频停止的情况下进行，因此还没有证明该问题只在视频负载下发生或只与视频任务有关。

## 4. 已审查的代码与问题

### 4.1 ESP32 RX transaction 回收分支

文件：`esp32/core/video/spi_video_receiver.cpp`

此前代码在以下情况直接增加错误计数并 `continue`：

```cpp
if (transaction == nullptr || transaction->rx_buffer == nullptr) {
    ++_stats.bad_frames;
    continue;
}
```

当 `transaction` 非空、但 `rx_buffer` 为空时，transaction 已从驱动完成队列取出，却没有重新放回 RX 队列，可能使预先排队的 transaction 数量减少。

本分支已修改为：
- `transaction == nullptr`：没有可回收的 transaction 对象，只记录错误并继续。
- `transaction != nullptr` 但 `rx_buffer == nullptr`：尝试将该 transaction 重新入队。
- 在开发诊断构建中，重新入队失败时增加 `queue_errors`。

该分支是否曾在实际运行中被触发尚无证据。它是独立的资源生命周期健壮性修复，**不应预设它就是一位错位的根因**。

### 4.2 OpenMV CS setup 时序实验

文件：`openmv/core/video/video_core.py`

此前发送顺序为：

```python
self.cs.low()
self.spi.send(packet)
self.cs.high()
```

本分支在 CS 拉低后增加了：

```python
time.sleep_us(3)
```

目的只是给从机更多 CS setup 时间，以判断一位错位比例是否对 CS 到首个 SCLK 的间隔敏感。该延时应用于当前共用的 packet 发送路径，因此固定长度测试和视频 packet 都会经过此延时。

这是**诊断实验参数，不是最终优化结论**。若错位率下降，仍应使用逻辑分析仪确认具体时序变化，并再考虑是否需要保留、调整或移除该延时。

## 5. 下一轮硬件验证步骤

### A. 确认部署版本

1. 从本分支拉取最新代码。
2. 将更新后的 `openmv/core/video/video_core.py` 部署到设备实际使用的 `/lib/video_core.py`。
3. 使用更新后的 ESP32 测试固件重新编译烧录。
4. 启动后确认两端诊断版本、SPI mode 和频率与本记录一致。
5. 固定测试期间关闭视频发送，避免混入不同 packet 类型。

### B. 执行同条件固定长度测试

建议首先运行：

```python
video.run_fixed_length_test(payload_size=3500, packet_count=1000, interval_ms=0)
```

若 OpenMV REPL 中对象名不是 `video`，请替换为实际的 `VideoTransmitter` 实例名称。

测试时记录：
- 本轮开始/结束时的 ESP32 `transactions`
- `test_packets_ok`、`test_packets_bad`
- `test_bitshift1_candidates`、`test_bitshift1_ok`、`test_bitshift1_bad`
- CRC/pattern errors、`queue_errors`
- OpenMV 的实际发送 packet 数和异常数
- 逻辑分析仪捕获的 CS 上升/下降沿、首个 SCLK 边沿、MOSI 首位，以及 SCLK 采样边沿

建议至少做 3 轮；每轮单独记录，避免把累计计数误当作单轮计数。

### C. 判断标准

- **错位比例明显降低/归零：** 支持 CS setup 时序与问题相关，但还需波形验证；随后可测试 1、2、3、5、10 微秒延时，找出稳定范围，并确认吞吐量影响。
- **错位比例基本不变：** 不要继续盲目增加延时。优先检查逻辑分析仪的 MOSI/SCLK 波形、Mode 1 解码、首位时序、CS 期间是否出现额外时钟，以及 ESP32-C3 GPIO matrix/从机驱动对首位采样的影响。
- **`queue_errors` 或 transaction 数量异常：** 另行排查 RX 队列生命周期、异常分支和任务调度；不要把它与一位错位自动等同。
- **正常视频仍然异常：** 在固定长度测试结论明确后，再恢复视频测试，以减少变量。

## 6. 官方驱动约束参考

ESP-IDF v5.5 的 ESP32-C3 SPI Slave Driver 文档：
https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/peripherals/spi_slave.html

文档强调从机 transaction 应在主机开始时钟前排队；DMA 接收缓冲区和 transaction 长度需要满足对齐要求。当前代码使用 DMA buffer、4 字节对齐的 4020-byte transaction，表面上满足这些基本约束。文档还建议使用额外 GPIO handshake 来同步主从准备状态；当前项目尚未增加该握手信号。

## 7. 会话交接摘要

下一次继续时，先阅读本文件，再查看分支 `fix/spi-dma-lifecycle-diagnostics` 的最新提交。

当前代码变更：
- ESP32 异常 RX buffer 分支回收 transaction。
- OpenMV CS 拉低后增加 3 微秒诊断延时。

当前尚未完成：
- 编译检查与硬件烧录验证。
- 对照延时前后的错位率。
- 逻辑分析仪确认 CS-to-first-SCLK 和 MOSI setup/hold。
- 确认异常 `rx_buffer == nullptr` 分支是否真实发生。
- 修正硬件测试文档中过时的 4 MHz / 4096-byte 配置描述。

**当前结论：现象已重复观测，位错重建验证通过；根因尚未确定。下一步最有价值的工作是用本分支的 3 微秒实验做同条件 A/B 对比，并同步捕获总线波形。**
