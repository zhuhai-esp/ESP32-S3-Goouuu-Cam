/**
 * @file device_info.cpp
 * @brief 设备信息打印实现
 */

#include <Arduino.h>
#include "device_info.h"

// 格式化字节大小为可读字符串
static String fmtSize(size_t bytes) {
  const char* sizes[] = {"B", "KB", "MB", "GB"};
  int order = 0;
  while (bytes >= 1024 && order < 3) {
    order++;
    bytes /= 1024;
  }
  return String(bytes) + " " + sizes[order];
}

void printDeviceInfo(RTSPStream& rtsp, CameraManager& cam,
                     NetworkQuality& netQ) {
  RTSPServer& srv = rtsp.getServer();

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

  // 摄像头信息
  Serial.println("\n--- 摄像头信息 ---");
  int w, h;
  cam.getResolution(w, h);
  Serial.printf("  分辨率: %dx%d\n", w, h);
  Serial.printf("  JPEG 质量: %d\n", cam.getQuality());

  // RTSP 服务器信息
  Serial.println("\n--- RTSP 服务器信息 ---");
  Serial.printf("  RTSP 端口: %d\n", srv.rtspPort);
  Serial.printf("  传输类型: %d\n", srv.transport);
  Serial.printf("  最大客户端数: %d\n", srv.maxRTSPClients);
  Serial.printf("  当前帧率: %u fps\n", srv.rtpFps);

  // RTSP 播放地址
  Serial.println("\n========================================");
  Serial.printf("  >>> RTSP 地址: rtsp://%s:%d <<<\n",
                WiFi.localIP().toString().c_str(), srv.rtspPort);
  Serial.println("  请使用 VLC 等播放器打开上述地址观看视频");
  Serial.println("========================================\n");

  // 系统信息
  Serial.printf("  活跃任务数: %u\n", uxTaskGetNumberOfTasks() - 1);
  Serial.printf("  空闲堆内存: %s\n", fmtSize(ESP.getFreeHeap()).c_str());
}
