# MediaTek FM 后端移植调研（Redmi Note 9 5G / cannon / MT6853 + MT6635）

调研日期：2026-10-10。目标设备：Redmi Note 9 5G（cannon，M2007J22C），MT6853（天玑 800U），
Android 12 / MIUI V14.0.6.0.SJECNXM，内核 4.14.186，已 root，MIUI 自带 FM 工作正常。

本文档是为 RFM-Radio 编写第三个（MediaTek）原生后端的前期调研，回答：内核驱动形态、
userspace 接口、可参考开源实现、音频通路、RDS 与信号强度。约定同
[`native-fm-backend.md`](native-fm-backend.md)：实现与文档冲突时以设备实测为准。

## 证据来源

| 标记 | 来源 |
| --- | --- |
| `[ROM]` | 本机 fastboot ROM：`/home/w0n9/workspaces/ghostlock-app/.scratch/cannon-root/cannon_images_V14.0.6.0.SJECNXM_20231017.0000.00_12.0_cn_ce9fc0ac48.tgz`（已解包 super.img 的 vendor/system/product 分区到 `/home/w0n9/scratch/mtk-cannon/parts/`，文件提取到 `/home/w0n9/scratch/mtk-cannon/fs/`） |
| `[BR]` | 设备 bugreport：`/home/w0n9/workspaces/ghostlock-app/.scratch/cannon-root/bugreport-2026-09-10-093817.zip` 内层 `bugreport-cannon-SP1A.210812.016-2026-09-10-09-38-22.txt`（SP1A.210812.016 = Android 12 实锤） |
| `[KO]` | 设备内核模块 `vendor/lib/modules/fmradio_drv_mt6631_6635.ko`（未 strip，带完整符号；提取于 `[ROM]` vendor.img，本地路径 `/home/w0n9/scratch/mtk-cannon/fs/vendor/lib/modules/`） |
| `[JNI]` | 设备 `system_ext/lib64/libfmjni.so`（aarch64，stripped，但导出符号完整；`/home/w0n9/scratch/mtk-cannon/fs/system/system/system_ext/lib64/libfmjni.so`） |
| `[KSRC]` | 同代 MTK 4.14 内核完整 FM 驱动源码：`Redmi-MT6768/android_kernel_xiaomi_mt6768`（GitHub），`drivers/misc/mediatek/connectivity/fmradio/`；本地 clone `/home/w0n9/scratch/mtk-cannon/kernel/mt6768/`。已与 `[KO]` 做字符串/符号级交叉验证（独有日志串逐条命中），可视为同一驱动世代 |
| `[QOSS]` | 小米官方开源内核 `MiCode/Xiaomi_Kernel_OpenSource` 的 `cannon-q-oss` 分支（本地 checkout `/home/w0n9/scratch/mtk-cannon/kernel/cannon-r-oss/` 当前在 q-oss FETCH_HEAD） |

## 一句话总结

cannon 的 MTK FM 栈是"内核态全家桶"：FM 驱动（`fmradio_drv_mt6631_6635.ko`）经 WMT/connys
子系统控制 MT6635 combo 芯片，对 userspace 暴露单一字符设备 **`/dev/fm`**（ioctl magic `0xf5`）；
**本机不存在 `libfmcust.so`**，stock FM app 的 `libfmjni.so` 直接 open/ioctl/read `/dev/fm`；
**驱动不支持硬件 seek/scan**（SEEK/SCAN ioctl 返回 -1），搜台由 userspace 步进
TUNE+GETRSSI 实现；RDS 由内核解析后经 `read()` 以 688 字节 `struct rds_t` 上抛；
音频是**数字通路**（MT6635 → Connsys I2S → AFE/ADSP），经 AudioFlinger 的
`FM Tuner In` 输入设备被 app 用 AudioRecord 采集后 AudioTrack 放音。

---

## 1. 内核侧：MT6853 FM 驱动形态

### 1.1 模块与设备节点

两个内核模块（`[ROM]` vendor 分区 `/vendor/lib/modules/`）：

| 模块 | 作用 | 设备节点 |
| --- | --- | --- |
| `wmt_drv.ko` | WMT connectivity 核心（电源、固件下载传输、STP 协议） | `/dev/stpwmt`（0600 段字符串见 `[KO]` 同级 `wmt_drv.ko`，"stpwmt"） |
| `fmradio_drv_mt6631_6635.ko` | FM 驱动，支持 MT6631/MT6635 两种 ADIE | `/dev/fm` |

