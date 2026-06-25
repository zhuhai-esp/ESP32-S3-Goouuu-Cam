/**
 * @file network_quality.h
 * @brief 网络质量监控与动态码率调整
 *
 * 根据 WiFi 信号强度 (RSSI) 和 RTSP 实际帧率 (rtpFps)
 * 动态调整摄像头 JPEG 质量, 在弱网环境下降低码率保证流畅性.
 */

#pragma once
#include <WiFi.h>
#include "config.h"

class NetworkQuality {
public:
  /// @brief 开始网络质量监控
  void begin();

  /// @brief 周期调用: 采集 RSSI + FPS, 计算推荐质量
  /// @param currentFps 当前 RTSP 实际帧率 (由 RTSPServer.rtpFps 提供)
  void update(uint32_t currentFps);

  /// @brief 获取推荐 JPEG 质量 (0-63, 越低质量越高)
  int getRecommendedQuality() const;

  /// @brief 获取当前 RSSI (dBm)
  int getRSSI() const;

  /// @brief 网络是否较差 (RSSI < RSSI_FAIR 或 FPS 过低)
  bool isPoor() const;

private:
  int _rssi = 0;
  int _recommendedQuality = CAMERA_JPEG_QUALITY;
  uint32_t _lastUpdate = 0;

  /// @brief 根据 RSSI 和 FPS 计算目标质量
  int computeQuality(int rssi, uint32_t fps) const;
};
