# targetSdk 24 → 37 升级研究（RFM-Radio）

> 调查日期：2026-10-12。方法：以 developer.android.com 官方行为变更文档为第一来源，逐档核对；SDK 平台可用性直接核对 Google SDK 仓库 `dl.google.com/android/repository/repository2-3.xml`（primary source）；AGP 要求核对官方 AGP 支持表。所有关键论断附引用（见第 6 节）。查不到官方来源的论断明确标注「未找到官方来源」。

---

## 1. TL;DR

1. **API 37 = Android 17 = 代号 Cinnamon Bun（`CINNAMON_BUN`）**，**已正式发布**（官方 uses-sdk 表已列出，SDK platform 37.0/37.1/37.2 已发布在 Google 仓库 **stable channel**，codename 为空 = 正式版）。targetSdk 37 现在是正常的生产目标，不存在 preview SDK 顾虑。
2. **compileSdk/targetSdk 37 的最低 AGP 是 9.1.1**（官方 API level 支持表）——本仓库用的正是 AGP 9.1.1，**恰好达标，无需升级**；Gradle 9.3.1 也正好是 AGP 9.1 的最低要求。AGP 9.x 全系可编 API 37（AGP 9.4 release notes 明确"最高支持 API 37"），不需要 AGP 10。
3. 升级路上的**硬阻断点**：28（缺 `FOREGROUND_SERVICE` 权限 → `SecurityException`）、30（scoped storage 强制，`requestLegacyExternalStorage` 被忽略，外部存储直写失效——对本 app 影响最大）、31（带 intent-filter 的组件必须显式 `android:exported`，否则装不上；PendingIntent 必须指定 mutability——本 app 已全部用 `FLAG_IMMUTABLE` ✓）、34（FGS 类型 + 类型权限强制，缺 `FOREGROUND_SERVICE_MEDIA_PLAYBACK` → `SecurityException`）、35（edge-to-edge 强制）、36（e2e opt-out 失效）、37（大屏竖屏锁彻底失效 + 后台音频加固）。
4. **「mediaPlayback FGS 要求 MediaSession 处于 active/播放状态、先警告后异常」未找到官方来源**。官方 FGS 类型文档写明 mediaPlayback 的 runtime prerequisites 是 **"None"**；Android 17 真实的新约束是 **Background audio hardening**：Android 17 上所有 app 的后台音频操作必须有可见 Activity 或非 `SHORT_SERVICE` FGS，**target 37 的 app 额外要求该 FGS 具备 while-in-use（WIU）能力**；强制方式是**静默失败**（播放被静音、音量 API 被忽略、audio focus 返回 `AUDIOFOCUS_REQUEST_FAILED`），不存在「先警告后异常」的时间线。
5. 隐式广播限制（26+）：**PHONE_STATE 和 NEW_OUTGOING_CALL 都在官方豁免清单**，`CallReceiver` 的 manifest 注册方式在 target 37 下依然有效。

---

## 2. 逐档行为变化（targetSdk 25 → 37）

每档只列「targetSdk 达到该档才强制」的行为（设备级变化与 targetSdk 无关，不重复展开）。标注含义：【影响本 app：是/否/间接】。

### targetSdk 25（Android 7.1）
- 无 targetSdk 25 专属的强制行为变更专页；Android 8.0 才引入分档强制（后台限制、通知渠道等）。【影响本 app：否】

### targetSdk 26（Android 8.0 Oreo）—— 后台限制元年
- **通知渠道强制**：`targetSdk ≥ 26` 时，发通知前必须创建 `NotificationChannel`，否则「通知不会显示且系统记录错误」。【影响本 app：已适配 ✓】（`FMService.createNotificationChannels()` 已创建 3 个渠道）
- **隐式广播限制**：manifest 注册的隐式广播 receiver 不再收到广播，除非在豁免清单。`ACTION_PHONE_STATE_CHANGED` 与 `ACTION_NEW_OUTGOING_CALL` **均在豁免清单**。【影响本 app：否 ✓】
- **后台服务限制**：后台 app 不能 `startService()`（抛 `IllegalStateException`），须用 `startForegroundService()` 且 5 秒内调 `startForeground()`。【影响本 app：间接 ✓】（本 app 以前台 Activity 启动 FGS）
- 权限组不再「授一连三」（target ≤ 25 时授 READ 会连带 WRITE，26 起分开）。【影响本 app：间接】（READ/WRITE_EXTERNAL_STORAGE 需分开请求）
- 悬浮窗类型 `TYPE_PHONE` 等 5 种废弃，改 `TYPE_APPLICATION_OVERLAY`。【影响本 app：否】

