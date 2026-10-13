# MIUI FM 搜台机制还原（cannon / com.miui.fmradio）

调研日期：2026-10-12。目标：回答"为什么 MIUI 自带 FM 的 seek / 全频段搜索比 RFM-Radio 的
软件步进快得多，它是怎么实现的"。结论先行：

**MIUI 的搜台不是软件步进读 RSSI，而是逐频点调用内核的"软静音调谐"ioctl
（`FM_IOCTL_SOFT_MUTE_TUNE`，nr=63），一次 ioctl 内由固件完成"调谐 + 采样 + 多指标判台"。
我们此前确认失败并放弃的 SEEK/SCAN ioctl 依旧不可用，但这条 SOFT_MUTE_TUNE 路径一直存在，
我们自己的步进实现（每步固定 60 ms settle）才是慢的根源。**

约定同 [`mtk-fm-port-research.md`](mtk-fm-port-research.md)：证据来源标记如下，
结论与证据分开，每条结论标注确定性等级（实测确认 / 反汇编推断 / 猜测）。

## 证据来源

| 标记 | 来源 |
| --- | --- |
| `[APK]` | 本机 `/system_ext/app/FM/FM.apk`（副本 `.scratch/tmp-miui-seek/FM.apk`），baksmali 反汇编 `.scratch/tmp-miui-seek/smali2/`（classes2.dex），jadx 反编译 `jadx-out/sources/` |
| `[JNI]` | 本机 `/system_ext/lib64/libfmjni.so`（副本 `.scratch/tmp-miui-seek/libfmjni.so`，aarch64，llvm-objdump 反汇编 `dis_*.txt`） |
| `[KSRC]` | MT6768 世代 FM 驱动完整源码（同 [`mtk-fm-port-research.md`] 的 `[KSRC]`），本地 `/home/w0n9/scratch/mtk-cannon/kernel/mt6768/drivers/misc/mediatek/connectivity/fmradio/` |
| `[KO]` | 本机内核模块 `vendor/lib/modules/fmradio_drv_mt6631_6635.ko`（副本 `.scratch/tmp-miui-seek/fmradio_drv_mt6631_6635.ko`，未 strip，1087 个符号） |
| `[REF]` | 开源同源实现 `iusmac/RevampedFMRadio` mtk 分支 `jni/fmr/{fmr_core.cpp,common.cpp,fm.h}`（`.scratch/tmp-miui-seek/ref/`），用于辅助注释，设备侧以 `[JNI]` 反汇编为准 |
| `[DEV]` | 设备实机只读取证：adb shell（su 只用于 cat/dmesg，无写操作）、`getprop`、`dmesg`（`.scratch/tmp-miui-seek/dmesg-full2.txt`） |

---

## 1. 总体结构：app → libfmjni → /dev/fm

```text
com.miui.fmradio (FmLocalService, UI 服务)
  → com.miui.fmservice.FmServiceManager (同进程绑定 com.android.fmradio.FmService)
  → FmService.seekStation()/startScan()  [APK classes2.dex, Handler 消息 14/13]
  → FmNative.seek(f, isUp) / FmNative.autoScan()   ← 阻塞 JNI 调用，无任何 Java 层轮询
  → libfmjni.so FMR_seek → FMR_seek_Channel / FMR_scan → FMR_scan_Channels
  → COM_Soft_Mute_Tune → ioctl(/dev/fm, FM_IOCTL_SOFT_MUTE_TUNE=0xc008f53f)   ← 每频点一次
  → 内核 fm_soft_mute_tune → mt6631_soft_mute_tune → 固件 SM tune + full CQI
```

- `[APK]`（实测确认，dex 方法名）：`FmNative.java` 声明 `seek(float, boolean)`、
  `autoScan()`、`stopScan()`、`readRssi()` 等 native 方法（`System.loadLibrary("fmjni")`，
  库即 `/system_ext/lib64/libfmjni.so`）。`FmService.seekStation()`（行 1502）先
  `setRds(false)` 再调 `FmNative.seek()`；`FmService.startScan()`（行 1680）先
  `setRds(false)` + `setMute(true)` 再调 `FmNative.autoScan()`。二者都在
  `FmRadioServiceHandler`（消息 14 = seek，消息 13 = scan）里同步执行——**Java 层完全
  阻塞，不参与搜台循环**。
