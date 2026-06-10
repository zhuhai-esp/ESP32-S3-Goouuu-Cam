/**
 * @file main.cpp
 * @brief Goouuu-CAM ESP32-S3 RTSP Video Streaming Server
 *
 * 基于 ESP32-S3 的摄像头 RTSP 视频推流服务器
 * 功能:
 *   1. WiFiManager 自动配网 (首次使用或 WiFi 断开时自动进入 AP 配网模式)
 *   2. OV2640 摄像头 JPEG 视频采集
 *   3. RTSP 视频流推送 (局域网内可通过 VLC 等播放器观看)
 *   4. NeoPixel LED 状态指示
 *   5. 详细的串口日志输出
 *
 * 硬件: Goouuu-CAM (ESP32-S3 + OV2640 + PSRAM)
 * 参考: ESP32-RTSPServer Library by RjSachse
 */

#include <Adafruit_NeoPixel.h>  // NeoPixel LED 控制
#include <ESP32-RTSPServer.h>   // RTSP 服务器库
#include <WiFi.h>
#include <WiFiManager.h>  // WiFi 自动配网库

#include "esp_camera.h"  // ESP32 摄像头驱动

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y2_GPIO_NUM 11
#define Y3_GPIO_NUM 9
#define Y4_GPIO_NUM 8
#define Y5_GPIO_NUM 10
#define Y6_GPIO_NUM 12
#define Y7_GPIO_NUM 18
#define Y8_GPIO_NUM 17
#define Y9_GPIO_NUM 16

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// ===========================
// 硬件引脚与常量定义
// ===========================

// NeoPixel RGB LED (Goouuu-CAM 板载)
#define PIN_PIXS 48  // NeoPixel 数据引脚 GPIO48
#define PIX_NUM 1    // LED 数量

// WiFi 配网 AP 基础名称, 后续会拼接 MAC 地址后 4 位
// 例如: Goouuu-CAM-A1B2
#define WIFI_AP_BASE "Goouuu-CAM"

// ===========================
// 视频参数配置 (统一宏定义, 方便调整)
// ===========================

// 推流分辨率, 修改此处即可全局生效
// 可选值: FRAMESIZE_QVGA(320x240) FRAMESIZE_VGA(640x480) FRAMESIZE_SVGA(800x600)
//         FRAMESIZE_XGA(1024x768)  FRAMESIZE_HD(1280x720)  FRAMESIZE_UXGA(1600x1200)
#define CAMERA_FRAME_SIZE   FRAMESIZE_VGA

// JPEG 质量 (0-63, 数值越低质量越高, 文件越大)
// 推荐范围: 8-15
#define CAMERA_JPEG_QUALITY 10

// 帧缓冲区数量 (利用 8MB PSRAM 可设为 3-4, 提高帧率和流畅度)
#define CAMERA_FB_COUNT     4

// ===========================
// 全局对象与变量
// ===========================

// NeoPixel LED 对象
Adafruit_NeoPixel pixels(PIX_NUM, PIN_PIXS, NEO_GRB + NEO_KHZ800);

// RTSP 服务器实例
RTSPServer rtspServer;

// 帧质量变量
int quality = 10;

// FreeRTOS 任务句柄
TaskHandle_t videoTaskHandle = NULL;

// ===========================
// LED 状态指示函数
// ===========================

/**
 * @brief 设置 NeoPixel LED 颜色
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 */
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

// ===========================
// WiFiManager 配网函数
// ===========================

/**
 * @brief 使用 WiFiManager 进行 WiFi 配网
 *
 * 流程:
 *   1. 尝试使用已保存的 WiFi 凭证连接
 *   2. 如果连接失败, 自动启动 AP 模式 (名称: Goouuu-CAM)
 *   3. 用户通过手机/电脑连接 AP 并在 Web 页面配置 WiFi
 *   4. 配置成功后自动连接目标 WiFi
 *
 * @return true  WiFi 连接成功
 * @return false WiFi 连接失败
 */
bool setupWiFi() {
  Serial.println("\n========== WiFi 配网 ==========");

  // 设置 LED 为蓝色, 表示正在配网
  setLED(0, 0, 64);

  // 生成 AP 名称: Goouuu-CAM-XXXX (XXXX 为 MAC 地址后 4 位十六进制)
  uint64_t mac = ESP.getEfuseMac();
  char apName[32];
  snprintf(apName, sizeof(apName), "%s-%04X", WIFI_AP_BASE,
           (uint16_t)(mac & 0xFFFF));

  // 创建 WiFiManager 实例
  WiFiManager wm;

  // 设置配网超时时间 (秒), 超时后重启设备
  wm.setConfigPortalTimeout(180);  // 3 分钟超时

  // 设置最小信号强度, 低于此值的 WiFi 不会显示
  wm.setMinimumSignalQuality(15);

  // 自动连接: 先尝试已保存的凭证, 失败则启动 AP 配网
  // 密码留空表示开放 AP, 无需密码即可连接
  Serial.printf("[WiFi] AP 名称: %s (开放热点, 无需密码)\n", apName);
  Serial.println("[WiFi] 正在连接 WiFi...");

  bool res = wm.autoConnect(apName);

  if (!res) {
    Serial.println("[WiFi] 连接失败! 3 分钟内未完成配网, 即将重启...");
    setLED(64, 0, 0);  // 红色表示失败
    delay(3000);
    ESP.restart();
    return false;
  }

  // WiFi 连接成功
  Serial.println("[WiFi] WiFi 连接成功!");
  Serial.printf("[WiFi] SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("[WiFi] IP 地址: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] 信号强度: %d dBm\n", WiFi.RSSI());

  // 设置 LED 为绿色, 表示 WiFi 已连接
  setLED(0, 64, 0);

  return true;
}