### targetSdk 27（Android 8.1）
- 无 targetSdk 27 专属强制行为变更；官方 8.1 页面只有全体 app 的平台级变化（通知提示音限频、加密库行为、`EditText.getText()` 返回类型等）。【影响本 app：否】

### targetSdk 28（Android 9）
- **前台服务必须声明 `FOREGROUND_SERVICE` 权限**，否则 `SecurityException`。本 app **未声明**。【影响本 app：是 —— 必须补】
- 明文 HTTP 流量默认禁用（`isCleartextTrafficPermitted()` 默认 false）。【影响本 app：间接 —— 需确认网络用法】
- Apache HTTP client 移出 bootclasspath。【影响本 app：否】（源码未见 org.apache.http）
- `Build.SERIAL` 固定为 "UNKNOWN"；多进程 WebView 数据目录隔离。【影响本 app：否】

### targetSdk 29（Android 10）
- **Scoped storage 成为 targetSdk 29+ 的默认**（只看到自己的 app-specific 目录和自己贡献的媒体），可用 `requestLegacyExternalStorage="true"` 临时退出。target ≤ 28 的 app 在 Android 10 设备上仍是 legacy 视图。【影响本 app：间接 —— 本档仍可 opt-out】
- **W^X**：target 29+ 的 app **不能对 app 主目录（home directory）内的文件直接 `execve()`**。本 app 把 `fmbin-*` 复制到 `/data/data/<pkg>/files/` 再执行。【影响本 app：是/需实测 —— 不过实际执行路径是 `ProcessBuilder("su")` → root shell 执行，app 进程本身没有 execve 该文件；SELinux 域在 su 之后切换，预计可绕过，但必须在 target 29+ 构建上实测】
- 全屏意图通知需 `USE_FULL_SCREEN_INTENT` 权限（normal 权限自动授予）。【影响本 app：否】
- ashmem 直接访问禁止、非 SDK 接口限制更新。【影响本 app：否】

### targetSdk 30（Android 11）—— 存储分水岭
- **Scoped storage 强制**：target 30 后系统**忽略 `requestLegacyExternalStorage`**；`WRITE_EXTERNAL_STORAGE`「不再提供任何额外访问」；不能再自建外部 app 专属目录（用 `getExternalFilesDirs()`）；不能访问其他 app 的数据目录；Android 11 仍允许用 `File`/`fopen()` 直接路径**读取**媒体文件，但任意路径直写公共目录失去权限支撑（出路：MediaStore 集合写入如 `MediaStore.Downloads`/`Music`——Android 10+ 无需存储权限，或 All files access 特殊权限）。【影响本 app：**是（最大单点）** —— `FavoriteController` 写 `/sdcard/RFM/favorites`、`RecordingStorage` 写 `DIRECTORY_MUSIC`，全部失效】
- APK Signature Scheme v2 必需；`resources.arsc` 不得压缩且 4 字节对齐（否则装不上）。【影响本 app：否 ✓】（现代 AGP 默认满足）
- 权限自动重置（数月不用即重置敏感权限）、包可见性过滤、后台自定义 Toast 禁止、native heap 指针 tagging。【影响本 app：间接/否】

### targetSdk 31（Android 12）
- **带 intent-filter 的组件必须显式声明 `android:exported`**，否则「app 无法安装在 Android 12+ 设备上」。本 app 的 `MainActivity`（MAIN/LAUNCHER）和 `CallReceiver`（PHONE_STATE/NEW_OUTGOING_CALL）都**未声明**。【影响本 app：是 —— 必须补（MainActivity → true；CallReceiver → false 即可，系统受保护广播仍可送达）】
- **PendingIntent 必须指定 `FLAG_IMMUTABLE` 或 `FLAG_MUTABLE`**（官方页写明必须指定并给 lint 警告 `UnspecifiedImmutableFlag`；不指定的运行时抛异常为通行认知，本次抓取页面未写原文）。【影响本 app：已适配 ✓】（`FMService` 全部 PendingIntent 已用 `FLAG_IMMUTABLE`）
- 后台启动 FGS 限制（后台不能 startForegroundService，除白名单场景）。【影响本 app：否 ✓】（前台 Activity 启动）
- 通知 trampoline 限制（点通知不得经由 service/receiver startActivity）。【影响本 app：否 ✓】
- 精确闹钟需 `SCHEDULE_EXACT_ALARM`；蓝牙换新权限组；备份配置换 `<data-extraction-rules>`。【影响本 app：否/间接】

