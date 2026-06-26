/**
 * @file main.cpp
 * @brief ESP32-S3 摄像头 Web 服务器主程序
 * 
 * 本文件是程序的主入口，负责：
 * 1. 初始化 NeoPixel RGB LED 用于状态指示
 * 2. 初始化 GC2145 摄像头（RGB565 格式，QVGA 分辨率）
 * 3. 通过 WiFiManager 进行 WiFi 配网
 * 4. 启动摄像头 Web 服务器
 * 
 * LED 状态指示：
 * - 红色：WiFi 配置中/出错
 * - 绿色：WiFi 连接成功
 * - 熄灭：正常运行
 */

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <string.h>
#include <stdlib.h>

#include "camera_pins.h"
#include "esp_camera.h"

// ── 硬件配置常量 ─────────────────────────────────────────────────────────────

#define PIN_PIXS 48   // NeoPixel LED 数据引脚（ESP32-S3 板载 RGB LED）
#define PIX_NUM 1     // LED 数量（单颗 RGB LED）

// ── 全局对象 ─────────────────────────────────────────────────────────────────

Adafruit_NeoPixel pixels(PIX_NUM, PIN_PIXS, NEO_GRB + NEO_KHZ800);  // RGB LED 控制对象
WiFiManager wm;                                                       // WiFi 配置管理器

// ── 外部函数声明 ─────────────────────────────────────────────────────────────

void startCameraServer();  // 启动摄像头 Web 服务器（定义在 httpd_server.cpp）

// ── LED 状态显示函数 ─────────────────────────────────────────────────────────

/**
 * @brief 设置 RGB LED 颜色
 * @param c 颜色值（RGB 格式，如 0xFF0000 表示红色）
 */
static void showPixelColor(uint32_t c) {
  pixels.setPixelColor(0, c);
  pixels.show();
}

// ── WiFi 配置函数 ────────────────────────────────────────────────────────────

/**
 * @brief 启动 WiFi 配网流程
 * 
 * 使用 WiFiManager 进行 WiFi 配置：
 * 1. LED 显示红色表示进入配网模式
 * 2. 启动 WiFi 配置门户（超时 180 秒）
 * 3. 配网成功后 LED 显示绿色
 * 4. 超时未配置则自动重启设备
 */
static void startWifiConfig() {
  showPixelColor(0xFF0000);  // 红色：进入 WiFi 配置模式
  wm.setConfigPortalTimeout(180);  // 配网超时时间：180 秒
  wm.setConfigPortalTimeoutCallback([]() { ESP.restart(); });  // 超时重启
  auto res = wm.autoConnect();  // 尝试自动连接已保存的 WiFi
  if (!res) {
    ESP.restart();  // 连接失败则重启
  }
  showPixelColor(0x00FF00);  // 绿色：WiFi 连接成功
  Serial.println(WiFi.localIP().toString().c_str());  // 打印 IP 地址
}

// ── 摄像头初始化函数 ─────────────────────────────────────────────────────────

/**
 * @brief 初始化 GC2145 摄像头
 * 
 * 配置摄像头参数：
 * - 像素格式：RGB565（GC2145 不支持硬件 JPEG）
 * - 分辨率：QVGA（320x240）
 * - 时钟频率：14MHz（GC2145 稳定工作频率）
 * - 帧缓冲：2 帧（PSRAM 中）
 * - 抓取模式：CAMERA_GRAB_LATEST（获取最新帧）
 */
static void initCamera() {
  camera_config_t config;
  // LEDC 通道配置（用于生成摄像头时钟）
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  
  // 摄像头数据引脚配置（8 位并行数据总线）
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  
  // 摄像头控制引脚配置
  config.pin_xclk = XCLK_GPIO_NUM;    // 外部时钟
  config.pin_pclk = PCLK_GPIO_NUM;    // 像素时钟
  config.pin_vsync = VSYNC_GPIO_NUM;  // 垂直同步
  config.pin_href = HREF_GPIO_NUM;    // 水平参考
  config.pin_sccb_sda = SIOD_GPIO_NUM;  // SCCB 数据线
  config.pin_sccb_scl = SIOC_GPIO_NUM;  // SCCB 时钟线
  config.pin_pwdn = PWDN_GPIO_NUM;    // 电源控制
  config.pin_reset = RESET_GPIO_NUM;  // 复位控制
  
  // 摄像头功能参数配置
  config.xclk_freq_hz = 14000000;      // 外部时钟频率：14MHz（GC2145 稳定工作频率）
  config.frame_size = FRAMESIZE_QVGA;  // 分辨率：QVGA（320x240）
  config.pixel_format = PIXFORMAT_RGB565;  // 像素格式：RGB565（GC2145 不支持硬件 JPEG）
  config.grab_mode = CAMERA_GRAB_LATEST;   // 抓取模式：获取最新帧
  config.fb_location = CAMERA_FB_IN_PSRAM; // 帧缓冲位置：PSRAM
  config.jpeg_quality = 12;                // JPEG 质量（仅 JPEG 格式有效）
  config.fb_count = 2;                     // 帧缓冲数量：2 帧
  
  // 初始化摄像头
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("摄像头初始化失败，错误代码: 0x%x\n", err);
    showPixelColor(0xFF0000);  // 红色：初始化错误
    delay(1000);  // 等待串口输出完成
    ESP.restart();  // 重启设备（与 demo 项目保持一致）
  }
  
  // 获取传感器对象并配置默认参数
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);       // 垂直翻转（根据摄像头安装方向调整）
    s->set_brightness(s, 1);  // 亮度 +1（略微提高亮度）
    s->set_saturation(s, 0);  // 饱和度 0（默认值）
  } else {
    Serial.println("获取摄像头传感器失败");
  }
}

// ── LED 初始化函数 ───────────────────────────────────────────────────────────

/**
 * @brief 初始化 NeoPixel RGB LED
 * 
 * 初始化 LED 并设置默认亮度，然后关闭 LED。
 */
static void initPixels() {
  pixels.begin();           // 初始化 LED
  pixels.setBrightness(8);  // 设置亮度（0-255）
  showPixelColor(0x0);      // 关闭 LED
}

// ── 程序入口点 ───────────────────────────────────────────────────────────────

/**
 * @brief Arduino setup 函数（程序启动时执行一次）
 * 
 * 初始化流程：
 * 1. 初始化串口通信（115200 波特率）
 * 2. 初始化 RGB LED
 * 3. 初始化摄像头
 * 4. 进行 WiFi 配网
 * 5. 启动摄像头 Web 服务器
 */
void setup() {
  Serial.begin(115200);  // 初始化串口通信
  initPixels();          // 初始化 RGB LED
  initCamera();          // 初始化摄像头
  startWifiConfig();     // WiFi 配网
  startCameraServer();   // 启动 Web 服务器
  Serial.print("摄像头就绪！请访问 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' 连接设备");
}

/**
 * @brief Arduino loop 函数（程序主循环）
 * 
 * 主循环中不做任何操作，所有功能由 HTTP 服务器处理。
 * 使用 delay 避免看门狗超时。
 */
void loop() { delay(10000); }