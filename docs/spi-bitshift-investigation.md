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


---

## 8. 2026-10-30：第二次逻辑分析仪抓取（3500-1c）

### 8.1 用户确认的固定事实

以下信息由用户明确确认，后续分析将作为已知条件，不再反复质疑：

- Channel 0 探头实际连接 ESP32-C3 GPIO7，GPIO7 就是 SPI CS。
- OpenMV 主机 SPI 发送频率配置/确认值为 2 MHz。
- OpenMV 是 SPI Master，ESP32-C3 是 SPI Slave。
- 后续所有诊断数据、实验条件、判断、结论和代码改动均须持续写入本文件。

注意：逻辑分析仪 CSV 的时间戳推算值与 2 MHz 配置值存在差异。两者分别记录；在确认逻辑分析仪时间基准、导出时间轴和采样设置前，不以 CSV 推算值否定用户确认的 2 MHz 配置。

### 8.2 本次输入文件

- `3500-1c.JPG`
- `digital-3500-1c.csv`

CSV 解析结果：

- 804,458 行，字段为 Time [s]、Channel 0、Channel 1、Channel 2、Channel 3。
- Channel 1 共 788,480 次电平变化；分成 14 组连续时钟活动。
- 每组包含 56,320 次时钟电平变化，即 28,160 个完整 SCLK 周期，对应 3,520 字节。
- Channel 3 在该 CSV 中始终为低电平，没有观察到 MISO 活动。
- Channel 0 共 16,426 次电平变化（8,213 次上升、8,213 次下降）。
- 在 Channel 0 发生变化的采样点，Channel 1 为高电平的有 16,398 次；为低电平的有 28 次。
- Channel 0 的相邻边沿间隔中位数约 3.937 us；其中存在大量约 62 ns 的短高脉冲（由相邻时间戳计算）。

### 8.3 对 GPIO7/CS 的重要观察

用户确认 Channel 0 就是 ESP32 GPIO7/CS。因此，不能再把 Channel 0 当作未知通道，也不能因其边沿数量多而排除其 CS 身份。

当前 CSV 显示 GPIO7 在每组连续 SCLK 活动期间出现大量电平变化。该行为与“CS 在整笔 SPI transaction 期间保持低电平”的预期不一致。截图中也能看到 Channel 0 在时钟活动期间存在窄脉冲/跳变。

这是目前最优先调查的异常，但尚不能仅凭 CSV 判定其物理原因。可能方向包括：

1. GPIO7/CS 确实出现窄脉冲或非预期释放，导致 ESP32 SPI Slave 将一笔主机发送拆成多个片段，产生字节边界错位或 transaction 不完整。
2. GPIO7 的电气波形存在振铃、串扰、地弹或阈值附近噪声，逻辑分析仪将其记录为多次跳变。
3. GPIO7 复用/软件控制/外部电路存在竞争，CS 被其他路径短暂驱动。
4. 探头、地线、阈值、CSV 导出/采集模式对短脉冲的记录需要进一步验证。

以上均为待验证假设，不是已确认根因。

### 8.4 本次抓取中的 SPI Decoder 设置

截图 `3500-1c.JPG` 中显示 Decoder 报错：

`The initial (idle) state of the CLK line does not match the settings.`

因此截图中的 SPI Decoder 解码结果不应作为字节正确性证据。后续应以 CSV 原始数字边沿自行解码，并将 Decoder 配置为与实际 Mode 1 一致（CPOL=0、CPHA=1、MSB first）。最好先移除重复/冲突的 SPI Decoder，只保留一个解码器。

### 8.5 时间轴与 2 MHz 配置的差异

按 CSV 相邻 Channel 1 边沿时间戳计算，典型半周期约 0.375 us，推算完整周期约 0.75 us（约 1.33 MHz）。该值与用户确认的 OpenMV 2 MHz 配置不一致。

处理原则：

