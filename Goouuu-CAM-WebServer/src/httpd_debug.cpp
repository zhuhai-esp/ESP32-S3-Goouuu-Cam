/**
 * @file httpd_debug.cpp
 * @brief 调试/高级功能模块
 * 
 * 本文件提供 5 个调试用 HTTP 处理器：
 * 1. xclk_handler - 动态调整摄像头外部时钟频率
 * 2. reg_handler - 直接写入摄像头传感器寄存器
 * 3. greg_handler - 直接读取摄像头传感器寄存器
 * 4. pll_handler - 配置摄像头 PLL 锁相环参数
 * 5. win_handler - 配置摄像头裁剪窗口
 * 
 * 这些接口主要用于调试和高级配置，普通用户不需要使用。
 */

#include "camera_httpd.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "httpd_debug";

// ── 辅助函数：获取传感器对象 ─────────────────────────────────────────────────

/**
 * @brief 获取摄像头传感器对象
 * 
 * 如果传感器不存在，自动发送 500 错误响应。
 * 用于消除重复的 null 检查代码。
 * 
 * @param req HTTP 请求句柄
 * @return 传感器对象指针，失败返回 NULL
 */
static inline sensor_t *get_sensor_or_500(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) httpd_resp_send_500(req);
    return s;
}

// ── 外部时钟频率设置处理器 ───────────────────────────────────────────────────