### targetSdk 32（Android 12L）
- 12L 为大屏小版本，无 targetSdk 32 专属强制行为变更页。【影响本 app：否】

### targetSdk 33（Android 13）
- **`POST_NOTIFICATIONS` 运行时权限**：Android 13+ 设备上新装的 app **通知默认关闭**，必须声明并请求该权限；用户拒绝后「所有通知渠道被阻止（除少数豁免角色）」。**FGS 通知也受影响**：「不需要 POST_NOTIFICATIONS 就能启动前台服务，但拒绝后通知栏里看不到 FGS 通知（Task Manager 里仍可见）」。声明了渠道的老 app 在设备 OTA 到 13 时会被自动预授权。【影响本 app：是 —— 必须新增运行时请求；不申请的后果 = 常驻通知与媒体控制不可见（服务仍运行），对 FM 收音机这种「无通知就不知道还在播」的 app 是显著 UX 退化】
- 细分媒体权限 `READ_MEDIA_IMAGES/VIDEO/AUDIO`（替代 READ_EXTERNAL_STORAGE 读取他人媒体）。【影响本 app：间接】（自己创建的录音走 MediaStore 归属无需权限）
- 媒体控制由 `PlaybackState` 派生（target 33+）。【影响本 app：间接】（无 MediaSession → 系统媒体面板无条目，只有普通通知）
- 蓝牙开关 API 废弃恒 false；AD_ID 权限；restricted 状态下不投递 BOOT_COMPLETED。【影响本 app：否】

### targetSdk 34（Android 14）
- **FGS 类型强制**：每个前台服务必须声明 `foregroundServiceType` + 对应类型权限；未声明类型 → `MissingForegroundServiceTypeException`；有类型但缺类型权限 → `SecurityException`。本 app 已声明 `mediaPlayback` 类型 ✓ 但**缺 `FOREGROUND_SERVICE_MEDIA_PLAYBACK` 权限**（以及 28 档的 `FOREGROUND_SERVICE`）。【影响本 app：是 —— 必须补】
- 运行时注册的 receiver 必须指定 `RECEIVER_EXPORTED/NOT_EXPORTED`（仅 manifest 注册不受影响）。【影响本 app：否 ✓】
- 隐式 intent 只投递给 exported 组件；mutable PendingIntent 不得带隐式 intent。【影响本 app：间接 —— 需检查内部 intent 用法】
- 照片/视频部分授权；`SCHEDULE_EXACT_ALARM` 默认拒绝；`USE_FULL_SCREEN_INTENT` 收紧为通话/闹钟类。【影响本 app：否】
- JobScheduler 超时改 ANR；DCL 文件必须只读。【影响本 app：否/间接】

### targetSdk 35（Android 15）
- **Edge-to-edge 强制**：「target SDK 35 且运行在 Android 15+ 设备上，app 默认 edge-to-edge」，官方明确这是 breaking change（状态栏/导航栏 API 大量废弃失效）。本档仍可用 `windowOptOutEdgeToEdgeEnforcement` 临时退出（该属性 API 35 引入）。【影响本 app：是 —— UI 需处理 insets，或先用 opt-out 拖延】
- **`BOOT_COMPLETED` receiver 禁止启动 mediaPlayback 等 5 类 FGS**（dataSync/camera/mediaPlayback/phoneCall/mediaProjection/microphone），违反抛 `ForegroundServiceStartNotAllowedException`。【影响本 app：否 ✓】（无 BOOT receiver）
- **audio focus 请求要求**：target 35 起「必须是顶部 app 或在跑前台服务」才能请求 audio focus，否则返回 `AUDIOFOCUS_REQUEST_FAILED`。【影响本 app：否 ✓】（audio focus 在 FGS 内请求）
- `dataSync`/新增 `mediaProcessing` 类型 6h/24h 配额；TLS 1.0/1.1 禁用；DND 全局状态限制。【影响本 app：否】