- 2 MHz 是用户确认的主机配置事实。
- 约 1.33 MHz 是本 CSV 时间戳的数学推算结果。
- 在确认 Saleae 采样/导出时间基准、设备时钟配置和该次捕获对应的固件版本前，不将两者强行解释为同一个值，也不据此更改主机配置。
- 下一轮必须用独立测量或 Saleae 时间基准校准确认实际 SCLK。

### 8.6 下一步定位顺序（优先级）

**实验 1：单独验证 GPIO7/CS 波形**

1. 保持当前接线，确认探头地线可靠且尽量短；探头直接接 ESP32 GPIO7 引脚附近。
2. 逻辑分析仪同时采集 GPIO7、SCLK、MOSI；保持足够采样率/带宽，并关闭不必要的协议解码器。
3. 在不发送 SPI 数据时观察 GPIO7 静态电平和噪声。
4. 发送一笔 3,520-byte 固定长度 transaction，放大观察 CS 从拉低到拉高的整个窗口。
5. 统计 CS 在 transaction 窗口内的边沿数、最窄脉宽、脉冲极性、与 SCLK/MOSI 边沿的关系。
6. 若 GPIO7 在传输中确实反复跳变，先不要继续调 DMA；查明 GPIO7 是否被其他 GPIO 操作、外部器件或电气问题影响。

**实验 2：确认 CS 是真实逻辑毛刺还是测量伪影**

- 用示波器探头（或第二台独立仪器）在 ESP32 GPIO7 引脚处同时观察 CS；优先使用短地弹簧/短地线。
- 对比逻辑分析仪与示波器是否都看到约 62 ns 脉冲。
- 若只有逻辑分析仪看到，检查数字阈值、探头接地、采样率和输入通道设置。
- 若两台仪器都看到，检查 GPIO7 驱动来源、CS 控制代码、引脚复用和板级电气完整性。

**实验 3：做最小化 CS/SPI 对照**

- 先用较短 transaction（例如 16、64、256 bytes）验证 CS 是否在整个传输中保持稳定。
- 暂停视频任务、其他 GPIO 操作和非必要中断负载。
- 分别测试现有 CS 软件控制方式与可控的外部稳定 CS（若硬件条件允许）；每次只改变一个变量。
- 将每笔 transaction 的 CS 低电平窗口、SCLK 周期数、ESP32 DMA 原始首 32 字节、接收长度、CRC 和 shift 检测结果写入同一轮日志。

**实验 4：只有 CS 波形稳定后，才继续定位 SPI Slave/DMA**

同一 transaction 做逐字节对照：

- Saleae 从 MOSI 原始边沿按 Mode 1 解码的完整字节流。
- ESP32 SPI Slave DMA buffer 的原始字节流（必须在任何 shift 修复/重建逻辑之前 dump）。
- ESP32 驱动返回的实际 transaction 长度、状态和时间戳。

若 CS 稳定、Saleae MOSI 字节流正确，而同一 transaction 的 ESP32 DMA buffer 仍发生一位错位，再集中检查 ESP32-C3 SPI Slave peripheral、GPIO matrix、DMA buffer/transaction 生命周期和驱动配置。

### 8.7 当前更新后的阶段性结论

- OpenMV 主机配置 2 MHz、CS=GPIO7、Channel 0 接 GPIO7：作为用户确认事实。
- 本次 MOSI 按 Mode 1 原始边沿解码得到的固定长度包此前分析为完整 header/payload/CRC 正确；但这不能解释或排除 GPIO7 在传输期间的异常跳变。
- 本次 CSV 最重要的新线索是 GPIO7/CS 在 SCLK 活动窗口内存在大量边沿，其中大量为极窄脉冲。
- **当前第一优先级从“直接怀疑 ESP32 DMA”调整为“先确认 GPIO7/CS 是否在传输期间真实发生毛刺/释放”。**
- CS 波形没有得到独立仪器确认前，不宣称 CS 毛刺就是根因；也不继续盲目增加 CS delay 或修改 DMA。



---

## 9. 2026-10-30：完整时间范围截图（3500-1c-frame）分析

### 9.1 完整截图显示的整体结构

