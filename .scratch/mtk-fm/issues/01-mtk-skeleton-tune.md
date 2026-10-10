# 01: MTK 后端骨架与最小调台通路

**What to build:** app 在 cannon(MT6853+MT6631)上自动识别出 MTK 后端并进入主界面(不再显示"设备不支持");能开启/关闭收音机、精确调台到指定频率,信号强度表每 2 秒更新。native 侧新增第三个后端(直 ioctl `/dev/fm`,自包含不依赖 MIUI 库),Java 侧完成驱动枚举/探测器/控制器三处镜像。本票不做音频——验证的是整条控制链。规格见 `.scratch/mtk-fm/spec.md`。

**Blocked by:** None (can start immediately)

**Status:** done(commit 04ee910)

- [ ] app 启动时检测到 MTK 后端并进入主界面(native 检测顺序保持高通两条在前,`/dev/fm` 最后;Java 镜像同序)
- [ ] 开启收音机成功:init 阶段完成芯片 ID/RDS 支持探测,日志带 `mtk/` 前缀;开启失败(含 root/open EACCES)报明确错误而非静默
- [ ] 精确调台:频率按区域钳制,kHz↔10kHz 换算正确(87500kHz→8750),UI 频率显示正确
- [ ] 信号强度:GETRSSI 值经 2 秒轮询上屏;调台瞬间与禁用状态下不轮询
- [ ] 关闭后芯片断电(驱动侧确认未上电),enable/disable 序列失败时按序回滚
- [ ] 无对应物的方法(省电/软静音/slimbus/自动 AF v1)恒真 no-op + 缓存标志,与 legacy 先例一致
- [ ] 主机测试:参数映射全表(band/spacing/**deemphasis 取反**/频率换算)、区域 profile 新列、ABI 布局编译期断言(结构体 8 字节、字段偏移、ioctl 编码值),`make test-native` 绿
- [ ] 既有主机测试全绿(高通零回归);`make build-native` 两个 ABI 均通过
