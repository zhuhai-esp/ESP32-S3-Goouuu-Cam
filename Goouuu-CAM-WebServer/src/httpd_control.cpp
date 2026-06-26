/**
 * @file httpd_control.cpp
 * @brief 摄像头控制与状态查询模块
 * 
 * 本文件提供：
 * 1. 表驱动的命令分发机制（24 种摄像头参数设置）
 * 2. apply_sensor_setting() 共享函数（HTTP 和 WebSocket 共用）
 * 3. cmd_handler - HTTP 接口的摄像头参数设置
 * 4. build_status_json() - 构建摄像头状态 JSON
 * 5. status_handler - HTTP 接口的状态查询
 */

#include "camera_httpd.h"
#include "camera_pins.h"
#include "esp_log.h"

static const char *TAG = "httpd_control";

// 前向声明
int apply_sensor_setting(const char *var, int val);

// ── 摄像头重入保护 ──────────────────────────────────────────────────────────
// 重新初始化摄像头时需要停止流媒体访问，防止 DMA/队列冲突
volatile bool camera_reinit_active = false;

// ── 摄像头重新初始化 ────────────────────────────────────────────────────────
// 切换分辨率或像素格式需要重新初始化摄像头以正确分配 DMA/帧缓冲

static camera_config_t cam_cfg;  // 保存摄像头配置用于重新初始化
static bool cam_cfg_inited = false;

/**
 * @brief 初始化摄像头配置（在首次设置前调用）
 */
static void ensure_cam_cfg()
{
    if (cam_cfg_inited) return;
    cam_cfg.ledc_channel = LEDC_CHANNEL_0;
    cam_cfg.ledc_timer   = LEDC_TIMER_0;
    cam_cfg.pin_d0 = Y2_GPIO_NUM;  cam_cfg.pin_d1 = Y3_GPIO_NUM;
    cam_cfg.pin_d2 = Y4_GPIO_NUM;  cam_cfg.pin_d3 = Y5_GPIO_NUM;
    cam_cfg.pin_d4 = Y6_GPIO_NUM;  cam_cfg.pin_d5 = Y7_GPIO_NUM;
    cam_cfg.pin_d6 = Y8_GPIO_NUM;  cam_cfg.pin_d7 = Y9_GPIO_NUM;
    cam_cfg.pin_xclk    = XCLK_GPIO_NUM;
    cam_cfg.pin_pclk    = PCLK_GPIO_NUM;
    cam_cfg.pin_vsync   = VSYNC_GPIO_NUM;
    cam_cfg.pin_href    = HREF_GPIO_NUM;
    cam_cfg.pin_sccb_sda = SIOD_GPIO_NUM;
    cam_cfg.pin_sccb_scl = SIOC_GPIO_NUM;
    cam_cfg.pin_pwdn    = PWDN_GPIO_NUM;
    cam_cfg.pin_reset   = RESET_GPIO_NUM;
    cam_cfg.xclk_freq_hz = 14000000;
    cam_cfg.frame_size   = FRAMESIZE_QVGA;
    cam_cfg.pixel_format = PIXFORMAT_RGB565;
    cam_cfg.grab_mode    = CAMERA_GRAB_LATEST;
    cam_cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    cam_cfg.jpeg_quality = 12;
    cam_cfg.fb_count     = 2;
    cam_cfg_inited = true;
}

/**
 * @brief 重新初始化摄像头（用于切换分辨率或像素格式）
 *
 * 流程：停止流媒体 → 等待帧操作完成 → 保存设置 → deinit → init → 恢复设置 → 重启流媒体
 *
 * @param new_fs 新的帧大小（-1 表示不改变）
 * @param new_pf 新的像素格式（-1 表示不改变）
 * @return 0 成功，-1 失败
 */
