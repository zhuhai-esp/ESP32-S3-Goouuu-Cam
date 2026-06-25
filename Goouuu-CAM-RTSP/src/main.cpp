/**
 * @file main.cpp
 * @brief Goouuu-CAM ESP32-S3 RTSP Video Streaming Server — 主入口
 *
 * 基于 ESP32-S3 的摄像头 RTSP 视频推流服务器
 *
 * 模块架构:
 *   config.h            — 全局配置 (引脚、参数、阈值)
 *   led_controller      — NeoPixel LED 状态指示 (含闪烁)
 *   watchdog_manager    — 硬件看门狗, 防止任务卡死
 *   camera_manager      — OV2640 摄像头初始化、帧获取、动态质量调整
 *   network_quality     — 网络质量监控 (RSSI + FPS), 动态码率推荐
 *   wifi_connector      — WiFiManager 配网、断线检测、超时重启
 *   rtsp_stream         — RTSP 服务器生命周期 + 推流任务
 *   device_info         — 设备信息打印
 *   main.cpp            — 编排所有模块
 *
 * 硬件: Goouuu-CAM (ESP32-S3 + OV2640 + PSRAM)
 */

#include <Arduino.h>
#include <esp_log.h>

#include "config.h"
#include "led_controller.h"
#include "watchdog_manager.h"
#include "camera_manager.h"
#include "network_quality.h"
#include "wifi_connector.h"
#include "rtsp_stream.h"
#include "device_info.h"

// ===========================
// 全局模块实例
// ===========================
LEDController     led;
WatchdogManager   watchdog;
CameraManager     camera;
NetworkQuality    networkQuality;
WiFiConnector     wifi(led);
RTSPStream        rtsp(camera, networkQuality, watchdog, led);

// WiFi 监控任务句柄
TaskHandle_t monitorTaskHandle = NULL;

// ===========================
// WiFi 断线监控任务
// ===========================
void monitorTask(void* pvParameters) {
  Serial.println("[Monitor] WiFi 监控任务已启动");

  while (true) {
    wifi.checkConnection();
    led.update();  // 同时驱动 LED 闪烁
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ===========================
// 主程序入口
// ===========================
void setup() {
  // --- 串口 ---
  Serial.begin(115200);
  delay(500);
  esp_log_level_set("*", ESP_LOG_INFO);

  Serial.println("\n");
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   Goouuu-CAM RTSP 视频推流服务器     ║");
  Serial.println("║   ESP32-S3 + OV2640                  ║");
  Serial.println("╚══════════════════════════════════════╝");

  // --- LED ---
  led.begin();
  led.setState(LEDState::BOOTING);

  // --- 看门狗 ---
  watchdog.begin();

  // --- WiFi 配网 ---
  if (!wifi.begin()) {
    led.setState(LEDState::ERROR);
    delay(3000);
    ESP.restart();
  }

  // --- 摄像头 ---
  led.setState(LEDState::BOOTING);
  if (!camera.begin()) {
    Serial.println("[Camera] 初始化失败! 5 秒后重启...");
    led.setState(LEDState::ERROR);
    delay(5000);
    ESP.restart();
  }

  // --- 网络质量监控 ---
  networkQuality.begin();

  // --- RTSP 服务器 ---
  if (!rtsp.begin()) {
    // rtsp.begin() 内部失败会重启, 此处不会到达
    return;
  }

  // --- 启动推流任务 ---
  rtsp.startVideoTask();

  // --- 启动 WiFi 监控任务 (低优先级) ---
  xTaskCreate(monitorTask, "Monitor", 4096, NULL, 1, &monitorTaskHandle);

  // --- 打印设备信息 ---
  printDeviceInfo(rtsp, camera, networkQuality);

  // --- 正常运行: LED 绿色 ---
  led.setState(LEDState::STREAMING);

  // --- 释放 Arduino loop 任务, 节省 ~8KB RAM ---
  Serial.println("[System] 初始化完成, 删除 loop 任务以释放内存...");
  delay(100);
  vTaskDelete(NULL);
}

/**
 * @brief Arduino 主循环 (已被删除, 不会执行)
 *
 * WiFi 监控由 monitorTask() 负责
 * 视频推流由 RTSPStream::videoLoop() 负责
 */
void loop() {
  vTaskDelete(NULL);
}