### targetSdk 36（Android 16）
- **Edge-to-edge opt-out 失效**：「target SDK 36 时 `windowOptOutEdgeToEdgeEnforcement` deprecated and disabled，app 无法再退出 e2e」（注意：仍 target 36 但跑在 Android 15 设备上时属性仍生效）。【影响本 app：是 —— e2e 适配无法再拖】
- **Predictive back 默认启用**：target 36 且设备 Android 16+ 时，系统返回动画默认开启，`onBackPressed()` 不再被调用、`KEYCODE_BACK` 不再分发；可临时用 `android:enableOnBackInvokedCallback="false"` 退出。【影响本 app：否 ✓】（源码无 onBackPressed/KEYCODE_BACK 处理）
- **大屏忽略方向/可调整性/宽高比限制**（sw ≥ 600dp）：`screenOrientation="portrait"` 等被忽略；可用 `PROPERTY_COMPAT_ALLOW_RESTRICTED_RESIZABILITY` 临时退出，「target API 37 起退出不再可用」。【影响本 app：间接】（手机为主，平板/折叠屏受影响）
- BODY_SENSORS 细化、ordered broadcast 跨进程优先级不再保证、16 KB page size 兼容模式、JobScheduler 配额强化（全体 app）。【影响本 app：否/间接】（NDK 29 构建 → 16KB 对齐默认满足）

### targetSdk 37（Android 17）
- **大屏退出机制移除**：「target API 37 起 `PROPERTY_COMPAT_ALLOW_RESTRICTED_RESIZABILITY` opt-out 不再可用」，竖屏锁在 sw≥600dp 屏上彻底失效（豁免仅限 `appCategory=game`）。【影响本 app：间接（手机不受影响）】
- **Background audio hardening（后台音频加固）**：Android 17 上**所有 app**（无论 targetSdk）在后台做音频播放/audio focus/音量操作，必须有可见 Activity 或在跑非 `SHORT_SERVICE` FGS；**target 37 的 app 进一步要求该 FGS 具备 while-in-use（WIU）能力**（FGS 是响应用户操作或在 app 可见时启动才获得 WIU；通知点击、微件交互、外部设备媒体按键属于可授予 WIU 的「明确用户意图」）。**后果是静默失败**：播放被静音、音量/铃声 API 被忽略、audio focus 返回 `AUDIOFOCUS_REQUEST_FAILED`；`IllegalStateException` 只存在于 adb `throw` 测试模式。【影响本 app：是/间接 —— 现有「用户在界面里点播放 → 前台启动 mediaPlayback FGS」模式可获得 WIU，但必须保证 FGS 由用户交互路径启动】
- **`ACCESS_LOCAL_NETWORK` 运行时权限强制**（target 37+；Android 16 只是 opt-in）。判定范围是「广播型网络接口（Wi-Fi/以太网）上的局域网地址」（169.254/16、100.64/10、10/8、172.16/12、192.168/16、组播、广播）——**loopback `127.0.0.0/8` 不在官方定义清单内**；Android 17 单独限制的只是**跨 profile** loopback（全体 app）。本 app 的 127.0.0.1 UDP 是同 app 同 profile 进程间通信，按官方定义预计不受影响，但官方页面未明说 loopback 豁免，建议 target 37 实测。【影响本 app：可能否，需实测】
- RemoteViews 内存上限（超限抛致命 `IllegalArgumentException`）、无锁 MessageQueue（反射私有成员会坏）、`static final` 字段不可改（反射 `IllegalAccessException`、JNI 改动崩溃）、ECH 默认启用、证书透明默认启用、原生 DCL 只读（`System.load()` 的文件必须只读）、短信 OTP 3 小时延迟（target 37 扩展到普通短信）、keystore 数量上限、【配置变更不再重建 Activity 需 `android:recreateOnConfigChanges` 显式加入】、NPU 需声明 hardware feature、蓝牙 RFCOMM `read()` 关闭时返回 -1。【影响本 app：否/间接】（app 不用 SMS/RemoteViews/动态加载原生库；无反射改 static final）
- 所有 app（与 target 无关）：跨 profile loopback 禁止、`usesCleartextTraffic` 弃用计划（本 app minSdk 23 < 24，未来若改用 network security config 需同时保留该属性）、隐式 URI 授权收紧（Android 18 起生效）。【影响本 app：间接/否】

