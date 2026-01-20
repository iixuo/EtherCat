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
- **网卡**：支持EtherCAT的以太网卡（推荐Intel系列）

## 软件依赖

- Linux 操作系统（推荐 Ubuntu 20.04/22.04）
- Qt 5.10+ 或 Qt 6.x
- CMake 3.16+
- C++17 编译器（GCC 7+ 或 Clang 5+）
- IgH EtherCAT Master

---

## Linux 详细安装步骤

### 第一步：安装系统依赖

```bash
# 更新系统
sudo apt update && sudo apt upgrade -y

# 安装编译工具
sudo apt install -y build-essential cmake git

# 安装Qt6（推荐）
sudo apt install -y qt6-base-dev libqt6widgets6-dev

# 或者安装Qt5
# sudo apt install -y qtbase5-dev libqt5widgets5-dev

# 安装内核头文件（编译EtherCAT驱动需要）
sudo apt install -y linux-headers-$(uname -r)

# 安装其他依赖
sudo apt install -y autoconf automake libtool pkg-config
```

### 第二步：安装 IgH EtherCAT Master

#### 2.1 下载源码

```bash
# 创建工作目录
mkdir -p ~/ethercat && cd ~/ethercat

# 克隆IgH EtherCAT Master仓库
git clone https://gitlab.com/etherlab.org/ethercat.git
cd ethercat

# 或者下载稳定版本
# wget https://etherlab.org/download/ethercat/ethercat-1.5.2.tar.bz2
# tar xjf ethercat-1.5.2.tar.bz2
# cd ethercat-1.5.2
```

#### 2.2 编译安装

```bash
# 生成配置脚本
./bootstrap

# 配置（根据你的网卡选择驱动）
# 查看网卡型号：lspci | grep -i ethernet
./configure --prefix=/opt/etherlab \
            --with-linux-dir=/usr/src/linux-headers-$(uname -r) \
            --enable-generic \
            --enable-8139too=no \
            --enable-e100=no \
            --enable-e1000=no \
            --enable-e1000e=no \
            --enable-igb=no \
            --enable-r8169=no

# 如果你使用Intel网卡，可以启用对应驱动：
# --enable-e1000e=yes  (Intel I217/I218/I219等)
# --enable-igb=yes     (Intel I210/I211/I350等)

# 编译
make -j$(nproc)

# 安装
sudo make install

# 创建符号链接
sudo ln -s /opt/etherlab/bin/ethercat /usr/local/bin/
sudo ln -s /opt/etherlab/etc/init.d/ethercat /etc/init.d/
```

#### 2.3 配置EtherCAT主站

```bash
# 复制配置文件
sudo cp /opt/etherlab/etc/sysconfig/ethercat /etc/sysconfig/
sudo mkdir -p /etc/sysconfig

# 编辑配置文件
sudo nano /etc/sysconfig/ethercat
```

配置文件内容（根据实际情况修改）：

```bash
# 主站使用的网卡MAC地址（使用 ip link 或 ifconfig 查看）
MASTER0_DEVICE="xx:xx:xx:xx:xx:xx"

# 使用的驱动（generic为通用驱动）
DEVICE_MODULES="generic"
```

#### 2.4 加载内核模块

```bash
# 创建udev规则
echo 'KERNEL=="EtherCAT[0-9]*", MODE="0666"' | sudo tee /etc/udev/rules.d/99-ethercat.rules
sudo udevadm control --reload-rules

# 加载模块
sudo depmod -a
sudo modprobe ec_master
sudo modprobe ec_generic

# 验证模块加载
lsmod | grep ec_

# 启动EtherCAT服务
sudo /etc/init.d/ethercat start

# 设置开机自启
sudo systemctl enable ethercat
```

#### 2.5 验证安装

```bash
# 查看主站状态
ethercat master

# 扫描从站
ethercat slaves

# 应该看到类似输出：
# 0  0:0  PREOP  +  EL1008 8Ch. Dig. Input 24V, 3ms
# 1  0:1  PREOP  +  EL3074 4Ch. Ana. Input 4-20mA
# 2  0:2  PREOP  +  EL2634 4Ch. Relay Output
```

### 第三步：编译本项目

```bash
# 克隆项目
git clone https://github.com/iixuo/EtherCat.git
cd EtherCat

# 创建构建目录
mkdir build && cd build

# 配置（自动检测IgH EtherCAT）
cmake .. -DCMAKE_BUILD_TYPE=Release

# 如果IgH安装在非标准路径，手动指定：
# cmake .. -DCMAKE_BUILD_TYPE=Release \
#          -DETHERCAT_INCLUDE_DIR=/opt/etherlab/include \
#          -DETHERCAT_LIBRARY=/opt/etherlab/lib/libethercat.so

# 编译
make -j$(nproc)
```

### 第四步：运行程序

```bash
# 确保EtherCAT服务正在运行
sudo /etc/init.d/ethercat status

# 运行程序（需要root权限访问EtherCAT设备）
sudo ./ethercat_beckhoff_control

# 或者添加用户到ethercat组（推荐）
sudo groupadd ethercat
sudo usermod -aG ethercat $USER
sudo chown root:ethercat /dev/EtherCAT0
sudo chmod 0660 /dev/EtherCAT0

# 重新登录后无需sudo即可运行
./ethercat_beckhoff_control
```

---

## 常见问题排查

### 问题1：找不到ecrt.h

```bash
# 确保IgH已正确安装
ls /opt/etherlab/include/ecrt.h

# 如果不存在，重新安装IgH EtherCAT Master
```

### 问题2：无法请求EtherCAT主站

```bash
# 检查模块是否加载
lsmod | grep ec_

# 检查设备文件
ls -la /dev/EtherCAT*

# 检查服务状态
sudo /etc/init.d/ethercat status

# 重启服务
sudo /etc/init.d/ethercat restart
```

### 问题3：从站不响应

```bash
# 检查网线连接
ethercat master  # 查看link_up状态

# 检查网卡配置
ip link show

# 禁用网络管理器对EtherCAT网卡的管理
sudo nmcli device set enp0s31f6 managed no
```

### 问题4：权限不足

```bash
# 添加udev规则
echo 'KERNEL=="EtherCAT[0-9]*", MODE="0666"' | sudo tee /etc/udev/rules.d/99-ethercat.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## macOS/Windows（模拟模式）

在非Linux系统上，程序将以模拟模式运行，不需要真实硬件：

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
./ethercat_beckhoff_control
```

---

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
