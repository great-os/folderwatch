# FolderWatch

极小体积的 Windows 文件夹活动监控工具：纯 Win32 API + ETW，不依赖 .NET / MFC / 运行时框架。

记录目标文件夹（含子目录）中被**哪个进程**在**什么时间**对**哪个文件**做了**什么操作**，并把连续相同的「同进程 + 同文件 + 同操作」合并为一条（含次数与最后操作时间）。

## 功能

- 监控操作：打开、读取、写入、新建、删除、重命名、修改
- 列表列：时间、操作、次数、最后操作时间、文件路径、进程完整路径、PID
- 选中行后可：**复制进程路径**、**打开进程所在目录**
- 目标：单文件 exe，体积约数十 KB（本机构建约 60–70 KB）

## 要求

- Windows 10/11 x64
- **管理员权限**（UAC 提权；否则无法看到其他进程的内核文件事件）

## 使用

1. 以管理员身份运行 `folderwatch.exe`
2. 「浏览」选择要监控的文件夹
3. 「开始」监控；「停止」结束；「清空」清空列表
4. 选中一行 →「复制进程路径」或「打开进程目录」

> 建议先点「开始」再操作文件。会话开始前已打开的句柄，其纯读事件可能无法解析出路径。

## 构建

需要 [MSYS2](https://www.msys2.org/) UCRT64 或其它带 `gcc`/`windres` 的 MinGW-w64：

```bat
build.bat
```

或手动：

```bat
windres -O coff src\app.rc obj\app_res.o
gcc -Os -flto -municode -mwindows -ffunction-sections -fdata-sections ^
  -fno-asynchronous-unwind-tables -fno-ident -fno-stack-protector ^
  -mno-stack-arg-probe -fno-exceptions -Wall -DUNICODE -D_UNICODE -Isrc ^
  src\ui.c src\etw.c src\fmap.c src\util.c obj\app_res.o ^
  -o folderwatch.exe -Wl,--gc-sections -Wl,--no-insert-timestamp -s -static ^
  -lkernel32 -luser32 -lshell32 -lole32 -ladvapi32 -lcomctl32
```

## 自检（可选）

```bat
folderwatch.exe -selftest C:\path\to\folder
```

隐藏窗口监控约 8 秒后退出，诊断写入 exe 同目录 `fw_diag.log`（不入库）。

## 技术说明

| 项 | 方案 |
|---|---|
| 采集 | ETW `Microsoft-Windows-Kernel-File` 私有/系统会话 |
| 关键字 | FILENAME / FILEIO / CREATE / READ / WRITE / DELETE / RENAME 等 |
| 解码 | 按 manifest 模板手写解析（区分 version 0/1、32/64 位指针） |
| 关联 | FileObject → 路径哈希表；PID → 进程镜像路径缓存 |
| 过滤 | 路径归一化（`\Device\...` → `C:\...`）+ 大小写不敏感前缀匹配 |
| UI | 单窗口 + ListView；事件线程 `PostMessage` 回 UI 线程 |

## 已知局限

- 需要管理员
- 会话前已打开句柄的读事件可能无文件名
- 列表最多保留 10000 行
- 高负载下 ETW 可能丢事件

## 许可

以仓库根目录许可证为准（若未声明则暂按所有者保留全部权利）。
