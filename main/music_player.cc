#include "music_player.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_audio_simple_dec.h>
#include <esp_ae_rate_cvt.h>
#include "board.h"
#include "display.h"

static const char* TAG = "MusicPlayer";

#define HTTP_RECV_BUFFER_SIZE 4096
#define OUTPUT_PCM_BUFFER_SIZE 4096
#define MAX_HTTP_RECONNECTS 3

MusicPlayer::MusicPlayer() {
}

MusicPlayer::~MusicPlayer() {
    Stop();
}

void MusicPlayer::Initialize(AudioCodec* audio_codec) {
    audio_codec_ = audio_codec;
}

bool MusicPlayer::Play(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (is_playing_) {
        Stop();
    }
    
    current_url_ = url;
    stop_requested_ = false;
    is_playing_ = true;
    
    // Set device state if needed, or notify UI
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", "Đang phát nhạc...");
    
    if (xTaskCreate(MusicTask, "music_task", 1024 * 8, this, 5, &music_task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create music task");
        is_playing_ = false;
        return false;
    }
    
    return true;
}

void MusicPlayer::Stop() {
    if (is_playing_) {
        stop_requested_ = true;
        // Wait for task to finish
        int timeout_ms = 3000;
        while (is_playing_ && timeout_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            timeout_ms -= 100;
        }
        
        if (is_playing_) {
            ESP_LOGW(TAG, "Music task did not stop gracefully, forcing deletion");
            if (music_task_handle_ != nullptr) {
                vTaskDelete(music_task_handle_);
                music_task_handle_ = nullptr;
            }
            is_playing_ = false;
        }
        
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("system", "Đã dừng phát nhạc");
    }
}

bool MusicPlayer::IsPlaying() const {
    return is_playing_;
}

void MusicPlayer::MusicTask(void* arg) {
    MusicPlayer* player = static_cast<MusicPlayer*>(arg);
    player->RunMusicTask();
    
    player->is_playing_ = false;
    player->music_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void MusicPlayer::RunMusicTask() {
    ESP_LOGI(TAG, "Starting music playback: %s", current_url_.c_str());
    
    if (!audio_codec_) {
        ESP_LOGE(TAG, "Audio codec not initialized");
        return;
    }

    // 1. Setup HTTP Client
    esp_http_client_config_t config = {};
    config.url = current_url_.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size_tx = 2048;
    config.keep_alive_enable = true;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return;
    }
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }
    
    // Need to handle redirection, wait for header
    esp_http_client_fetch_headers(client);
    
    // Detect format
    esp_audio_simple_dec_type_t dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    if (current_url_.find(".mp3") != std::string::npos || current_url_.find("youtube") != std::string::npos) {
        // Most yt-dlp audio is m4a or mp3/opus, let's try M4A first for m4a links, else AAC/MP3
        if (current_url_.find(".m4a") != std::string::npos) dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
        else if (current_url_.find(".aac") != std::string::npos) dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        else if (current_url_.find(".opus") != std::string::npos) dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS;
        else dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_M4A; // yt-dlp requests ba[ext=m4a]
    }
    
    // 2. Setup Simple Decoder
    esp_audio_simple_dec_cfg_t dec_cfg = {};
    dec_cfg.dec_type = dec_type;
    dec_cfg.use_frame_dec = false; // We feed chunks, not frames
    
    esp_audio_simple_dec_handle_t dec_handle = nullptr;
    err = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
    if (err != ESP_OK) {
        // Fallback to MP3
        ESP_LOGW(TAG, "Failed to open decoder %d, falling back to MP3", dec_type);
        dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        err = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open fallback decoder: %x", err);
            esp_http_client_cleanup(client);
            return;
        }
    }
    
    // 3. Prepare buffers (allocate in PSRAM if available)
    uint8_t* http_buf = (uint8_t*)heap_caps_malloc(HTTP_RECV_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t* pcm_buf = (uint8_t*)heap_caps_malloc(OUTPUT_PCM_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!http_buf || !pcm_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        if (http_buf) heap_caps_free(http_buf);
        if (pcm_buf) heap_caps_free(pcm_buf);
        esp_audio_simple_dec_close(dec_handle);
        esp_http_client_cleanup(client);
        return;
    }
    
    // Rate Converter State
    esp_ae_rate_cvt_handle_t rate_cvt = nullptr;
    int16_t* resampled_buf = nullptr;
    uint32_t current_src_rate = 0;
    
    ESP_LOGI(TAG, "Streaming started");
    
    int bytes_read = 0;
    while (!stop_requested_) {
        bytes_read = esp_http_client_read(client, (char*)http_buf, HTTP_RECV_BUFFER_SIZE);
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            break;
        } else if (bytes_read == 0) {
            ESP_LOGI(TAG, "End of stream");
            break;
        }
        
        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = http_buf;
        raw.len = bytes_read;
        
        while (raw.consumed < raw.len && !stop_requested_) {
            esp_audio_simple_dec_out_t out = {};
            out.buffer = pcm_buf;
            out.len = OUTPUT_PCM_BUFFER_SIZE;
            
            err = esp_audio_simple_dec_process(dec_handle, &raw, &out);
            
            if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                // Reallocate larger PCM buffer
                uint32_t new_size = out.needed_size;
                ESP_LOGI(TAG, "Reallocating PCM buffer to %lu", new_size);
                heap_caps_free(pcm_buf);
                pcm_buf = (uint8_t*)heap_caps_malloc(new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!pcm_buf) break;
                continue; // Try again with larger buffer
            }
            
            if (out.decoded_size > 0) {
                // Get audio info to setup rate converter if needed
                esp_audio_simple_dec_info_t info;
                if (esp_audio_simple_dec_get_info(dec_handle, &info) == ESP_AUDIO_ERR_OK) {
                    if (info.sample_rate != current_src_rate) {
                        if (rate_cvt) {
                            esp_ae_rate_cvt_close(rate_cvt);
                            rate_cvt = nullptr;
                        }
                        
                        esp_ae_rate_cvt_cfg_t cvt_cfg = {};
                        cvt_cfg.src_rate = info.sample_rate;
                        cvt_cfg.dest_rate = audio_codec_->output_sample_rate();
                        cvt_cfg.channel = info.channel;
                        cvt_cfg.bits_per_sample = info.bits_per_sample;
                        cvt_cfg.complexity = 1;
                        cvt_cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                        
                        esp_ae_rate_cvt_open(&cvt_cfg, &rate_cvt);
                        current_src_rate = info.sample_rate;
                        
                        ESP_LOGI(TAG, "Audio Info: %lu Hz, %u channels, %u bits. Resampling to %d Hz", 
                            info.sample_rate, info.channel, info.bits_per_sample, audio_codec_->output_sample_rate());
                    }
                }
                
                // Rate Convert
                uint32_t samples_in = out.decoded_size / (info.channel * (info.bits_per_sample / 8));
                uint32_t samples_out = 0;
                
                if (rate_cvt) {
                    esp_ae_rate_cvt_get_max_out_sample_num(rate_cvt, samples_in, &samples_out);
                    
                    if (!resampled_buf) {
                        resampled_buf = (int16_t*)heap_caps_malloc(samples_out * sizeof(int16_t) * info.channel, MALLOC_CAP_SPIRAM);
                    }
                    
                    esp_ae_rate_cvt_process(rate_cvt, (esp_ae_sample_t)pcm_buf, samples_in, (esp_ae_sample_t)resampled_buf, &samples_out);
                    
                    // Mix down stereo to mono if needed, or pass directly
                    // Note: If codec takes mono, and audio is stereo, we need to mix.
                    // Let's create vector for OutputData
                    std::vector<int16_t> out_vector(samples_out * info.channel);
                    memcpy(out_vector.data(), resampled_buf, samples_out * sizeof(int16_t) * info.channel);
                    
                    // Write to I2S
                    audio_codec_->OutputData(out_vector);
                } else {
                    // No resampler, direct write
                    std::vector<int16_t> out_vector(samples_in * info.channel);
                    memcpy(out_vector.data(), pcm_buf, out.decoded_size);
                    audio_codec_->OutputData(out_vector);
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Cleaning up music player");
    
    if (rate_cvt) esp_ae_rate_cvt_close(rate_cvt);
    if (resampled_buf) heap_caps_free(resampled_buf);
    
    esp_audio_simple_dec_close(dec_handle);
    esp_http_client_cleanup(client);
    heap_caps_free(http_buf);
    heap_caps_free(pcm_buf);
    
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", "");
}
