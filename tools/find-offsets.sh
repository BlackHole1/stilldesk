#!/bin/bash
# 打印当前这个 ToDesk 版本里 StillDesk 要打补丁的镜像偏移。
# ToDesk 升级后跑一次：偏移变了就更新 src/stilldesk.c 里的常量
# （如果序言变了，还要更新 src/trampoline.s）。
#
#   ./tools/find-offsets.sh [path/to/ToDesk.app]
set -euo pipefail

APP="${1:-/Applications/ToDesk.app}"
BIN="$APP/Contents/MacOS/ToDesk"
[ -x "$BIN" ] || { echo "error: not found: $BIN" >&2; exit 1; }

echo "binary : $BIN"
echo "version: $(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist" 2>/dev/null) ($(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP/Contents/Info.plist" 2>/dev/null))"
echo

nm -arch arm64 "$BIN" 2>/dev/null | c++filt | grep -E \
  'MainDlgPresetDialogLogic::HandlePresetDialogCandidate|CSettingLogic::DealPresetDialogConfig|CMainDlg::processOverdueNotify|CPlaywndUI::processOverdueNotify|CMainDlg::processExpiringNotify' \
| while read -r addr _ name; do
    off=$(( 0x$addr - 0x100000000 ))
    printf '  %-64s 0x%x\n' "$name" "$off"
done

cat <<'MSG'

Expected (ToDesk 4.10.1.0, arm64):
  HandlePresetDialogCandidate  0x44cb34   (4 movs -> trampoline island)
  DealPresetDialogConfig       0xc90b6c   (diagnostic hook, optional)
  CMainDlg::processOverdueNotify    0x7eefc
  CPlaywndUI::processOverdueNotify  0x27e518
  CMainDlg::processExpiringNotify   0x7f5dc
MSG
