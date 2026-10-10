# Spec: MediaTek(MT6631/6635)FM 后端 —— RFM-Radio 移植 cannon

> 状态:ready-for-agent(用户已批准)。本仓库未配置 issue tracker,本文件即规格的唯一权威副本。
> 依据:`.agents/mtk-fm-port-research.md`(调研,带引用)+ 真机侦察 + 冒烟测试(open→POWERUP→TUNE→GETRSSI→POWERDOWN 全通)+ 两轮代码库探索 + 模块设计评审。

## Problem Statement

用户的 Redmi Note 9 5G(cannon,MT6853 + MT6631 combo,Android 12 MIUI,已 root)硬件、内核驱动、root 环境全部就绪,但 RFM-Radio 只有两个高通后端,在这台手机上检测不到可用后端,直接落入"不支持设备"。用户要在自己手机上使用这个 app 的全部功能。

## Solution

为 `fmbin` 增加第三个后端 **MTK**:root 运行,直接对 `/dev/fm` 发 ioctl(自包含,不依赖 MIUI 私有库),对 Java 层暴露与现有后端完全一致的 UDP 命令/JSON 事件协议。音频采用 app 内已有的软件桥形态(AudioRecord → PCM → AudioTrack + 录音分路),与 MIUI 自带 FM 服务同构。对用户的全部既有功能(调台/搜台/RDS/录音/收藏)在 MTK 上等价可用,RDS 国家码甚至优于高通 legacy(ECC 可得)。

## User Stories

**收听**
1. 作为 cannon 用户,我希望 app 启动时自动检测到 MTK 后端,以便进入主界面而不是"设备不支持"。
2. 作为用户,我希望开启/关闭收音机,以便控制收听。
3. 作为用户,我希望输入频率精确调台,以便直达已知电台。
4. 作为用户,我希望按当前步进间隔上下步进调台,以便手动浏览频段。
5. 作为用户,我希望向上/向下自动 seek 到下一个有信号的台,以便快速找台;到达频段边缘时回绕继续。
6. 作为用户,我希望全频段自动搜台并列出结果,以便建立收藏;搜台约 15 秒量级、中途可取消。
7. 作为用户,我希望搜台被取消或被禁用时不会收到假结果,以便状态可信。

**信息呈现**
8. 作为用户,我希望每 2 秒看到信号强度更新(搜台/调台过程中除外),以便判断天线位置。
9. 作为用户,我希望看到立体声/单声道指示,并可强制单声道,以便在弱台降低噪声。
10. 作为用户,我希望看到 RDS 台名(PS)与滚动文本(RT),以便识别电台内容。
11. 作为用户,我希望看到 PI/PTY 与国家码,以便识别电台来源(本后端 ECC 直接可得)。
12. 作为用户,我希望看到 AF 备用频率列表,以便了解同网电台。

**设置**
13. 作为用户,我希望切换区域(欧/美/日/日宽)时当前频率被正确钳制、去加重正确应用,以便跨区域使用。
14. 作为用户,我希望切换 50/100/200kHz 步进立即生效,以便匹配本地台网。
15. 作为用户,我希望天线设置在硬件支持时生效、不支持时得到明确错误,以便尝试内置天线。
16. 作为用户,我希望软静音/省电/自动 AF 这些设置在 MTK 上行为有文档声明(接受为 no-op 或生效),以便不误解。

**音频与录音**
17. 作为用户,我希望插上耳机即出声(耳机即天线),以便正常收听。
18. 作为用户,我希望切换到扬声器放音(若系统策略允许),以便免提收听。
19. 作为用户,我希望录音到 WAV/MP3 并可含按录音键前 5 秒,以便保存节目。
20. 作为用户,我希望 app 被杀后收音正确断电,以便不耗电、下次可正常开启。

**健壮性与开发者**
21. 作为用户,我希望飞行模式等导致连接芯片重启后 app 降级为关闭态且可重新开启,以便状态不错乱。
22. 作为用户,我希望 root 授权异常时得到明确报错而不是静默失败,以便知道去检查授权。
23. 作为开发者,我希望新后端的纯逻辑(参数映射/RDS 解析/步进搜台)有主机单测,以便改动静冷代码时有安全网。
24. 作为开发者,我希望高通两条后端行为零回归(检测顺序、协议、事件不变),以便合并安全。
25. 作为开发者,我希望日志可按 `mtk/` 前缀过滤,以便真机排障。