完整时间范围截图显示，采集窗口内至少有两段明显的 SPI 活动：

- 第一段位于触发点附近（约 0 ms）。
- 第二段位于约 +100 ms 附近。
- 两段活动中，Channel 1（SCLK）和 Channel 2（MOSI）同步出现密集脉冲。
- Channel 3（MISO）保持低电平，没有可见活动。
- Channel 0 是用户确认连接 GPIO7/CS 的通道。由于完整视图横向压缩比例很大，几十纳秒级的窄脉冲在全景图中无法逐个分辨；必须结合放大视图和 CSV 边沿统计判断，不能因为全景图看起来近似一条平线就认为 CS 稳定。

### 9.2 全景图与放大图应结合解释

全景图主要用于确认两段 SPI 传输在时间轴上的位置和间隔；放大图用于观察每段传输内部的信号关系。结合 3500-1c.JPG 放大视图及 digital-3500-1c.csv：

- SCLK 在每段活动中连续运行。
- MOSI 在 SCLK 活动期间持续变化。
- GPIO7/CS 在 SCLK 活动窗口内存在大量窄跳变；CSV 统计为 16,426 次边沿变化，且其中大量相邻边沿间隔约 62 ns。
- 因而全景图中看似不明显的 CS 异常，在放大视图和 CSV 中是可见的。应以原始 CSV 的边沿时间和示波器复核为准。

### 9.3 对故障机理的判断

SPI Slave 的 CS 是 transaction 边界信号。若 GPIO7/CS 为低有效，且在一笔连续 SCLK 传输过程中短暂回到高电平，ESP32-C3 从机可能把当前 transaction 提前终止；之后再次拉低则可能重新开始接收。若这种情况发生在字节或 bit 中间，就可能造成：

- 从机实际接收长度与主机发送长度不一致；
- 一笔主机数据被从机视为多个片段；
- 首位/末位被截断或重新同步；
- DMA buffer 中的数据相对预期字节边界偏移。

该机理与现有一位错位现象有合理关联，因此 GPIO7/CS 毛刺是当前首要待验证方向。但截图本身尚不能证明窄脉冲是真实引脚电平变化，也不能仅凭逻辑分析仪的数字波形证明从机一定按这些脉冲重新划分 transaction。

### 9.4 当前不应得出的结论

- 不能因为 Decoder 显示 “The initial (idle) state of the CLK line does not match the settings.” 就认定 MOSI 数据错误；该提示说明 SPI 解码器时钟空闲配置与波形不匹配，当前解码结果不可靠。
- 不能仅凭全景截图判断 CS 在两笔传输之间是否正确释放；全景横向压缩无法分辨窄脉冲，必须放大并结合 CSV。
- 不能将约 62 ns 脉冲直接认定为 GPIO 软件主动翻转；它也可能来自振铃、串扰、地弹、阈值或采样伪影。
- 不能因此直接修改 DMA、SPI mode、SPI 频率或加入自动 bit-shift 修复。

### 9.5 下一步实验的明确判据

1. 在 ESP32-C3 GPIO7 引脚处用示波器观察 CS，同时观察 SCLK；使用短地弹簧/短地线，确认放大视图中的窄高脉冲是否真实存在。
2. 若示波器也看到 CS 高脉冲，检查 GPIO7 的所有软件写入、pin mux/复用配置、初始化顺序、其他任务/中断以及板级连接；定位并消除非预期驱动。
3. 若示波器看不到而逻辑分析仪仍看到，调整逻辑分析仪阈值、采样率和接地方式，确认是否为测量伪影。
4. CS 稳定后，再运行 16/64/256-byte 小包测试；逐笔记录 CS 窗口、SCLK 周期数、MOSI 解码字节、ESP32 DMA 原始字节和接收长度。
5. 只有当同一笔传输的 CS 波形确认稳定、MOSI 外部解码正确，而 ESP32 DMA buffer 仍错位时，才将重点转回 SPI Slave 外设、GPIO matrix 和 DMA transaction 生命周期。

### 9.6 本次结论

