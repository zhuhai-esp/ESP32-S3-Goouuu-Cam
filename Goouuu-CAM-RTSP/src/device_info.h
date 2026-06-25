/**
 * @file device_info.h
 * @brief 设备信息打印 (芯片、内存、网络、RTSP 服务器信息)
 */

#pragma once
#include "rtsp_stream.h"
#include "camera_manager.h"
#include "network_quality.h"

/**
 * @brief 打印完整设备信息到串口
 * @param rtsp RTSP 推流管理器
 * @param cam 摄像头管理器
 * @param netQ 网络质量监控器
 */
void printDeviceInfo(RTSPStream& rtsp, CameraManager& cam,
                     NetworkQuality& netQ);