/**
 * @brief 外部时钟频率设置处理器
 * 
 * 动态调整摄像头外部时钟频率（XCLK）。
 * 请求格式：/xclk?xclk=<频率 MHz>
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t xclk_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char xclk_str[32];

    // 解析请求参数
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
    if (httpd_query_key_value(buf, "xclk", xclk_str, sizeof(xclk_str)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int xclk = atoi(xclk_str);
    ESP_LOGI(TAG, "设置 XCLK: %d MHz", xclk);

    // 获取传感器对象
    sensor_t *s = get_sensor_or_500(req);
    if (!s) return ESP_FAIL;

    // 设置时钟频率
    int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
    if (res) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

// ── 寄存器写入处理器 ─────────────────────────────────────────────────────────

/**
 * @brief 寄存器写入处理器
 * 
 * 直接写入摄像头传感器寄存器。
 * 请求格式：/reg?reg=<地址>&mask=<掩码>&val=<值>
 * 
 * 注意：此接口用于调试，错误的寄存器写入可能导致摄像头异常。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t reg_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char reg_str[32], mask_str[32], val_str[32];

    // 解析请求参数
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
    if (httpd_query_key_value(buf, "reg", reg_str, sizeof(reg_str)) != ESP_OK ||
        httpd_query_key_value(buf, "mask", mask_str, sizeof(mask_str)) != ESP_OK ||
        httpd_query_key_value(buf, "val", val_str, sizeof(val_str)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    // 转换参数
    int reg  = atoi(reg_str);
    int mask = atoi(mask_str);
    int val  = atoi(val_str);
    ESP_LOGI(TAG, "写入寄存器: 0x%02x, 掩码: 0x%02x, 值: 0x%02x", reg, mask, val);

    // 获取传感器对象
    sensor_t *s = get_sensor_or_500(req);
    if (!s) return ESP_FAIL;

    // 写入寄存器
    int res = s->set_reg(s, reg, mask, val);
    if (res) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

// ── 寄存器读取处理器 ─────────────────────────────────────────────────────────

/**
 * @brief 寄存器读取处理器
 * 
 * 直接读取摄像头传感器寄存器。
 * 请求格式：/greg?reg=<地址>&mask=<掩码>
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t greg_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char reg_str[32], mask_str[32];

    // 解析请求参数
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
    if (httpd_query_key_value(buf, "reg", reg_str, sizeof(reg_str)) != ESP_OK ||
        httpd_query_key_value(buf, "mask", mask_str, sizeof(mask_str)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    // 转换参数
    int reg  = atoi(reg_str);
    int mask = atoi(mask_str);

    // 获取传感器对象
    sensor_t *s = get_sensor_or_500(req);
    if (!s) return ESP_FAIL;

    // 读取寄存器
    int res = s->get_reg(s, reg, mask);
    if (res < 0) return httpd_resp_send_500(req);
    ESP_LOGI(TAG, "读取寄存器: 0x%02x, 掩码: 0x%02x, 值: 0x%02x", reg, mask, res);

    // 返回结果
    char buffer[20];
    itoa(res, buffer, 10);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buffer, strlen(buffer));
}

// ── PLL 锁相环配置处理器 ─────────────────────────────────────────────────────

/**
 * @brief PLL 锁相环配置处理器
 * 
 * 配置摄像头 PLL 锁相环参数。
 * 请求格式：/pll?bypass=<>&mul=<>&sys=<>&root=<>&pre=<>&seld5=<>&pclken=<>&pclk=<>
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t pll_handler(httpd_req_t *req)
{
    char *buf = NULL;
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

    // 解析 PLL 参数
    int bypass = parse_get_var(buf, "bypass", 0);
    int mul    = parse_get_var(buf, "mul",    0);
    int sys    = parse_get_var(buf, "sys",    0);
    int root   = parse_get_var(buf, "root",   0);
    int pre    = parse_get_var(buf, "pre",    0);
    int seld5  = parse_get_var(buf, "seld5",  0);
    int pclken = parse_get_var(buf, "pclken", 0);
    int pclk   = parse_get_var(buf, "pclk",   0);
    free(buf);

    ESP_LOGI(TAG, "设置 PLL: bypass:%d mul:%d sys:%d root:%d pre:%d seld5:%d pclken:%d pclk:%d",
             bypass, mul, sys, root, pre, seld5, pclken, pclk);

    // 获取传感器对象
    sensor_t *s = get_sensor_or_500(req);
    if (!s) return ESP_FAIL;

    // 设置 PLL 参数
    int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
    if (res) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

// ── 窗口配置处理器 ───────────────────────────────────────────────────────────

/**
 * @brief 窗口配置处理器
 * 
 * 配置摄像头裁剪窗口参数。
 * 请求格式：/resolution?sx=<>&sy=<>&ex=<>&ey=<>&offx=<>&offy=<>&tx=<>&ty=<>&ox=<>&oy=<>&scale=<>&binning=<>
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t win_handler(httpd_req_t *req)
{
    char *buf = NULL;
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

    // 解析窗口参数
    int startX    = parse_get_var(buf, "sx", 0);       // 起始 X 坐标
    int startY    = parse_get_var(buf, "sy", 0);       // 起始 Y 坐标
    int endX      = parse_get_var(buf, "ex", 0);       // 结束 X 坐标
    int endY      = parse_get_var(buf, "ey", 0);       // 结束 Y 坐标
    int offsetX   = parse_get_var(buf, "offx", 0);     // X 偏移
    int offsetY   = parse_get_var(buf, "offy", 0);     // Y 偏移
    int totalX    = parse_get_var(buf, "tx", 0);       // 总 X 像素
    int totalY    = parse_get_var(buf, "ty", 0);       // 总 Y 像素
    int outputX   = parse_get_var(buf, "ox", 0);       // 输出 X 分辨率
    int outputY   = parse_get_var(buf, "oy", 0);       // 输出 Y 分辨率
    bool scale    = parse_get_var(buf, "scale", 0) == 1;    // 缩放模式
    bool binning  = parse_get_var(buf, "binning", 0) == 1;  // 合并模式
    free(buf);

    ESP_LOGI(TAG, "设置窗口: 起始:%d,%d 结束:%d,%d 偏移:%d,%d 总计:%d,%d 输出:%d,%d 缩放:%u 合并:%u",
             startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);

    // 获取传感器对象
    sensor_t *s = get_sensor_or_500(req);
    if (!s) return ESP_FAIL;

    // 设置窗口参数
    int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
    if (res) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}
