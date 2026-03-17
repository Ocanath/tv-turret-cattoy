#pragma once
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>

// forward-declare so callers don't need GL headers
typedef unsigned int GLuint;

enum class MjpegStatus { Idle, Connecting, Streaming, Reconnecting };

class MjpegStream {
public:
    MjpegStream();
    ~MjpegStream();

    void connect(const char* host, uint16_t port, const char* path);
    void disconnect();

    // Call once per frame from main thread — uploads pending frame to GPU
    GLuint pump_upload();

    GLuint      get_texture_id() const { return m_tex_id; }
    int         get_tex_width()  const { return m_tex_w; }
    int         get_tex_height() const { return m_tex_h; }
    MjpegStatus get_status()     const { return m_status.load(); }

    void teardown_gl_resources();

private:
    void thread_func(std::string host, uint16_t port, std::string path);

    std::thread  m_thread;
    std::mutex   m_frame_mutex;

    // Shared between bg thread (writer) and main thread (reader)
    struct PendingFrame {
        std::vector<uint8_t> pixels; // RGBA
        int  width = 0, height = 0;
        bool dirty = false;
    } m_pending;

    std::atomic<bool>        m_stop_flag{false};
    std::atomic<MjpegStatus> m_status{MjpegStatus::Idle};

    GLuint m_tex_id = 0;
    int    m_tex_w  = 0;
    int    m_tex_h  = 0;
};