## Implementation Decisions

**检测与选型(native + Java 镜像)**
- 后端枚举尾部追加 MTK 值;检测顺序保持高通两条在前、`/dev/fm` 存在性检测最后(首个命中胜出,高通设备无此节点,零扰动)。Java 侧驱动枚举与探测器以严格相同的顺序镜像(native 选后端、Java 选控制器与音频服务,顺序错位会产生错配)。
- 检测只测存在性,open 验证留给 init;init 做芯片 ID/RDS 支持探测,把权限与驱动问题暴露在启动阶段。

**控制器与音频(Java)**
- 复用现有 native 控制器类,不新建子类:enable 协议、安装/su 拉起、UDP 事件解析均与后端无关;仅有的 per-driver 分支(去抖、slimbus、录音采样率)按驱动类型自然落位,MTK 落进正确分支。
- 新增 MTK 音频服务,继承现有软件桥音频服务,覆写录音器创建(绑定 FM_TUNER 输入设备,失败回退旧构造器)与扬声器路由(软件桥下 AudioTrack 需要框架级路由);设备枚举与录音器创建辅助方法上移复用。选择开关点只有一处:音频服务工厂按驱动类型分流。放音、录音(WAV/MP3)、pre-roll 经既有软件桥获得。
- 不采用高通音频参数路径(硬件 loopback):MTK audio HAL 不识别那些私有参数。

**fmbin 与 native 模块**
- 单一 fmbin 二进制内含三后端、运行时自检;构建与资产校验不变,只登记新源文件。
- 新增后端包:适配器类(19 方法、线程所有权、enable/disable 序列、单例工厂)+ 四个无 fd 依赖的纯逻辑模块(ioctl 常量与 ABI 布局头、参数映射、rds_t 解析、步进搜台状态机)+ fd 薄封装 + 日志头(`mtk/` 前缀)。
- 复用既有件:共享搜台结果收集器(去重/64 上限/排序)、ECC+PI→国家码解码器、区域 profile 表(加一列 MTK band 常量,eu/us→1、jp→3、jp_wide→3;**评审修正**:驱动 band 2 只覆盖 76–90MHz,装不下 RFM jp 区域的 76–95MHz,故 jp 也用 band 3)、单调时钟与 deadline 工具、事件发送三函数(白得 diff 缓存与 512B 限制)。

**ABI 契约(已验证,常量即决策)**
- ioctl magic `0xf5`;`fm_tune_parm` 布局 `{u8 err; u8 band; u8 space; u8 hilo; u8 deemphasis; u16 freq}` 共 8 字节;频率单位 10kHz(87500kHz→8750),换算收敛在后端边界。
- 关键命令码:POWERUP `0xc008f500`、POWERDOWN `0xc008f501`、TUNE `0xc008f502`、MUTE `0xc008f506`、GETRSSI `0xc008f507`、RDS_ONOFF `0xc008f512`、SETMONOSTERO `0xc004f525`、GETMONOSTERO、GETCHIPID、IS_FM_POWERED_UP、ANA_SWITCH(30)。
- band:1=UE/2=JP/3=JPW;space:1=100k、2=200k、5=50k;**deemphasis 与现有区域表的 emphasis 字段语义相反,映射时必须取反**(us=75µs→1,eu/jp=50µs→0)——最隐蔽的映射错误,单测锁死。

