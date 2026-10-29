#!/bin/bash
# 把 StillDesk 安装到 ToDesk.app 里。
#
#   ./scripts/install.sh [path/to/ToDesk.app]
#
# 会改什么，不会改什么：
#   * 把 libStillDesk.dylib 和 stilldesk-launch 复制到 Contents/libexec/
#   * 把 Info.plist 的 :CFBundleExecutable 指向启动器
#   * ToDesk 的可执行文件本身从不触碰，签名保持不变
#
# ToDesk 每次升级都会恢复自己的 Info.plist，所以升级后要重跑一次。
set -euo pipefail

APP="${1:-${STILLDESK_APP:-/Applications/ToDesk.app}}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONTENTS="$APP/Contents"
LIBEXEC="$CONTENTS/libexec"
TARGET="$CONTENTS/MacOS/ToDesk"
EXEC_NAME="libexec/stilldesk-launch"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"

die() { echo "error: $*" >&2; exit 1; }

[ -d "$APP" ]      || die "app not found: $APP"
[ -x "$TARGET" ]   || die "unexpected bundle layout, missing $TARGET"
[ -f "$HERE/build/libStillDesk.dylib" ] || die "run 'make' first"
[ -f "$HERE/build/stilldesk-launch" ]   || die "run 'make' first"

mkdir -p "$LIBEXEC"
cp -f "$HERE/build/libStillDesk.dylib" "$LIBEXEC/libStillDesk.dylib"
cp -f "$HERE/build/stilldesk-launch"   "$LIBEXEC/stilldesk-launch"
chmod 755 "$LIBEXEC/stilldesk-launch" "$LIBEXEC/libStillDesk.dylib"

# StillDesk 之前的命名留下的产物
rm -f "$LIBEXEC/libNoPreset.dylib" "$LIBEXEC/ToDeskLauncher"

CUR="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$CONTENTS/Info.plist" 2>/dev/null || true)"
if [ "$CUR" != "$EXEC_NAME" ]; then
    # 保留原始 plist 给 uninstall.sh 用（只在第一次）
    if [ ! -f "$LIBEXEC/Info.plist.orig" ]; then
        cp "$CONTENTS/Info.plist" "$LIBEXEC/Info.plist.orig"
    fi
    TMP="$(mktemp /tmp/stilldesk-info.XXXXXX)"
    cp "$CONTENTS/Info.plist" "$TMP"
    /usr/libexec/PlistBuddy -c "Set :CFBundleExecutable $EXEC_NAME" "$TMP"
    plutil -lint "$TMP" >/dev/null
    # Info.plist 属主是 root：只能在 Contents/ 里通过改名替换
    cp "$TMP" "$CONTENTS/.Info.plist.stilldesk"
    chmod 644 "$CONTENTS/.Info.plist.stilldesk"
    mv "$CONTENTS/.Info.plist.stilldesk" "$CONTENTS/Info.plist"
    rm -f "$TMP"
    echo "Info.plist: CFBundleExecutable -> $EXEC_NAME"
else
    echo "Info.plist: already installed"
fi

[ -x "$LSREGISTER" ] && "$LSREGISTER" -f "$APP" >/dev/null 2>&1 || true

cat <<MSG

StillDesk installed into $APP

  1. quit ToDesk completely (menu bar icon -> quit)
  2. open it again:  open -a "$APP"

Logs:  ~/Library/Logs/StillDesk.log
Rules: ~/.config/stilldesk/preset-blocklist
       ~/.config/stilldesk/hide-notify

Uninstall: ./scripts/uninstall.sh "$APP"
MSG