`[KO]` 关键证据：
- `nm` 可见 `fm_open`、`fm_powerup`、`fm_ops_ioctl`、`fm_rds_parser`、`fm_rds_onoff` 等 1416 个符号；`depends=wmt_drv.lto`（依赖 wmt_drv）。
- 创建字符设备：`alloc_chrdev_region` / `device_create` 字符串与源码 `[KSRC] core/fm_module.c:1399,1429` 对应，`FM_NAME = "fm"`（`[KSRC] inc/fm_main.h:27`）。
- `/proc/fm` proc 节点（`[KSRC] core/fm_module.c:1545`，`FM_PROC_FILE "fm"`）。
- 与 WMT 的对接**在内核内完成**：`fm_wmt_func_on/off` → `mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM)`（`[KSRC] plat/legacy_wmt.c:85-101`）。**后端进程不需要直接碰 `/dev/stpwmt`**。

加载链（init 脚本，`[ROM]` vendor `/vendor/etc/init/`）：

```text
init.wmt_drv.rc:   on boot → insmod /vendor/lib/modules/wmt_drv.ko
init_connectivity.rc（hw/）:
  service wmt_loader   /vendor/bin/wmt_loader        (oneshot, 探测 /dev/wmtdetect)
  service wmt_launcher /vendor/bin/wmt_launcher -p /vendor/firmware/ -o 1
init.fmradio_drv.rc: on property:vendor.connsys.driver.ready=yes
                     → insmod /vendor/lib/modules/fmradio_drv_${ro.vendor.fm.platform}.ko
```

`/vendor/build.prop`（`[ROM]`）：`ro.vendor.fm.platform=mt6631_6635`、
`persist.vendor.connsys.fm_chipid=mt6631_6635`。
`wmt_launcher` 打开 `/dev/stpwmt` 下载 connectivity 固件——`[BR]` 行 10238-10310：
`wmt_launcher: /vendor/firmware//soc2_0_patch_mcu_3c_1_hdr.bin`、
`set persist.vendor.connsys.patch.version to 220726100802000`。
`[BR]` 行 124882-124883 可见内核线程 `fm_timer_wq`/`fm_eint_wq`——它们由 FM 驱动创建
（`[KSRC] core/fm_main.c:2475,2485`），证明**设备正常启动后 FM 模块已加载**。
FM 芯片 DSP 补丁由内核驱动自己下载：`mt6635_fm_v1_patch.bin`/`mt6635_fm_v1_coeff.bin`/
`fm_cust.cfg`（`[ROM] /vendor/firmware/`；`[KSRC] core/fm_config.c:29` 的
`FM_CUST_CFG_PATH "fm_cust.cfg"` 经 request_firmware 读取；`[KO]` 字符串
"DL DSPpatch failed" / "fm_coeff_download failed"）。

ADIE 选择：`fmradio_drv_mt6631_6635.ko` 运行时查询 ADIE id（`mtk_wcn_wmt_ic_info_get(WMTCHIN_ADIE)`，
`[KSRC] plat/legacy_wmt.c:251-262,344,362-371`），0x6631/0x6635 分别注册
`mt6631_*` / `mt6635_*` 芯片 ops（两套符号都在 `[KO]` 中，如 `mt6635_pre_search`）。

节点权限与 SELinux（root 后端不受影响，仅记录）：
- `ueventd.rc`（`[ROM]` vendor）：`/dev/fm 0660 media media`，`/dev/stpwmt 0660 system system`。
- `init.mt6853.rc:546-547`：`chmod 0660 /dev/fm; chown media media /dev/fm`。
- `vendor_file_contexts`：`/dev/fm → fm_device`，`/dev/stpwmt → stpwmt_device`。

### 1.2 小米开源内核状况（cannon-r-oss / cannon-q-oss）

`[QOSS]` 两个分支的 `drivers/misc/mediatek/connectivity/` 均被裁剪到只剩
`Kconfig`/`Makefile`/`common/`（6 个文件），**不含 fmradio、不含 wmt 驱动本体**。
MTK connectivity 驱动以预编译 `.ko` 分发。功能等价的完整源码见第 3 节。
（cannon-q-oss 的 `sound/soc/mediatek/mt6853` ASoC 机器驱动是完整的，第 4 节用到。）

### 1.3 `/dev/fm` ioctl 协议