**enable 序列与线程模型**
- enable:open → 天线切换(仅 0/1,其余报错)→ POWERUP(映射后的 band/space/deemphasis/clamp 后频率)→ 立体声设置 → RDS 开 + 读线程 → 最后解除静音;任一步失败按序回滚。
- 区域/步进运行时变更:更新缓存映射 → 钳制当前频率 → 重发 TUNE;失败回退重新 POWERUP。
- 线程模型针对 legacy 已知竞态反向设计:fd 只由命令线程开关;RDS 读线程只 read(poll 优先,驱动无 poll 则降级阻塞 read),永不 close;搜台工作线程原子标志取消、≤1 步生效;所有线程退出都有界等待。RDS 解析直接消费 688 字节 rds_t(布局已验证)。
- 搜台为用户态步进(无硬件 seek/scan):步长=当前 spacing,每步 TUNE→驻留→GETRSSI→阈值判决,默认阈值 -74dBm(标度假设待真机校准,单常量集中);**带 deadline 兜底**(高通两后端"扫台无超时"是已登记风险,本次不带入)。
- RMSSI:GETRSSI 直接返回类 dBm 值,不用高通私有的字节解码;沿用现有 2 秒轮询与三段门控;同轮顺带读立体声状态(无中断事件,只能轮询)。
- 无对应物的方法(省电模式、软静音、slimbus、v1 自动 AF)按既有先例实现为恒真 no-op + 缓存标志,语义在文档声明;自动 AF 留用户态跳转钩子(rds_t 含 AF 与弱信号位)。

**错误与降级**
- ioctl 失败透传 errno 的人读错误串;连续 ioctl 失败(如飞行模式致 connsys 重启)判定为 disabled 并通知 Java 落回关闭态。
- root/open 失败(EACCES 悬案)在 init 阶段暴露为启动失败,带明确日志。

## Testing Decisions

好测试标准:只测外部行为,不测实现细节;不碰 fd/线程/ioctl(与现有测试边界一致)。

**主机 gtest(登记进测试构建,实现与测试成对)**:
- 参数映射模块:band/spacing/deemphasis(取反)/频率换算全表断言;ABI 布局编译期断言(结构体大小=8、字段偏移、ioctl 编码值)。
- rds_t 解析模块:合成 688 字节缓冲,逐字段(PS 轮转窗、RT A/B 型与清洗、PI/PTY/ECC、AF 单位换算与截断、事件位分发、国家码),全零/越界输入不崩不产事件。
- 步进搜台模块:注入假 tune/rssi 回调——阈值判决、边界、取消、deadline、seek 命中即停与回绕、空结果语义、与结果收集器集成上限。
- 区域 profile 测试扩展:新增列四行断言。

**真机验证矩阵(cannon)**:init 探测、enable 出声、四区域边界调台、seek 双向与回绕、搜台全程与取消、RDS 各字段、RMSSI 节奏与立体声标志、运行时区域/步进切换、enable/disable×5 无泄漏、飞行模式降级、kill -9 后内核自动断电、音频(耳机/扬声器/录音/pre-roll/采样率回填)。

**先例**:现有 `rds_parser_test`、`scan_result_test`、`legacy_scan_state_test`、`region_profile_test` 的模式照抄。

## Out of Scope

- FM 发射(TX)、FM-over-BT、工程模式(EM)ioctl、CQI 日志。
- 其他 MTK 平台/芯片族(只认 mt6631_6635 这一代;泛化留后续)。
- 非 MIUI ROM 适配、SELinux 策略修改、EACCES 悬案的根因修复(记录并给出症状级处理)。
- 高通两后端的任何行为变更;协议字段向后兼容。
- 用户态自动 AF 跳转(v2 候选,本次留钩子不实现)。

## Further Notes

- 实现期验证清单(真机):RDS read 阻塞语义与短读行为、立体声标志来源(rds_t 位 vs GETMONOSTERO)、SETMONOSTERO 取值方向、ANA_SWITCH 有效性(受短天线属性门控)、搜台阈值校准(对拍 MIUI 扫描结果)、AudioRecord 三种打开方式与采样率、扬声器策略属性影响、connsys 重启后的 fd 行为。
- EACCES 悬案:冒烟期观察到一次性"uid 0 + DAC_OVERRIDE + SELinux permissive 仍 EACCES",后自行恢复且 8/8 复测通过;若实现期再现,备 strace。
- 调研中间产物(解包分区、内核源码、参考实现)在 `/home/w0n9/scratch/mtk-cannon/`;侦察探针源码在 `/tmp/rfm-recon/`(临时)。
