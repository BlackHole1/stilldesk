#!/usr/bin/env python3
"""验证独立启动、旧安装迁移及卸载，所有写入都限制在临时目录。"""
import os
import pathlib
import plistlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="stilldesk-install-") as tmp:
    base = pathlib.Path(tmp)
    app = base / "带空格 ToDesk.app"
    contents = app / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    target = contents / "MacOS" / "ToDesk"
    marker = base / "started"
    target.write_text('#!/bin/sh\nprintf started > "$STILLDESK_TEST_MARKER"\n')
    target.chmod(0o755)
    plist = contents / "Info.plist"
    original = plistlib.dumps({"CFBundleExecutable": "ToDesk", "CFBundleIdentifier": "test.fixture"})
    plist.write_bytes(original)
    wrapper = base / "StillDesk.app"
    env = dict(os.environ, STILLDESK_LAUNCHER_APP=str(wrapper), STILLDESK_TEST_MARKER=str(marker))

    def run(script):
        subprocess.run(["bash", str(ROOT / "scripts" / script), str(app)], env=env, check=True,
                       stdout=subprocess.DEVNULL)

    run("install.sh")
    assert plist.read_bytes() == original, "安装改变了原版启动配置"
    subprocess.run([str(wrapper / "Contents/MacOS/stilldesk-launch")], env=env, check=True)
    assert marker.read_text() == "started", "独立启动器未执行目标"
    run("install.sh")
    assert plist.read_bytes() == original, "重复安装改变了原版启动配置"
    run("uninstall.sh")
    assert not wrapper.exists(), "独立启动器卸载不完整"

    # 两种旧启动器都要迁移；历史备份可能也指向旧入口。
    for executable in ("libexec/stilldesk-launch", "libexec/ToDeskLauncher"):
        old = plistlib.dumps({"CFBundleExecutable": executable, "CFBundleIdentifier": "test.fixture"})
        plist.write_bytes(old)
        libexec = contents / "libexec"
        libexec.mkdir(exist_ok=True)
        (libexec / "Info.plist.orig").write_bytes(old)
        run("install.sh")
        assert plistlib.loads(plist.read_bytes())["CFBundleExecutable"] == "ToDesk"
        plist.write_bytes(old)
        run("uninstall.sh")
        assert plistlib.loads(plist.read_bytes())["CFBundleExecutable"] == "ToDesk"

print("PASS: 原启动配置保持不变，独立启动、重复安装、两种旧版迁移和卸载均通过")