---

## 3. API 37 现状（截至 2026-10-12）

| 项目 | 事实 | 来源 |
|---|---|---|
| 名称/代号 | Android 17，代号 **Cinnamon Bun**（版本常量 `CINNAMON_BUN`） | uses-sdk-element 官方平台表 |
| API level | **37**（QPR 递增为 37.1 / 37.2） | 同上 + SDK 仓库 |
| 发布状态 | **已正式发布**。官方 release notes 记载：**Beta 1 2026-02-13、Platform Stability 于 Beta 3（2026-03-26）达成**（「API surface is now locked」）、Beta 4.1 2026-06-01；正式版发布于 2026 年 6 月中（6 月 16 日，来自 Wikipedia/媒体，**二手来源**；官方文档页截至 2026-10-09 仍保留 Beta/QPR Beta 程序入口） | Android 17 release notes（官方）；zh.wikipedia.org（二手） |
| SDK 平台 | `platforms;android-37.0`（r02）、`android-37.1`、`android-37.2` 均已发布在 **stable channel（channel-0），codename 为空（= 正式版）**；preview 包（如 `android-37.2-beta3`）codename 为 `DEV` | dl.google.com 仓库 XML（已解析核对） |
| preview 顾虑 | **无**。targetSdk 37 不再是 preview target；`Build.VERSION.PREVIEW_SDK_INT` 相关注意点只适用于 beta 期。QPR 扩展（37.1/37.2）是可选的 extension-level SDK，targetSdk 用整数 37 即可 | 仓库数据 + uses-sdk 文档 |
| Play 合规 | Google Play 对 targetSdk 有年度递增要求，但 RFM-Radio 走 GitHub 侧载分发，**Play 政策不适用**；系统级行为变化（上文各档）仍会在设备上生效 | 本文第 2 节 |

**targetSdk 37 新增行为变化**（全部列于上表 targetSdk 37 节）：对 FGS 的新限制 = **Background audio hardening**（target 37 需 WIU FGS）；**通知无新限制**（官方 target-37 行为变更页无通知条目）；**存储无新限制**（同上）。其余为 RemoteViews 限内存、MessageQueue 无锁化、static final 不可变、ECH/CT 默认开启、`ACCESS_LOCAL_NETWORK` 强制、短信 OTP 延迟扩展、大屏退出机制移除等。

---

## 4. 工具链要求（AGP vs 本仓库）

官方「API level → 最低 AGP」支持表（/build/releases/about-agp，抓取于 2026-10-12）：

| API level | 最低 Android Studio | 最低 AGP |
|---|---|---|
| 37.0 | Panda 3 \| 2025.3.3 Patch 1 | **9.1.1** |
| 36.1 | Narwhal 3 Feature Drop \| 2025.1.3 | 8.13.0 |
| 36 | Meerkat \| 2024.3.1 Patch 1 | 8.9.1 |
| 35 | Koala Feature Drop \| 2024.2.1 | 8.6.0 |
| 34 | Hedgehog \| 2023.1.1 | 8.1.1 |
| 33 | Flamingo \| 2022.2.1 | 7.2 |

（该表自 API 33 起、至 37.0 止，无单独 preview 行。）

对照本仓库现状（`/home/w0n9/workspaces/RFM-Radio/build.gradle`、`gradle/wrapper/gradle-wrapper.properties`）：

- **AGP 9.1.1（classpath `com.android.tools.build:gradle:9.1.1`）= API 37.0 的最低要求，恰好达标 → 编 compileSdk 37 不需要升级 AGP**。
- **Gradle wrapper 9.3.1 = AGP 9.1 的最低 Gradle 版本，同样恰好达标**。
- AGP 9.x 系列可编 API 37：AGP 9.4.0（2026-09）release notes 明确「The maximum API level that Android Gradle plugin 9.4 supports is API level 37」；AGP 9.0 按支持表推断不足以编 37（表中无 9.0 最低行、最高只覆盖到 36.1 的 8.13.0）。
- AGP 9.4 起最低 Gradle 抬到 9.6.0（如果将来升级 AGP，wrapper 要跟着动）。
- SDK 平台侧：本机 SDK（`/home/w0n9/Android/Sdk/platforms`）只有 `android-36`，需 `sdkmanager "platforms;android-37.0"` 安装（仓库已确认可下载）。

