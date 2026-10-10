# 02: MTK 软件音频桥(出声与录音)

**What to build:** 开启收音机后耳机立刻出声,可切扬声器,可录音到 WAV/MP3(含按录音键前 5 秒 pre-roll)。新增 MTK 音频服务变体,继承 app 现有软件桥(采集→PCM→放音+录音分路,与 MIUI 自带 FM 服务同构),AudioRecord 绑定 FM_TUNER 输入设备,失败时回退旧构造器路径。规格见 `.scratch/mtk-fm/spec.md`。

**Blocked by:** 01(MTK 后端骨架与最小调台通路)

**Status:** ready-for-agent

- [ ] 开启后耳机出声(软件桥通路,延迟可接受)
- [ ] AudioRecord 三种打开方式真机实测(Builder+绑定 FM_TUNER 设备 / 隐藏源 1998 / DEFAULT 不绑定),结论与采样率实测值回填代码
- [ ] 绑定失败自动回退并记日志
- [ ] 扬声器切换:可用则可用;若系统策略禁止(设备属性所限),行为与文档声明一致
- [ ] WAV 与 MP3 录音产出可播放文件,pre-roll 内容正确
- [ ] 音频服务选择仅按驱动类型分流,高通两条路径(硬件 loopback / 旧软件桥)行为零改动
