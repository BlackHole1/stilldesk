# StillDesk 的逆向推导过程

以下内容全部针对 **ToDesk 4.10.1.0（CFBundleVersion 2591）的 arm64 切片**。
偏移以 Mach-O 镜像基址为基准，`__TEXT` 映射在 `0x100000000`，所以这个切片里
`文件偏移 = 地址 - 0x100000000`。该二进制未 strip（约 8.85 万个符号），这是能够靠符号名下手的前提。

使用的方法按顺序是：`nm -arch arm64 | c++filt`、对整个 bundle 做明文和 UTF-16 字符串搜索、
在 `__text` 上扫描 BL 目标，最后对少数关键函数用 `llvm-objdump` 反汇编。
定位入口不需要反编译器，反编译只用来读决策逻辑。

## 两套互不相干的子系统

用户抱怨的弹窗其实来自两条完全不同的代码路径：

| 子系统 | 长什么样 | 代码位置 |
| --- | --- | --- |
| 预置弹窗（`PresetDialog*`） | 老用户专属特惠 / ¥14.9 / 升级会员、3 天 VIP、拥堵升级建议 | `MainDlgPresetDialogLogic` |
| 产品到期（`OverdueNotify` / `ProductExpiringNotify`） | 游戏版已于 2026-05-21 到期 / 前往购买 | `CMainDlg::processOverdueNotify`、`CMainDlg::processExpiringNotify`、`CPlaywndUI::processOverdueNotify` |

只处理其中一个，另一个还会继续弹。

## 为什么不能直接改磁盘上的内容

* `Contents/ImageResources/uires_v4.10.1.0_*.sar`（474 KB）开头是 `ENC`，熵很高。
  这些窗口的 Sciter HTML/CSS 是加密的，只在内存里解密。
* 弹窗文案确实以明文存在于 `Contents/lang/lang.zip` 里（`lang.txt` 是按语言分组的 JSON，
  条目 id 从 `001` 到 `041`）。推销那三条是 `039` = 老用户专属特惠首月仅¥14.9…、
  `040` = 老用户专属特惠、`041` = 升级会员。但这个文件只承载文字，窗口开不开由别处决定，
  布局和数据来自服务端。
* `Contents/Logs/*.xlog` 是腾讯 mars 的加密日志。不过 App 的 stdout 是明文的 spdlog，
  `[preset]` 那批日志可以从这里读（StillDesk 的启动器就把它重定向到
  `~/Library/Logs/StillDesk.stdout.log`）。

## 子系统 A：预置弹窗

### 唯一决策点

```
MainDlgPresetDialogLogic::HandlePresetDialogCandidate(std::string const& owner,
                                                      int type, int errorcode)
    函数体镜像偏移 0x44cb34，结束于 0x44dda0
```

`CGlobalSettingData::GetPresetDialogConfigData()`（0x5c84e4）只有 **一个** 调用者：
`0x44cb94`，就在这个函数里。而这个函数本身只有两个调用者：

* `0x44cabc`，来自 `OnMsgShowPresetDialog`（消息主题 `msg_subject_maindlg`，消息 id `0x1725`）
* `0x44dec8`，来自 `OnMsgPresetDialogCompeteRequest`

也就是说，主窗口上的所有预置弹窗，会话内的和退出远程后弹的，都汇聚到这一个函数。

### 编译后的决策逻辑

参数落栈用 `stp w3, w2, [x29, #-0x68]`，所以
`[x29-0x68] = errorcode`，`[x29-0x64] = type`。

