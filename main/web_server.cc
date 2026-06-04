#include "web_server.h"
#include "board.h"
#include "display.h"
#include "application.h"
#include "audio_codec.h"
#include "settings.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <cJSON.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_app_desc.h>
#include <driver/temperature_sensor.h>
#include "system_info.h"
#include <cmath>
#include "music_player.h"

static const char *TAG = "WebServer";

// Define static members of ChatHistory
std::vector<ChatMessage> ChatHistory::messages_;
std::mutex ChatHistory::mutex_;

void ChatHistory::AddMessage(const std::string& role, const std::string& content) {
    if (content.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    ChatMessage msg = {role, content, time(NULL)};
    messages_.push_back(msg);
    // Limit to 50 messages
    if (messages_.size() > 50) {
        messages_.erase(messages_.begin());
    }
}

std::vector<ChatMessage> ChatHistory::GetMessages() {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
}

void ChatHistory::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
}

WebServer::WebServer() {}

WebServer::~WebServer() {
    Stop();
}

bool WebServer::Start(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_open_sockets = 5;
    config.stack_size = 8192; // Ensure sufficient stack for cJSON parsing

    // Handlers
    httpd_uri_t get_root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_root_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t get_status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = get_status_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t post_volume_uri = {
        .uri = "/api/volume",
        .method = HTTP_POST,
        .handler = post_volume_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t post_brightness_uri = {
        .uri = "/api/brightness",
        .method = HTTP_POST,
        .handler = post_brightness_handler,
        .user_ctx = nullptr
    };



    httpd_uri_t get_chat_history_uri = {
        .uri = "/api/chat_history",
        .method = HTTP_GET,
        .handler = get_chat_history_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t post_notification_uri = {
        .uri = "/api/notification",
        .method = HTTP_POST,
        .handler = post_notification_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t post_mic_gain_uri = {
        .uri = "/api/mic_gain",
        .method = HTTP_POST,
        .handler = post_mic_gain_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t get_mic_level_uri = {
        .uri = "/api/mic_level",
        .method = HTTP_GET,
        .handler = get_mic_level_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t post_play_music_uri = {
        .uri = "/api/play_music",
        .method = HTTP_POST,
        .handler = post_play_music_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t post_stop_music_uri = {
        .uri = "/api/stop_music",
        .method = HTTP_POST,
        .handler = post_stop_music_handler,
        .user_ctx = nullptr
    };

    httpd_uri_t get_music_status_uri = {
        .uri = "/api/music_status",
        .method = HTTP_GET,
        .handler = get_music_status_handler,
        .user_ctx = nullptr
    };

    if (httpd_start(&server_handle_, &config) == ESP_OK) {
        httpd_register_uri_handler(server_handle_, &get_root_uri);
        httpd_register_uri_handler(server_handle_, &get_status_uri);
        httpd_register_uri_handler(server_handle_, &post_volume_uri);
        httpd_register_uri_handler(server_handle_, &post_brightness_uri);

        httpd_register_uri_handler(server_handle_, &get_chat_history_uri);
        httpd_register_uri_handler(server_handle_, &post_notification_uri);
        httpd_register_uri_handler(server_handle_, &post_mic_gain_uri);
        httpd_register_uri_handler(server_handle_, &get_mic_level_uri);
        httpd_register_uri_handler(server_handle_, &post_play_music_uri);
        httpd_register_uri_handler(server_handle_, &post_stop_music_uri);
        httpd_register_uri_handler(server_handle_, &get_music_status_uri);
        
        ESP_LOGI(TAG, "Local Dashboard WebServer started on port %d", port);
        return true;
    }

    ESP_LOGE(TAG, "Failed to start Local Dashboard WebServer");
    return false;
}

void WebServer::Stop() {
    if (server_handle_) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        ESP_LOGI(TAG, "WebServer stopped");
    }
}

// -----------------------------------------------------------------------------
// Endpoint Handlers
// -----------------------------------------------------------------------------

esp_err_t WebServer::get_root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char* html_page = R"HTML(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
    <title>Bảng điều khiển Xiaozhi ESP32</title>
    <!-- Standalone Web App for iOS and Android -->
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <meta name="apple-mobile-web-app-title" content="Xiaozhi">
    <meta name="mobile-web-app-capable" content="yes">
    <meta name="application-name" content="Xiaozhi">
    <link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(255, 255, 255, 0.04);
            --card-border: rgba(255, 255, 255, 0.08);
            --primary-accent: #00E5FF;
            --accent-green: #39FF14;
            --accent-yellow: #ffd54f;
            --text-main: #f8fafc;
            --text-secondary: #94a3b8;
            --shadow-glow: rgba(0, 229, 255, 0.15);
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Plus Jakarta Sans', sans-serif;
            -webkit-tap-highlight-color: transparent;
        }

        body {
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            overflow-x: hidden;
            background-image: radial-gradient(circle at 10% 20%, rgba(0, 229, 255, 0.05) 0%, transparent 40%),
                              radial-gradient(circle at 90% 80%, rgba(157, 78, 221, 0.05) 0%, transparent 40%);
            padding: 20px;
        }

        .container {
            width: 100%;
            max-width: 600px;
            display: flex;
            flex-direction: column;
            gap: 20px;
            margin-bottom: 40px;
        }

        header {
            text-align: center;
            margin: 20px 0 10px;
        }

        header h1 {
            font-size: 1.8rem;
            font-weight: 700;
            background: linear-gradient(135deg, #ffffff 30%, var(--primary-accent) 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.5px;
            margin-bottom: 5px;
        }

        header p {
            color: var(--text-secondary);
            font-size: 0.9rem;
        }

        .card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 20px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
        }

        .card:hover {
            border-color: rgba(0, 229, 255, 0.25);
            box-shadow: 0 8px 32px 0 rgba(0, 229, 255, 0.05);
        }

        .card-title {
            font-size: 1rem;
            font-weight: 600;
            color: var(--text-secondary);
            margin-bottom: 15px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .card-title span.accent {
            color: var(--primary-accent);
            font-size: 0.85rem;
            font-weight: 500;
        }

        /* Stats Grid */
        .stats-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
        }

        .stat-box {
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 12px;
            padding: 12px;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }

        .stat-box .label {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }

        .stat-box .value {
            font-size: 1.1rem;
            font-weight: 600;
        }

        /* Control sliders */
        .control-group {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }

        .control-item {
            display: flex;
            flex-direction: column;
            gap: 8px;
        }

        .control-label {
            display: flex;
            justify-content: space-between;
            font-size: 0.85rem;
            color: var(--text-main);
        }

        .control-label .val {
            font-weight: 600;
            color: var(--primary-accent);
        }

        input[type="range"] {
            -webkit-appearance: none;
            width: 100%;
            height: 6px;
            border-radius: 3px;
            background: rgba(255, 255, 255, 0.1);
            outline: none;
        }

        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 18px;
            height: 18px;
            border-radius: 50%;
            background: var(--primary-accent);
            cursor: pointer;
            box-shadow: 0 0 10px var(--primary-accent);
            transition: transform 0.1s;
        }

        input[type="range"]::-webkit-slider-thumb:active {
            transform: scale(1.2);
        }

        /* Input styling */
        .form-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
            margin-bottom: 12px;
        }

        .input-group {
            display: flex;
            flex-direction: column;
            gap: 6px;
        }

        label {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }

        input[type="text"], input[type="number"] {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 10px;
            padding: 10px 14px;
            color: var(--text-main);
            font-size: 0.85rem;
            outline: none;
            transition: all 0.2s;
            width: 100%;
        }

        input[type="text"]:focus, input[type="number"]:focus {
            border-color: var(--primary-accent);
            background: rgba(255, 255, 255, 0.08);
            box-shadow: 0 0 8px rgba(0, 229, 255, 0.2);
        }

        button {
            background: linear-gradient(135deg, var(--primary-accent), #00B0FF);
            color: #000;
            font-weight: 600;
            border: none;
            border-radius: 10px;
            padding: 12px 20px;
            cursor: pointer;
            transition: all 0.2s;
            width: 100%;
            font-size: 0.9rem;
        }

        button:hover {
            transform: translateY(-1px);
            box-shadow: 0 4px 15px rgba(0, 229, 255, 0.3);
        }

        button:active {
            transform: translateY(0);
        }

        /* Chat history design */
        .chat-container {
            max-height: 280px;
            overflow-y: auto;
            display: flex;
            flex-direction: column;
            gap: 10px;
            padding-right: 5px;
        }

        .chat-bubble {
            max-width: 85%;
            padding: 10px 14px;
            border-radius: 16px;
            font-size: 0.85rem;
            line-height: 1.4;
            animation: fadeIn 0.3s ease-out;
        }

        .chat-bubble.user {
            background: rgba(0, 229, 255, 0.1);
            border: 1px solid rgba(0, 229, 255, 0.15);
            align-self: flex-end;
            border-bottom-right-radius: 4px;
        }

        .chat-bubble.assistant {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid rgba(255, 255, 255, 0.07);
            align-self: flex-start;
            border-bottom-left-radius: 4px;
        }

        .chat-bubble .meta {
            font-size: 0.65rem;
            color: var(--text-secondary);
            margin-bottom: 2px;
            font-weight: 500;
        }

        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(5px); }
            to { opacity: 1; transform: translateY(0); }
        }

        /* Scrollbar styling */
        ::-webkit-scrollbar {
            width: 4px;
        }
        ::-webkit-scrollbar-track {
            background: transparent;
        }
        ::-webkit-scrollbar-thumb {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 2px;
        }

        /* Toast notification */
        .toast {
            position: fixed;
            bottom: 20px;
            background: rgba(0, 229, 255, 0.9);
            color: #000;
            padding: 10px 20px;
            border-radius: 30px;
            font-size: 0.85rem;
            font-weight: 600;
            box-shadow: 0 4px 15px rgba(0, 229, 255, 0.3);
            opacity: 0;
            transform: translateY(20px);
            transition: all 0.3s;
            pointer-events: none;
            z-index: 1000;
        }

        .toast.show {
            opacity: 1;
            transform: translateY(0);
        }

        /* System Monitoring Styles */
        .sys-info-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
        }
        .sys-info-item {
            background: rgba(255,255,255,0.02);
            border: 1px solid rgba(255,255,255,0.05);
            border-radius: 10px;
            padding: 10px;
        }
        .sys-info-item .si-label {
            font-size: 0.7rem;
            color: var(--text-secondary);
            margin-bottom: 3px;
        }
        .sys-info-item .si-value {
            font-size: 0.9rem;
            font-weight: 600;
            color: var(--text-main);
            word-break: break-all;
        }
        .progress-bar-bg {
            width: 100%;
            height: 8px;
            background: rgba(255,255,255,0.08);
            border-radius: 4px;
            margin-top: 6px;
            overflow: hidden;
        }
        .progress-bar-fill {
            height: 100%;
            border-radius: 4px;
            transition: width 0.5s ease;
        }
        .fill-cyan { background: linear-gradient(90deg, #00E5FF, #00B0FF); }
        .fill-green { background: linear-gradient(90deg, #39FF14, #00E676); }
        .fill-yellow { background: linear-gradient(90deg, #ffd54f, #ffab00); }
        .fill-red { background: linear-gradient(90deg, #ff5252, #d50000); }
        .rssi-container {
            display: flex;
            align-items: center;
            gap: 8px;
            margin-top: 8px;
        }
        .rssi-bars {
            display: flex;
            align-items: flex-end;
            gap: 3px;
            height: 20px;
        }
        .rssi-bar {
            width: 5px;
            border-radius: 2px;
            background: rgba(255,255,255,0.1);
            transition: background 0.3s;
        }
        .rssi-bar.active { background: var(--accent-green); }
        .rssi-bar.medium { background: var(--accent-yellow); }
        .rssi-bar.weak { background: #ff5252; }
        .chart-canvas {
            width: 100%;
            height: 120px;
            border-radius: 8px;
            background: rgba(255,255,255,0.02);
            border: 1px solid rgba(255,255,255,0.05);
        }
        .temp-display {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-top: 5px;
        }
        .temp-circle {
            width: 50px;
            height: 50px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1rem;
            font-weight: 700;
            border: 2px solid;
            transition: all 0.5s;
        }
    </style>
</head>
<body>

    <header>
        <h1>Trợ lý ảo Xiaozhi</h1>
        <p>Bảng điều khiển hệ thống nội bộ</p>
    </header>

    <div class="container">
        <!-- System Stats -->
        <div class="card">
            <div class="card-title">Trạng thái thiết bị <span class="accent" id="status-tag">Đang trực tuyến</span></div>
            <div class="stats-grid">
                <div class="stat-box">
                    <span class="label">Mạng WiFi</span>
                    <span class="value" id="stat-wifi">--</span>
                </div>
                <div class="stat-box">
                    <span class="label">Phần trăm Pin</span>
                    <span class="value" id="stat-battery">--</span>
                </div>
            </div>
        </div>

        <!-- System Controls -->
        <div class="card">
            <div class="card-title">Thiết lập âm lượng & Độ sáng</div>
            <div class="control-group">
                <div class="control-item">
                    <div class="control-label">
                        <span>Âm lượng loa</span>
                        <span class="val" id="vol-val">70%</span>
                    </div>
                    <input type="range" id="vol-slider" min="0" max="100" value="70">
                </div>
                <div class="control-item">
                    <div class="control-label">
                        <span>Độ sáng màn hình</span>
                        <span class="val" id="bright-val">80%</span>
                    </div>
                    <input type="range" id="bright-slider" min="0" max="100" value="80">
                </div>
            </div>
        </div>

        <!-- Microphone Controls -->
        <div class="card">
            <div class="card-title">Cài đặt Microphone <span class="accent" id="mic-status">Sẵn sàng</span></div>
            <div class="control-group">
                <div class="control-item">
                    <div class="control-label">
                        <span>Độ nhạy Mic (Gain)</span>
                        <span class="val" id="mic-gain-val">0</span>
                    </div>
                    <input type="range" id="mic-gain-slider" min="0" max="100" value="0" step="1">
                </div>
                <div class="control-item">
                    <div class="control-label">
                        <span>Test Microphone</span>
                        <span class="val" id="mic-test-label">Tắt</span>
                    </div>
                    <button id="mic-test-btn" onclick="toggleMicTest()" style="background: linear-gradient(135deg, #39FF14, #00E676); margin-top: 6px;">Bắt đầu Test Mic</button>
                    <div id="vu-meter" style="display:none; margin-top: 12px;">
                        <div class="control-label"><span>Mức âm thanh (RMS)</span><span class="val" id="vu-rms">0%</span></div>
                        <div class="progress-bar-bg" style="height:12px; margin-top:4px;">
                            <div class="progress-bar-fill fill-green" id="vu-rms-bar" style="width:0%; transition: width 0.15s;"></div>
                        </div>
                        <div class="control-label" style="margin-top: 8px;"><span>Đỉnh (Peak)</span><span class="val" id="vu-peak">0%</span></div>
                        <div class="progress-bar-bg" style="height:12px; margin-top:4px;">
                            <div class="progress-bar-fill fill-yellow" id="vu-peak-bar" style="width:0%; transition: width 0.15s;"></div>
                        </div>
                    </div>
                </div>
            </div>
        </div>



        <!-- Music Player -->
        <div class="card">
            <div class="card-title">Trình phát nhạc <span class="accent" id="music-status">Đang dừng</span></div>
            <form id="music-form" onsubmit="playMusic(event)">
                <div class="form-row">
                    <div class="input-group">
                        <label for="music-url">Đường dẫn nhạc (URL)</label>
                        <input type="text" id="music-url" placeholder="http://.../audio.mp3" required>
                    </div>
                </div>
                <div style="display:flex;gap:10px;margin-top:10px;">
                    <button type="submit" style="background: linear-gradient(135deg, #00E5FF, #00B0FF);">Phát nhạc</button>
                    <button type="button" onclick="stopMusic()" style="background: linear-gradient(135deg, #ff5252, #d50000);">Dừng phát</button>
                </div>
            </form>
        </div>

        <!-- Push notifications test -->
        <div class="card">
            <div class="card-title">Gửi thông báo đẩy thử nghiệm <span class="accent">API Android</span></div>
            <form id="notif-form" onsubmit="sendNotification(event)">
                <div class="form-row">
                    <div class="input-group">
                        <label for="notif-title">Ứng dụng (Title)</label>
                        <input type="text" id="notif-title" placeholder="Zalo, Messenger, SMS..." required>
                    </div>
                    <div class="input-group">
                        <label for="notif-body">Nội dung (Body)</label>
                        <input type="text" id="notif-body" placeholder="Nhập tin nhắn..." required>
                    </div>
                </div>
                <button type="submit">Gửi thông báo lên Mạch</button>
            </form>
        </div>

        <!-- Chat History -->
        <div class="card">
            <div class="card-title">Lịch sử trò chuyện</div>
            <div class="chat-container" id="chat-box">
                <div style="text-align: center; color: var(--text-secondary); font-size: 0.8rem; margin: 20px 0;">Đang tải lịch sử...</div>
            </div>
        </div>

        <!-- System Monitoring -->
        <div class="card">
            <div class="card-title">Thông tin hệ thống <span class="accent" id="sys-uptime">--</span></div>
            <div class="sys-info-grid">
                <div class="sys-info-item">
                    <div class="si-label">Firmware</div>
                    <div class="si-value" id="sys-firmware">--</div>
                </div>
                <div class="sys-info-item">
                    <div class="si-label">Board / Chip</div>
                    <div class="si-value" id="sys-board">--</div>
                </div>
                <div class="sys-info-item">
                    <div class="si-label">Địa chỉ IP</div>
                    <div class="si-value" id="sys-ip">--</div>
                </div>
                <div class="sys-info-item">
                    <div class="si-label">Địa chỉ MAC</div>
                    <div class="si-value" id="sys-mac">--</div>
                </div>
            </div>
        </div>

        <!-- RAM & WiFi RSSI -->
        <div class="card">
            <div class="card-title">Bộ nhớ & WiFi <span class="accent" id="sys-temp-badge">--</span></div>
            <div style="margin-bottom: 12px;">
                <div class="control-label"><span>SRAM nội bộ</span><span class="val" id="ram-internal-text">--</span></div>
                <div class="progress-bar-bg"><div class="progress-bar-fill fill-cyan" id="ram-internal-bar" style="width:0%"></div></div>
            </div>
            <div style="margin-bottom: 12px;">
                <div class="control-label"><span>PSRAM</span><span class="val" id="ram-psram-text">--</span></div>
                <div class="progress-bar-bg"><div class="progress-bar-fill fill-green" id="ram-psram-bar" style="width:0%"></div></div>
            </div>
            <div style="margin-bottom: 12px;">
                <div class="control-label"><span>Cường độ WiFi (RSSI)</span><span class="val" id="rssi-text">--</span></div>
                <div class="rssi-container">
                    <div class="rssi-bars" id="rssi-bars">
                        <div class="rssi-bar" style="height:4px"></div>
                        <div class="rssi-bar" style="height:8px"></div>
                        <div class="rssi-bar" style="height:12px"></div>
                        <div class="rssi-bar" style="height:16px"></div>
                        <div class="rssi-bar" style="height:20px"></div>
                    </div>
                    <span id="rssi-label" style="font-size:0.75rem; color: var(--text-secondary)">--</span>
                </div>
            </div>
            <div>
                <div class="control-label"><span>Nhiệt độ chip</span></div>
                <div class="temp-display">
                    <div class="temp-circle" id="temp-circle">--</div>
                    <div style="font-size:0.8rem; color: var(--text-secondary)" id="temp-status">Đang đo...</div>
                </div>
            </div>
        </div>

        <!-- RAM Chart -->
        <div class="card">
            <div class="card-title">Biểu đồ RAM theo thời gian <span class="accent">Realtime</span></div>
            <canvas class="chart-canvas" id="ram-chart"></canvas>
            <div style="display:flex;gap:15px;margin-top:8px;font-size:0.7rem;color:var(--text-secondary)">
                <span>&#9632; <span style="color:#00E5FF">SRAM</span></span>
                <span>&#9632; <span style="color:#39FF14">PSRAM</span></span>
            </div>
        </div>
    </div>

    <div class="toast" id="toast">Thông báo thành công!</div>

    <script>
        // DOM Elements
        const volSlider = document.getElementById('vol-slider');
        const volVal = document.getElementById('vol-val');
        const brightSlider = document.getElementById('bright-slider');
        const brightVal = document.getElementById('bright-val');
        const chatBox = document.getElementById('chat-box');
        const statWifi = document.getElementById('stat-wifi');
        const statBattery = document.getElementById('stat-battery');
        const micGainSlider = document.getElementById('mic-gain-slider');
        const micGainVal = document.getElementById('mic-gain-val');
        let micTestActive = false;
        let micTestInterval = null;


        // Toast show helper
        function showToast(msg) {
            const toast = document.getElementById('toast');
            toast.innerText = msg;
            toast.classList.add('show');
            setTimeout(() => {
                toast.classList.remove('show');
            }, 2500);
        }

        // Mic Gain Slider
        micGainSlider.addEventListener('input', (e) => {
            micGainVal.innerText = e.target.value;
        });
        micGainSlider.addEventListener('change', async (e) => {
            try {
                await fetch('/api/mic_gain', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({gain: parseFloat(e.target.value)})
                });
                showToast('Đã lưu độ nhạy mic!');
            } catch (err) { console.error(err); }
        });

        // Music Player Controls
        async function playMusic(e) {
            e.preventDefault();
            const url = document.getElementById('music-url').value;
            if (!url) return;
            try {
                await fetch('/api/play_music', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({url: url})
                });
                showToast('Đã gửi yêu cầu phát nhạc!');
                document.getElementById('music-status').innerText = 'Đang phát...';
            } catch (err) { console.error(err); showToast('Lỗi phát nhạc'); }
        }

        async function stopMusic() {
            try {
                await fetch('/api/stop_music', { method: 'POST' });
                showToast('Đã dừng nhạc!');
                document.getElementById('music-status').innerText = 'Đang dừng';
            } catch (err) { console.error(err); }
        }

        // Mic Test Toggle
        function toggleMicTest() {
            micTestActive = !micTestActive;
            const btn = document.getElementById('mic-test-btn');
            const vu = document.getElementById('vu-meter');
            const label = document.getElementById('mic-test-label');
            const status = document.getElementById('mic-status');
            if (micTestActive) {
                btn.innerText = 'Dừng Test';
                btn.style.background = 'linear-gradient(135deg, #ff5252, #d50000)';
                vu.style.display = 'block';
                label.innerText = 'Đang test...';
                status.innerText = 'Đang test';
                status.style.color = '#39FF14';
                micTestInterval = setInterval(fetchMicLevel, 150);
            } else {
                btn.innerText = 'Bắt đầu Test Mic';
                btn.style.background = 'linear-gradient(135deg, #39FF14, #00E676)';
                vu.style.display = 'none';
                label.innerText = 'Tắt';
                status.innerText = 'Sẵn sàng';
                status.style.color = '';
                clearInterval(micTestInterval);
                micTestInterval = null;
            }
        }

        async function fetchMicLevel() {
            try {
                const res = await fetch('/api/mic_level');
                const data = await res.json();
                const rms = Math.min(data.rms, 100).toFixed(1);
                const peak = Math.min(data.peak, 100).toFixed(1);
                document.getElementById('vu-rms').innerText = rms + '%';
                document.getElementById('vu-rms-bar').style.width = rms + '%';
                document.getElementById('vu-peak').innerText = peak + '%';
                document.getElementById('vu-peak-bar').style.width = peak + '%';
                // Color coding
                const rmsBar = document.getElementById('vu-rms-bar');
                rmsBar.className = 'progress-bar-fill ' + (rms > 80 ? 'fill-red' : rms > 40 ? 'fill-yellow' : 'fill-green');
            } catch (err) { console.error(err); }
        }

        // Fetch API settings status
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                
                volSlider.value = data.volume;
                volVal.innerText = data.volume + '%';
                brightSlider.value = data.brightness;
                brightVal.innerText = data.brightness + '%';

                statWifi.innerText = data.wifi_ssid || 'Đã kết nối';
                statBattery.innerText = data.battery_level !== undefined ? (data.battery_level + '%') : 'Không đo được';

                // Sync mic gain
                if (data.input_gain !== undefined && !micTestActive) {
                    micGainSlider.value = data.input_gain;
                    micGainVal.innerText = data.input_gain.toFixed(0);
                }

                // Sync music status
                try {
                    const musicRes = await fetch('/api/music_status');
                    const musicData = await musicRes.json();
                    document.getElementById('music-status').innerText = musicData.playing ? 'Đang phát...' : 'Đang dừng';
                } catch (e) {}

                // Update system monitoring cards
                updateSysInfo(data);

            } catch (err) {
                console.error("Lỗi đồng bộ trạng thái:", err);
            }
        }

        // Fetch Chat History
        async function fetchChat() {
            try {
                const res = await fetch('/api/chat_history');
                const list = await res.json();
                if (list.length === 0) {
                    chatBox.innerHTML = '<div style="text-align: center; color: var(--text-secondary); font-size: 0.8rem; margin: 20px 0;">Không có tin nhắn trò chuyện gần đây</div>';
                    return;
                }

                let html = '';
                list.forEach(msg => {
                    const roleClass = msg.role === 'user' ? 'user' : 'assistant';
                    const roleTitle = msg.role === 'user' ? 'Bạn' : 'Xiaozhi';
                    const timeStr = msg.time ? new Date(msg.time * 1000).toLocaleTimeString('vi-VN', {hour: '2-digit', minute:'2-digit', second:'2-digit'}) : '';
                    
                    html += `
                        <div class="chat-bubble ${roleClass}">
                            <div class="meta">${roleTitle} • ${timeStr}</div>
                            <div>${msg.content}</div>
                        </div>
                    `;
                });
                chatBox.innerHTML = html;
            } catch (err) {
                console.error("Lỗi đồng bộ lịch sử chat:", err);
            }
        }

        // Vol Slider Action
        volSlider.addEventListener('input', (e) => {
            volVal.innerText = e.target.value + '%';
        });
        volSlider.addEventListener('change', async (e) => {
            try {
                await fetch('/api/volume', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({volume: parseInt(e.target.value)})
                });
                showToast("Đã lưu âm lượng!");
            } catch (err) {
                console.error(err);
            }
        });

        // Bright Slider Action
        brightSlider.addEventListener('input', (e) => {
            brightVal.innerText = e.target.value + '%';
        });
        brightSlider.addEventListener('change', async (e) => {
            try {
                await fetch('/api/brightness', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({brightness: parseInt(e.target.value)})
                });
                showToast("Đã thay đổi độ sáng!");
            } catch (err) {
                console.error(err);
            }
        });



        // Send Test Notification
        async function sendNotification(event) {
            event.preventDefault();
            const titleInput = document.getElementById('notif-title');
            const bodyInput = document.getElementById('notif-body');
            const body = {
                title: titleInput.value,
                body: bodyInput.value
            };
            try {
                await fetch('/api/notification', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(body)
                });
                showToast("Đã gửi thông báo đẩy!");
                bodyInput.value = '';
            } catch (err) {
                console.error(err);
            }
        }

        // Init & Polling
        fetchStatus();
        fetchChat();
        setInterval(fetchChat, 2000);
        setInterval(fetchStatus, 3000);

        // RAM chart data
        const ramHistory = { sram: [], psram: [], labels: [] };
        const MAX_POINTS = 30;

        function formatBytes(b) {
            if (b >= 1048576) return (b/1048576).toFixed(1) + ' MB';
            if (b >= 1024) return (b/1024).toFixed(0) + ' KB';
            return b + ' B';
        }

        function formatUptime(s) {
            const h = Math.floor(s/3600);
            const m = Math.floor((s%3600)/60);
            const sec = Math.floor(s%60);
            if (h > 0) return h+'h '+m+'m '+sec+'s';
            if (m > 0) return m+'m '+sec+'s';
            return sec+'s';
        }

        function updateSysInfo(data) {
            // Firmware & board
            const fwEl = document.getElementById('sys-firmware');
            const boardEl = document.getElementById('sys-board');
            const ipEl = document.getElementById('sys-ip');
            const macEl = document.getElementById('sys-mac');
            const uptimeEl = document.getElementById('sys-uptime');

            if (data.firmware_version) fwEl.innerText = data.firmware_version;
            if (data.board_name) boardEl.innerText = data.board_name + ' / ' + (data.chip_model||'');
            if (data.ip_address) ipEl.innerText = data.ip_address;
            if (data.mac_address) macEl.innerText = data.mac_address;
            if (data.uptime_s !== undefined) uptimeEl.innerText = 'Uptime: ' + formatUptime(data.uptime_s);

            // Use actual WiFi SSID
            if (data.wifi_ssid_actual) statWifi.innerText = data.wifi_ssid_actual;

            // RAM bars
            if (data.total_internal > 0) {
                const usedPct = ((data.total_internal - data.free_internal) / data.total_internal * 100).toFixed(0);
                document.getElementById('ram-internal-text').innerText = formatBytes(data.free_internal) + ' / ' + formatBytes(data.total_internal) + ' (' + usedPct + '% used)';
                document.getElementById('ram-internal-bar').style.width = usedPct + '%';
                const bar = document.getElementById('ram-internal-bar');
                bar.className = 'progress-bar-fill ' + (usedPct > 85 ? 'fill-red' : usedPct > 60 ? 'fill-yellow' : 'fill-cyan');
            }
            if (data.total_psram > 0) {
                const usedPct = ((data.total_psram - data.free_psram) / data.total_psram * 100).toFixed(0);
                document.getElementById('ram-psram-text').innerText = formatBytes(data.free_psram) + ' / ' + formatBytes(data.total_psram) + ' (' + usedPct + '% used)';
                document.getElementById('ram-psram-bar').style.width = usedPct + '%';
            }

            // RSSI
            if (data.rssi !== undefined && data.rssi !== 0) {
                const rssi = data.rssi;
                document.getElementById('rssi-text').innerText = rssi + ' dBm';
                const bars = document.getElementById('rssi-bars').children;
                let level = rssi >= -50 ? 5 : rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
                let label = level >= 4 ? 'Rất mạnh' : level >= 3 ? 'Tốt' : level >= 2 ? 'Trung bình' : 'Yếu';
                let colorClass = level >= 3 ? 'active' : level >= 2 ? 'medium' : 'weak';
                document.getElementById('rssi-label').innerText = label;
                for (let i = 0; i < 5; i++) {
                    bars[i].className = 'rssi-bar' + (i < level ? ' ' + colorClass : '');
                }
            }

            // Temperature
            if (data.chip_temp !== undefined) {
                const t = data.chip_temp.toFixed(1);
                const circle = document.getElementById('temp-circle');
                const status = document.getElementById('temp-status');
                const badge = document.getElementById('sys-temp-badge');
                circle.innerText = t + '°';
                badge.innerText = t + '°C';
                if (data.chip_temp < 45) {
                    circle.style.borderColor = '#39FF14';
                    circle.style.color = '#39FF14';
                    status.innerText = 'Bình thường';
                } else if (data.chip_temp < 65) {
                    circle.style.borderColor = '#ffd54f';
                    circle.style.color = '#ffd54f';
                    status.innerText = 'Ấm';
                } else {
                    circle.style.borderColor = '#ff5252';
                    circle.style.color = '#ff5252';
                    status.innerText = 'Nóng!';
                }
            }

            // Chart data
            const now = new Date().toLocaleTimeString('vi-VN', {hour:'2-digit',minute:'2-digit',second:'2-digit'});
            if (data.total_internal > 0) {
                ramHistory.sram.push(((data.total_internal - data.free_internal) / data.total_internal * 100));
            }
            if (data.total_psram > 0) {
                ramHistory.psram.push(((data.total_psram - data.free_psram) / data.total_psram * 100));
            }
            ramHistory.labels.push(now);
            while (ramHistory.sram.length > MAX_POINTS) { ramHistory.sram.shift(); ramHistory.psram.shift(); ramHistory.labels.shift(); }
            drawChart();
        }

        function drawChart() {
            const canvas = document.getElementById('ram-chart');
            if (!canvas) return;
            const ctx = canvas.getContext('2d');
            const dpr = window.devicePixelRatio || 1;
            const rect = canvas.getBoundingClientRect();
            canvas.width = rect.width * dpr;
            canvas.height = rect.height * dpr;
            ctx.scale(dpr, dpr);
            const W = rect.width, H = rect.height;
            ctx.clearRect(0, 0, W, H);

            // Grid lines
            ctx.strokeStyle = 'rgba(255,255,255,0.05)';
            ctx.lineWidth = 1;
            for (let i = 0; i <= 4; i++) {
                const y = (H/4)*i;
                ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
            }
            // Labels
            ctx.fillStyle = 'rgba(255,255,255,0.2)';
            ctx.font = '10px sans-serif';
            ctx.fillText('100%', 2, 12);
            ctx.fillText('50%', 2, H/2+4);
            ctx.fillText('0%', 2, H-2);

            if (ramHistory.sram.length < 2) return;

            function drawLine(data, color) {
                ctx.beginPath();
                ctx.strokeStyle = color;
                ctx.lineWidth = 2;
                ctx.lineJoin = 'round';
                const step = W / (MAX_POINTS - 1);
                for (let i = 0; i < data.length; i++) {
                    const x = i * step;
                    const y = H - (data[i] / 100) * H;
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                }
                ctx.stroke();
                // Fill area
                ctx.lineTo((data.length-1)*step, H);
                ctx.lineTo(0, H);
                ctx.closePath();
                ctx.fillStyle = color.replace('1)', '0.08)');
                ctx.fill();
            }
            drawLine(ramHistory.sram, 'rgba(0,229,255,1)');
            drawLine(ramHistory.psram, 'rgba(57,255,20,1)');
        }
    </script>
</body>
</html>
)HTML";

    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t WebServer::get_status_handler(httpd_req_t *req) {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    auto backlight = board.GetBacklight();

    int volume = codec != nullptr ? codec->output_volume() : 70;
    int brightness = backlight != nullptr ? backlight->brightness() : 80;

    int battery_level = 0;
    bool charging = false;
    bool discharging = true;
    bool got_battery = board.GetBatteryLevel(battery_level, charging, discharging);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "volume", volume);
    cJSON_AddNumberToObject(root, "brightness", brightness);
    if (got_battery) {
        cJSON_AddNumberToObject(root, "battery_level", battery_level);
    }
    cJSON_AddStringToObject(root, "wifi_ssid", "WiFi Connected");

    // Mic input gain
    float input_gain = codec != nullptr ? codec->input_gain() : 0.0f;
    cJSON_AddNumberToObject(root, "input_gain", input_gain);

    // System info: RAM
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    cJSON_AddNumberToObject(root, "free_heap", (double)free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", (double)min_free_heap);
    cJSON_AddNumberToObject(root, "total_internal", (double)total_internal);
    cJSON_AddNumberToObject(root, "free_internal", (double)free_internal);
    cJSON_AddNumberToObject(root, "total_psram", (double)total_psram);
    cJSON_AddNumberToObject(root, "free_psram", (double)free_psram);

    // Uptime
    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_s", (double)(uptime_us / 1000000));

    // Firmware version & MAC
    auto app_desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware_version", app_desc->version);
    cJSON_AddStringToObject(root, "board_name", BOARD_NAME);
    cJSON_AddStringToObject(root, "chip_model", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(root, "mac_address", SystemInfo::GetMacAddress().c_str());

    // IP Address
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(root, "ip_address", ip_str);
    } else {
        cJSON_AddStringToObject(root, "ip_address", "N/A");
    }

    // WiFi RSSI
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi", ap_info.rssi);
        cJSON_AddStringToObject(root, "wifi_ssid_actual", (const char*)ap_info.ssid);
    } else {
        cJSON_AddNumberToObject(root, "rssi", 0);
    }

    // Chip temperature
    static temperature_sensor_handle_t temp_sensor = nullptr;
    if (temp_sensor == nullptr) {
        temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        temperature_sensor_install(&temp_cfg, &temp_sensor);
        if (temp_sensor) temperature_sensor_enable(temp_sensor);
    }
    if (temp_sensor) {
        float chip_temp = 0;
        temperature_sensor_get_celsius(temp_sensor, &chip_temp);
        cJSON_AddNumberToObject(root, "chip_temp", chip_temp);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

esp_err_t WebServer::post_volume_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, std::min((size_t)req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Zero payload size");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        return ESP_FAIL;
    }

    cJSON* vol_item = cJSON_GetObjectItem(root, "volume");
    if (vol_item && cJSON_IsNumber(vol_item)) {
        int vol = vol_item->valueint;
        if (vol >= 0 && vol <= 100) {
            auto codec = Board::GetInstance().GetAudioCodec();
            if (codec) {
                codec->SetOutputVolume(vol);
                ESP_LOGI(TAG, "WebDashboard set volume to %d", vol);
            }
        }
    }
    cJSON_Delete(root);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t WebServer::post_brightness_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, std::min((size_t)req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Zero payload size");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        return ESP_FAIL;
    }

    cJSON* bright_item = cJSON_GetObjectItem(root, "brightness");
    if (bright_item && cJSON_IsNumber(bright_item)) {
        int bright = bright_item->valueint;
        if (bright >= 0 && bright <= 100) {
            auto backlight = Board::GetInstance().GetBacklight();
            if (backlight) {
                backlight->SetBrightness(bright, true);
                ESP_LOGI(TAG, "WebDashboard set brightness to %d", bright);
            }
        }
    }
    cJSON_Delete(root);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}



esp_err_t WebServer::get_chat_history_handler(httpd_req_t *req) {
    auto messages = ChatHistory::GetMessages();

    cJSON* root = cJSON_CreateArray();
    for (const auto& msg : messages) {
        cJSON* m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", msg.role.c_str());
        cJSON_AddStringToObject(m, "content", msg.content.c_str());
        cJSON_AddNumberToObject(m, "time", msg.timestamp);
        cJSON_AddItemToArray(root, m);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

esp_err_t WebServer::post_notification_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, std::min((size_t)req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Zero payload size");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        return ESP_FAIL;
    }

    cJSON* title_item = cJSON_GetObjectItem(root, "title");
    cJSON* body_item = cJSON_GetObjectItem(root, "body");

    if (title_item && body_item && cJSON_IsString(title_item) && cJSON_IsString(body_item)) {
        std::string title = title_item->valuestring;
        std::string body = body_item->valuestring;

        std::string full_msg = title + ": " + body;
        
        // Show notification popup on LCD Screen
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->ShowNotification(full_msg.c_str(), 5000);
        }

        // Play warning/beep audio
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
        ESP_LOGI(TAG, "WebDashboard notification processed: %s", full_msg.c_str());
    }
    cJSON_Delete(root);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t WebServer::post_mic_gain_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, std::min((size_t)req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Zero payload size");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        return ESP_FAIL;
    }

    cJSON* gain_item = cJSON_GetObjectItem(root, "gain");
    if (gain_item && cJSON_IsNumber(gain_item)) {
        float gain = (float)gain_item->valuedouble;
        if (gain >= 0.0f && gain <= 100.0f) {
            auto codec = Board::GetInstance().GetAudioCodec();
            if (codec) {
                codec->SetInputGain(gain);
                ESP_LOGI(TAG, "WebDashboard set mic gain to %.1f", gain);
            }
        }
    }
    cJSON_Delete(root);

    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t WebServer::get_mic_level_handler(httpd_req_t *req) {
    auto codec = Board::GetInstance().GetAudioCodec();
    float rms = 0.0f;
    float peak = 0.0f;

    if (codec) {
        // Read a small buffer of audio samples to compute level
        std::vector<int16_t> samples(256);
        bool got_data = codec->InputData(samples);
        if (got_data) {
            int64_t sum_sq = 0;
            int16_t max_val = 0;
            for (int i = 0; i < (int)samples.size(); i++) {
                int16_t s = samples[i];
                sum_sq += (int64_t)s * s;
                int16_t abs_s = s < 0 ? -s : s;
                if (abs_s > max_val) max_val = abs_s;
            }
            rms = sqrtf((float)sum_sq / samples.size()) / 32768.0f * 100.0f;
            peak = (float)max_val / 32768.0f * 100.0f;
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "rms", rms);
    cJSON_AddNumberToObject(root, "peak", peak);

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

esp_err_t WebServer::post_play_music_handler(httpd_req_t *req) {
    char buf[1024];
    int ret, remaining = req->content_len;
    std::string body;

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, std::min((size_t)remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        body.append(buf, ret);
        remaining -= ret;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (cJSON_IsString(url_item)) {
        std::string url = url_item->valuestring;
        auto& app = Application::GetInstance();
        // Stop any current voice chat if playing music
        app.SetDeviceState(kDeviceStateIdle);
        
        MusicPlayer::GetInstance().Play(url);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'url'");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t WebServer::post_stop_music_handler(httpd_req_t *req) {
    MusicPlayer::GetInstance().Stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t WebServer::get_music_status_handler(httpd_req_t *req) {
    bool playing = MusicPlayer::GetInstance().IsPlaying();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "playing", playing);
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}