static int reinit_camera(int new_fs, int new_pf)
{
    ensure_cam_cfg();

    // 保存当前传感器参数
    sensor_t *old_s = esp_camera_sensor_get();
    int saved[12];
    const char *keys[] = {
        "brightness", "contrast", "saturation", "quality",
        "awb", "agc", "aec", "hmirror", "vflip",
        "wb_mode", "special_effect", "ae_level"
    };
    if (old_s) {
        saved[0]  = old_s->status.brightness;
        saved[1]  = old_s->status.contrast;
        saved[2]  = old_s->status.saturation;
        saved[3]  = old_s->status.quality;
        saved[4]  = old_s->status.awb;
        saved[5]  = old_s->status.agc;
        saved[6]  = old_s->status.aec;
        saved[7]  = old_s->status.hmirror;
        saved[8]  = old_s->status.vflip;
        saved[9]  = old_s->status.wb_mode;
        saved[10] = old_s->status.special_effect;
        saved[11] = old_s->status.ae_level;
    }

    // 更新配置
    if (new_fs >= 0) cam_cfg.frame_size   = (framesize_t)new_fs;
    if (new_pf >= 0) cam_cfg.pixel_format = (pixformat_t)new_pf;

    // 1. 先设置标志位，阻止新的帧请求
    camera_reinit_active = true;
    ESP_LOGI(TAG, "停止流媒体服务器...");
    
    // 2. 停止流媒体服务器（关闭 socket 连接，使正在运行的 handler 退出循环）
    if (stream_httpd) {
        httpd_stop(stream_httpd);
        stream_httpd = NULL;
    }

    // 3. 等待进行中的帧操作完成
    vTaskDelay(pdMS_TO_TICKS(300));

    // 4. 重新初始化摄像头
    ESP_LOGI(TAG, "重新初始化摄像头 (fs=%d, pf=%d)...", cam_cfg.frame_size, cam_cfg.pixel_format);
    esp_camera_deinit();
    esp_err_t err = esp_camera_init(&cam_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头重新初始化失败: 0x%x，尝试恢复", err);
        // 恢复原配置重试
        if (new_fs >= 0) cam_cfg.frame_size   = old_s ? (framesize_t)old_s->status.framesize : FRAMESIZE_QVGA;
        if (new_pf >= 0) cam_cfg.pixel_format = old_s ? (pixformat_t)old_s->pixformat : PIXFORMAT_RGB565;
        err = esp_camera_init(&cam_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "恢复也失败: 0x%x", err);
            camera_reinit_active = false;
            return -1;
        }
    }

    // 5. 恢复传感器参数
    sensor_t *new_s = esp_camera_sensor_get();
    if (new_s && old_s) {
        for (int i = 0; i < 12; i++) {
            apply_sensor_setting(keys[i], saved[i]);
        }
    }

    // 6. 重启流媒体服务器（端口 81，处理 stream/capture/bmp）
    camera_reinit_active = false;
    ESP_LOGI(TAG, "重启流媒体服务器...");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;
    config.max_uri_handlers = 4;
    config.max_open_sockets = 4;
    config.backlog_conn = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        static const httpd_uri_t stream_uris[] = {
            { .uri = "/stream",  .method = HTTP_GET, .handler = stream_handler,  .user_ctx = NULL },
            { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL },
        };
        for (size_t i = 0; i < sizeof(stream_uris) / sizeof(stream_uris[0]); i++) {
            httpd_register_uri_handler(stream_httpd, &stream_uris[i]);
        }
    }

    ESP_LOGI(TAG, "摄像头重新初始化成功 (fs=%d, pf=%d)", cam_cfg.frame_size, cam_cfg.pixel_format);
    return 0;
}

// ── 命令查找表 ───────────────────────────────────────────────────────────────
// 使用函数指针查找表替代 20+ if-else 链，提高代码可维护性

// 帧大小设置（通过重新初始化摄像头）
static int cmd_set_framesize(sensor_t *s, int val) {
    (void)s;
    return reinit_camera(val, -1);
}

// 像素格式设置（通过重新初始化摄像头）
static int cmd_set_pixformat(sensor_t *s, int val) {
    (void)s;
    return reinit_camera(-1, val);
}

// 图像质量设置（0-63，值越小质量越高）
static int cmd_set_quality(sensor_t *s, int val)     { return s->set_quality(s, val); }

// 对比度设置（-2 到 2）
static int cmd_set_contrast(sensor_t *s, int val)    { return s->set_contrast(s, val); }

// 亮度设置（-2 到 2）
static int cmd_set_brightness(sensor_t *s, int val)  { return s->set_brightness(s, val); }

// 饱和度设置（-2 到 2）
static int cmd_set_saturation(sensor_t *s, int val)  { return s->set_saturation(s, val); }