```
0x44cc78  bl  CGlobalAppSetting::GetPresetDialogNoRemind(type)
0x44cc7c  mov x21, x0                      ; no_remind
0x44cc88  bl  GetPresetDialogShowCount()
0x44cc8c  ldr x8, [x23]
0x44cc90  ldr w8, [x8, #0x8c]              ; pop_num（服务端配置）
...
0x44cf9c  ldur w2, [x29, #-0x68]           ; errorcode
0x44cfa0  cmp  w2, #1
0x44cfa4  b.lt 0x44cfec
          ; errorcode >= 1 时调 HandleLocalErrorFallback(type, errorcode) 后返回
0x44cfec  tbnz w21, #0, 0x44cff8           ; 已设置不再提示则跳过次数检查
0x44cff0  cmp  w0, w8                      ; show_count < pop_num ?
0x44cff4  b.lt 0x44cca0                    ;   是，展示
0x44cff8  cmn  w8, #1                      ; pop_num == -1 ?
0x44cffc  b.eq 0x44cca0                    ;   是，强制展示（忽略不再提示）
          ; 其余情况打日志 "[preset][limit] display blocked ..." 后返回
0x44cca0  ; 展示分支：在 sp+0x260 构建请求，打日志
          ; "[preset][competition] candidate dialog_id={} ..."，
          ; 再由竞争回收状态机决定真正弹出哪个窗口
```

`GetPresetDialogNoRemind(type)`（0x5abf2c）是一个叶子函数，对 `this+0x140` 的 64 位掩码
按 `type` 取位。`HandleLocalErrorFallback(type, errorcode)` 只能从这个函数内部到达，
所以让这个函数失效，本地兜底路径也一起失效。

### 退出远程后才触发的那条链路

```
CViewerDlg 销毁
  └─ CViewerDlg::RecyclePresetDialogsOnDestroy()
       └─ CViewerDlgManager::TransferPresetDialogRecallCodeLocked(...)
            └─ CClientConnManager::SavePendingPresetDialogRandom(code)
服务端 0xB2 消息
  └─ MainDlgPresetDialogLogic::OnMsg0xB2RecallRandom()
       └─ EnqueuePendingRecallRandomError(errorcode)     ; 存入 [this+0x68]
            ...
  └─ TryShowPendingRecallRandomPresetDialog()
       打日志 "[preset][recall_random] trigger preset dialog errorcode={}"
       向 "msg_subject_maindlg" 发 0x1725，携带 variant(0x10) 和 variant(errorcode)
            └─ OnMsgShowPresetDialog -> HandlePresetDialogCandidate(owner, 16, errorcode)
```

登录和连接错误走同一条消息，只是 `type = 15`：

```
OnMsg0xE0ServerMsg -> HandleRecallLoginError(errorcode)
    除非 CGlobalSettingData::IsPresetDialogOverrideLocal(15, errorcode) 为真
    -> postMessage("msg_subject_maindlg", 0x1725, variant(15), variant(errorcode))
```

recall 的 errorcode 是服务端随机选的，所以对这类弹窗按 `(type, errorcode)` 过滤并不可靠，
按 `type` 过滤才稳。

### 诊断字符串（都在 `__cstring`）

```
[preset][match] config found owner_subject={} type={} errorcode={} errorcode_matched={} dialog_id={}
[preset][match] config not found owner_subject={} type={} errorcode={}
[preset][limit] display blocked type={} errorcode={} no_remind={} show_count={} pop_num={}
[preset][competition] candidate dialog_id={} type={} errorcode={} group={} priority={} competition_enabled={}
[preset][competition] decision=discard|refresh|replace|show
[preset][recall_random] enqueue errorcode={}
[preset][recall_random] trigger preset dialog errorcode={}
```

落盘状态在 `Contents/config.ini` 的 `ConfigInfo` 段：`PresetDialogNoRemind`、
`PresetDialogShowCount`、`PresetDialogUpdateDate`、`PresetDialogConfig`。
服务端配置由 `CSettingLogic::DealPresetDialogConfig(std::string const&)`（0x0c90b6c）解析，
键名有 `setting`、`data`、`type`、`errorcode`、`dialogId`、`priority`、
`competitionGroup`、`controlcount`、`overrideLocalError`、`popNum`、`md5`、`url`。

### 补丁实现