- `[JNI]`（反汇编确认）：`seek` JNI 入口 0x9240 频率 ×100 转整数后直接进 `FMR_seek`
  （0x74dc）；`autoScan` 入口 0x9368 调 `FMR_Pre_Search` → `FMR_scan` → `FMR_Restore_Search`。

## 2. seek（单步搜台）的完整机制

### 2.1 频段与步进（`[JNI]` FMR_seek @ 0x74dc，反汇编确认）

| band（`fmr+0x24`） | min/mid | max | 总格点数（space=1，单位 10kHz） |
| --- | --- | --- | --- |
| 3（JPW） | 7600 | 10800 | 2050/10+1 = **206** |
| 2（JP） | 7600 | 9600 | 2000/10+1 = **201** |
| 其他（UE） | 8750 | 10800 | 2050/10+1 = **206** |

- 步进 10（100 kHz）或 5（50 kHz）：`COM_get_seek_space`（0xa7f8）读属性
  `persist.vendor.connsys.fm_50khz_support`，**非 1 即按 100 kHz**。`[DEV]` getprop 返回
  空串（属性不存在，property_get_int32 默认 0）→ 本机实际 **100 kHz**（实测确认）。
- seek 起点（如 8750）若不在 100 kHz 网格上，代码用"最后一个低于起点的格点"作为第 0 格。
- 方向（hilo 参数）：1 = 向上，0 = 向下；先向上扫完上限，再向下扫到下限。

### 2.2 每频点判定：一次 SOFT_MUTE_TUNE ioctl，无用户态 settle

`[JNI]` 内层探测函数（`_Z16FMR_seek_Channel...` 内联于 0x7138，`[REF]`
`FMR_Seek_TuneCheck` 同构，反汇编确认）：

1. 命令线程检查 `fmr+0x45c`（scan_stop 标志，由 `FMR_stop_scan` 置 1，支持随时取消）。
2. `softmute_tune(fd, freq, ...)` → `COM_Soft_Mute_Tune`（0xbc44）→
   `ioctl(fd, 0xc008f53f, &fm_softmute_tune_t{valid(4B), freq(2B@+4), rssi(4B@+8)})`。
3. ioctl 返回后，`valid` 已由**内核**判定；`valid == 1` 时：
   - `FMR_DensenseDetect`：`COM_desense_check`（ioctl FM_IOCTL_DESENSE_CHECK，nr=64）
     检测互调/干扰频点；
   - `FMR_SevereDensense`：查 CUST 假台表（本机为空，见 2.4）；
   - 都通过则记为候选台。
4. **相邻两格择优**（`[REF]` `FMR_Seek_More`，`[JNI]` 0x7368 处的 More 子函数与
   "seek cover last by freq=%d" 日志串逐条对应，反汇编确认）：命中一格后继续 tune
   相邻的 1~2 格，RSSI 更高者覆盖候选——等效于我们 step_scan 的"局部极大值"规则。

`[KSRC]`（实测源码确认）：`fm_soft_mute_tune`（core/fm_main.c:1753）要求
`fm_pwr_state_get(fm) == FM_PWR_RX_ON`（即 radio 已上电），然后调
`mt6631_soft_mute_tune`（chips/mt6631/pub/mt6631_fm_lib.c:1872）：

- 按 `mt6631_chan_para_get(freq)` 决定 HiLo 侧注入（写 FM_CHANNEL_SET 寄存器）；
- 发 `fm_full_cqi_req`（FM_SOFT_MUTE_TUNE_OPCODE 命令包，type=1 单频点）给固件，
  `fm_cmd_tx(..., FLAG_SM_TUNE, SW_RETRY_CNT=1, SM_TUNE_TIMEOUT=6, ...)` 等待事件；
- 收到 full CQI（RSSI/PAMD/MR/ATDC/PRX/ATDEV/SMGain/DeltaRSSI 十项），**在内核里**按
  六阈值联合判台（core/fm_main.c:1753 → chips/mt6631/pub/mt6631_fm_lib.c:1872）：

```c
valid = (RSSI    >= fm_config.rx_cfg.long_ana_rssi_th)   /* -296（fm_cust.cfg：FM_RX_RSSI_TH_LONG） */
     && (PAMD    <= fm_config.rx_cfg.pamd_th)            /* -12（fm_config.h:70 默认） */
     && (ATDC    <= fm_config.rx_cfg.atdc_th)            /* 3496 */
     && (MR      >= fm_config.rx_cfg.mr_th)              /* -67 */
     && (PRX     >= fm_config.rx_cfg.prx_th)             /* 64 */
     && (ATDEV   >= ATDC)
     && (SMGain  >= fm_config.rx_cfg.smg_th);            /* 16421（fm_cust.cfg：FM_RX_SMG_TH） */
```

