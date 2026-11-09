#!/bin/bash
# 安装独立的 StillDesk.app，保留 ToDesk 原来的启动入口。
# 用法：./scripts/install.sh [path/to/ToDesk.app]
set -euo pipefail

APP="${1:-${STILLDESK_APP:-/Applications/ToDesk.app}}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WRAPPER="${STILLDESK_LAUNCHER_APP:-$HOME/Applications/StillDesk.app}"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
die() { echo "error: $*" >&2; exit 1; }

[ -d "$APP" ] || die "app not found: $APP"
APP="$(cd "$APP" && pwd -P)"
CONTENTS="$APP/Contents"
TARGET="$CONTENTS/MacOS/ToDesk"
[ -x "$TARGET" ] || die "unexpected bundle layout, missing $TARGET"
[ -f "$HERE/build/libStillDesk.dylib" ] || die "run 'make' first"
[ -f "$HERE/build/stilldesk-launch" ] || die "run 'make' first"
[[ "$WRAPPER" = /*/StillDesk.app ]] || die "launcher path must be absolute and end with /StillDesk.app"
[ "$WRAPPER" != "$APP" ] || die "launcher must be separate from ToDesk"

# 迁移旧安装时只还原启动入口，不使用可能已被修改的历史备份。
CUR="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$CONTENTS/Info.plist")"
if [ "$CUR" = "libexec/stilldesk-launch" ] || [ "$CUR" = "libexec/ToDeskLauncher" ]; then
    TMP="$(mktemp /tmp/stilldesk-info.XXXXXX)"
    trap 'rm -f "${TMP:-}"' EXIT
    cp "$CONTENTS/Info.plist" "$TMP"
    /usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable ToDesk' "$TMP"
    plutil -lint "$TMP" >/dev/null
    cp "$TMP" "$CONTENTS/.Info.plist.stilldesk"
    chmod 644 "$CONTENTS/.Info.plist.stilldesk"
    mv "$CONTENTS/.Info.plist.stilldesk" "$CONTENTS/Info.plist"
elif [ "$CUR" != "ToDesk" ]; then
    die "unexpected executable: $CUR"
fi

mkdir -p "$WRAPPER/Contents/MacOS"
cp "$HERE/build/libStillDesk.dylib" "$WRAPPER/Contents/MacOS/libStillDesk.dylib"
cp "$HERE/build/stilldesk-launch" "$WRAPPER/Contents/MacOS/stilldesk-launch"
chmod 755 "$WRAPPER/Contents/MacOS/stilldesk-launch" "$WRAPPER/Contents/MacOS/libStillDesk.dylib"
printf '%s\n' "$TARGET" > "$WRAPPER/Contents/MacOS/target-path"
cat > "$WRAPPER/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>stilldesk-launch</string>
<key>CFBundleIdentifier</key><string>io.github.BlackHole1.stilldesk</string>
<key>CFBundleName</key><string>StillDesk</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>
PLIST
plutil -lint "$WRAPPER/Contents/Info.plist" >/dev/null
if [ -x "$LSREGISTER" ]; then
    "$LSREGISTER" -f "$APP" >/dev/null 2>&1 || true
    "$LSREGISTER" -f "$WRAPPER" >/dev/null 2>&1 || true
fi
printf '已安装：%s\n退出 ToDesk 后，打开 StillDesk 启用弹窗过滤：\n  open "%s"\n直接打开 ToDesk 则使用原版启动方式。\n' "$WRAPPER" "$WRAPPER"
