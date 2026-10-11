#!/bin/bash
# MTK FM 音频路由验证工具（一次性调试脚本）
#
# 背景：app 调 AudioSystem.setForceUse 会被 Android 12 的
# MODIFY_AUDIO_ROUTING(signature|privileged|role) 拒绝，必须借 root。
# 本脚本用 /data/local/tmp/fmroute 以 root 切换 FOR_MEDIA 的 force use，
# 并打印可自动核对的状态；实际听感需要人来确认。
#
# 用法:
#   ./verify-route.sh status    打印当前状态
#   ./verify-route.sh speaker   强制扬声器 (FOR_MEDIA = FORCE_SPEAKER = 1)
#   ./verify-route.sh restore   恢复 ROM 默认 (FOR_MEDIA = 10 = FORCE_NO_BT_A2DP)

set -u

ADB=${ADB:-adb}
FMROUTE=/data/local/tmp/fmroute
ROM_DEFAULT=10

force_use() {
  $ADB shell dumpsys media.audio_policy 2>/dev/null \
    | grep -m1 "Force use for media" | awk '{print $NF}'
}

fmbin_count() {
  $ADB shell ps -A 2>/dev/null | grep -c "fmbin-aarch64"
}

comm_device() {
  $ADB shell dumpsys audio 2>/dev/null \
    | grep -m1 "Active communication device" | sed 's/.*type:\([a-z]*\).*/\1/'
}

freq_info() {
  $ADB shell uiautomator dump /sdcard/_vr.xml >/dev/null 2>&1
  $ADB shell cat /sdcard/_vr.xml 2>/dev/null | python3 -c "
import sys, re
d = sys.stdin.read()
out = {}
for m in re.finditer(r'<node[^>]*>', d):
    t = m.group(0)
    rid = re.search(r'resource-id=\"[^\"]*/([a-z_]+)\"', t)
    txt = re.search(r'text=\"([^\"]*)\"', t)
    if rid and txt and rid.group(1) in ('frequency_mhz', 'stereo_mono'):
        out[rid.group(1)] = txt.group(1).strip()
print('freq=%s mode=%s' % (out.get('frequency_mhz', '?'), out.get('stereo_mono', '?')), end='')
"
}

status() {
  echo "force_use=$(force_use) (ROM默认=$ROM_DEFAULT, FORCE_SPEAKER=1)  fmbin=$(fmbin_count)  comm=$(comm_device)  $(freq_info)"
}

case "${1:-status}" in
  status)
    status
    ;;
  speaker)
    $ADB shell "su -c '$FMROUTE speaker'" 2>&1 | tail -1
    sleep 2
    echo "after: $(status)"
    ;;
  restore)
    $ADB shell "su -c '$FMROUTE raw $ROM_DEFAULT'" 2>&1 | tail -1
    sleep 2
    echo "after: $(status)"
    ;;
  *)
    echo "usage: $0 status|speaker|restore" >&2
    exit 2
    ;;
esac