完整截图确认了采集窗口内有两段 SPI 数据活动；放大截图与 CSV 则显示 GPIO7/CS 在 SCLK 活动期间有大量窄跳变。当前最合理的排查顺序是先用示波器验证 CS 窄脉冲的真实性，再决定是查 GPIO7 驱动/电气完整性，还是查逻辑分析仪测量设置。**CS 毛刺是当前最强线索，但仍是待独立验证的假设，不是已经确认的根因。**

---
## 10. 2026-10-30：CS 毛刺成因、SPI 影响与责任边界分析

### 10.1 CS 的控制关系

本项目中 OpenMV 是 SPI Master，负责输出 SCLK、MOSI，并控制 CS；ESP32-C3 是 SPI Slave，GPIO7 接收 CS。正常情况下，一笔 packet 传输期间 CS 应保持有效（低电平），在 transaction 结束后释放（高电平）。

因此，CS 的逻辑切换通常由 OpenMV 主机发起；但 ESP32 端 GPIO7 的错误配置、外部电路争用或信号完整性问题，也可能改变 ESP32 引脚实际看到的电平。逻辑分析仪在 ESP32 GPIO7 处看到异常，并不能单独证明异常由 OpenMV 软件产生。

### 10.2 可能成因及责任边界

**A. OpenMV 主机软件/引脚控制**

可能原因：
- packet 发送期间存在其他代码路径操作同一个 CS GPIO。
- 发送函数、异常处理或任务切换导致 CS 提前释放/重复拉低。
- CS GPIO 对象被重复初始化，或被其他功能重新配置。
- 多个任务/中断共享并操作 CS。
- SPI API 的 CS 硬件管理与手动 GPIO CS 控制同时启用，造成控制冲突（需结合实际 API 和代码确认）。

判断方法：同时测量 OpenMV 的 CS 输出引脚和 ESP32 GPIO7。若 OpenMV 端已经出现同样的窄脉冲，优先查 OpenMV 软件、GPIO 配置和主机输出；若 OpenMV 端稳定而 ESP32 端异常，则不支持“OpenMV 软件主动翻转 CS”这一解释。

**B. ESP32 GPIO7 配置或电气争用**

SPI Slave 正常使用 GPIO7 作为 CS 输入时，ESP32 不应主动驱动该线。但如果项目其他代码把 GPIO7 配成输出、复用给其他外设，或外部器件也驱动该线，就可能与 OpenMV 输出争用，导致 ESP32 端看到异常电平。初始化顺序、重复 pin 配置、错误 GPIO 编号和板级外设连接均需检查。

判断方法：全项目搜索 GPIO7 的所有配置和写入（包括 pinMode、digitalWrite、GPIO matrix、SPI pin 配置和宏定义）；在隔离其他连接/功能后重复测量。若 OpenMV 端稳定而 ESP32 端有异常，应优先查线路、ESP32 引脚配置及外部负载。

**C. 线路信号完整性问题**

可能原因：
- CS 线路过长或阻抗不连续造成反射/振铃。
- CS 与 SCLK/MOSI 并行走线导致串扰。
- 两端地线/回流路径不良，产生地弹或地电位差。
- 连接器、杜邦线、焊点接触不良。
- 输入阈值附近的噪声使数字分析仪记录出多个边沿。
- 缺少合适的默认偏置，导致上电/空闲阶段 CS 电平不稳。

判断方法：用示波器在 ESP32 GPIO7 引脚处观察模拟波形，使用短地弹簧；同时在 OpenMV CS 输出端测量。若 OpenMV 端波形干净、ESP32 端出现振铃/过冲/多次越阈值，问题主要在连接线路、负载、接地或 ESP32 端电气环境，而不是 OpenMV 软件主动切换。

**D. 逻辑分析仪测量伪影**

若数字分析仪显示窄脉冲，但示波器在同一引脚处看不到对应越阈值的有效脉冲，应检查数字输入阈值、采样率、地线、通道设置和导出数据。CSV 中的约 62 ns 间隔只是时间戳统计结果，不足以证明这些脉冲具有能被 ESP32 输入判定为高电平的幅度和宽度。

