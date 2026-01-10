# 🚀 SyncMT - 高性能多线程文件同步工具

<div align="center">

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Language](https://img.shields.io/badge/language-C++17-orange.svg)

**⚡ 极速复制 · 🔄 智能同步 · 📊 实时监控**

</div>

---

## ✨ 核心特性

### 🚀 极速性能
- **多线程并行处理**：充分利用多核CPU性能，大幅提升传输速度
- **智能任务调度**：自动分配文件任务到不同线程，最大化吞吐量
- **零拷贝技术**：使用原生Linux系统调用，减少内存拷贝开销
- **自适应分块**：根据文件大小自动优化传输块大小

### 📁 智能文件管理
- **目录递归**：自动遍历子目录，完整保持目录结构
- **批量操作**：支持同时处理多个源路径
- **移动模式**：可选复制或移动操作，满足不同场景需求
- **权限保留**：完整保留源文件的访问权限和时间戳

### ⚙️ 灵活冲突处理
- **覆盖模式**：强制覆盖已存在文件
- **跳过模式**：保留目标文件，避免重复操作
- **错误模式**：遇到冲突时立即终止并报错

### 📊 实时进度监控
- **可视化进度条**：直观显示文件处理进度
- **详细统计信息**：实时展示文件数量、字节数、传输速度
- **性能分析**：精确计算平均传输速率，优化操作体验
- **预计剩余时间**：智能估算完成时间

## 📖 快速开始

### 安装

**使用 .deb 包（推荐）：**
```bash
sudo dpkg -i syncmt_1.0.0_amd64.deb
```

**使用二进制文件：**
```bash
chmod +x syncmt-1.0.0-linux-x86_64
sudo mv syncmt-1.0.0-linux-x86_64 /usr/local/bin/syncmt
```

### 基本使用

**复制单个文件：**
```bash
syncmt /path/to/source.txt /path/to/destination/
```

**复制整个目录：**
```bash
syncmt /home/user/documents /backup/documents
```

**使用8个线程复制：**
```bash
syncmt -t 8 /source /destination
```

**移动文件而非复制：**
```bash
syncmt -m /source /destination
```
## 📋 命令行选项

| 选项 | 说明 | 示例 |
|------|------|------|
| `-t, --threads <数量>` | 设置线程数（默认：4） | `-t 8` |
| `-m, --move` | 移动文件而非复制 | `-m` |
| `-v, --verbose` | 显示详细输出信息 | `-v` |
| `-o, --overwrite` | 覆盖已存在的文件 | `-o` |
| `-s, --skip` | 跳过已存在的文件 | `-s` |
| `--zh` | 使用中文界面 | `--zh` |
| `-V, --version` | 显示版本信息 | `-V` |
| `-h, --help` | 显示帮助信息 | `-h` |
<div align="center">

**⭐ 如果这个项目对你有帮助，请给我们一个Star！**

Made with ❤️ by Xiaobai

</div>
