# 盘寻 DiskSeek（diskseek.space）

> **盘寻一下，空间回来。**

盘寻（DiskSeek）是一款 Windows 磁盘空间分析与清理助手，基于开源项目 WinDirStat 二次开发。支持扫描本地磁盘、文件夹、网络共享、MTP 设备、S3 兼容存储、Azure Blob Storage 与 WebDAV，并通过可排序的文件列表、文件类型统计以及交互式矩形树图（treemap）、旭日图、火焰图查看空间占用。

快速找到大文件与重复文件、查看权限、估算存储成本、导出报告，并使用清理或已删除文件恢复工具。访问[官网](https://diskseek.space/)了解概览，或查阅[用户手册](https://github.com/windirstat/windirstat/wiki)获取使用细节。

## 主要特性

* 多磁盘/多目录同时扫描，支持暂停/继续、刷新，提权后可加速本地 NTFS 扫描
* 通过元数据列出云端存储内容，支持保存连接、逻辑大小合计与未完成扫描标记
* 以可配置的可视化方式浏览最大文件、文件类型、逻辑/物理大小、硬链接与空闲空间
* 按名称、大小、文件/目录类型或所有者搜索扫描结果；支持正则的路径/名称/大小/时间过滤
* 使用可配置哈希检测重复文件，并带云端文件保护；可选抽样哈希标记“疑似重复”
* 查看文件与目录权限条目，按修改时间估算存储分层成本
* 监视文件系统变化，扫描结果可保存/加载为 CSV 或 JSON，命令行可导出扫描、重复、权限报告
* 提权后可从本地 NTFS、exFAT、FAT 磁盘[恢复已删除文件](https://github.com/windirstat/windirstat/wiki/Recovering-Files)；恢复效果取决于元数据与文件数据是否幸存
* 打开、移动、删除文件；调用 Windows 清理工具、NTFS 压缩、硬链接去重与自定义清理命令
* 自定义布局、列、颜色、字体与工具栏大小；支持深色模式、便携设置、多语言与资源管理器集成

各版本变更请查看[更新日志](CHANGELOG.md)。

## 快速上手

1. 在“选择磁盘”中选择磁盘或**单个文件夹**，可点击添加文件夹按钮同时扫描多个目录；MTP 设备出现在**单个磁盘**下；云存储请使用 **文件 > 选择云存储** 并添加连接。
2. 仅在需要时勾选**扫描重复文件**（哈希会增加磁盘读取）。直接云扫描只列出元数据、不下载文件内容，也不做重复检测。需要加速整盘 NTFS 扫描请使用 **文件 > 以管理员身份运行**。
3. 对**所有文件**排序或打开**最大文件**，选中项目可在可视化中定位，使用 `Ctrl+F` 搜索；右键列标题可选择显示的细节，使用 **视图** 切换图形。
4. 清理前先在资源管理器中确认内容，之后刷新受影响的结果；使用 **文件 > 保存结果到 CSV/JSON** 保留扫描快照。

脚本调用可使用 `/SaveTo`、`/SaveDupesTo`、`/SavePermsTo` 加输出文件名与扫描目标；保存模式写出报告后退出，`/LoadFrom` 重新打开已保存的扫描。命令行示例与导出格式见[文档](https://github.com/windirstat/windirstat/wiki/Command-Line-and-CSV)。

未完成的扫描会保留已发现的项目并标记受影响的分支。保存模式在扫描不完整时返回非零退出码（即使报告已写出）。云端的物理占用未知并显示为空白，比较云内容请使用逻辑大小。

## 安装

从 [GitHub Releases](https://github.com/windirstat/windirstat/releases/) 下载安装包或便携版：

| 下载 | 适用场景 | 说明 |
| --- | --- | --- |
| DiskSeek-x64.msi | 现代 64 位 Windows | 标准 Windows 安装器，加入开始菜单 |
| DiskSeek-arm64.msi | Windows on ARM 设备 | ARM64 架构安装器 |
| DiskSeek-x86.msi | 32 位 Windows | 32 位架构安装器 |
| DiskSeek.zip / DiskSeek.7z | 便携使用、免安装 | 解压后直接运行对应架构的可执行文件 |

从压缩包运行不会自动启用便携设置。请启用 **选项 > 设置 > 常规 > 以便携模式运行**，或将可执行文件重命名为 `DiskSeek-portable.exe` 以在旁侧同名 `.ini` 文件中保存本地设置。云端凭据保存在 Windows 凭据管理器中，不会随 INI 文件一起迁移。

## 版权 / 许可证

* Copyright © WinDirStat Team（[windirstat.net](https://windirstat.net/)）
* Copyright © diskseek.space

应用本体依据 [GPL v3 或更新版本](windirstat/res/license.txt) 分发；部分源代码采用更宽松的许可条款。徽标及其衍生作品依据 [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) 许可。

盘寻是 WinDirStat 的衍生项目，感谢 WinDirStat Team 以及 KDirStat、SequoiaView 等前辈项目的贡献。

## 兼容性

桌面版可执行文件支持以下操作系统。Store/MSIX 包要求 Windows 10 1809 或更高；更早的系统请选择 MSI 安装器或便携压缩包。

* Windows 7 / 8 / 8.1 / 10 / 11
* Windows Server 2008 R2 至 Windows Server 2025

## 构建

在 Visual Studio 中打开 `windirstat.sln`，需安装“使用 C++ 的桌面开发”工作负载、MSVC v145 工具集（含 C++23 支持）、ATL 与 Windows SDK。按需安装 x64、Win32 或 ARM64 的编译工具；构建脚本依赖 Git 与 Windows PowerShell，首次构建会自动下载 xxHash。

## 贡献

欢迎通过回复 issue、[开发源代码](https://github.com/windirstat/windirstat/wiki/Developers)或[翻译](https://github.com/windirstat/windirstat/wiki/Contribute-Translation)参与贡献。历史贡献者、测试者与译者名单见[贡献者页面](CONTRIBUTORS.md)。

<a href="https://github.com/windirstat/windirstat/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=windirstat/windirstat&max=1000" alt="WinDirStat contributor tiles" />
</a>

## 徽标

![盘寻 logo](windirstat/logos/logo_256px.png)
