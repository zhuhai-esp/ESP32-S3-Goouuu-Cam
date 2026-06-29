/**
 * @file camera_httpd.h
 * @brief 摄像头 HTTP 服务器总头文件
 * 
 * 本文件是项目的中心声明头文件，包含：
 * - HTTP 服务器句柄声明
 * - 嵌入式 HTML 资源引用
 * - MJPEG 流常量定义
 * - 共享数据结构
 * - 所有 HTTP 处理模块的函数前向声明
 */

#ifndef CAMERA_HTTPD_H
#define CAMERA_HTTPD_H

#include "esp_http_server.h"
#include "esp_camera.h"
#include <string.h>
#include <stdlib.h>

// ── HTTP 服务器句柄 ──────────────────────────────────────────────────────────
// 控制服务器（端口80）和流媒体服务器（端口81）

extern httpd_handle_t stream_httpd;   // MJPEG 流媒体服务器句柄
extern httpd_handle_t camera_httpd;   // 摄像头控制服务器句柄

// 摄像头重入保护（防止流媒体/捕获在重新初始化期间访问摄像头）
extern volatile bool camera_reinit_active;

// ── 嵌入式 HTML 文件 ─────────────────────────────────────────────────────────
// 通过 PlatformIO 的 board_build.embed_files 功能将 HTML 嵌入到固件中

extern const uint8_t index_html_start[] asm("_binary_src_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_src_index_html_end");

// ── MJPEG 流媒体常量 ─────────────────────────────────────────────────────────
// 定义 multipart HTTP 响应的边界和格式

#define PART_BOUNDARY "123456789000000000000987654321"
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY
#define STREAM_BOUNDARY "\r\n--" PART_BOUNDARY "\r\n"
#define STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n"

// ── 共享数据结构 ─────────────────────────────────────────────────────────────

// JPEG 分块编码上下文（用于流式 JPEG 转换）
typedef struct {
    httpd_req_t *req;   // HTTP 请求句柄
    size_t len;         // 已发送数据长度
} jpg_chunking_t;

// 滑动平均滤波器（用于计算平均帧率）
typedef struct {
    size_t size;        // 采样窗口大小
    size_t index;       // 当前写入位置
    size_t count;       // 已收集样本数
    int sum;            // 当前总和
    int *values;        // 采样值数组
} ra_filter_t;

// ── 公共工具函数 (httpd_common.cpp) ──────────────────────────────────────────
// 所有 HTTP 处理器共享的底层功能

extern ra_filter_t ra_filter;  // 全局帧率滤波器

ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size);  // 初始化滤波器
int ra_filter_run(ra_filter_t *filter, int value);                     // 运行滤波器
esp_err_t parse_get(httpd_req_t *req, char **obuf);                    // 解析 GET 请求
int parse_get_var(char *buf, const char *key, int def);                // 获取 GET 参数值
size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len);  // JPEG 编码回调

// ── HTTP 处理器声明 ──────────────────────────────────────────────────────────

// 首页和服务器启动 (httpd_server.cpp)
esp_err_t index_handler(httpd_req_t *req);   // 首页 HTML 服务
void startCameraServer();                    // 启动摄像头 Web 服务器
bool start_stream_server();                  // 启动流媒体服务器（端口 81），返回是否成功

// WebSocket 通信 (httpd_ws.cpp)
esp_err_t ws_handler(httpd_req_t *req);      // WebSocket 连接处理
void ws_broadcast_status(httpd_handle_t hd); // 广播状态到所有客户端
void ws_start_status_push(httpd_handle_t hd); // 启动定时状态推送
void ws_load_saved_settings();               // 加载 NVS 中保存的设置

// 图像捕获 (httpd_capture.cpp)
esp_err_t capture_handler(httpd_req_t *req); // JPEG 图像捕获
esp_err_t bmp_handler(httpd_req_t *req);     // BMP 图像捕获

// MJPEG 流媒体 (httpd_stream.cpp)
esp_err_t stream_handler(httpd_req_t *req);  // MJPEG 视频流

// 摄像头控制 (httpd_control.cpp)
esp_err_t cmd_handler(httpd_req_t *req);     // 摄像头参数设置
esp_err_t status_handler(httpd_req_t *req);  // 摄像头状态查询
int build_status_json(char *buf, size_t max_len);  // 构建状态 JSON
int apply_sensor_setting(const char *var, int val); // 应用传感器设置
void gc2145_patch_sensor();                        // GC2145 寄存器级补丁（替换 set_dummy）

// 调试功能 (httpd_debug.cpp)
esp_err_t xclk_handler(httpd_req_t *req);    // 外部时钟频率设置
esp_err_t reg_handler(httpd_req_t *req);     // 寄存器写入
esp_err_t greg_handler(httpd_req_t *req);    // 寄存器读取
esp_err_t pll_handler(httpd_req_t *req);     // PLL 锁相环配置
esp_err_t win_handler(httpd_req_t *req);     // 摄像头窗口配置

#endif // CAMERA_HTTPD_H