// 增益上限设置
static int cmd_set_gainceiling(sensor_t *s, int val) { return s->set_gainceiling(s, (gainceiling_t)val); }

// 彩条测试图案（0 或 1）
static int cmd_set_colorbar(sensor_t *s, int val)    { return s->set_colorbar(s, val); }

// 自动白平衡（0 或 1）
static int cmd_set_awb(sensor_t *s, int val)         { return s->set_whitebal(s, val); }

// 自动增益控制（0 或 1）
static int cmd_set_agc(sensor_t *s, int val)         { return s->set_gain_ctrl(s, val); }

// 自动曝光控制（0 或 1）
static int cmd_set_aec(sensor_t *s, int val)         { return s->set_exposure_ctrl(s, val); }

// 水平镜像（0 或 1）
static int cmd_set_hmirror(sensor_t *s, int val)     { return s->set_hmirror(s, val); }

// 垂直翻转（0 或 1）
static int cmd_set_vflip(sensor_t *s, int val)       { return s->set_vflip(s, val); }

// 自动白平衡增益（0 或 1）
static int cmd_set_awb_gain(sensor_t *s, int val)    { return s->set_awb_gain(s, val); }

// 自动增益控制值（0-30）
static int cmd_set_agc_gain(sensor_t *s, int val)    { return s->set_agc_gain(s, val); }

// 自动曝光控制值（0-1200）
static int cmd_set_aec_value(sensor_t *s, int val)   { return s->set_aec_value(s, val); }

// AEC2 算法（0 或 1）
static int cmd_set_aec2(sensor_t *s, int val)        { return s->set_aec2(s, val); }

// 降采样（0 或 1）
static int cmd_set_dcw(sensor_t *s, int val)         { return s->set_dcw(s, val); }

// 坏点校正（0 或 1）
static int cmd_set_bpc(sensor_t *s, int val)         { return s->set_bpc(s, val); }

// 白点校正（0 或 1）
static int cmd_set_wpc(sensor_t *s, int val)         { return s->set_wpc(s, val); }

// 原始 Gamma（0 或 1）
static int cmd_set_raw_gma(sensor_t *s, int val)     { return s->set_raw_gma(s, val); }

// 镜头校正（0 或 1）
static int cmd_set_lenc(sensor_t *s, int val)        { return s->set_lenc(s, val); }

// 特殊效果（0-6）
static int cmd_set_special_effect(sensor_t *s, int val) { return s->set_special_effect(s, val); }

// 白平衡模式（0-4）
static int cmd_set_wb_mode(sensor_t *s, int val)     { return s->set_wb_mode(s, val); }

// 曝光级别（-2 到 2）
static int cmd_set_ae_level(sensor_t *s, int val)    { return s->set_ae_level(s, val); }

// 命令表结构体
typedef struct {
    const char *name;                        // 参数名称
    int (*handler)(sensor_t *s, int val);    // 处理函数
} cmd_entry_t;

// 命令查找表（25 个摄像头参数）
static const cmd_entry_t cmd_table[] = {
    { "framesize",      cmd_set_framesize      },  // 帧大小
    { "pixformat",      cmd_set_pixformat      },  // 像素格式
    { "quality",        cmd_set_quality        },  // 图像质量
    { "contrast",       cmd_set_contrast       },  // 对比度
    { "brightness",     cmd_set_brightness     },  // 亮度
    { "saturation",     cmd_set_saturation     },  // 饱和度
    { "gainceiling",    cmd_set_gainceiling    },  // 增益上限
    { "colorbar",       cmd_set_colorbar       },  // 彩条测试
    { "awb",            cmd_set_awb            },  // 自动白平衡
    { "agc",            cmd_set_agc            },  // 自动增益控制
    { "aec",            cmd_set_aec            },  // 自动曝光控制
    { "hmirror",        cmd_set_hmirror        },  // 水平镜像
    { "vflip",          cmd_set_vflip          },  // 垂直翻转
    { "awb_gain",       cmd_set_awb_gain       },  // 白平衡增益
    { "agc_gain",       cmd_set_agc_gain       },  // 增益值
    { "aec_value",      cmd_set_aec_value      },  // 曝光值
    { "aec2",           cmd_set_aec2           },  // AEC2 算法
    { "dcw",            cmd_set_dcw            },  // 降采样
    { "bpc",            cmd_set_bpc            },  // 坏点校正
    { "wpc",            cmd_set_wpc            },  // 白点校正
    { "raw_gma",        cmd_set_raw_gma        },  // 原始 Gamma
    { "lenc",           cmd_set_lenc           },  // 镜头校正
    { "special_effect", cmd_set_special_effect },  // 特殊效果
    { "wb_mode",        cmd_set_wb_mode        },  // 白平衡模式
    { "ae_level",       cmd_set_ae_level       },  // 曝光级别
};