命令字：`magic=0xf5`，`_IOWR(0xf5, nr, type)`。64 位 userspace 下指针参数 size=8
（如 `FM_IOCTL_TUNE = 0xc008f502`），个别 by-value int32 参数 size=4
（如 `FM_IOCTL_SETMONOSTERO = 0xc004f525`）。完整定义：`[KSRC] inc/fm_ioctl.h:20-108`。

**cannon `[KO]` 的 `fm_ops_ioctl` 跳转表实测**（反汇编 `.rodata+0x9b3a` 跳转表 +
`0xfc84` 小命令子开关；未命中返回 -1）：

| 状态 | ioctl（nr） |
| --- | --- |
| 有实际处理分支 | POWERUP(0) POWERDOWN(1) TUNE(2) SETVOL(4) GETVOL(5) MUTE(6) GETRSSI(7) GETCHIPID(10) EM_TEST(11) RW_REG(12) GETMONOSTERO(13) GETCURPAMD(14) GETGOODBCNT(15) GETBADBNT(16) GETBLERRATIO(17) RDS_ONOFF(18) RDS_SUPPORT(19) POWERUP_TX(20) TUNE_TX(21) RDS_TX(22) IS_FM_POWERED_UP(24) TX_SUPPORT(25) RDSTX_SUPPORT(26) RDSTX_ENABLE(27) TX_SCAN(28) OVER_BT_ENABLE(29) ANA_SWITCH(30) GETCAPARRAY(31) GPS_RTC_DRIFT(32) I2S_SETTING(33) RDS_GROUPCNT(34) RDS_GET_LOG(35) RDS_BC_RST(38) CQI_GET(39) GET_HW_INFO(40) GET_I2S_INFO(41) IS_DESE_CHAN(42) TOP_RDWR(43) GET_AUDIO_INFO(48) FM_SET_STATUS(49) FM_GET_STATUS(50) SOFT_MUTE_TUNE(63) DESENSE_CHECK(64) PMIC_RDWR(65)；小命令子开关另有 SETMONOSTERO(37) PRE_SEARCH(45) RESTORE_SEARCH(46) |
| 有分支但运行时"不支持" | SCAN_GETRSSI(36)、HOST_RDWR(44)、SET_SEARCH_THRESHOLD(47)（`[KO]` 字符串："FM_IOCTL_SCAN_GETRSSI:not support"、"Not support FM_IOCTL_HOST_RDWR"、"FM_IOCTL_SET_SEARCH_THRESHOLD not supported"） |
| **不支持（返回 -1）** | **SEEK(3)、SCAN(8)、STOP_SCAN(9)**、RDS_SIM_DATA(23)、SCAN_NEW(60)、SEEK_NEW(61)、TUNE_NEW(62)、DUMP_REG(0xFF) |

**关键结论：本机驱动没有任何硬件 seek/scan。**搜台只能 userspace 步进（见 2.3）。

关键结构体（`[KSRC] inc/fm_main.h:78-118`、`inc/fm_config.h:24-49`）：

```c
struct fm_tune_parm {   /* 8 字节；POWERUP/TUNE 共用 */
    u8 err;  u8 band;  u8 space;  u8 hilo;  u8 deemphasis;  u8 pad;  u16 freq;
};
/* freq: 10 kHz 单位（8750 = 87.5 MHz；7600..10800）。驱动也容忍 100 kHz 单位自动换算
   （fm_get_channel_space(freq)==0 时 freq*=10，mt6635_fm_lib.c:2078） */
/* band:   1=UE(87.5-108) 2=Japan(76-90) 3=JapanW(76-108) 4=special */
/* space:  1=100kHz 2=200kHz 5=50kHz；deemphasis: 0=50us 1=75us */
```

`FM_IOCTL_POWERUP` 语义 = 上电**并** tune 到 parm.freq（`[KSRC] core/fm_module.c:150-166`
中 `fm_powerup` 后接 `fm_tune`）。`FM_IOCTL_MUTE` u32 1/0。
`FM_IOCTL_SETVOL/GETVOL`：u32 0..15（`mt6635_vol_tbl[16]`，`[KSRC] chips/mt6635/pub/mt6635_fm_lib.c:1471`）。

### 1.4 RDS 上抛通路（read）

