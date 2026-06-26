# Goouuu-CAM WebServer

基于 ESP32-S3 + GC2145 摄像头的 HTTP Web 控制面板，支持 MJPEG 实时推流、拍照和摄像头参数调节。

## 功能特性

- **MJPEG 实时推流** — 浏览器直接观看摄像头画面，支持 FPS 显示
- **Web 控制面板** — Tailwind CSS 构建的响应式 UI，支持手机/PC 访问
- **JPEG 拍照 & BMP 导出** — 一键抓拍，支持 JPEG 和 BMP 两种格式
- **摄像头参数调节** — 分辨率、画质、亮度、对比度、饱和度、白平衡、特效等
- **传感器控制** — 水平镜像、垂直翻转、自动曝光/增益/白平衡开关
- **WiFi 信息显示** — SSID、IP 地址、信号强度实时监控
- **设备管理** — 一键重置默认参数、远程重启设备
- **寄存器级调试** — 支持 XCLK、PLL、窗口裁剪、寄存器读写等底层操作
- **HTML 文件分离** — 前端页面独立编辑，构建时自动嵌入固件
- **WebSocket 实时通信** — 双向实时通信，支持实时配置和状态监控
- **代码模块化重构** — 清晰的模块分离，便于维护和扩展
- **简体中文注释** — 所有源代码添加详细的中文注释

## 硬件要求

| 组件 | 规格 |
|------|------|
| 开发板 | Goouuu-CAM (ESP32-S3) |
| 摄像头 | GC2145 (2MP, DVP 接口) |
| Flash | 16MB |
| PSRAM | 8MB (推荐) |

## 开发环境

- **IDE**: VSCode + PlatformIO
- **框架**: Arduino for ESP32
- **平台**: Espressif32

### 依赖库

| 库名 | 版本 | 用途 |
|------|------|------|
| Adafruit NeoPixel | ^1.12.2 | 板载 RGB LED 控制 |
| WiFiManager | ^2.0.17 | WiFi 自动配网 |

## 快速开始

### 1. 克隆项目

```bash
git clone <your-repo-url>
cd Goouuu-CAM-WebServer
```

### 2. 编译烧录

```bash
pio run
pio run --target upload
pio device monitor
```

### 3. 连接使用

1. 烧录后设备通过 WiFiManager 进入配网模式
2. 连接设备热点，配置家庭 WiFi
3. 串口输出设备 IP 地址
4. 浏览器访问 `http://<device-ip>/` 打开控制面板

### 4. 使用控制面板

| 操作 | 说明 |
|------|------|
| **Start** | 开始 MJPEG 推流 |
| **Stop** | 停止推流 |
| **Capture** | 拍照并预览 |
| **BMP** | 下载 BMP 格式图片 |
| **Reset Defaults** | 重置所有参数为默认值 |
| **Reboot** | 远程重启设备 |
| **S 键** | 快捷键开始推流 |
| **X 键** | 快捷键停止推流 |
| **C 键** | 快捷键拍照 |
| **Esc** | 关闭预览窗口 |

## API 接口

### HTTP 接口

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 返回控制面板 HTML |
| `/stream` | GET | MJPEG 视频流 (端口 81) |
| `/capture` | GET | 拍照返回 JPEG |
| `/bmp` | GET | 拍照返回 BMP |
| `/status` | GET | 返回摄像头状态 JSON |
| `/wifi` | GET | 返回 WiFi 信息 JSON |
| `/control?var=xxx&val=xxx` | GET | 设置摄像头参数 |
| `/restart` | GET | 重启设备 |
| `/xclk?xclk=14` | GET | 设置 XCLK 频率 (MHz) |
| `/reg?reg=0xXX&mask=0xXX&val=0xXX` | GET | 写寄存器 |
| `/greg?reg=0xXX&mask=0xXX` | GET | 读寄存器 |
| `/pll?bypass=0&mul=...` | GET | 配置 PLL |
| `/resolution?sx=...&sy=...` | GET | 配置窗口裁剪 |

