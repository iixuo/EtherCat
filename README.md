# EtherCAT 液压脚撑可靠性测试系统

基于 EtherCAT 协议的液压脚撑自动化可靠性测试系统，使用 Qt5/Qt6 构建图形界面。

## 功能特性

- 🔧 **继电器控制**：4通道继电器输出控制（EL2634）
- 📊 **压力监控**：4通道压力传感器实时读取（EL3074）
- 📥 **数字输入**：8通道数字输入监控（EL1008）
- 🔄 **可靠性测试**：自动化支撑/收回循环测试
- 📝 **日志记录**：完整的测试日志和报告导出

## 硬件要求

- **EtherCAT 主站**：IgH EtherCAT Master
- **从站设备**：
  - EL1008：8通道数字输入
  - EL3074：4通道4-20mA模拟输入
  - EL2634：4通道继电器输出
- **压力传感器**：4-20mA输出，0-100bar量程

## 软件依赖

- Qt 5.10+ 或 Qt 6.x
- CMake 3.16+
- C++17 编译器
- IgH EtherCAT Master（仅Linux）

## 编译

### Linux（真实硬件模式）

```bash
# 安装依赖
sudo apt install cmake qt6-base-dev libqt6widgets6-dev

# 编译
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行（需要root权限）
sudo ./ethercat_beckhoff_control
```

### macOS/Windows（模拟模式）

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
./ethercat_beckhoff_control
```

## 项目结构

```
├── CMakeLists.txt           # CMake构建配置
├── include/
│   └── ethercat/
│       └── EtherCATMaster.h # EtherCAT主站头文件
├── src/
│   ├── main.cpp             # 程序入口
│   ├── ethercat/
│   │   └── EtherCATMaster.cpp # EtherCAT业务逻辑
│   └── gui/
│       ├── mainwindow.cpp   # 主窗口实现
│       ├── mainwindow.h     # 主窗口头文件
│       └── mainwindow.ui    # Qt Designer UI文件
└── examples/                # 示例代码
```

## 测试参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 支撑目标压力 | 22 bar | 压力传感器需达到的最小值 |
| 收回目标压力 | 1 bar | 压力传感器需降至的最大值 |
| 支撑超时 | 15 秒 | 单次支撑测试最大等待时间 |
| 收回超时 | 15 秒 | 单次收回测试最大等待时间 |

## 许可证

MIT License
