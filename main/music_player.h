#pragma once

#include <string>
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "audio_codec.h"

class MusicPlayer {
public:
    static MusicPlayer& GetInstance() {
        static MusicPlayer instance;
        return instance;
    }

    // Initialize with audio codec
    void Initialize(AudioCodec* audio_codec);

    // Play music from URL
    bool Play(const std::string& url);

    // Stop current music
    void Stop();

    // Check if music is currently playing
    bool IsPlaying() const;

private:
    MusicPlayer();
    ~MusicPlayer();

    static void MusicTask(void* arg);
    void RunMusicTask();

    AudioCodec* audio_codec_ = nullptr;
    std::string current_url_;
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> stop_requested_{false};
    TaskHandle_t music_task_handle_ = nullptr;
    std::mutex mutex_;
};
