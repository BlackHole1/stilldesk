#!/bin/bash
# 从 ToDesk.app 里移除 StillDesk，并还原原来的启动方式。
#
#   ./scripts/uninstall.sh [path/to/ToDesk.app]
set -euo pipefail

APP="${1:-${STILLDESK_APP:-/Applications/ToDesk.app}}"
CONTENTS="$APP/Contents"
LIBEXEC="$CONTENTS/libexec"
EXEC_NAME="libexec/stilldesk-launch"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"

die() { echo "error: $*" >&2; exit 1; }

[ -d "$APP" ] || die "app not found: $APP"

CUR="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$CONTENTS/Info.plist" 2>/dev/null || true)"
if [ "$CUR" = "$EXEC_NAME" ] || [ "$CUR" = "libexec/ToDeskLauncher" ]; then
    TMP="$(mktemp /tmp/stilldesk-info.XXXXXX)"
    # 历史备份也可能指向旧启动器，因此明确恢复真正的可执行文件名。
    cp "$CONTENTS/Info.plist" "$TMP"
    /usr/libexec/PlistBuddy -c "Set :CFBundleExecutable ToDesk" "$TMP"
    echo "Info.plist: CFBundleExecutable -> ToDesk"
    plutil -lint "$TMP" >/dev/null
    cp "$TMP" "$CONTENTS/.Info.plist.stilldesk"
    chmod 644 "$CONTENTS/.Info.plist.stilldesk"
    mv "$CONTENTS/.Info.plist.stilldesk" "$CONTENTS/Info.plist"
    rm -f "$TMP"
fi

# 仅删除明确属于 StillDesk 的文件，保留历史备份和用户规则。
[ "$LIBEXEC" = "$APP/Contents/libexec" ] || die "unexpected libexec path"
rm -f "$LIBEXEC/libStillDesk.dylib" "$LIBEXEC/stilldesk-launch" \
      "$LIBEXEC/libNoPreset.dylib"  "$LIBEXEC/ToDeskLauncher"
WRAPPER="${STILLDESK_LAUNCHER_APP:-$HOME/Applications/StillDesk.app}"
[[ "$WRAPPER" = /*/StillDesk.app ]] || die "unexpected launcher path"
if [ -d "$WRAPPER" ]; then
    ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$WRAPPER/Contents/Info.plist" 2>/dev/null || true)"
    [ "$ID" = "io.github.BlackHole1.stilldesk" ] || die "unexpected launcher identifier"
    rm -f "$WRAPPER/Contents/MacOS/stilldesk-launch" "$WRAPPER/Contents/MacOS/libStillDesk.dylib" \
        "$WRAPPER/Contents/MacOS/target-path" "$WRAPPER/Contents/Info.plist"
    rmdir "$WRAPPER/Contents/MacOS" "$WRAPPER/Contents" "$WRAPPER" 2>/dev/null || true
fi

[ -x "$LSREGISTER" ] && "$LSREGISTER" -f "$APP" >/dev/null 2>&1 || true

echo "StillDesk removed from $APP"
echo "(your rules in ~/.config/stilldesk/ and the logs are left untouched)"
