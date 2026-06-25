/**
 * @file wifi_connector.h
 * @brief WiFi 连接管理器: 自动配网、断线检测、超时重启
 *
 * 注意: 不使用 WiFi.onEvent() 事件回调, 改用轮询 WiFi.status().
 * 原因: WiFiManager 析构时调用 WiFi.removeEvent() 会修改 lwIP 事件
 *       处理链表, 若同时注册了其他事件回调, 可能导致 TCP/IP 线程
 *       在 igmp_lookfor_group 中空指针崩溃.
 */

#pragma once
#include <WiFi.h>
#include "config.h"
#include "led_controller.h"

class WiFiConnector {
public:
  /// @param led LED 控制器引用, 用于状态指示
  explicit WiFiConnector(LEDController& led);

  /// @brief 初始化 WiFi: WiFiManager 配网
  /// @return true 连接成功, false 失败 (内部会重启)
  bool begin();

  /// @brief 检查 WiFi 是否已连接
  bool isConnected() const;

  /// @brief 周期调用: 轮询检测断线, 超时则重启设备
  void checkConnection();

private:
  LEDController& _led;
  unsigned long _disconnectTime = 0;  // 断线时刻 (millis), 0=未断线
  bool _wasConnected = false;
  bool _wasDisconnected = false;      // 当前是否处于断线状态
};