// ===========================
// 摄像头初始化函数
// ===========================

/**
 * @brief 初始化 OV2640 摄像头
 *
 * 配置摄像头参数, 包括:
 *   - 引脚映射 (来自 camera_pins.h)
 *   - 时钟频率 20MHz
 *   - JPEG 格式输出
 *   - PSRAM 帧缓冲区优化
 *
 * @return true  摄像头初始化成功
 * @return false 摄像头初始化失败
 */
bool setupCamera() {
  Serial.println("\n========== 摄像头初始化 ==========");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;           // 20MHz 时钟
  config.frame_size   = CAMERA_FRAME_SIZE;  // 统一宏定义的分辨率
  config.pixel_format = PIXFORMAT_JPEG;     // JPEG 格式 (适合推流)
  config.grab_mode    = CAMERA_GRAB_LATEST; // 获取最新帧, 减少延迟
  config.fb_location  = CAMERA_FB_IN_PSRAM;// 帧缓冲区存放在 PSRAM
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count     = CAMERA_FB_COUNT;

  // 根据 PSRAM 是否存在调整配置
  if (psramFound()) {
    Serial.printf("[Camera] 检测到 PSRAM (%d MB)\n", ESP.getPsramSize() / 1024 / 1024);
    // 8MB PSRAM 充分利用: 更多帧缓冲区 + 高质量
    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("[Camera] 未检测到 PSRAM, 降级为低配置");
    config.frame_size  = FRAMESIZE_QVGA;    // 无 PSRAM 强制 QVGA
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 2;                 // DRAM 有限, 减少缓冲区
  }

  // 初始化摄像头
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[Camera] 初始化失败! 错误码: 0x%x\n", err);
    return false;
  }

  // 获取传感器信息
  sensor_t* s = esp_camera_sensor_get();

  Serial.println("[Camera] 摄像头初始化成功!");
  Serial.printf("[Camera] 传感器 PID: 0x%02X\n", s->id.PID);
  Serial.printf("[Camera] 分辨率: %d, JPEG 质量: %d, 帧缓冲区: %d\n",
                CAMERA_FRAME_SIZE, CAMERA_JPEG_QUALITY, CAMERA_FB_COUNT);

  return true;
}

/**
 * @brief 获取并打印当前帧质量
 */
void getFrameQuality() {
  sensor_t* s = esp_camera_sensor_get();
  quality = s->status.quality;
  Serial.printf("[Camera] 帧质量: %d\n", quality);
}

// ===========================
// 视频推流任务 (FreeRTOS)
// ===========================

/**
 * @brief RTSP 视频帧发送任务
 *
 * 在 FreeRTOS 任务中持续循环:
 *   1. 检查 RTSP 服务器是否准备好发送帧
 *   2. 从摄像头获取一帧 JPEG 图像
 *   3. 通过 RTP 协议发送给所有连接的 RTSP 客户端
 *   4. 释放帧缓冲区
 *
 * @param pvParameters FreeRTOS 任务参数 (未使用)
 */