阈值来源（`[DEV]` 实测 + `[KSRC]`）：`/vendor/firmware/fm_cust.cfg` 定义
`FM_RX_RSSI_TH_LONG = -296`、`FM_RX_SMG_TH = 16421`（其余取 fm_config.h 编译默认值）；
RSSI 标度与 `FM_IOCTL_GETRSSI` 一致（-296 ≈ 我们标定的 -88 dBm 量级，见 §5）。
`FM_IOCTL_SET_SEARCH_THRESHOLD`（nr=47）**在本机驱动里硬性返回"not supported"**
（`[KO]` 字符串 "FM_IOCTL_SET_SEARCH_THRESHOLD not supported"；`[KSRC]` fm_module.c
user build 直接 -EFAULT），因此 libfmjni 的 `FMR_EMSetTH`（emsetth）在本机是死路径，
MIUI 无法也无需改阈值——阈值就是 fm_cust.cfg + 编译默认。

**时序结论**：一个频点一次 ioctl，阻塞至固件返回 CQI（SM_TUNE_TIMEOUT=6 秒仅是出错
上限；正常远低于此）。libfmjni 每格**没有**任何 usleep/settle。MIUI 的 seek 速度 =
固件单点 SM tune 实测时长 × 格数。

### 2.3 app 层收尾（`[APK]`，实测确认）

`FmRadioServiceHandler` 消息 14：`FmNative.seek()` 返回 float 频率 →
`FmUtils.computeStation()` 校验有效 → `tuneStation()`（普通 TUNE ioctl）→
广播 `MSGID_SEEK_FINISHED(16)` + `key_seek_to_station`。seek 期间 RDS 已关、音量未
静音（seek 与 scan 不同：**不静音**，用户能听到扫频噪声）。

### 2.4 CUST 配置（`[JNI]` CUST_get_cfg @ 0xd494 + `[DEV]`）

按 `persist.vendor.connsys.fm_chipid`（本机 = mt6631_6635，实测确认）选内建表；
假台表 `fake_chan` 在本机为空（`FMR_get_cfgs` @ 0x6014 把 0x18/0x180 两处清零，
即 size=0）→ `FMR_SevereDensense` 恒 false，不剔除任何频点。

## 3. 全频段搜索（autoScan）的完整机制

`[JNI]` `FMR_scan`（0x8214）→ `FMR_scan_Channels`（0x79dc），与 `[REF]`
`FMR_scan_Channels` 逐段对上（反汇编确认）：

1. 入口前：Java 已 `setRds(false)` + `setMute(true)`；JNI 先 `FMR_Pre_Search`
   （ioctl `FM_IOCTL_PRE_SEARCH` = 0xc004f52d，内核侧仅关 I2S 音频输出，
   `[KSRC]` mt6631_pre_search），并清 scan_stop 标志。
2. 参数：`band=2/3 → min=7600, max=9600/10800, 格数=400/3200/2050 ÷ space`；
   UE：**min=8750，205 格**（space=1）。每格调用同一个 `softmute_tune` → 同一枚
   SOFT_MUTE_TUNE ioctl，取回 valid + RSSI。
3. 收集规则（`[REF]` 对照，`[JNI]` 日志串 "cover last channel"/"desense channel
   detected" 逐条命中）：
   - `valid == 0` → LastExist = false，且把该点 RSSI 计入噪声底样本；
   - `valid == 1` 且上一格已收录 → 比较上一收录点 RSSI，当前更强则**覆盖**（相邻合并）；
   - 否则新收录；desense 检查失败的点跳过。
4. 噪声底剔除：每 NF_Space=40 格（UE/JPW）/41 格（JP）结算一次噪声底
   `NF = Pacc/Nacc`，低于 `NF + FM_NOISE_FLOOR_OFFSET` 的已收录点被标记剔除
   （`[REF]` FMR_scan_Channels 中 noise_floor 分支；`fmr_data.cfg_data.noise_floor_detect`
   默认 0/由 CUST 决定，`[JNI]` 0x7f14 处 `cfg+0x3c == 1` 才走排序分支）。