### 10.3 CS 毛刺对 SPI Slave 接收的影响

CS 是 SPI Slave 的 transaction 选择/边界信号。若 CS 为低有效，在 SCLK 正在传输时 CS 确实短暂升高，并且脉冲幅度、宽度满足 ESP32 输入和外设识别条件，则可能导致从机将当前 transaction 提前终止或重置其位计数/接收状态。之后 CS 再次拉低，后续时钟可能被视为新的片段。

可能后果包括：
- ESP32 实际接收长度短于主机发送长度，或驱动报告 transaction 长度异常。
- 一笔主机 packet 被从机边界信号切分。
- 当前片段的末尾 bit 或下一片段的起始 bit 丢失/重新对齐。
- DMA buffer 与预期 packet 的边界不一致，造成 header/CRC 错误或看似 bit-shift 的数据。
- 若毛刺发生在 SCLK 不活动时，可能只造成从机短暂取消选择，不一定破坏数据；具体影响取决于脉冲与 SCLK 的相对位置以及外设对 CS 的识别。

重要限定：CS 短暂释放更常见的结果是 transaction 截断/分段或长度异常，不必然表现为整条数据恰好一位移位。当前观察到的一位重建成功与 CS 异常有可能相关，但还不能证明因果关系。必须将同一笔传输的 CS、SCLK、MOSI 和 ESP32 DMA 原始 buffer 对齐比较。

### 10.4 目前能否判断是 OpenMV 还是 ESP32？

现有证据只能说明：逻辑分析仪在 ESP32 GPIO7/CS 通道记录到大量边沿；不能确定这些边沿是 OpenMV 输出、ESP32 端争用/配置问题，还是线路/测量伪影。

责任判断规则：
- OpenMV CS 输出端与 ESP32 GPIO7 端都出现相同脉冲：优先定位 OpenMV GPIO/软件控制或主机输出端电气问题。
- OpenMV CS 输出端稳定，ESP32 GPIO7 端出现脉冲：优先定位连接线路、ESP32 GPIO7 配置/争用、接地和负载。
- 示波器在 ESP32 GPIO7 端看不到有效脉冲，只有逻辑分析仪有记录：优先定位逻辑分析仪阈值、采样或接地问题。
- 两端都稳定，但 ESP32 DMA 仍错位：再调查 SPI Slave 外设、驱动、DMA 和 transaction 生命周期。

### 10.5 推荐的最小判别实验

1. 暂停视频、Wi-Fi 和其他非必要 GPIO 功能，保留最小 SPI 固定包发送。
2. 示波器双通道同时测量 OpenMV CS 输出端与 ESP32 GPIO7 端（两端共地，注意探头接地安全）；必要时再增加 SCLK 通道做第二次捕获。
3. 观察同一笔 16/64/256-byte packet 的 CS 波形，确认高脉冲是否真实、两端是否同时出现、脉冲是否与 SCLK 活动重叠。
4. 对项目代码全局搜索 GPIO7 的配置和写操作，确认没有其他所有者。
5. 若两端 CS 波形一致且存在脉冲，检查 OpenMV CS 控制路径；若两端不一致，检查线路与 ESP32 端配置/电气争用；若只有逻辑分析仪记录，先解决测量设置。
6. 只有 CS 已确认稳定后，才做 MOSI 解码流与 ESP32 DMA 原始 buffer 的逐笔对照。

### 10.6 阶段性结论

CS 的控制源通常是 OpenMV Master，但 ESP32 GPIO7 是接收端，端点观测到的毛刺不等于主机软件一定主动翻转。当前不能把责任直接归给 OpenMV 或 ESP32。最有区分度的证据是同时比较 OpenMV CS 输出端与 ESP32 GPIO7 引脚端的模拟波形。CS 毛刺若真实且被从机识别，可能破坏 transaction 边界、长度和位对齐；但其是否能解释现有一位错位，仍需同一 transaction 的波形与 DMA 原始数据对照验证。
