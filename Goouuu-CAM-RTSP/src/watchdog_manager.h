/**
 * @file watchdog_manager.h
 * @brief 硬件看门狗管理器, 防止任务卡死导致设备假死
 */

#pragma once
#include <esp_task_wdt.h>
#include "config.h"

class WatchdogManager {
public:
  /// @brief 初始化看门狗, 超时时间由 WDT_TIMEOUT_SEC 定义
  void begin();

  /// @brief 将当前任务加入看门狗监控
  void addCurrentTask();

  /// @brief 喂狗, 必须在超时前调用
  void feed();
};
