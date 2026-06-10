# Goouuu-CAM RTSP 视频推流服务器

基于 ESP32-S3 + OV2640 摄像头的 RTSP 视频推流方案，支持 WiFiManager 一键配网。

## 功能特性

- **WiFiManager 自动配网** — 首次使用无需写死 WiFi 密码，通过手机/浏览器即可配置
- **RTSP 视频推流** — 局域网内通过 VLC 等播放器实时观看摄像头画面
- **NeoPixel LED 状态指示** — LED 颜色直观显示设备当前状态
- **PSRAM 自动检测** — 有 PSRAM 时自动使用高质量配置，无 PSRAM 时自动降级
- **FreeRTOS 多任务** — 视频推流运行在独立的高优先级任务中，不影响系统响应

## 硬件要求

| 组件 | 规格 |
|------|------|
| 开发板 | Goouuu-CAM (ESP32-S3) |
| 摄像头 | OV2640 |
| Flash | 16MB |
| PSRAM | 支持 (推荐) |
| RGB LED | 板载 NeoPixel (GPIO48) |

## 开发环境

- **IDE**: VSCode + PlatformIO
- **框架**: Arduino for ESP32
- **平台**: Espressif32

### 依赖库

| 库名 | 版本 | 用途 |
|------|------|------|
| ESP32-RTSPServer | ^1.3.5 | RTSP 视频流服务器 |
| WiFiManager | ^2.0.17 | WiFi 自动配网 |
| Adafruit NeoPixel | ^1.12.3 | 板载 RGB LED 控制 |

## 快速开始

### 1. 克隆项目

```bash
git clone <your-repo-url>
cd Goouuu-CAM-RTSP
```

### 2. 编译烧录

```bash
# 编译
pio run

# 烧录 (连接 USB 后)
pio run --target upload

# 查看串口日志
pio device monitor
```

### 3. 首次配网

1. 烧录后设备会自动进入 **AP 配网模式**
2. 用手机搜索并连接 WiFi: **`Goouuu-CAM-XXXX`** (开放热点，无需密码)
   - XXXX 为设备 MAC 地址后 4 位，每台设备不同
   - 串口日志中会显示具体 AP 名称
3. 浏览器会自动弹出配网页面 (若未弹出，手动访问 `192.168.4.1`)
4. 选择你的家庭 WiFi 并输入密码
5. 保存后设备自动连接目标 WiFi

> **提示**: WiFi 凭证会保存在 ESP32 的 Flash 中，下次上电自动连接。如需重新配网，可通过 WiFiManager 的重置功能清除。

### 4. 观看视频

配网成功后，串口会输出 RTSP 地址，格式如下：

```
rtsp://192.168.x.x:554/
```

使用以下任一方式观看：

- **VLC 播放器**: 媒体 → 打开网络串流 → 输入 RTSP 地址
- **ffplay**: `ffplay rtsp://192.168.x.x:554/`
- **PotPlayer**: 打开 → 打开链接 → 输入 RTSP 地址

## LED 状态指示

| LED 颜色 | 状态说明 |
|----------|----------|
| 蓝色 | 正在配网 / 正在初始化摄像头 |
| 绿色 | 正常运行，正在推流 |
| 红色 | 错误 (WiFi 连接失败 / 摄像头初始化失败) |

## 项目结构

```
Goouuu-CAM-RTSP/
├── platformio.ini          # PlatformIO 项目配置
├── README.md               # 项目说明文档
├── src/
│   ├── main.cpp            # 主程序 (WiFiManager + 摄像头 + RTSP)
│   └── camera_pins.h       # Goouuu-CAM 摄像头引脚定义
├── include/                # 项目头文件
├── lib/                    # 私有库
├── test/                   # 测试
└── misc/
    └── merge.py            # 固件合并脚本
```

## 串口输出示例

```
╔══════════════════════════════════════╗
║   Goouuu-CAM RTSP 视频推流服务器     ║
║   ESP32-S3 + OV2640                  ║
╚══════════════════════════════════════╝
[System] NeoPixel LED 初始化完成

========== WiFi 配网 ==========
[WiFi] AP 名称: Goouuu-CAM-A1B2 (开放热点, 无需密码)
[WiFi] 正在连接 WiFi...
[WiFi] WiFi 连接成功!
[WiFi] SSID: MyHomeWiFi
[WiFi] IP 地址: 192.168.1.100
[WiFi] 信号强度: -45 dBm

========== 摄像头初始化 ==========
[Camera] 检测到 PSRAM, 使用高质量配置
[Camera] 摄像头初始化成功!
[Camera] 传感器 PID: 0x26
[Camera] 帧质量: 10

========== RTSP 服务器启动 ==========
[RTSP] RTSP 服务器启动成功!
[RTSP] 地址: rtsp://192.168.1.100:554/
[RTSP] 视频推流任务已启动

========== 设备信息 ==========
--- 芯片信息 ---
  芯片 MAC: XXXXXXXXXXXX
  Flash 大小: 16 MB
  PSRAM 大小: 8 MB

--- 网络信息 ---
  WiFi SSID: MyHomeWiFi
  IP 地址: 192.168.1.100

--- RTSP 服务器信息 ---
  RTSP 端口: 554
  最大客户端数: 5

========================================
  >>> RTSP 地址: rtsp://192.168.1.100:554 <<<
  请使用 VLC 等播放器打开上述地址观看视频
========================================
```

## 常见问题

### Q: 摄像头初始化失败？

检查摄像头排线是否插紧，确认使用的是 OV2640 摄像头模块。

### Q: VLC 无法播放？

1. 确认设备和电脑在同一局域网
2. 检查防火墙是否放行 554 端口
3. 尝试使用 `rtsp://IP:554/` 格式的完整地址

### Q: 如何重新配网？

设备上电后如果已保存的 WiFi 不可用，WiFiManager 会自动进入 AP 配网模式。

### Q: 如何修改视频分辨率？

在 `main.cpp` 顶部修改宏定义即可，无需改动其他代码：

```cpp
// 推流分辨率
#define CAMERA_FRAME_SIZE   FRAMESIZE_VGA    // 640x480, ~25 FPS
// 可选: FRAMESIZE_QVGA(320x240) FRAMESIZE_VGA(640x480) FRAMESIZE_SVGA(800x600)
//       FRAMESIZE_XGA(1024x768) FRAMESIZE_HD(1280x720) FRAMESIZE_UXGA(1600x1200)

// JPEG 质量 (0-63, 数值越低质量越高)
#define CAMERA_JPEG_QUALITY 10

// 帧缓冲区数量 (8MB PSRAM 下推荐 4)
#define CAMERA_FB_COUNT     4
```

## 性能参考 (OV2640 on ESP32-S3)

| 分辨率 | 预期帧率 |
|--------|----------|
| QVGA (320x240) | ~50 FPS |
| VGA (640x480) | ~25 FPS |
| SVGA (800x600) | ~25 FPS |
| XGA (1024x768) | ~12.5 FPS |
| UXGA (1600x1200) | ~5 FPS |

## 参考资料

- [ESP32-RTSPServer 库文档](https://github.com/rjsachse/ESP32-RTSPServer)
- [WiFiManager 库文档](https://github.com/tzapu/WiFiManager)
- [ESP32 Camera 驱动](https://github.com/espressif/esp32-camera)

## 许可证

MIT License