`read(/dev/fm)` 返回内核**已解析**的 `struct rds_t`（`[KSRC] core/fm_module.c:1211`
`fm_ops_read` → `fm_rds_read`；大小固定 688 字节）。内核里跑完整 RDS 解析器
（`fm_rds_parser`，`[KO]`/`[KSRC] core/fm_rds_parser.c`），事件位见
`[KSRC] inc/fm_rds.h:182-199`（`RDS_EVENT_FLAGS/PI_CODE/PTY_CODE/PROGRAMNAME/
UTCDATETIME/LOCDATETIME/LAST_RADIOTEXT/AF/AF_LIST/AFON_LIST/TAON/TAON_OFF/
ECC_CODE/RDS/NO_RDS/RDS_TIMER`）。布局详见第 5 节。

---

## 2. Userspace 接口：没有 libfmcust，libfmjni 直打 /dev/fm

### 2.1 库的位置与形态

- 全盘搜索三个分区：**无 `libfmcust.so`**（该库是 MT6582/MT6753 等老平台形态，
  见 3.4）。CUST 配置层已编进 `libfmjni.so`（导出 `_Z12CUST_get_cfgP11CUST_cfg_ds`、
  日志 tag `FMLIB_CUST`）。
- stock app：`/system_ext/app/FM/FM.apk`（`[ROM]` system 分区），包名 **com.miui.fmradio**
  （MIUI 重写版；dex 中 `Lcom/miui/fmradio/FmLocalService` 等）。bugreport 里
  `com.mediatek.fmradio.FmRadioPackageManager not found`（`[BR]` 行 17795）说明 MTK
  框架版 FM 服务不在系统中，MIUI app 直接依赖 JNI。
- JNI 库：`/system_ext/lib64/libfmjni.so`（`[JNI]`）。动态依赖只有 libc/libc++/liblog/
  libcutils/libdl/libm（libmedia 列入 NEEDED 但**未引用任何符号**——音频不经由此库）。
  未定义符号仅 14 个：`open/ioctl/read/close/property_get[_int32]/usleep/...`。
- JNI 注册类：`com/android/fmradio/FmNative`（`[JNI]` 字符串；RegisterNatives 目标）。

### 2.2 导出 API 面（116 个导出符号，`nm -D`）

三层结构：`JNI 导出 → FMR_* → COM_* → ioctl`。主要符号与已实测的 ioctl（`[JNI]`
反汇编验证，立即数逐一比对）：

| JNI/COM 函数 | 语义 | 实测 ioctl |
| --- | --- | --- |
| `openDev` → `COM_open_dev` | `open("/dev/fm", O_RDWR)`（"[JNI]" rodata `/dev/fm`） | — |
| `powerUp(float)` → `COM_pwr_up(fd, band, freq)` | `freq*100`（0x42c80000=100.0f 浮点乘，`[JNI]` 0x90a0-90b0）→ 10kHz 单位；参数块 `{err=0, band, space=1, hilo=0, deemphasis=0, freq}` | `0xc008f500` POWERUP |
| `powerDown` → `COM_pwr_down` | 关机 | `0xc008f501` POWERDOWN |
| `tune(float)` → `COM_tune` | freq 写 parm 偏移 6（10kHz 单位） | `0xc008f502` TUNE |
| `setMute` → `COM_set_mute` | | `0xc008f506` MUTE |
| `readRssi` → `COM_get_rssi` | | `0xc008f507` GETRSSI |
| `getchipid` → `COM_get_chip_id` | | `0xc008f50a` GETCHIPID |
| `setRds` → `COM_turn_on_off_rds` | | `0xc008f512` RDS_ONOFF |
| `isFMPoweredUp` → `COM_is_fm_pwrup` | | `0xc008f518` IS_FM_POWERED_UP |
| `switchAntenna` → `COM_ana_switch` | 天线切换 | `0xc008f51e` ANA_SWITCH |
| `setStereoMono` → `COM_set_stereomono` | by-value int32 | `0xc004f525` SETMONOSTERO |
| `stereoMono` → `COM_get_stereomono` | | `0xc008f50d` GETMONOSTERO（nr=13） |
| `readRds` → `COM_read_rds_data` | **`read(fd, buf, 688)`**，读 `event_status` @ 偏移 0x22a | read() |
| `seek`/`autoScan`/`stopScan` → `FMR_seek`/`FMR_scan`/`FMR_stop_scan` | **用户态软扫描**（见 2.3） | TUNE+GETRSSI 循环 |
| `getPs`/`getLrText`(RT)/`getPI`/`getPTY`/`getECC`/`getAFList`/`readRdsBler`/`readCapArray`/`getHardwareVersion`/`isRdsSupport`/`setSearchThreshold(emsetth)`/`softMuteTune` 等 | RDS 字段从最近一次 `read()` 的 rds_t 缓存解析 | 对应 GETBLERRATIO(17)/GETCAPARRAY(31)/GET_HW_INFO(40)/RDS_SUPPORT(19)/SET_SEARCH_THRESHOLD(47)/SOFT_MUTE_TUNE(63) |