`0x44cb34` 这四条指令只是搬寄存器：

```
0x44cb34  mov x20, x3   0xAA0303F4
0x44cb38  mov x21, x2   0xAA0203F5
0x44cb3c  mov x24, x1   0xAA0103F8
0x44cb40  mov x22, x0   0xAA0003F6
```

把它们换成 `ldr x9, #8 ; br x9 ; .quad trampoline`（`0x58000049`、`0xD61F0120`）。
这样蹦床里能看到 `x0=this, x1=&owner, x2=type, x3=errorcode`，而且原函数的栈帧已经分配好，
调用规则引擎之后分别走两条路：

* **放行**：重放那四条 `mov`，再 `br` 到 `0x44cb44`（它们后面那条指令），
  行为与未打补丁的 App 完全一致。
* **拦截**：执行函数共用的尾声 `0x44cf50`（`add sp,sp,#0x340; ldp x29,x30,[sp,#0x50]; …; ret`）
  后返回，等价于这个弹窗刚被次数限制挡掉。

用尾声而不是直接 `ret`，是因为这个函数所有不展示的出口都会经过它；也没有走
`config not found` 那种捷径，因为那条路会调用 `HandleLocalErrorFallback()`，仍然会尝试显示点什么。

## 子系统 B：到期提醒

```
CMainDlg::processOverdueNotify     0x07eefc   首指令 0xD10603FF
CPlaywndUI::processOverdueNotify   0x27e518   首指令 0xD10643FF
CMainDlg::processExpiringNotify    0x07f5dc   首指令 0xA9BA6FFC
```

只有 `CMainDlg::processOverdueNotify`（调用者 `0x07af1c`）和
`CMainDlg::processExpiringNotify`（调用者 `0x07af3c`）有直接调用者，查看器那个是间接调用，
所以三个都打。

把首指令换成 `ret`（`0xD65F03C0`）等于整段函数体不执行。因为栈帧还没分配，对首指令属于
序言的函数是安全的（`0xD10603FF` = `sub sp, sp, #0x180`，`0xA9BA6FFC` = `stp x28, x27, [sp, #-0x60]!`）。

另外再加一道保险：「是否已经提示过到期」的标记是 `Contents/config.ini` 里的
`overduetipdate`（`20261029` 就是提示当天），StillDesk 会把它改写成 `20991231`。
提示文案本身在内嵌的 Sciter/JS 大块里（UTF-8 的 不再提示、如需继续使用 出现在
文件偏移 `0x2b072d5`、`0x2b32d24` 附近），而日期和产品名是运行时拼的，
所以也不该从内容下手。

## 排除掉的假设

* `PopAdvs` 那一族（`DealPopAdvsDataVipUpgrade`、`ShowVipUpgradePopup` 等）：
  `advInfo.json` 里 `PopAdvsData=""`，而且实际也没观察到这种弹窗。
* 辅助进程：`ToDesk_Session`（已 strip）和 `ToDesk_Session_Proxy` 里没有
  `display blocked`、`decision=show`、`recall_random`、`ShowPresetDialog` 这些决策字符串，
  说明逻辑只在主进程。
* 改 `lang.zip`：只换文案，不改变行为。
* 直接改磁盘上的二进制：会破坏代码签名（App 是开发者 ID 签名加 hardened runtime），
  还会牵连 TCC 授权、钥匙串条目和特权辅助进程。注入方式这些全都不动。

## 升级后如何重新取偏移

```bash
./tools/find-offsets.sh
```

会打印 StillDesk 需要的五个符号的当前偏移。函数位置变了就更新 `src/stilldesk.c` 里的常量；
如果那四条 `mov` 或序言变了，就要重新核对 dylib 校验的字节签名
（`expect_gate[]`、`expect_cfg[]` 以及各个 `_INSN` 值）。签名对不上时 dylib 会拒绝打补丁，
所以偏移写错最多是「不过滤」，不会把 App 改坏。
