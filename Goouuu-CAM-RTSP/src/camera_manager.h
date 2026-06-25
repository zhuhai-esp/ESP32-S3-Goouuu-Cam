/**
 * @file camera_manager.h
 * @brief 摄像头管理器: 初始化、帧获取、软件 JPEG 编码
 */

#pragma once
#include "esp_camera.h"
#include "img_converters.h"
#include "config.h"

class CameraManager {
public:
  /// @brief 初始化摄像头, 自动检测传感器类型和 JPEG 能力
  /// @return true 成功, false 失败
  bool begin();

  /// @brief 获取一帧图像 (JPEG 或原始格式)
  /// @return camera_fb_t 指针, 用完必须调用 returnFrame()
  camera_fb_t* getFrame();

  /// @brief 释放帧缓冲区
  void returnFrame(camera_fb_t* fb);

  /// @brief 将原始格式帧编码为 JPEG (软件编码)
  /// @param fb 原始帧缓冲区
  /// @param out 输出 JPEG 数据指针 (调用方需 free())
  /// @param outLen 输出 JPEG 数据长度
  /// @return true 成功
  bool encodeJpeg(camera_fb_t* fb, uint8_t** out, size_t* outLen);

  /// @brief 获取传感器句柄
  sensor_t* getSensor();

  /// @brief 动态设置 JPEG 质量 (0-63, 越低质量越高)
  void setQuality(int quality);

  /// @brief 获取当前 JPEG 质量
  int getQuality() const;

  /// @brief 获取当前分辨率信息
  void getResolution(int& width, int& height) const;

  /// @brief 传感器是否支持硬件 JPEG 编码
  bool isJpegSupported() const;

  /// @brief 像素格式字符串描述
  const char* getPixelFormatStr() const;

private:
  int _quality = CAMERA_JPEG_QUALITY;
  int _width = 0;
  int _height = 0;
  bool _jpegSupported = false;        // 硬件 JPEG
  pixformat_t _pixelFormat = PIXFORMAT_JPEG;
};