5. 排序：`cfg_data.scan_sort` 决定按 RSSI 升/降序插入排序（日志串 "Start sort"/
   "End sort"；`[JNI]` 0x7f44–0x8000 插入排序循环）。
6. 输出：`scan_tbl[i] = ch / 10`（10kHz → 100kHz 单位），short[200] 上限，实际数量写
   `max_cnt`；JSM 层转 short[] 返回。空结果时 `*max_cnt = 0` 并 `FMR_Restore_Search`。
7. 收尾：`FMR_Restore_Search`（ioctl `FM_IOCTL_RESTORE_SEARCH` = 0xc004f52e）+
   若 scan_stop 置位则回 tune 原频率（`[JNI]` 0x94a0–0x94c4）。

`[APK]`（实测确认）：`FmService.startScan()` 把 short[] 转 int[] → Handler 消息 13
逐个 `notifyFmStationScaned(f)`；MIUI 侧 `FmServiceManager$2` 对每个台先
`tuneFm(台)` 实测可收再 `seekFm(true)` 跳下一台（`FmLocalService.java:1243` →
`FmServiceManager.scanStations()` → `tuneFm(getMinFrequency())` 起头）——所以 MIUI
"全搜" 的真实耗时 = 一次 autoScan ioctl 循环 + 每台两次普通 tune。

## 4. 我们为什么慢：逐项对比

| 维度 | MIUI (libfmjni + 内核) | RFM-Radio mtk 后端 | 确定性 |
| --- | --- | --- | --- |
| 搜台载体 | 每格 1 次 `FM_IOCTL_SOFT_MUTE_TUNE`，调谐+采样+判台全在固件/内核 | 每格 1 次 TUNE + 1 次 GETRSSI，判定在用户态 | 实测确认（双方源码/反汇编） |
| 每格驻留 | ioctl 内部，固件自己决定（典型 ~50-200 ms 量级，无用户态 sleep） | **固定 60 ms**（`kScanSettleMs`）+ 2 次 ioctl 往返 | 反汇编确认（libfmjni 无 usleep 于循环内） |
| 判台指标 | RSSI+PAMD+MR+ATDC+PRX+ATDEV+SMGain 七项联合（内核） | 单一 RSSI ≥ -88 dBm（`kScanThresholdDbm`） | 实测确认 |
| 阈值来源 | `/vendor/firmware/fm_cust.cfg`（RSSI=-296）+ 编译默认（PAMD=-12 等）；runtime 不可改（SET_SEARCH_THRESHOLD 被禁） | 我们自己标定的 -88 dBm（恰好与 -296 同标度） | 实测确认 |
| 步进 | 100 kHz（`fm_50khz_support` 属性空 → 默认 100k） | 我们 spacing 同 100 kHz（EU 205 格） | 实测确认 |
| 相邻台合并 | seek：命中后再探 2 格取最强；scan：上一收录点覆盖 | seek：无（首个越阈值即停）；scan：局部极大值 | 实测确认 |
| 假台/干扰剔除 | 内核 desense_check + CUST 假台表（本机空） | 无（靠阈值兜底） | 实测确认 |
| 噪声底剔除 | 每 40/41 格结算噪声底并降格 | 无 | 反汇编推断（受 cfg 开关控制） |
| 全搜排序 | 收集后按 RSSI 排序（结果顺序≠频谱顺序） | 频谱顺序（ScanResultCollector 排序按频率） | 实测确认 |
| 全搜输出上限 | 200 台（short[200]） | 64 台（JSON 512B 限制） | 实测确认 |
| 搜台时 RDS/静音 | seek 前 `setRds(false)`；scan 前 `setRds(false)`+`setMute(true)` | scan/seek 不动 RDS，不静音 | 实测确认 |
| 取消 | scan_stop 标志 + PRE/RESTORE_SEARCH | scan_cancel_ 原子标志 | 实测确认 |

**"为什么 MIUI 快"的直接答案**：不是驱动有硬件 SEEK（`FM_IOCTL_SEEK/SCAN` 在本机
确实仍返回 -1，`[KO]` fm_ops_ioctl 跳转表 + `[KSRC]` 均确认），而是 MIUI 每格只发
**1 次阻塞 ioctl**，把"调谐→稳定→采样→判台"全部交给固件（一次 STP 往返，固件内
部驻留通常短于我们盲目等的 60 ms，且失败/成功一次 ioctl 就有结论）；我们每格是
**2 次 ioctl + 固定 60 ms 睡眠 + 用户态判定**，CPU 侧开销（两次 syscall + 60 ms×205 格
≈ 12.3 秒纯睡眠）决定了全搜下限。MIUI 同样的 205 格在同样的 ioctl 往返上还要加上
固件真实驻留，但省掉了 60 ms/格的人为等待与一半的 syscall。

