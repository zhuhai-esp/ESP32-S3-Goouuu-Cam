# Goouuu-CAM RTSP 视频推流服务器

基于 ESP32-S3 + GC2145 摄像头的 RTSP 视频推流方案，支持 WiFiManager 一键配网。

## 功能特性

- **WiFiManager 自动配网** — 首次使用无需写死 WiFi 密码，通过手机/浏览器即可配置
- **RTSP 视频推流** — 局域网内通过 VLC 等播放器实时观看摄像头画面
- **软件 JPEG 编码** — 支持 RGB565 / YUV422 像素格式，YUV422 编码更快
- **动态码率调整** — 根据 WiFi 信号强度和帧率自动调整 JPEG 质量
- **NeoPixel LED 状态指示** — LED 颜色直观显示设备当前状态
- **PSRAM 自动检测** — 有 PSRAM 时自动使用高质量配置，无 PSRAM 时自动降级
- **看门狗保护** — 10 秒超时，防止任务卡死

## 硬件要求

| 组件 | 规格 |
|------|------|
| 开发板 | Goouuu-CAM (ESP32-S3) |
| 摄像头 | GC2145 (2MP, DVP 接口) |
| Flash | 16MB |
| PSRAM | 8MB (推荐) |
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
pio run
pio run --target upload
pio device monitor
```

### 3. 首次配网

1. 烧录后设备自动进入 AP 配网模式
2. 手机连接 WiFi: `Goouuu-Cam-XXXX` (开放热点)
3. 浏览器自动弹出配网页面 (或手动访问 `192.168.4.1`)
4. 选择家庭 WiFi 并输入密码，保存后设备自动连接

> WiFi 凭证保存在 Flash 中，下次上电自动连接。

### 4. 观看视频

串口输出 RTSP 地址，使用 VLC 等播放器打开：

```
rtsp://192.168.x.x:554/
```

## 配置说明

所有参数集中在 `src/config.h`，修改后重新编译即可：

### 像素格式

```c
#define CAMERA_PIXEL_FORMAT  0   // 0=RGB565 (默认), 1=YUV422 (编码更快)
```

| 格式 | 优点 | 缺点 |
|------|------|------|
| RGB565 | 兼容性好，色彩准确 | 编码时需 RGB→YUV 转换，较慢 |
| YUV422 | 无需色彩空间转换，编码快约 40% | 少数播放器色彩可能偏差 |

### 分辨率

```c
#define CAMERA_FRAME_SIZE  FRAMESIZE_QVGA   // 320x240
```

可选值：`FRAMESIZE_QVGA`(320x240)、`FRAMESIZE_VGA`(640x480)、`FRAMESIZE_SVGA`(800x600)

> 软件 JPEG 编码建议使用 QVGA，更高分辨率帧率会显著下降。

### XCLK 频率

```c
#define CAMERA_XCLK_FREQ  14000000   // 14MHz
```

GC2145 在 14MHz 下稳定，16MHz 可能花屏，10MHz 帧率偏低。

### JPEG 质量

```c
#define CAMERA_JPEG_QUALITY  8        // 0-63, 越低质量越高
#define SW_JPEG_QUALITY_MAX  30       // 软件编码最高质量 (1-100)
#define SW_JPEG_QUALITY_MIN  15       // 软件编码最低质量
```

系统会根据 WiFi 信号强度自动在 MIN/MAX 之间调整。

## LED 状态指示

| LED 颜色 | 状态 |
|----------|------|
| 蓝色 | 配网中 / 初始化中 |
| 绿色 | 正常推流 |
| 红色 | 错误 (WiFi/摄像头失败) |

## 项目结构

```
Goouuu-CAM-RTSP/
├── platformio.ini          # PlatformIO 配置
├── README.md
├── src/
│   ├── main.cpp            # 主程序入口
│   ├── config.h            # 全局配置 (引脚、参数)
│   ├── camera_manager.*    # 摄像头初始化与编码
│   ├── rtsp_stream.*       # RTSP 推流任务
│   ├── network_quality.*   # 动态码率调整
│   ├── led_controller.*    # LED 状态控制
│   ├── watchdog_manager.*  # 看门狗管理
│   └── wifi_portal.*       # WiFi 配网
├── include/
├── lib/
├── test/
└── misc/
    └── merge.py            # 固件合并脚本
```

## 性能参考 (GC2145 QVGA)

| 像素格式 | XCLK | 预期帧率 |
|----------|------|----------|
| RGB565 | 14 MHz | ~12 FPS |
| YUV422 | 14 MHz | ~17 FPS |

## 已知问题

- GC2145 QVGA 模式实际输出 320x216，驱动自动补黑边到 320x240
- cam_task 栈大小需从默认 2048 增加到 8192（已通过二进制修补 `libesp32-camera.a` 解决）
- 超过 14MHz XCLK 可能出现花屏

## 参考资料

- [ESP32-RTSPServer](https://github.com/rjsachse/ESP32-RTSPServer)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ESP32 Camera 驱动](https://github.com/espressif/esp32-camera)
- [GC2145 DVP Driver](https://github.com/Camemake/GC2145_ESP32_DVP_DRIVER)

## 许可证

MIT License
