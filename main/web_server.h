#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_http_server.h>
#include <string>
#include <vector>
#include <mutex>
#include <ctime>

struct ChatMessage {
    std::string role;
    std::string content;
    time_t timestamp;
};

class ChatHistory {
private:
    static std::vector<ChatMessage> messages_;
    static std::mutex mutex_;
public:
    static void AddMessage(const std::string& role, const std::string& content);
    static std::vector<ChatMessage> GetMessages();
    static void Clear();
};

class WebServer {
public:
    WebServer();
    ~WebServer();

    bool Start(int port = 80);
    void Stop();

private:
    httpd_handle_t server_handle_ = nullptr;

    static esp_err_t get_root_handler(httpd_req_t *req);
    static esp_err_t get_status_handler(httpd_req_t *req);
    static esp_err_t post_volume_handler(httpd_req_t *req);
    static esp_err_t post_brightness_handler(httpd_req_t *req);
    static esp_err_t get_chat_history_handler(httpd_req_t *req);
    static esp_err_t post_notification_handler(httpd_req_t *req);
    static esp_err_t post_mic_gain_handler(httpd_req_t *req);
    static esp_err_t get_mic_level_handler(httpd_req_t *req);
    
    static esp_err_t post_play_music_handler(httpd_req_t *req);
    static esp_err_t post_stop_music_handler(httpd_req_t *req);
    static esp_err_t get_music_status_handler(httpd_req_t *req);
};

#endif // WEB_SERVER_H