属性交互（`[JNI]` 字符串）：`persist.vendor.connsys.fm_chipid`、
`persist.vendor.connsys.fm_50khz_support`、`persist.vendor.connsys.fm_short_antenna_support`、
`vendor.connsys.adie.chipid`、`ro.miui.customized.region`。

### 2.3 seek/scan 是用户态步进

`FMR_seek`（`[JNI]` 0x74dc）内嵌频段边界常数 10800/9600/8750/7600 与步进 3200/2050/2000，
调用内部 `FMR_seek_Channel`（0x6e1c）逐级 TUNE→读 RSSI→与阈值比较。
`FMR_scan`/`FMR_scan_Channels`（0x8214/0x79dc）全频段同理收集有效台。
**RFM 的 MTK 后端必须同样自己实现 seek/scan 状态机**——这与现有 legacy 后端的全扫描
状态机设计天然契合（`native/legacy/scan_state.cpp` 可复用模式），但"上一个有效台"
语义要靠 RSSI 阈值自己判定（阈值来源见 5.3）。

### 2.4 CUST 配置层

`CUST_get_cfg` 按 `persist.vendor.connsys.fm_chipid` 选芯片配置表（band/seek 阈值/
假台列表/50kHz/短天线支持），逻辑与开源参考实现 `iusmac/RevampedFMRadio` mtk 分支
`jni/fmr/custom.cpp:95-149` 完全同构（该文件即 MTK 原生 custom.cpp 的演化版，
含 `FM_CHIP_MT6635` 分支，行 118-119）。本机 fm_cust.cfg（`[ROM]`）内容：
`FM_RX_RSSI_TH_LONG=-296`、`FM_RX_DESENSE_RSSI=-240`、`FM_RX_SMG_TH=16421`、
`FM_RX_DEEMPHASIS=0`（50us）、`FM_RX_OSC_FREQ=0`（26MHz）——此文件由**内核**读取
（1.1 节），userspace 的 CUST 表是编译内建+属性覆盖。

---

## 3. 可参考的开源实现

### 3.1 内核驱动完整源码（高价值）

- `Redmi-MT6768/android_kernel_xiaomi_mt6768`：
  `drivers/misc/mediatek/connectivity/fmradio/{core,inc,plat,chips/{mt6631,mt6635,...}}`。
  与 cannon `[KO]` 交叉验证：独有字符串逐条命中（"FM_IOCTL_SET_SEARCH_THRESHOLD not
  supported"、"Not support FM_IOCTL_HOST_RDWR"、"FM_IOCTL_SCAN_GETRSSI:not support"、
  "WMT turn on FM Fail"、"fm platform device alloc fail"），函数符号同名
  （`fm_hwscan_stop`、`fm_tx_scan`、`fm_get_read_result`…）。**本调研所有内核侧结论以
  `[KO]` 反汇编为准、`[KSRC]` 为语义注释。**
- 同一代码还存在于：`realme-kernel-opensource/realmeC12_realmeC15_AndroidR-kernel-source`
  （`modules/connectivity/fmradio/`，官方 OEM 发布）、`AgentFabulous/begonia`、
  `NothingOSS/android_kernel_modules_nothing_mt6878` 等（GitHub 搜索
  `fm_wmt_chipid_query` 命中列表）。
- WMT/conninfra 用户态守护 `wmt_launcher` 源码未见公开发布；但其角色（固件下载、
  置 `vendor.connsys.driver.ready`）已由 `[ROM]` init 脚本 + `[BR]` 日志完全覆盖，
  **FM 后端不需要与其交互**。

### 3.2 MTK FMRadio app / JNI（高价值）