void sendVideo(void* pvParameters) {
  Serial.println("[RTSP] 视频推流任务已启动");

  while (true) {
    // 检查是否有客户端连接并准备好接收帧
    if (rtspServer.readyToSendFrame()) {
      // 从摄像头获取一帧图像
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        // 通过 RTSP/RTP 发送视频帧
        rtspServer.sendRTSPFrame(fb->buf, fb->len, quality, fb->width,
                                 fb->height);
        // 释放帧缓冲区, 必须调用!
        esp_camera_fb_return(fb);
      } else {
        Serial.println("[Camera] 获取帧失败!");
      }
    }
    // 短暂延时, 让出 CPU 时间
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ===========================
// 设备信息打印函数
// ===========================

/**
 * @brief 打印设备详细信息到串口
 *
 * 包括: 芯片信息、Flash/PSRAM 大小、网络信息、RTSP 服务器信息等
 */
void printDeviceInfo() {
  // 格式化大小的辅助函数
  auto fmtSize = [](size_t bytes) -> String {
    const char* sizes[] = {"B", "KB", "MB", "GB"};
    int order = 0;
    while (bytes >= 1024 && order < 3) {
      order++;
      bytes = bytes / 1024;
    }
    return String(bytes) + " " + sizes[order];
  };

  Serial.println("\n========== 设备信息 ==========");

  // 芯片信息
  Serial.println("--- 芯片信息 ---");
  Serial.printf("  芯片 MAC: %012llX\n", ESP.getEfuseMac());
  Serial.printf("  Flash 大小: %s\n", fmtSize(ESP.getFlashChipSize()).c_str());
  if (psramFound()) {
    Serial.printf("  PSRAM 大小: %s\n", fmtSize(ESP.getPsramSize()).c_str());
  } else {
    Serial.println("  PSRAM: 未检测到");
  }

  // 固件信息
  Serial.println("\n--- 固件信息 ---");
  Serial.printf("  固件大小: %s\n", fmtSize(ESP.getSketchSize()).c_str());
  Serial.printf("  可用空间: %s\n", fmtSize(ESP.getFreeSketchSpace()).c_str());

  // 网络信息
  Serial.println("\n--- 网络信息 ---");
  Serial.printf("  WiFi SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("  IP 地址: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  MAC 地址: %s\n", WiFi.macAddress().c_str());
  Serial.printf("  信号强度: %d dBm\n", WiFi.RSSI());

  // RTSP 服务器信息
  Serial.println("\n--- RTSP 服务器信息 ---");
  Serial.printf("  RTSP 端口: %d\n", rtspServer.rtspPort);
  Serial.printf("  传输类型: %d\n", rtspServer.transport);
  Serial.printf("  最大客户端数: %d\n", rtspServer.maxRTSPClients);

  // RTSP 播放地址
  Serial.println("\n========================================");
  Serial.printf("  >>> RTSP 地址: rtsp://%s:%d <<<\n",
                WiFi.localIP().toString().c_str(), rtspServer.rtspPort);
  Serial.println("  请使用 VLC 等播放器打开上述地址观看视频");
  Serial.println("========================================\n");

  // FreeRTOS 任务信息
  Serial.printf("  活跃任务数: %u\n", uxTaskGetNumberOfTasks() - 1);
  Serial.printf("  空闲堆内存: %s\n", fmtSize(ESP.getFreeHeap()).c_str());
}

// ===========================
// 主程序入口
// ===========================

void setup() {
  // 初始化串口
  Serial.begin(115200);
  delay(500);  // 等待串口稳定

  // 设置 ESP-IDF 日志级别
  esp_log_level_set("*", ESP_LOG_INFO);

  Serial.println("\n");
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   Goouuu-CAM RTSP 视频推流服务器     ║");
  Serial.println("║   ESP32-S3 + OV2640                  ║");
  Serial.println("╚══════════════════════════════════════╝");

  // 初始化 NeoPixel LED
  pixels.begin();
  pixels.setBrightness(6);
  pixels.clear();
  pixels.show();
  Serial.println("[System] NeoPixel LED 初始化完成");

  // 步骤 1: WiFiManager 配网
  if (!setupWiFi()) {
    // WiFi 连接失败 (WiFiManager 内部会重启)
    setLED(64, 0, 0);
    while (true) {
      delay(1000);
    }
  }

  // 步骤 2: 初始化摄像头
  setLED(0, 0, 64);  // 蓝色: 正在初始化摄像头
  if (!setupCamera()) {
    Serial.println("[Camera] 摄像头初始化失败! 请检查硬件连接.");
    setLED(64, 0, 0);  // 红色: 错误
    while (true) {
      delay(1000);  // 停止运行
    }
  }
  getFrameQuality();

  // 步骤 3: 配置并启动 RTSP 服务器
  Serial.println("\n========== RTSP 服务器启动 ==========");

  // 设置 RTSP 认证 (留空表示无需认证)
  rtspServer.setCredentials("", "");

  // 设置最大客户端数 (仅对多播模式有效)
  rtspServer.maxRTSPClients = 5;

  // 初始化 RTSP 服务器 (使用默认配置: VIDEO_ONLY, 端口 554)
  if (rtspServer.init()) {
    Serial.printf("[RTSP] RTSP 服务器启动成功!\n");
    Serial.printf("[RTSP] 地址: rtsp://%s:554/\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[RTSP] RTSP 服务器启动失败!");
    setLED(64, 0, 0);
    while (true) {
      delay(1000);
    }
  }

  // 步骤 4: 创建视频推流 FreeRTOS 任务
  // 优先级 9 (高优先级, 确保视频流畅)
  xTaskCreate(sendVideo, "Video", 8192, NULL, 9, &videoTaskHandle);
  Serial.println("[System] 视频推流任务已创建");

  // 步骤 5: 打印设备信息
  printDeviceInfo();

  // LED 绿色常亮: 一切正常, 正在推流
  setLED(0, 128, 0);

  // 释放 loop() 任务, 节省约 8KB RAM
  // 注意: 执行此操作后 loop() 不会再被调用
  Serial.println("[System] 初始化完成, loop 任务即将删除以释放内存...");
  delay(100);
  vTaskDelete(NULL);
}

/**
 * @brief Arduino 主循环 (初始化完成后会被删除以释放内存)
 *
 * 由于 setup() 末尾调用了 vTaskDelete(NULL),
 * 此函数在正常运行时不会被执行.
 * 视频推流由 FreeRTOS 任务 sendVideo() 负责.
 */
void loop() {
  // 此函数不会被调用 (已被删除)
  vTaskDelete(NULL);
}