// ── 共享命令分发器 ───────────────────────────────────────────────────────────
// HTTP 和 WebSocket 处理器共用此函数

/**
 * @brief 应用摄像头传感器设置
 * 
 * 在命令查找表中查找匹配的参数名，并调用对应的处理函数。
 * 被 HTTP 处理器和 WebSocket 处理器共同调用。
 * 
 * @param var 参数名称
 * @param val 参数值
 * @return 成功返回 0，失败返回 -1
 */
int apply_sensor_setting(const char *var, int val)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return -1;

    // 在命令表中查找匹配项
    const size_t table_size = sizeof(cmd_table) / sizeof(cmd_table[0]);
    for (size_t i = 0; i < table_size; i++) {
        if (strcmp(var, cmd_table[i].name) == 0) {
            return cmd_table[i].handler(s, val);
        }
    }
    ESP_LOGW(TAG, "未知设置项: %s", var);
    return -1;
}

// ── HTTP 摄像头参数设置处理器 ────────────────────────────────────────────────

/**
 * @brief HTTP 摄像头参数设置处理器
 * 
 * 处理 GET 请求，格式：/control?var=<参数名>&val=<值>
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char variable[32];
    char value[32];

    // 解析 GET 请求参数
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
    if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
        httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int val = atoi(value);
    ESP_LOGI(TAG, "%s = %d", variable, val);

    // 应用设置
    int res = apply_sensor_setting(variable, val);
    if (res < 0) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

// ── 状态 JSON 构建器 ─────────────────────────────────────────────────────────
// HTTP 和 WebSocket 处理器共用此函数

/**
 * @brief 添加寄存器值到 JSON 缓冲区
 * @param p JSON 缓冲区指针
 * @param remaining 缓冲区剩余空间
 * @param s 传感器对象
 * @param reg 寄存器地址
 * @param mask 掩码
 * @return 写入的字符数，失败返回 0
 */
