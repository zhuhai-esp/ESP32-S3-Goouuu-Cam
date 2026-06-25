/**
 * @file rtsp_stream.h
 * @brief RTSP 视频推流管理器: 服务器生命周期 + 推流任务 + 看门狗 + 动态码率
 */

#pragma once
#include <ESP32-RTSPServer.h>
#include "camera_manager.h"
#include "network_quality.h"
#include "watchdog_manager.h"
#include "led_controller.h"
#include "config.h"

class RTSPStream {
public:
  RTSPStream(CameraManager& cam, NetworkQuality& netQ, WatchdogManager& wdt,
             LEDController& led);

  /// @brief 配置并启动 RTSP 服务器
  /// @return true 成功, false 失败
  bool begin();

  /// @brief 启动视频推流 FreeRTOS 任务
  void startVideoTask();

  /// @brief 获取 RTSP 服务器引用 (供 device_info 查询)
  RTSPServer& getServer();

private:
  RTSPServer _server;
  CameraManager&    _cam;
  NetworkQuality&   _netQ;
  WatchdogManager&  _wdt;
  LEDController&    _led;

  TaskHandle_t _videoTaskHandle = NULL;

  /// @brief 推流任务主循环
  void videoLoop();

  /// @brief FreeRTOS 任务入口 (静态)
  static void videoTaskEntry(void* pvParameters);
};