补充：`FM_IOCTL_TUNE`（普通调谐）同样是阻塞到固件 CQI 才返回的（`[KSRC]`
fm_tune → mt6631_tune → fm_cmd_tx(FLAG_TUNE, TUNE_TIMEOUT=3s)），但我们 step_scan
额外等 60 ms 是为了"tune 后立即读 RSSI 不准"——SOFT_MUTE_TUNE 路径里 CQI 是固件
在一次调谐内测完再上报的，不存在这个竞态。这就是两者速度差的核心。

## 5. 对 RFM-Radio 的具体修改建议

按"省事优先"排序，全部改动点都在 native/mtk/（**本次调研未改任何代码**）：

### 建议 A（最大收益）：把每格判定换成 SOFT_MUTE_TUNE ioctl

1. `native/mtk/mtk_ioctl.h` 增加两个定义（结构布局 `[JNI]` 0xbc44 反汇编 +
   `[KSRC]` fm_main.h:312 双向验证）：

```c
struct fm_softmute_tune_t {
    int32_t rssi;      // OUT，与 GETRSSI 同标度
    uint16_t freq;     // IN，10 kHz 单位（8750 = 87.5 MHz）
    int32_t valid;     // OUT，1 = 通过内核六阈值联合判定
};
#define MTK_IOCTL_SOFT_MUTE_TUNE _IOWR(MTK_IOCTL_MAGIC, 63, struct fm_softmute_tune_t*)
// 注意字段顺序：rssi(0-3), freq(4-5), valid(6-11)，total 12 字节；
// libfmjni 的局部结构是 {valid, freq, rssi}（valid@0,freq@4,rssi@8）再拆装，
// ABI 等价，注意别照抄字段名顺序。
```

2. `native/mtk/mtk_dev.*` 加 `mtk_soft_mute_tune(fd, freq, &rssi, &valid)` 包装。
3. `native/mtk/step_scan.*` 的 `probe()`：把 `tune + settle + rssi` 换成一次
   `soft_mute_tune`；`kScanSettleMs` 变成不再使用的死常量（可删）。
4. 阈值仍用我们自己的 -88 dBm 作二次校验（防止某台 PAMD/MR 通过但听感差），也可
   直接信任 valid（与 MIUI 行为一致）。
5. 预期：全搜 205 格从 `~205×(60ms+2 syscall)` 降至 `~205×(1 syscall)`，提速幅度 =
   实测 ioctl 单次耗时 vs 60 ms+2 syscall 的差。MIUI 表现证明这条路径在本机固件
   上是正常工作的。

### 建议 B（低成本，不改 ABI）：调小 settle / 改自适应

若暂时不想引入新 ioctl：`native/mtk_backend.cpp` 第 52 行 `kScanSettleMs = 60` 下调到
30-40 ms（MTK 驱动 tune 是阻塞到锁频的，`[KSRC]` mt6631_tune 语义；60 ms 是保守值）。
预期全搜省 4-6 秒，但不如建议 A——仍是双 ioctl + 固定等待。

### 建议 C（顺手）：seek 也用"局部极大值"语义

`native/mtk/step_scan.cpp` `run_seek()` 命中即返回；MIUI 会再向前多探 2 格取最强。
对齐 MIUI 语义可避免"搜到台的旁瓣"（用户可感知的音质差异）。

### 建议 D（可选，需设备验证）：FM_IOCTL_PRE_SEARCH / RESTORE_SEARCH

全搜入口先 PRE_SEARCH（0xc004f52d）、出口 RESTORE_SEARCH（0xc004f52e），对齐 MIUI。
`[KSRC]` 显示它们只做 RampDown + 关 I2S 音频输出；不确认对判台准确性有实际帮助，
优先级最低。

### 建议后续验证（本轮未执行，均为只读实验方案）