- `iusmac/RevampedFMRadio` **mtk 分支**（https://github.com/iusmac/RevampedFMRadio，
  branch `mtk`）：`jni/fmr/{fmr_core.cpp, common.cpp, custom.cpp, libfm_jni.cpp, fm.h,
  fmr.h, fmlib_cust.h}` —— 即 MTK 原生 libfmjni 的开源版本，FMR_/COM_ 函数与设备
  `[JNI]` 导出同名同结构，`FM_DEV_NAME "/dev/fm"`（fmr.h:66），`COM_open_dev` 即
  `open(pname, O_RDWR)`（common.cpp:24-41）。**ABI 警告**：该仓库 `fm.h` 是旧版 ABI
  （`fm_tune_parm` 无 deemphasis 字段、`freq*10` 得 100kHz 单位、ScanTBL[16]）；
  本机以 1.3 节内核头为准（freq*100、10kHz 单位、8 字节 tune_parm，
  已由 `[JNI]` 0xc008f502 的 size=8 与 freq@偏移6 实证）。
- `LineageOS/android_packages_apps_FMRadio`（如 branch `lineage-17.1`）：
  `jni/fmr/{fm.h, fmr_core.cpp, common.cpp, fmr_err.cpp, libfm_jni.cpp}`——MTK
  FMRadio app 的 AOSP 化版本，同为旧 ABI，可参考 RDS 解析与 UI 层语义。

### 3.3 SpiritFM（mikereidis/spirit2_free）

**未找到 MTK 支持代码。**开源仓库 `jni/tnr/` 只有 Broadcom（`tnr_bch.c`）、
Qualcomm（`tnr_qcv.c`）、SiliconLabs（`tnr_ssl.c`）与一个空壳 "Custom" 演示
（`tnr_cus.c`，自述 "Does nothing at present"）。对 MTK 无参考价值。

### 3.4 libfmcust / stpwmt 直操作项目

`libfmcust.so` 只见于老平台设备树（MT6582/MT6752/MT6753 时代，如
`janengelmohr/android_device_elephone_p8000`、`rohantaneja/android_device_mediatek_mt6582`
的 prebuilt 列表）；本机不存在该库（2.1）。未找到直接以 stpwmt 协议实现 FM 的开源
userspace；且对本机无必要（WMT 交互在内核内，见 1.1）。

---

## 4. 音频通路：数字 I2S，经 AudioFlinger

### 4.1 芯片到 SoC

MT6635 FM 音频经 **Connsys I2S** 进入 SoC（数字，非模拟直通）：

- 内核 ASoC（`[QOSS]` cannon-q-oss，即本机内核同源代码）：
  `sound/soc/mediatek/mt6853/mt6853-mt6359.c:731-739` 定义 `Hostless_FM` DAI link；
  行 949-950 定义 `CONNSYS_I2S` DAI；
  `sound/soc/mediatek/mt6853/mt6853-dai-hostless.c:117-140` 路由：
  `"Hostless FM UL" ← "Connsys I2S"`（上行采集）；
  `"HW_GAIN1_IN_CHx" ← "CONNSYS_I2S_CHx"`（"Hostless FM DL"）→ `ADDA_DL_CHx`（DAC 到
  扬声器/耳机）或 I2S0/3/5。
  设备内核符号表中有 `tdsp_fm_default_get/set`、`tdsp_fm_runtime_get/set`
  （`/home/w0n9/workspaces/ghostlock-app/.scratch/cannon-root/research/kallsyms-names.txt`），
  对应 ADSP FM 场景控制。
- HAL 侧断言证明设计意图是数字通路：`audio.primary.mt6853.so`（`[ROM]` vendor）
  `WCNChipController.cpp` 的 `AUD_ASSERT(mFmAudioInfo.aud_path != FM_AUD_ANALOG)`。
  （`FM_AUD_ANALOG/I2S/MRGIF` 枚举见 `[KSRC] inc/fm_interface.h:96-101`。）

### 4.2 AudioFlinger 层

`audio_policy_configuration.xml`（`[ROM]` vendor `/vendor/etc/`）：devicePort
`"FM Tuner In"`（`AUDIO_DEVICE_IN_FM_TUNER`）与 `"FM Tuner Out"`（`AUDIO_DEVICE_OUT_FM`）；
mixPort `"FM Tuner input"`（sink），route `sink="FM Tuner input" sources="FM Tuner In,Echo Ref In"`。
`[BR]` dumpsys media.audio_policy 同样列出 FM Tuner In/Out 设备与 route 18。