---

## 5. 对 RFM-Radio 的具体影响评估

App 现状（已核对源码/manifest）：minSdk 23、targetSdk 24、compileSdk 36、AGP 9.1.1、Gradle 9.3.1；root 依赖（`ProcessBuilder("su")`）、GitHub 侧载分发、manifest 已有 `foregroundServiceType="mediaPlayback"`、**无 MediaSession**、遗留外部存储直写、UDP loopback（127.0.0.1）与 `:service` 进程通信。

### 必须修（按触发档位排序）

| 档位 | 问题 | 修法 |
|---|---|---|
| 28 | 缺 `FOREGROUND_SERVICE` 权限 → startForeground 抛 `SecurityException` | manifest 补 `FOREGROUND_SERVICE` |
| 30 | `FavoriteController` 写 `Environment.getExternalStorageDirectory()/RFM/favorites`、`RecordingStorage` 写 `getExternalStoragePublicDirectory(DIRECTORY_MUSIC)` 全部失效（scoped storage 强制、WRITE 失效） | 迁移到 MediaStore（`MediaStore.Audio`/`Downloads`，无需权限）或 app-specific 目录；侧载 app 也可考虑 `MANAGE_EXTERNAL_STORAGE`（无 Play 政策约束） |
| 31 | `MainActivity`、`CallReceiver` 带 intent-filter 但未显式 `android:exported` → 装不上 Android 12+ 设备 | MainActivity 加 `exported="true"`；CallReceiver 加 `exported="false"`（系统广播可送达；此细节官方页未明说，需实测） |
| 34 | 缺 `FOREGROUND_SERVICE_MEDIA_PLAYBACK` 权限 → `SecurityException` | manifest 补该权限；建议同时把 `startForeground(id, notification)` 换成 `ServiceCompat.startForeground(..., FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)` 显式传类型 |
| 33 | 无 `POST_NOTIFICATIONS` 运行时请求 → Android 13+ 新装用户通知默认关，常驻通知/媒体控制不可见 | 加权限声明 + 运行时请求（老用户渠道已在，OTA 到 13 会自动预授权） |
| 35/36 | edge-to-edge 强制且 36 起 opt-out 失效 | `enableEdgeToEdge()` + insets 适配（AppCompat 主题，改动集中在 Activity 布局） |

### 需要实测/注意

- **fmbin 执行**（target 29 的 W^X）：二进制复制到 `/data/data/<pkg>/files/` 后由 root shell 执行；app 进程本身不 execve 该文件，预计不受「home 目录不可 exec」限制，但需在 target 29+ 构建上验证。
- **`CallReceiver` 导出细节**（见上表 31 行）。
- **127.0.0.1 UDP**（target 37 `ACCESS_LOCAL_NETWORK`）：loopback 不在官方 local network 定义清单内，预计不受影响；建议 API 37 设备实测。
- **Android 17 后台音频加固**（target 37）：FM 播放由 `mediaPlayback` FGS 承载且由用户交互启动 → 满足 WIU；但要避免未来加「后台自动恢复播放」之类不经用户交互启动播放的路径（CallReceiver 挂断后恢复播放的 TODO 就属此类，恢复路径需走通知点击/媒体按键等「用户意图」入口）。
- **无 MediaSession 的代价**：26 的媒体按键路由规则把前台/最近播放优先级排在前，33 的媒体面板以 `PlaybackState` 派生——现状下系统媒体面板不会出现本 app 条目，只有普通通知。这不是 target 37 的新强制项，但 media3 `MediaSessionService` 是官方在 bg-audio 文档里点名推荐的载体（库自身在暂停/停止/出错 10 分钟后自动退出前台——这是库行为，非平台强制）。

### 「MediaSession active 要求」核查结论（任务重点问题）

- 官方 FGS 类型文档对 `mediaPlayback` 的 **Runtime prerequisites 写明 "None"**，全文未要求 MediaSession 处于 active/playback 状态。
- Android 17 `bg-audio` 专页也**没有**规定 MediaSession 必须处于某状态、没有「先警告后异常」时间线；其强制是**静默失败**（静音/忽略/`AUDIOFOCUS_REQUEST_FAILED`），`IllegalStateException` 仅存在于 adb `throw` 测试模式。
- 结论：**「mediaPlayback FGS 要求 MediaSession 处于播放状态、先警告后异常」未找到官方来源**。真实存在且与本 app 相关的相邻强制项是：① target 35 起 audio focus 需顶部 app 或 FGS；② target 35 起 `BOOT_COMPLETED` 不得启动 mediaPlayback FGS；③ target 37 的 WIU 要求（见上）。