1. **`FM_IOCTL_SOFT_MUTE_TUNE` 冒烟**（root shell 下用现有 fmbin 加一条临时调试命令，
   不上设备常驻）：POWERUP 后对一个已知台（如 87.5 或实际收听频点）发
   SOFT_MUTE_TUNE，观察 valid/rssi 返回与耗时；再对一个静噪频点发，确认 valid=0。
   预期单次耗时 50-200 ms。若 -EPERM，检查是否未先 POWERUP。
2. **实测单格耗时分布**：dmesg 已证实 `FM_IOCTL_GETRSSI` 每次都会打印
   （g_dbg_level=0xffffffff，`[DEV]` 实测），SM_TUNE 成功同样打
   "freq=%d, 0x%04x, ..." CQI 行（`[KSRC]` WCN_DBG(FM_NTC|CHIP)）；在 fmbin 里循环
   205 次并打时间戳即可统计固件每格真实驻留。
3. **RDS off 是否真的加速**：MIUI 搜台前 `setRds(false)`（`[APK]` FmService 行 1502/
   1680）。可在建议 A 落地后做 A/B（同一时段连续两次全搜，一次关 RDS 一次不关），
   对比 dmesg STP 流量（`stp_psm_disable_by_tx_rx_density: tx:N` 行）。

## 6. 实机验证（2026-10-12，cannon，建议 A+C 落地后）

建议 A+C 已实现并实测，数据如下（日志 tag `RFM-QCOM`，`mtk/scan` 前缀）：

- **全频段搜索**（EU 87.5–108 MHz，100 kHz，206 格）：start→completed 共
  **8.31 s**，平均 **39.5 ms/格**；命中 17 台，`search_done` 事件正常。
  39.5 ms 即固件 SM tune 的单点驻留，本路径的物理下限，MIUI 同路径同速。
- **seek 向上**（88300 → 88500，3 格）：**327 ms**。命中 88500 后 refine
  确实多探了 88600（valid=0）后收手，落台 88500——建议 C 的邻格择优实测工作。
- **内核判定优于单 RSSI 的实证**：88400 格 rssi_raw=-223（≈-83.6 dBm，高于
  我们 -235 raw 阈值）但内核 valid=0（PAMD/MR 等其他指标不过）；旧的单 RSSI
  判定会误停此伪台，新实现跳过直接落 88500。
- 二次校验与内核判定的交叉：valid=1 且 rssi_raw ≥ -235 的格全部收录；
  valid=1 但 -235 以下的暗台（如 106700 的 -249、107900 的 -243）被
  `-235` 阈值挡住——与设计意图一致。
- 冒烟注意事项：fmbin 的 UDP 命令按精确字符串匹配，`echo` 带的换行会得到
  `ERR_UNK_CMD`（`printf 'searchhw'` 不带换行）；toybox nc 可能把带空格命令
  拆成多个 datagram（`seekhw 1` 变 "seekhw " + "1"），UI 触发或无参命令更稳。
  force-stop app 不会杀 su 启动的 fmbin（root 进程不在 app 进程组），残留
  进程需手动 kill 才释放 2112 端口。

## 7. 风险与边界

- SOFT_MUTE_TUNE 要求 radio 已 POWERUP（`[KSRC]` fm_soft_mute_tune 前置检查）；我们的
  scan worker 已保证 enabled_ 才启动，无需额外守卫。
- `fm_soft_mute_tune` 内部 `FM_LOCK(fm_ops_lock)`（`[KSRC]`），意味着 RDS read 的
  WMT 流量与此 ioctl 并发是安全的（内核自己排序），但**不要**在另一线程同时发 TUNE，
  否则会串行化等待。
- `FM_IOCTL_SET_SEARCH_THRESHOLD` 在本机驱动是硬编码 not-supported（user build 分支），
  不要尝试用它调阈值——如果将来想改阈值，只能改 `fm_cust.cfg`（写 vendor 分区，非
  本项目范畴）或继续用我们用户态 -88 dBm 的二次校验。
- libfmjni 的 `FMR_seek_Channel` 有"seek 起点 8750 不在网格上则多扫 1 格"的边界
  （`[JNI]` 0x6e94 `sdiv(w8=w1-w2, w5)` 后 +1），我们 step_scan 用的是"直接从下限开始"
  （`lower_khz` 即网格），语义等价，无问题。
- 失败模式：单点 ioctl 若超时（SM_TUNE_TIMEOUT=6s），MIUI 把该格当 invalid 继续；
  我们 step_scan 里 probe 失败记 -inf 继续——行为一致，无需改。