HAL 组件（`audio.primary.mt6853.so` 字符串/符号）：
`AudioALSAFMController`（`setFmEnable(bool, audio_devices_t, ...)`、`setFmDirectConnection_l`、
`setFmVolume`/`SetFmChipVolume`）、`AudioALSACaptureHandlerFMRadio` +
`AudioALSACaptureDataProviderFMRadio`（FM 采集 PCM）、`AudioALSAPlaybackHandlerFmAdsp`
（ADSP 共享内存播放，`adsp_fm_sharemem_scenario`）。`WCNChipController` 自己
`open("/dev/fm")`，用 `FM_IOCTL_GET_AUDIO_INFO`(48)/`FM_IOCTL_I2S_SETTING`(33)/
`FM_IOCTL_SETVOL`(4) 与芯片协商 I2S 格式与芯片侧音量（日志串 "ioctl:
FM_IOCTL_GET_AUDIO_INFO, ret = %d" 等）。`[BR]` 行 11344-11351：开机时
`AudioALSAFMController()`、`setFmAdspEnable(), mFmAdspEnable = 0 to 1`。
`[BR]` 行 11482：`MTK_FM_ENABLE : 1`。

### 4.3 app 层用法（MIUI 实测形态）

MIUI FM app（dex 字符串，`[ROM]` FM.apk classes2.dex）：创建 **AudioRecord**
（`initAudioRecordSink, mAudioRecord`）+ **AudioTrack**（`startAudioTrack`）做
采集-转发放音，并枚举输入设备（`getDevices`、`[Landroid/media/AudioDeviceInfo;`），
未使用 `setPreferredDevice`。录音功能复用同一 AudioRecord 数据落盘
（`MIUI/sound_recorder/fm_rec` 路径）。
**RFM 的 MTK 后端音频建议照此：Java 层 AudioRecord（选 FM Tuner In 设备）→
AudioTrack；native 无需触碰音频路由。** 具体 AudioRecord 的 source/device 绑定方式
（设备枚举 + 何种 source）需设备实测确认（见第 6 节）。

vendor 属性 `ro.vendor.audio.fm.disable.spk=true`（`[ROM]` build.prop）提示 MIUI 对
FM 扬声器输出有策略限制（耳机即天线）；具体执行位置未在 HAL 字符串中命中。
**实测结论（2026-10-11）**：扬声器放音可行——MIUI FmService 的扬声器模式是创建
**双 sink patch**（`fm_tuner -> speaker + earphone`，dex 字符串原话
"create fm->speaker+earphone patch to avoid noise"）；单 speaker sink 的 patch 虽建成功、
HAL 上电序列完整（speaker_output + ext_speaker_output），但实际无声。双 sink 下 HAL 走
`headphoneSpeaker_output` 组合设备配置，扬声器可听（用户实测确认）。

---

## 5. RDS 与信号强度

### 5.1 RDS 获取

1. `ioctl(fd, FM_IOCTL_RDS_ONOFF, &on)`（nr=18，`0xc008f512`）开关 RDS 接收；
   `FM_IOCTL_RDS_SUPPORT`(19) 查询支持性。
2. 后台线程 `read(fd, buf, 688)` 阻塞收取 `struct rds_t`（内核解析完成的事件包）。
   布局（`[KSRC] inc/fm_rds.h:79-167`；已用 `[JNI]` 的 `read=688`、
   `event_status@0x22a` 逐字段对齐验证）：

| 偏移 | 字段 |
| --- | --- |
| 0 | `rds_ct_t CT`（12B：月/日/年/时/分 + 时区） |
| 12 | `rds_flag_t RDSFlag`（12B：TP/TA/Music/Stereo/… + u32 flag_status） |
| 24 | u16 `PI`；26 u8 `Switch_TP`；27 u8 `PTY` |
| 28 | `rds_af_t AF_Data`（106B：AF_Num + s16 AF[2][25]（100kHz 单位）+ 计数） |
| 134 | `rds_af_t AFON_Data`（106B） |
| 240 | u8 Radio_Page_Code；242 u16 PIN；244 u8 **ECC**；246 u16 Language_Code |
| 248 | `rds_ps_t PS_Data`（33B：PS[4][8] + Addr_Cnt） |
| 281(pad→282) | u8 `PS_ON[8]` |
| 290 | `rds_rt_t RT_Data`（264B：TextData[4][64] + 长度/标志） |
| 554 | u16 `event_status`（位定义见 1.4） |
| 556 | `rds_group_cnt_t gc`（132B：各 group 计数） |

