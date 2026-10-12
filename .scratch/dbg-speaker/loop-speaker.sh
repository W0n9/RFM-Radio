#!/bin/bash
# 反馈循环：验证 "Play through speaker" 菜单项是否真正切换 FM 音频路由。
#
# 断言根因信号（不是 UI 表象）：
#   AudioSystem.getForceUse(FOR_MEDIA) 由 dumpsys 暴露为 "Force use for media"。
#   FORCE_NONE=0, FORCE_SPEAKER=1。点击一次应把它变成 1，再点击一次应变回 0。
#
# 用法: ./loop-speaker.sh [taps]   默认 2 次点击（开 -> 关）
# 退出码: 0 = 行为正确, 1 = 复现 bug

set -u

PKG=com.vlad805.fmradio.dev
MENU_X=1024; MENU_Y=176      # 溢出菜单按钮
SPEAKER_X=725; SPEAKER_Y=439 # "Play through speaker" 菜单项

adb_bin=${ADB:-adb}

force_use() {
  $adb_bin shell dumpsys media.audio_policy 2>/dev/null \
    | grep -m1 "Force use for media" | awk '{print $NF}'
}

menu_checked() {
  # 打开菜单并读回 menu_speaker 的 checked 属性
  $adb_bin shell input tap $MENU_X $MENU_Y >/dev/null 2>&1
  sleep 1
  $adb_bin shell uiautomator dump /sdcard/_loop.xml >/dev/null 2>&1
  $adb_bin shell cat /sdcard/_loop.xml 2>/dev/null | python3 -c "
import sys, re
d = sys.stdin.read()
# 菜单项容器节点带有 resource-id=menu_speaker
for m in re.finditer(r'<node[^>]*resource-id=\"[^\"]*menu_speaker\"[^>]*>', d):
    ck = re.search(r'checked=\"([^\"]*)\"', m.group(0))
    print(ck.group(1) if ck else 'unknown')
    break
else:
    print('not-found')
"
  $adb_bin shell input keyevent KEYCODE_BACK >/dev/null 2>&1
  sleep 1
}

tap_speaker() {
  $adb_bin shell input tap $MENU_X $MENU_Y >/dev/null 2>&1
  sleep 1
  $adb_bin shell input tap $SPEAKER_X $SPEAKER_Y >/dev/null 2>&1
  sleep 2
}

taps=${1:-2}
base=$(force_use)
echo "baseline:            force_use=$base  menu_checked=$(menu_checked)"

fails=0
expect=1
for i in $(seq 1 "$taps"); do
  tap_speaker
  got=$(force_use)
  ck=$(menu_checked)
  if [ "$got" = "$expect" ]; then
    echo "tap $i: OK   force_use=$got (expected $expect)  menu_checked=$ck"
  else
    echo "tap $i: FAIL force_use=$got (expected $expect)  menu_checked=$ck"
    fails=$((fails + 1))
  fi
  expect=$((1 - expect))
done

if [ "$fails" -gt 0 ]; then
  echo "RESULT: RED — $fails/$taps 次点击没有改变音频路由"
  exit 1
fi
echo "RESULT: GREEN"
exit 0
