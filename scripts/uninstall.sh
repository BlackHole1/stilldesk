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
    if [ -f "$LIBEXEC/Info.plist.orig" ]; then
        cp "$LIBEXEC/Info.plist.orig" "$TMP"
        echo "Info.plist: restored from backup"
    else
        cp "$CONTENTS/Info.plist" "$TMP"
        /usr/libexec/PlistBuddy -c "Set :CFBundleExecutable ToDesk" "$TMP"
        echo "Info.plist: CFBundleExecutable -> ToDesk"
    fi
    plutil -lint "$TMP" >/dev/null
    cp "$TMP" "$CONTENTS/.Info.plist.stilldesk"
    chmod 644 "$CONTENTS/.Info.plist.stilldesk"
    mv "$CONTENTS/.Info.plist.stilldesk" "$CONTENTS/Info.plist"
    rm -f "$TMP"
fi

rm -f "$LIBEXEC/libStillDesk.dylib" "$LIBEXEC/stilldesk-launch" \
      "$LIBEXEC/libNoPreset.dylib"  "$LIBEXEC/ToDeskLauncher"

[ -x "$LSREGISTER" ] && "$LSREGISTER" -f "$APP" >/dev/null 2>&1 || true

echo "StillDesk removed from $APP"
echo "(your rules in ~/.config/stilldesk/ and the logs are left untouched)"
