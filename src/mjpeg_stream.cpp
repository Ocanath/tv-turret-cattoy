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
        printf("[mjpeg] connecting to %s:%u%s\n", host.c_str(), port, path.c_str());

        TcsSocket sock = TCS_SOCKET_INVALID;
        {
            TcsResult res = tcs_socket_preset(&sock, TCS_PRESET_TCP_IP4);
            if (res != TCS_SUCCESS) {
                printf("[mjpeg] socket create failed (%d)\n", res);
                tcs_close(&sock);
                goto reconnect;
            }

            struct TcsAddress addr = TCS_ADDRESS_NONE;
            size_t addr_count = 0;
            res = tcs_address_resolve(host.c_str(), TCS_AF_IP4, &addr, 1, &addr_count);
            if (res != TCS_SUCCESS || addr_count == 0) {
                printf("[mjpeg] address resolve failed (%d)\n", res);
                tcs_close(&sock);
                goto reconnect;
            }
            addr.data.ip4.port = port;

            // Non-blocking connect so we can respect m_stop_flag
            tcs_opt_nonblocking_set(sock, true);
            tcs_connect(sock, &addr); // returns immediately (EINPROGRESS)

            // Poll for writability (= connected) or error, 100ms slices
            struct TcsPool* pool = nullptr;
            tcs_pool_create(&pool);
            tcs_pool_add(pool, sock, nullptr, false, true, true);

            bool connected = false;
            for (int i = 0; i < 30 && !m_stop_flag.load(); ++i) { // 3s timeout
                struct TcsPollEvent ev = TCS_POOL_EVENT_EMPTY;
                size_t populated = 0;
                tcs_pool_poll(pool, &ev, 1, &populated, 100);
                if (populated > 0) {
                    connected = ev.can_write && ev.error == TCS_SUCCESS;
                    break;
                }
            }
            tcs_pool_destroy(&pool);

            if (!connected) {
                printf("[mjpeg] tcp connect failed or cancelled\n");
                tcs_close(&sock);
                goto reconnect;
            }

            // Back to blocking for normal I/O
            tcs_opt_nonblocking_set(sock, false);
        }
        printf("[mjpeg] tcp connected\n");

        // Set receive timeout (5 seconds) to detect stalled streams
        tcs_opt_receive_timeout_set(sock, 5000);

        {
            // ---- Send HTTP GET — use HTTP/1.0 to avoid chunked encoding ----
            std::string req = "GET " + path + " HTTP/1.0\r\n"
                              "Host: " + host + "\r\n"
                              "\r\n";
            size_t sent = 0;
            if (tcs_send(sock, (const uint8_t*)req.c_str(), req.size(), TCS_MSG_SENDALL, &sent) != TCS_SUCCESS) {
                printf("[mjpeg] send failed\n");
                tcs_close(&sock);
                goto reconnect;
            }

            // ---- Parse response headers ----
            std::string boundary;
            {
                std::string line;
                while (recv_line(sock, line)) {
                    if (m_stop_flag.load()) { tcs_close(&sock); return; }
                    printf("[mjpeg] hdr: %s\n", line.c_str());
                    if (line.empty()) break; // end of headers

                    std::string ll = to_lower(line);
                    if (ll.find("content-type:") != std::string::npos) {
                        auto bpos = ll.find("boundary=");
                        if (bpos != std::string::npos) {
                            boundary = line.substr(bpos + 9);
                            // Strip optional quotes
                            if (!boundary.empty() && boundary.front() == '"')
                                boundary = boundary.substr(1);
                            if (!boundary.empty() && boundary.back() == '"')
                                boundary.pop_back();
                            // Strip optional leading "--"
                            if (boundary.size() >= 2 && boundary[0] == '-' && boundary[1] == '-')
                                boundary = boundary.substr(2);
                            // Normalize: prepend "--"
                            boundary = "--" + boundary;
                            printf("[mjpeg] boundary: [%s]\n", boundary.c_str());
                        }
                    }
                }
            }

            if (boundary.empty()) {
                printf("[mjpeg] no boundary found, dropping connection\n");
                tcs_close(&sock);
                goto reconnect;
            }

            m_status.store(MjpegStatus::Streaming);
            int frame_count = 0;

            // ---- Per-frame loop ----
            while (!m_stop_flag.load()) {
                // Find boundary line
                {
                    std::string line;
                    bool found = false;
                    while (recv_line(sock, line)) {
                        if (m_stop_flag.load()) { tcs_close(&sock); return; }
                        if (line.rfind(boundary, 0) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) { printf("[mjpeg] boundary search failed\n"); break; }
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
                            // value starts after "content-length:" (15 chars), skip spaces
                            size_t i = 15;
                            while (i < line.size() && line[i] == ' ') ++i;
                            content_length = std::stoi(line.substr(i));
                        }
                    }
                }

                if (content_length <= 0) {
                    printf("[mjpeg] bad content-length: %d\n", content_length);
                    break;
                }

                // Always drain TCP buffer to prevent backlog, but skip decode
                // if the main thread hasn't consumed the previous frame yet.
                std::vector<uint8_t> jpeg_buf((size_t)content_length);
                if (!recv_exact(sock, jpeg_buf.data(), (size_t)content_length)) {
                    printf("[mjpeg] recv_exact failed for frame %d\n", frame_count);
                    break;
                }

                // Check if main thread is still holding a pending frame
                bool behind;
                {
                    std::lock_guard<std::mutex> lock(m_frame_mutex);
                    behind = m_pending.dirty;
                }
                if (behind) continue; // drained the data, skip decode to catch up

                // Decode JPEG on background thread
                int w = 0, h = 0, channels = 0;
                uint8_t* decoded = stbi_load_from_memory(
                    jpeg_buf.data(), content_length,
                    &w, &h, &channels, 4 /*force RGBA*/);

                if (!decoded) {
                    printf("[mjpeg] stbi decode failed frame %d: %s\n", frame_count, stbi_failure_reason());
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(m_frame_mutex);
                    m_pending.width  = w;
                    m_pending.height = h;
                    m_pending.pixels.assign(decoded, decoded + (size_t)(w * h * 4));
                    m_pending.dirty  = true;
                }
                stbi_image_free(decoded);

                if (frame_count == 0)
                    printf("[mjpeg] first frame decoded: %dx%d\n", w, h);
                ++frame_count;
            }
        }

        tcs_close(&sock);

    reconnect:
        if (m_stop_flag.load()) break;
        m_status.store(MjpegStatus::Reconnecting);
        printf("[mjpeg] reconnecting in 3s\n");
        for (int i = 0; i < 30 && !m_stop_flag.load(); ++i)
            SDL_Delay(100);
    }

    m_status.store(MjpegStatus::Idle);
}
