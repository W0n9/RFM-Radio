# 03: RDS 与立体声指示

**What to build:** 收听 RDS 电台时,台名(PS)、滚动文本(RT)、PI、PTY、国家码(ECC 直接可得,优于高通 legacy)、AF 备用频率列表全部上屏;立体声/单声道指示与实际收听一致。native 侧新增 RDS 读线程(poll 优先、驱动不支持则降级阻塞读;只 read 永不 close,fd 由命令线程独占开关)与 rds_t(688 字节,布局已验证)解析纯逻辑模块。规格见 `.scratch/mtk-fm/spec.md`。

**Blocked by:** 01(MTK 后端骨架与最小调台通路)

**Status:** implemented — 主机测试 102/102 绿;RDS 上屏与立体声来源裁定待真机验证

- [ ] RDS 电台:PS/RT/PI/PTY/country/AF 逐字段上屏;换台时旧台元数据清除
- [ ] 无 RDS 台零误报(全零/异常包不产出事件)
- [ ] 立体声指示来源实测裁定(rds_t 标志位优先,GETMONOSTERO 轮询兜底),结论记录在案;指示与实听一致
- [ ] RDS read 阻塞语义与短读行为真机实测,结论记录;短读拼接或丢弃策略正确
- [ ] 解析模块主机测试:合成缓冲逐字段(PS 轮转窗、RT A/B 型与换行清洗、AF 单位换算与截断、事件位分发、ECC 国家码),`make test-native` 绿
- [ ] disable 时读线程有界退出,`/proc/<pid>/fd` 无泄漏