static int append_reg(char *p, size_t remaining, sensor_t *s, uint16_t reg, uint32_t mask)
{
    if (remaining < 32) return 0;  // 预留最小空间
    return snprintf(p, remaining, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

/**
 * @brief 构建摄像头状态 JSON
 * 
 * 构建包含所有摄像头参数和传感器寄存器信息的完整 JSON。
 * 被 HTTP 处理器和 WebSocket 处理器共同调用。
 * 
 * @param buf 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @return JSON 字符串长度，失败返回 0
 */
int build_status_json(char *buf, size_t max_len)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) { buf[0] = '\0'; return 0; }

    char *p = buf;
    char *end = buf + max_len - 2;  // 为 }\0 保留空间
    *p++ = '{';

    // 传感器特定寄存器（小缓冲区跳过以节省空间）
    if (max_len > 512) {
        if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
            // OV5640/OV3660 传感器寄存器
            for (int reg = 0x3400; reg < 0x3406 && p < end; reg += 2) {
                size_t remaining = end - p;
                p += append_reg(p, remaining, s, reg, 0xFFF);
            }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x3406, 0xFF); }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x3500, 0xFFFF0); }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x3503, 0xFF); }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x350a, 0x3FF); }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x350c, 0xFFFF); }
            for (int reg = 0x5480; reg <= 0x5490 && p < end; reg++) {
                size_t remaining = end - p;
                p += append_reg(p, remaining, s, reg, 0xFF);
            }
            for (int reg = 0x5380; reg <= 0x538b && p < end; reg++) {
                size_t remaining = end - p;
                p += append_reg(p, remaining, s, reg, 0xFF);
            }
            for (int reg = 0x5580; reg < 0x558a && p < end; reg++) {
                size_t remaining = end - p;
                p += append_reg(p, remaining, s, reg, 0xFF);
            }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x558a, 0x1FF); }
        } else if (s->id.PID == OV2640_PID) {
            // OV2640 传感器寄存器
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0xd3, 0xFF); }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x111, 0xFF); }
            if (p < end) { size_t remaining = end - p; p += append_reg(p, remaining, s, 0x132, 0xFF); }
        }
    }

    // 通用摄像头参数
    if (p < end) p += snprintf(p, end - p, "\"PID\":%u,",              s->id.PID);        // 传感器 PID
    if (p < end) p += snprintf(p, end - p, "\"xclk\":%u,",             s->xclk_freq_hz / 1000000);  // 时钟频率（MHz）
    if (p < end) p += snprintf(p, end - p, "\"pixformat\":%u,",        s->pixformat);     // 像素格式
    if (p < end) p += snprintf(p, end - p, "\"framesize\":%u,",        s->status.framesize);  // 帧大小
    if (p < end) p += snprintf(p, end - p, "\"quality\":%u,",          s->status.quality);    // 图像质量
    if (p < end) p += snprintf(p, end - p, "\"brightness\":%d,",       s->status.brightness); // 亮度
    if (p < end) p += snprintf(p, end - p, "\"contrast\":%d,",         s->status.contrast);   // 对比度
    if (p < end) p += snprintf(p, end - p, "\"saturation\":%d,",       s->status.saturation); // 饱和度
    if (p < end) p += snprintf(p, end - p, "\"sharpness\":%d,",        s->status.sharpness);  // 锐度
    if (p < end) p += snprintf(p, end - p, "\"special_effect\":%u,",   s->status.special_effect);  // 特殊效果
    if (p < end) p += snprintf(p, end - p, "\"wb_mode\":%u,",          s->status.wb_mode);    // 白平衡模式
    if (p < end) p += snprintf(p, end - p, "\"awb\":%u,",              s->status.awb);        // 自动白平衡
    if (p < end) p += snprintf(p, end - p, "\"awb_gain\":%u,",         s->status.awb_gain);   // 白平衡增益
    if (p < end) p += snprintf(p, end - p, "\"aec\":%u,",              s->status.aec);        // 自动曝光控制
    if (p < end) p += snprintf(p, end - p, "\"aec2\":%u,",             s->status.aec2);       // AEC2 算法
    if (p < end) p += snprintf(p, end - p, "\"ae_level\":%d,",         s->status.ae_level);   // 曝光级别
    if (p < end) p += snprintf(p, end - p, "\"aec_value\":%u,",        s->status.aec_value);  // 曝光值
    if (p < end) p += snprintf(p, end - p, "\"agc\":%u,",              s->status.agc);        // 自动增益控制
    if (p < end) p += snprintf(p, end - p, "\"agc_gain\":%u,",         s->status.agc_gain);   // 增益值
    if (p < end) p += snprintf(p, end - p, "\"gainceiling\":%u,",      s->status.gainceiling); // 增益上限
    if (p < end) p += snprintf(p, end - p, "\"bpc\":%u,",              s->status.bpc);        // 坏点校正
    if (p < end) p += snprintf(p, end - p, "\"wpc\":%u,",              s->status.wpc);        // 白点校正
    if (p < end) p += snprintf(p, end - p, "\"raw_gma\":%u,",          s->status.raw_gma);    // 原始 Gamma
    if (p < end) p += snprintf(p, end - p, "\"lenc\":%u,",             s->status.lenc);       // 镜头校正
    if (p < end) p += snprintf(p, end - p, "\"hmirror\":%u,",          s->status.hmirror);    // 水平镜像
    if (p < end) p += snprintf(p, end - p, "\"vflip\":%u,",            s->status.vflip);      // 垂直翻转
    if (p < end) p += snprintf(p, end - p, "\"dcw\":%u,",              s->status.dcw);        // 降采样
    if (p < end) p += snprintf(p, end - p, "\"colorbar\":%u",          s->status.colorbar);   // 彩条测试

    *p++ = '}';
    *p = '\0';
    return (int)(p - buf);
}

// ── HTTP 状态查询处理器 ──────────────────────────────────────────────────────

/**
 * @brief HTTP 状态查询处理器
 * 
 * 返回摄像头状态的 JSON 响应，包含所有参数和传感器信息。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1536];
    int len = build_status_json(json_response, sizeof(json_response));
    if (len == 0) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, len);
}
