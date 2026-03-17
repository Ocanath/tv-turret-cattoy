#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#endif

// GL headers — same conditional pattern as plotting.cpp
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <GLES3/gl3.h>
#else
#include "imgui_impl_opengl3_loader.h"
#endif

// stb_image — one TU only
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"

// tinycsocket — declarations only (TINYCSOCKET_IMPLEMENTATION is in main.cpp)
#include "tinycsocket.h"
#include "mjpeg_stream.h"

#include <SDL.h>
#include <cstring>
#include <cctype>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool recv_exact(TcsSocket sock, uint8_t* buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        size_t got = 0;
        TcsResult r = tcs_receive(sock, buf + total, len - total, TCS_FLAG_NONE, &got);
        if (r != TCS_SUCCESS || got == 0) return false;
        total += got;
    }
    return true;
}

// Read one line (up to and including \n). Returns false on error/disconnect.
// Strips trailing \r\n. Max 4096 chars.
static bool recv_line(TcsSocket sock, std::string& out)
{
    out.clear();
    char c = 0;
    while (true) {
        size_t got = 0;
        TcsResult r = tcs_receive(sock, (uint8_t*)&c, 1, TCS_FLAG_NONE, &got);
        if (r != TCS_SUCCESS || got == 0) return false;
        if (c == '\n') {
            if (!out.empty() && out.back() == '\r')
                out.pop_back();
            return true;
        }
        out += c;
        if (out.size() > 4096) return false; // runaway line
    }
}

static std::string to_lower(std::string s)
{
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------------------
// MjpegStream
// ---------------------------------------------------------------------------

MjpegStream::MjpegStream() = default;

MjpegStream::~MjpegStream()
{
    disconnect();
}

void MjpegStream::connect(const char* host, uint16_t port, const char* path)
{
    disconnect(); // join any previous thread first

    m_stop_flag.store(false);
    m_thread = std::thread(&MjpegStream::thread_func, this,
                           std::string(host), port, std::string(path));
}

void MjpegStream::disconnect()
{
    m_stop_flag.store(true);
    if (m_thread.joinable())
        m_thread.join();
    m_status.store(MjpegStatus::Idle);
}

void MjpegStream::teardown_gl_resources()
{
    if (m_tex_id != 0) {
        glDeleteTextures(1, &m_tex_id);
        m_tex_id = 0;
        m_tex_w  = 0;
        m_tex_h  = 0;
    }
}

GLuint MjpegStream::pump_upload()
{
    bool dirty = false;
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    {
        std::lock_guard<std::mutex> lock(m_frame_mutex);
        if (!m_pending.dirty) return m_tex_id;
        dirty = true;
        w = m_pending.width;
        h = m_pending.height;
        pixels = m_pending.pixels; // copy out before releasing lock
        m_pending.dirty = false;
    }
    if (!dirty || w <= 0) return m_tex_id;

    if (m_tex_id == 0) {
        glGenTextures(1, &m_tex_id);
        glBindTexture(GL_TEXTURE_2D, m_tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_tex_id);
    }

    if (w != m_tex_w || h != m_tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        m_tex_w = w;
        m_tex_h = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return m_tex_id;
}

// ---------------------------------------------------------------------------
// Background thread
// ---------------------------------------------------------------------------

void MjpegStream::thread_func(std::string host, uint16_t port, std::string path)
{
    while (!m_stop_flag.load()) {
        // ---- Connect ----
        m_status.store(MjpegStatus::Connecting);

        TcsSocket sock = TCS_SOCKET_INVALID;
        if (tcs_tcp_client_str(&sock, host.c_str(), port, 3000) != TCS_SUCCESS) {
            tcs_close(&sock);
            goto reconnect;
        }

        // Set receive timeout (5 seconds) to detect stalled streams
        tcs_opt_receive_timeout_set(sock, 5000);

        {
            // ---- Send HTTP GET ----
            std::string req = "GET " + path + " HTTP/1.1\r\n"
                              "Host: " + host + "\r\n"
                              "Connection: close\r\n"
                              "\r\n";
            size_t sent = 0;
            if (tcs_send(sock, (const uint8_t*)req.c_str(), req.size(), TCS_MSG_SENDALL, &sent) != TCS_SUCCESS) {
                tcs_close(&sock);
                goto reconnect;
            }

            // ---- Parse response headers ----
            std::string boundary;
            {
                std::string line;
                while (recv_line(sock, line)) {
                    if (m_stop_flag.load()) { tcs_close(&sock); return; }
                    if (line.empty()) break; // end of headers

                    std::string ll = to_lower(line);
                    auto pos = ll.find("content-type:");
                    if (pos != std::string::npos) {
                        auto bpos = ll.find("boundary=");
                        if (bpos != std::string::npos) {
                            boundary = line.substr(bpos + 9); // original case
                            // Strip optional leading "--"
                            if (boundary.size() >= 2 && boundary[0] == '-' && boundary[1] == '-')
                                boundary = boundary.substr(2);
                            // Normalize: prepend "--"
                            boundary = "--" + boundary;
                        }
                    }
                }
            }

            if (boundary.empty()) {
                tcs_close(&sock);
                goto reconnect;
            }

            m_status.store(MjpegStatus::Streaming);

            // ---- Per-frame loop ----
            while (!m_stop_flag.load()) {
                // Find boundary line
                {
                    std::string line;
                    bool found = false;
                    while (recv_line(sock, line)) {
                        if (m_stop_flag.load()) { tcs_close(&sock); return; }
                        if (line == boundary || line.rfind(boundary, 0) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) break;
                }

                // Read part headers
                int content_length = -1;
                {
                    std::string line;
                    while (recv_line(sock, line)) {
                        if (m_stop_flag.load()) { tcs_close(&sock); return; }
                        if (line.empty()) break;
                        std::string ll = to_lower(line);
                        if (ll.rfind("content-length:", 0) == 0) {
                            content_length = std::stoi(line.substr(15));
                        }
                    }
                }

                if (content_length <= 0) break;

                // Read JPEG payload
                std::vector<uint8_t> jpeg_buf((size_t)content_length);
                if (!recv_exact(sock, jpeg_buf.data(), (size_t)content_length)) break;

                // Decode JPEG on background thread
                int w = 0, h = 0, channels = 0;
                uint8_t* decoded = stbi_load_from_memory(
                    jpeg_buf.data(), content_length,
                    &w, &h, &channels, 4 /*force RGBA*/);

                if (!decoded) continue;

                // Store in pending frame
                {
                    std::lock_guard<std::mutex> lock(m_frame_mutex);
                    m_pending.width  = w;
                    m_pending.height = h;
                    m_pending.pixels.assign(decoded, decoded + (size_t)(w * h * 4));
                    m_pending.dirty  = true;
                }
                stbi_image_free(decoded);
            }
        }

        tcs_close(&sock);

    reconnect:
        if (m_stop_flag.load()) break;
        m_status.store(MjpegStatus::Reconnecting);
        // Sleep 3 seconds, checking stop flag every 100ms
        for (int i = 0; i < 30 && !m_stop_flag.load(); ++i)
            SDL_Delay(100);
    }

    m_status.store(MjpegStatus::Idle);
}
