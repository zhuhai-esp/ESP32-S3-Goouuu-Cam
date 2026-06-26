/**
 * @file camera_pins.h
 * @brief ESP32-S3 与 GC2145 摄像头模组的 GPIO 引脚定义
 * 
 * 本文件定义了 Goouuu-CAM 开发板上摄像头模组的所有硬件连接引脚。
 * 适用于 ESP32-S3 + GC2145 摄像头模组的引脚映射。
 */

// 摄像头电源控制引脚（未使用，设为 -1）
#define PWDN_GPIO_NUM -1   // 摄像头电源关闭控制（低电平有效）
#define RESET_GPIO_NUM -1  // 摄像头复位控制（低电平有效）

// 摄像头时钟和控制引脚
#define XCLK_GPIO_NUM 15   // 摄像头外部时钟输入（14MHz）
#define SIOD_GPIO_NUM 4    // SCCB 数据线（I2C SDA）
#define SIOC_GPIO_NUM 5    // SCCB 时钟线（I2C SCL）

// 摄像头 8 位并行数据总线（D0-D7）
#define Y2_GPIO_NUM 11     // 数据位 D0
#define Y3_GPIO_NUM 9      // 数据位 D1
#define Y4_GPIO_NUM 8      // 数据位 D2
#define Y5_GPIO_NUM 10     // 数据位 D3
#define Y6_GPIO_NUM 12     // 数据位 D4
#define Y7_GPIO_NUM 18     // 数据位 D5
#define Y8_GPIO_NUM 17     // 数据位 D6
#define Y9_GPIO_NUM 16     // 数据位 D7

// 摄像头同步信号引脚
#define VSYNC_GPIO_NUM 6   // 垂直同步信号（帧起始标志）
#define HREF_GPIO_NUM 7    // 水平参考信号（行有效标志）
#define PCLK_GPIO_NUM 13   // 像素时钟（数据采样时钟）