/**
 * @file config.h
 * @brief 全局配置: 引脚定义、摄像头参数、网络参数、看门狗参数
 *
 * 修改本文件即可调整所有硬件和软件参数, 无需改动其他源文件.
 */

#pragma once

// ===========================
// 摄像头引脚定义 (Goouuu-CAM ESP32-S3)
// ===========================
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   15
#define SIOD_GPIO_NUM   4
#define SIOC_GPIO_NUM   5

#define Y2_GPIO_NUM     11
#define Y3_GPIO_NUM     9
#define Y4_GPIO_NUM     8
#define Y5_GPIO_NUM     10
#define Y6_GPIO_NUM     12
#define Y7_GPIO_NUM     18
#define Y8_GPIO_NUM     17
#define Y9_GPIO_NUM     16

#define VSYNC_GPIO_NUM  6
#define HREF_GPIO_NUM   7
#define PCLK_GPIO_NUM   13

// ===========================
// NeoPixel LED
// ===========================
#define PIN_PIXS         48   // GPIO48
#define PIX_NUM          1
#define LED_BRIGHTNESS   80    // 0-255

// ===========================
// WiFi 配网
// ===========================
#define WIFI_AP_BASE        "Goouuu-Cam"
#define WIFI_PORTAL_TIMEOUT 180   // 配网 AP 超时 (秒)
#define WIFI_RECONNECT_TIMEOUT 30 // 断线后超时重启 (秒)
#define WIFI_MONITOR_INTERVAL 5   // 断线检测间隔 (秒)

// ===========================
// 摄像头参数
// ===========================
// 像素格式: 0=RGB565 (默认), 1=YUV422 (编码更快, 约快 40%)
// YUV422 无需色彩空间转换, 但部分播放器可能需要额外处理
#define CAMERA_PIXEL_FORMAT  0

// 分辨率 (软件 JPEG 推流建议 QVGA, 硬件 JPEG 可用更高分辨率)
// 可选: FRAMESIZE_QVGA(320x240) FRAMESIZE_VGA(640x480) FRAMESIZE_SVGA(800x600)
//       FRAMESIZE_HD(1280x720)  FRAMESIZE_UXGA(1600x1200)
#define CAMERA_FRAME_SIZE    FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY  8     // 0-63, 越低质量越高
#define CAMERA_FB_COUNT      3     // 帧缓冲区数量 (越多采集/编码并行度越高)

// XCLK 时钟频率 (GC2145 在 14MHz 下稳定, 16MHz 可能花屏)
#define CAMERA_XCLK_FREQ     14000000

// 软件 JPEG 编码质量映射 (1-100, 越高越好但越慢)
// QVGA 实时推流建议 25-35, 过高会严重拖慢帧率
#define SW_JPEG_QUALITY_MAX  30    // 信号最好时的软件编码质量
#define SW_JPEG_QUALITY_MIN  15    // 信号最差时的软件编码质量

// ===========================
// 推流参数
// ===========================
#define TARGET_FPS           0     // 目标帧率, 0=不限制
#define RTSP_MAX_CLIENTS     5
#define VIDEO_TASK_STACK     8192
#define VIDEO_TASK_PRIORITY  9
#define IDLE_DELAY_MS        100   // 无客户端时的空闲延迟 (减少CPU占用)

// ===========================
// 看门狗
// ===========================
#define WDT_TIMEOUT_SEC      10    // 看门狗超时 (秒)

// ===========================
// 动态码率调整
// ===========================
#define ADAPTIVE_QUALITY_INTERVAL_MS  5000  // 质量调整检查间隔 (毫秒)
#define QUALITY_MIN          8     // 最佳网络时 JPEG 质量 (高质量)
#define QUALITY_MAX          31    // 最差网络时 JPEG 质量 (低质量)
#define QUALITY_STEP         2     // 每次调整步长

// RSSI 阈值 (dBm)
#define RSSI_EXCELLENT      -55
#define RSSI_GOOD           -65
#define RSSI_FAIR           -75
// 低于 RSSI_FAIR 视为差

// FPS 警戒线 (低于此值认为网络拥塞)
#define FPS_LOW_THRESHOLD   5
