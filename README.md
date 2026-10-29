# StillDesk

![build](https://github.com/BlackHole1/stilldesk/actions/workflows/build.yml/badge.svg)
![license](https://img.shields.io/badge/license-MIT-blue.svg)

关掉 macOS 版 ToDesk 的推销和到期弹窗。远程结束后不会再被「老用户专属特惠」「3 天 VIP 免费领取」「游戏版已到期」打断，跨境流量、连接错误这类功能提示照常保留。

不改二进制，不重新签名。

## 特性

- 拦截「老用户专属特惠 / 首月 ¥14.9 / 升级会员」，以及「3 天 VIP 免费领取」这类引流弹窗
- 拦截「游戏版已到期」这类产品到期提醒
- 保留跨境流量、连接错误、多控插件等功能提示
- 规则文件可自行调整，预置弹窗规则改完即时生效
- 版本不匹配时只写日志不打补丁，不会把 App 弄坏

## 安装

需要 macOS 11 以上和 Xcode 命令行工具。

```bash
git clone https://github.com/BlackHole1/stilldesk.git
cd stilldesk
make
./scripts/install.sh
```

然后彻底退出 ToDesk，重新打开：

```bash
open -a ToDesk
```

确认是否生效：

```bash
tail -n 20 ~/Library/Logs/StillDesk.log
```

正常输出里会有 `preset gate: type=N -> BLOCK` 和三条 `hook ... patched -> ret`。

## 规则

两个可选的规则文件，文件不存在时表示全部隐藏。

`~/.config/stilldesk/preset-blocklist` 控制预置弹窗，改完即时生效。

| 规则 | 作用 |
| --- | --- |
| `all` | 拦截全部预置弹窗 |
| `type:N` | 只拦截类型 N |
| `pair:T/E` | 只拦截类型 T、错误码 E |
| `allow:type:N` | 例外，保留类型 N |
| `allow:pair:T/E` | 例外，保留某一对 |

`~/.config/stilldesk/hide-notify` 控制到期提醒，改完需重启 App。

| 规则 | 作用 |
| --- | --- |
| `overdue` | 隐藏产品到期提醒 |
| `expiring` | 隐藏订阅即将到期提醒 |

完整示例在 `examples/` 目录。想保留某个提示但不知道类型号，看日志里的 `preset gate: type=N`，加一行 `allow:type:N` 即可。

## 升级 ToDesk 之后

ToDesk 升级会覆盖启动器，重跑一次安装即可。

```bash
make && ./scripts/install.sh
```

如果日志里出现 `ABORT unexpected code`，说明偏移变了，用 `./tools/find-offsets.sh` 重新取值，再更新 `src/stilldesk.c` 里的常量。

## 卸载

```bash
./scripts/uninstall.sh
```

会还原 `Info.plist` 并删掉注入的文件，规则和日志保留。

## 原理

弹窗文案在 `Contents/lang/lang.zip` 里是明文，但界面资源 `uires_*.sar` 是加密的，是否展示又由服务端配置决定，所以改文案没有意义。

StillDesk 让启动器 `execv` 原来那个已签名的可执行文件，并注入一个小库，在运行时改掉两个决策点：

- 预置弹窗：在唯一决策函数 `HandlePresetDialogCandidate` 入口写 16 字节分支岛，按规则决定放行还是直接返回
- 到期提醒：把 `processOverdueNotify`、`processExpiringNotify` 三个入口的首指令改成 `ret`，同时把 `config.ini` 的 `overduetipdate` 推到 2099

进程镜像、签名、Team ID 都没变，TCC 权限、钥匙串、辅助进程不受影响。每个补丁点写入前逐字节校验，不匹配就放弃。逆向过程见 [docs/EVIDENCE.md](docs/EVIDENCE.md)。

## 说明

只隐藏营销和到期弹窗，不破解，也不解锁任何付费功能。仓库不包含 ToDesk 的任何二进制或资源。ToDesk 是权利人的商标，本项目与其无关。

[MIT](LICENSE) 协议。