### 建议的升级切分（供架构决策）

低风险档：25/26/27（几乎零改动）→ 28/29（补权限 + W^X 验证）→ 30（存储重写，工作量最大）→ 31/33/34（manifest + 通知权限，机械改动）→ 35/36（UI 适配，可并行）→ 37（验证性收尾）。每档都可在真机回归「开关 FM、通话暂停、录音落盘、收藏读写」四条核心链路。

---

## 6. 引用列表

官方文档（developer.android.com，均为 2026-10-12 抓取）：

1. 行为变更 target 17：https://developer.android.com/about/versions/17/behavior-changes-17
2. 行为变更 all-apps 17：https://developer.android.com/about/versions/17/behavior-changes-all
3. Background audio hardening 专页：https://developer.android.com/about/versions/17/changes/bg-audio
4. Android 17 索引/release notes：https://developer.android.com/about/versions/17 、https://developer.android.com/about/versions/17/release-notes
5. 行为变更 target 16 / all-apps 16：https://developer.android.com/about/versions/16/behavior-changes-16 、https://developer.android.com/about/versions/16/behavior-changes-all
6. 行为变更 target 15：https://developer.android.com/about/versions/15/behavior-changes-15
7. FGS 类型强制（14）：https://developer.android.com/about/versions/14/changes/fgs-types-required 、https://developer.android.com/about/versions/14/behavior-changes-14
8. FGS 类型总览（mediaPlayback runtime prerequisites "None"）：https://developer.android.com/develop/background-work/services/fg-service-types
9. 行为变更 target 13：https://developer.android.com/about/versions/13/behavior-changes-13 ；通知权限：https://developer.android.com/develop/ui/views/notifications/notification-permission
10. 行为变更 target 12：https://developer.android.com/about/versions/12/behavior-changes-12
11. 行为变更 target 11 / 存储：https://developer.android.com/about/versions/11/behavior-changes-11 、https://developer.android.com/about/versions/11/privacy/storage
12. 行为变更 target 10 / 隐私（scoped storage 默认）：https://developer.android.com/about/versions/10/behavior-changes-10 、https://developer.android.com/about/versions/10/privacy/changes
13. 行为变更 target 9：https://developer.android.com/about/versions/pie/android-9.0-changes-28
14. Oreo 行为变更 / 后台限制 / 8.1：https://developer.android.com/about/versions/oreo/android-8.0-changes 、https://developer.android.com/about/versions/oreo/background 、https://developer.android.com/about/versions/oreo/android-8.1
15. 通知渠道：https://developer.android.com/develop/ui/views/notifications/channels
16. 隐式广播豁免清单（PHONE_STATE、NEW_OUTGOING_CALL）：https://developer.android.com/develop/background-work/background-tasks/broadcasts/broadcast-exceptions
17. Edge-to-edge：https://developer.android.com/develop/ui/views/layout/edge-to-edge
18. uses-sdk（Android 17 = 37 = CINNAMON_BUN 表）：https://developer.android.com/guide/topics/manifest/uses-sdk-element
19. AGP 支持表 / release notes：https://developer.android.com/build/releases/about-agp 、https://developer.android.com/build/releases/gradle-plugin
20. Local network permission / definition：https://developer.android.com/privacy-and-security/local-network-permission 、https://developer.android.com/privacy-and-security/local-network-definition
21. Media3 后台播放（10 分钟暂停退前台为库行为）：https://developer.android.com/media/media3/session/background-playback
22. SDK 仓库（platforms;android-37.0/37.1/37.2，channel-0=stable，codename 空）：https://dl.google.com/android/repository/repository2-3.xml （2026-10-12 curl 解析核对）

二手来源（仅用于正式版发布日期，已标注不确定性）：

23. Wikipedia Android 17（stable 2026-06-16）及 2026 年 2 月媒体报导（代号「肉桂卷」、6 月发布计划）：zh.wikipedia.org/zh/Android_17 、m.techweb.com.cn/article/2026-02-13/2971927.shtml
