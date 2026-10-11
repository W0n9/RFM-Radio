#!/bin/bash
# 反馈回路：耳机插入时 RFM "Play through speaker" 能否把 FM 声音切到扬声器、并能反选。
#
# 断言用户的两个原始症状（不是代理指标的代理）：
#   A. 勾选后 FM 声音应走扬声器：audio policy 里 FM_TUNER 源的 patch，sink 必须是
#      AUDIO_DEVICE_OUT_SPEAKER；反选后 sink 回到 WIRED_HEADPHONE/HEADSET（物理耳机是 HEADSET）。
#      （与 MIUI 自带 FM 的通路同构：FmService 建 fm_tuner->device patch，HAL 据此
#       setFmEnable(output_device) 并打开 Hostless_FM。）
#   B. 菜单 CheckBox 两次点击后应回到未勾选。
#
# 驱动方式：真实点击 UI（溢出菜单 -> Play through speaker），坐标从 uiautomator 动态取。
# 安全：绝不读取 /proc/asound/*/sub0/status（会让 cannon 内核 panic）。
#
# 前置：已 root（su 可用）、RFM 在前台且 FM 已开启。耳机插入由脚本经 ACCDET 注入。
# 退出码：0 = GREEN，1 = RED（复现），2 = 前置条件不满足

set -u
ADB=${ADB:-adb}
PKG=com.vlad805.fmradio.dev
UI=/data/local/tmp/_loop_ui.xml

sh_() { $ADB shell "$@" 2>/dev/null | tr -d '\r'; }

plug_headphone() {
  # EV_SW: SW_HEADPHONE_INSERT=2, SW_JACK_PHYSICAL_INSERT=7
  sh_ "su -c 'sendevent /dev/input/event0 5 2 1; sendevent /dev/input/event0 5 7 1; sendevent /dev/input/event0 0 0 0'" >/dev/null
}

headphone_available() {
  sh_ dumpsys media.audio_policy | sed -n '/Available output devices/,/Available input devices/p' \
    | grep -q "AUDIO_DEVICE_OUT_WIRED"
}

# FM_TUNER 源 patch 的 sink 列表（逗号分隔），无 patch 输出 none
fm_sinks() {
  sh_ dumpsys media.audio_policy | sed -n '/Audio Patches:/,/Audio Policy Mix/p' | awk '
    /Patch [0-9]+:/ { fm = 0 }
    /\[src/ && /FM_TUNER/ { fm = 1 }
    /\[sink/ && fm { sub(/.*AUDIO_DEVICE_OUT_/, ""); s = s (s ? "," : "") $0 }
    END { print (s ? s : "none") }'
}

force_use_media() {
  sh_ dumpsys media.audio_policy | grep -m1 "Force use for media" | awk '{print $NF}'
}

dump_ui() {
  sh_ "uiautomator dump $UI >/dev/null 2>&1; cat $UI"
}

MENU_XY=""   # 溢出菜单按钮坐标（静态，取一次）
ITEM_XY=""   # "Play through speaker" 菜单项坐标（菜单打开后取）

find_menu_button() {
  MENU_XY=$(dump_ui | python3 -c "
import sys, re, xml.etree.ElementTree as ET
for n in ET.fromstring(sys.stdin.read()).iter('node'):
    if n.attrib.get('content-desc') in ('更多选项', 'More options'):
        x1, y1, x2, y2 = map(int, re.findall(r'\d+', n.attrib['bounds']))
        print((x1 + x2) // 2, (y1 + y2) // 2)
        break
")
  [ -n "$MENU_XY" ]
}

# 打开菜单、一次 dump 同时读出 CheckBox 状态和菜单项坐标，然后关闭菜单。
# 结果写入全局 CHECKED / ITEM_XY（不能用 $(...) 调用，子 shell 会吞掉赋值）
CHECKED=""
read_menu() {
  sh_ input tap $MENU_XY >/dev/null
  sleep 0.7
  local out
  out=$(dump_ui | python3 -c "
import sys, re, xml.etree.ElementTree as ET
nodes = list(ET.fromstring(sys.stdin.read()).iter('node'))
for i, n in enumerate(nodes):
    if n.attrib.get('text') == 'Play through speaker':
        x1, y1, x2, y2 = map(int, re.findall(r'\d+', n.attrib['bounds']))
        ck = next((m.attrib.get('checked') for m in nodes[i:i+4] if m.attrib.get('checkable') == 'true'), 'unknown')
        print(ck, (x1 + x2) // 2, (y1 + y2) // 2)
        sys.exit()
print('not-found')
")
  sh_ input keyevent KEYCODE_BACK >/dev/null
  sleep 0.5
  set -- $out
  CHECKED=$1
  [ "$#" -eq 3 ] && ITEM_XY="$2 $3"
}

tap_speaker() {
  [ -n "$ITEM_XY" ] || return 1
  sh_ input tap $MENU_XY >/dev/null
  sleep 0.7
  sh_ input tap $ITEM_XY >/dev/null
  sleep 2
}

# ---- 前置条件 ----
sh_ "su -c id" | grep -q "uid=0" || { echo "PRECONDITION: 无 root"; exit 2; }
[ "$(sh_ ps -A | grep -c fmbin)" -ge 1 ] || { echo "PRECONDITION: fmbin 未运行（先在 RFM 里开启 FM）"; exit 2; }
sh_ dumpsys window | grep mCurrentFocus | grep -q "$PKG" || { echo "PRECONDITION: RFM 不在前台"; exit 2; }
headphone_available || { plug_headphone; sleep 2; }
headphone_available || { echo "PRECONDITION: 耳机注入后 audio policy 仍无 WIRED_HEADPHONE"; exit 2; }

fails=0
check() { # name expected(支持 glob) actual
  if [[ "$3" == $2 ]]; then echo "  ok   $1 = $3"; else echo "  FAIL $1 = $3 (expected $2)"; fails=$((fails + 1)); fi
}

# 勾选状态 -> 期望的 FM sink
sink_for() { [ "$1" = "true" ] && echo "SPEAKER" || echo "WIRED_*"; }
negate() { [ "$1" = "true" ] && echo "false" || echo "true"; }

find_menu_button || { echo "PRECONDITION: 找不到溢出菜单按钮"; exit 2; }
read_menu
base=$CHECKED
case "$base" in true|false) ;; *) echo "PRECONDITION: 读不到菜单勾选状态 ($base)"; exit 2;; esac
echo "baseline: checked=$base fm_sinks=$(fm_sinks) force_use=$(force_use_media)"

# 期望相对基线：每次点击翻转勾选，FM 声音跟着勾选状态走
expect=$base
for i in 1 2; do
  expect=$(negate "$expect")
  echo "tap $i (期望勾选=$expect):"
  tap_speaker || { echo "PRECONDITION: 找不到菜单项"; exit 2; }
  read_menu
  check "menu checked" "$expect" "$CHECKED"
  check "FM sink" "$(sink_for "$expect")" "$(fm_sinks)"
  echo "       force_use=$(force_use_media)"
done

if [ "$fails" -gt 0 ]; then
  echo "RESULT: RED ($fails 项失败)"
  exit 1
fi
echo "RESULT: GREEN"
exit 0
