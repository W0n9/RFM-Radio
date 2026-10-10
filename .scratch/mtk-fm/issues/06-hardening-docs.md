# 06: 健壮性加固与文档

**What to build:** 把真机上已观察到的边界情况做成可信行为:root/open 失败(含 EACCES 悬案)给明确报错而非静默;飞行模式等导致连接芯片重启后,app 降级为关闭态且可重新开启;进程生命周期干净。更新 `.agents/native-fm-backend.md` 增加 MTK 后端章节,使本项目文档对第三个后端保持权威。规格见 `.scratch/mtk-fm/spec.md`。

**Blocked by:** 02(MTK 软件音频桥)、03(RDS 与立体声指示)、04(步进搜台与 seek)、05(设置项运行时语义)

**Status:** ready-for-agent

- [ ] root 探测/open 失败路径:明确报错文案与日志(含 errno),不复现静默失败;EACCES 悬案的处理与遗留说明记录
- [ ] 连续 ioctl 失败(connsys 重启特征)判定为 disabled 并通知 Java 落回关闭态;飞行模式切换实测可恢复
- [ ] enable/disable 循环 ×5:线程干净退出、无 fd 泄漏、无残留上电
- [ ] kill -9 fmbin 后内核自动断电,重启 app 可正常再用
- [ ] 真机验证矩阵(spec Testing Decisions 节 12 项)全量跑过并记录结果
- [ ] `.agents/native-fm-backend.md` 更新:MTK 后端架构、检测顺序、线程模型、已知风险与边界;`native/README` 类过时表述不动的前提下以 .agents 文档为准
- [ ] 全部主机测试绿;两个 ABI 构建绿;高通路径零回归终验