### WebSocket 接口

| 端点 | 说明 |
|------|------|
| `/ws` | WebSocket 实时通信端点 |

**WebSocket 消息协议：**

**客户端 -> 服务器：**
```json
{"cmd":"set","var":"brightness","val":1}
{"cmd":"get_status"}
{"cmd":"get_wifi"}
{"cmd":"restart"}
```

**服务器 -> 客户端：**
```json
{"type":"status","data":{...}}
{"type":"wifi","ssid":"...","ip":"...","rssi":-50,"quality":100,"mac":"..."}
{"type":"ack","var":"brightness","val":1,"ok":true}
{"type":"update","status":{...},"wifi":{...}}
{"type":"restarting"}
```

## 项目结构

```
Goouuu-CAM-WebServer/
├── platformio.ini          # PlatformIO 配置
├── README.md
├── src/
│   ├── main.cpp            # 主程序入口 (摄像头初始化 + WiFi + LED)
│   ├── camera_httpd.h      # 共享头文件 (类型、常量、函数声明)
│   ├── camera_pins.h       # 各开发板摄像头引脚定义
│   ├── httpd_common.cpp    # 公共工具 (FPS 滤波、URL 解析、JPG 回调)
│   ├── httpd_server.cpp    # 服务器启动 + 页面 handler + 重启/WiFi
│   ├── httpd_capture.cpp   # JPEG 拍照 + BMP 导出
│   ├── httpd_stream.cpp    # MJPEG 实时推流
│   ├── httpd_control.cpp   # 参数设置 (查找表驱动) + 状态查询
│   ├── httpd_debug.cpp     # 寄存器读写、XCLK、PLL、窗口配置
│   ├── httpd_ws.cpp        # WebSocket 实时通信模块
│   └── index.html          # Web 控制面板前端 (Tailwind CSS)
├── demo/                   # RTSP 推流参考项目 (独立可编译)
├── include/
├── lib/
├── test/
└── misc/
    └── merge.py            # 固件合并脚本
```

## 技术要点

- **HTML 嵌入方式**: `platformio.ini` 中 `board_build.embed_files` 将 `index.html` 编译进固件，运行时通过 GCC 链接器符号 (`_binary_src_index_html_start/end`) 直接内存访问
- **双 HTTP 服务**: 控制面板运行在默认端口 (80)，MJPEG 推流运行在端口 81，避免阻塞
- **软件 JPEG 编码**: GC2145 输出 RGB565 格式，通过 `frame2jpg()` 软件编码为 JPEG
- **查找表驱动**: `cmd_handler` 使用函数指针表替代 20+ if-else 链，便于扩展
- **前端框架**: Tailwind CSS via CDN，无需构建工具，编辑 HTML 即可修改界面
- **WebSocket 实时通信**: 使用 `httpd_ws_*` API 实现双向实时通信，支持实时配置和状态监控
- **定时状态推送**: 通过 `esp_timer` 每 2 秒自动向所有 WebSocket 客户端推送最新状态
- **客户端连接管理**: 支持最多 4 个并发 WebSocket 客户端连接
- **代码模块化**: 6 个独立的 HTTP 处理模块，职责清晰，便于维护和扩展
- **简体中文注释**: 所有源代码添加详细的中文注释，便于理解和维护

## 与 RTSP 版本的对比

| 特性 | WebServer (本项目) | RTSP (demo/) |
|------|-------------------|--------------|
| 传输协议 | HTTP MJPEG + WebSocket | RTSP |
| 观看方式 | 浏览器 | VLC 等播放器 |
| 参数调节 | Web UI 实时控制 | 无 |
| 实时通信 | WebSocket 双向通信 | 无 |
| 前端可编辑 | 是 (index.html) | 无前端 |
| 依赖库 | 少 (仅 NeoPixel + WiFiManager) | 多 (RTSPServer + WiFiManager + NeoPixel) |
| 状态监控 | 实时推送 | 无 |

## 许可证

MIT License