PS 取自 `PS_Data.PS[0]`——内核解析器(`[KSRC] core/fm_rds_parser.c:1331,1336`)始终把当前台名累积进窗口 0,PS[1]/PS[2] 是比对草稿,`Addr_Cnt` 是收齐位图(0xF=4 段全),不是"最新窗口索引"(本节早前描述有误,已据内核源码更正);RT 取
`RT_Data.TextData`（TextLength/isTypeA）；AF 列表频率为 100kHz 单位。
**ECC 直接可得**（偏移 244）——比高通 legacy 后端（HCI 丢 ECC）幸运。
`event_status` 的 `RDS_EVENT_AF`(0x0080) 语义是"当前台信号弱，建议 AF 切换"。
PS/RT 字符编码与 RBDS 差异（如货币符号页）需按 MTK 惯例处理，细节待实测抓包校准。

### 5.2 信号强度

- `FM_IOCTL_GETRSSI`（nr=7，`0xc008f507`，s32 出参）。MT6635 语义
  （`[KSRC] chips/mt6635/pub/mt6635_fm_lib.c:1448-1469`）：读 `FM_RSSI_IND` 寄存器
  10bit 原始值 raw；`raw>511 → dBm=(raw-1024)*6/16`，否则 `dBm=raw*6/16`
  （即带符号的 dBm 近似值，典型范围约 -110..-20 dBm 量级，需实测校准）。
- 辅助指标：`GETCURPAMD`(14)、`GETBLERRATIO`(17, RDS 误码率)、`CQI_GET`(39)。
- 立体声状态：`GETMONOSTERO`(13)；`SETMONOSTERO`(37, by-value int32) 强制单/双声道。

### 5.3 seek 阈值

userspace 软扫描的判定阈值来自 libfmjni CUST 表（`fmr_data.cfg_data.seek_lev`，
属性 `…seek_lev` 可覆盖；开源对应 `custom.cpp:141`）。`fm_cust.cfg` 的
`FM_RX_RSSI_TH_LONG=-296` 与 GETRSSI 的 dBm 标度是否同一口径未证实——
**扫描阈值必须用真机在不同信号下校准**（建议先按 stock app 实测 RSSI 分布定标）。

---

## 6. 后端形态建议与待实测清单

建议的 fmbin 第三后端（与现有 Backend 接口对齐）：

```text
open("/dev/fm", O_RDWR)
enable:  POWERUP{band=1, space=1, hilo=0, deemphasis=region, freq} → RDS_ONOFF(on)
tune:    TUNE{...}
seek/scan: 用户态步进 TUNE + GETRSSI 阈值判定（复用 legacy 扫描状态机模式）
rds:     线程 read() 688B → event_status 分发 PS/RT/PI/PTY/ECC/AF → JSON patch
volume:  优先走 Android 音量（AudioTrack）；芯片侧 SETVOL(0..15) 可选
audio:   Java 层 AudioRecord(FM Tuner In) → AudioTrack（照 MIUI）
disable: RDS_ONOFF(off) → POWERDOWN → close
```

注意 `fm_ops_release` 在最后引用关闭时自动 `fm_powerdown`（`[KSRC]
core/fm_module.c:1261-1276`）——fd 泄漏约等于关机，仍需显式 POWERDOWN 保序。

待设备实测验证（host 分析无法覆盖）：

1. `/dev/fm` 在 root shell 下的实际 open/POWERUP/TUNE/GETRSSI 行为与返回码
   （尤其 POWERUP 的耗时与 err 字段语义）。
2. ~~MIUI app 的 AudioRecord 究竟如何选中 FM Tuner In（source 值/设备绑定方式）；
   `ro.vendor.audio.fm.disable.spk=true` 下扬声器放音是否可行及所需设置~~（已实测，
   见 4.3 节末尾：扬声器可行但需双 sink patch）。
3. 是否存在 hostless 直通（不经过 app 的 Connsys I2S → ADDA_DL 监听通路）及其
   mixer 控件名（`tinymix` 枚举 "Hostless FM" 相关控件），若有则音频可零拷贝。
4. RSSI 读数标度与软扫描阈值校准；立体声/单声道事件是否有中断通知（`fm_eint_wq`
   对应的 EINT 事件是否上抛 userspace——read() 流里除 rds_t 外是否夹杂其它事件，
   需 strace/抓包确认）。
5. RDS read() 的阻塞语义与超时（无 RDS 台时的行为）、PS/RT 字符集细节。
6. `ro.vendor.connsys.adie.chipid` 属性在本机的实际值（应命中 mt6635 分支）。
7. 断电重启 connsys（如开关飞行模式/WiFi）后 `/dev/fm` 句柄是否失效、是否需要
   重新 POWERUP。
