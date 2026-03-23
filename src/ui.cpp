#include "ui.h"
#include "config.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>
#include <vector>
#include <string>
#include "colors.h"
#include "dartt_init.h"
#include "control_interface.h"

bool init_imgui(SDL_Window* window, SDL_GLContext gl_context) 
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context)) 
	{
        fprintf(stderr, "Failed to init ImGui SDL2 backend\n");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init(nullptr))
	{
        fprintf(stderr, "Failed to init ImGui OpenGL3 backend\n");
        return false;
    }

#ifdef __ANDROID__
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    float ddpi = 160.0f;
    SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr);
    float ui_scale = ddpi / 160.0f;
    io.FontGlobalScale = ui_scale;
    ImGui::GetStyle().ScaleAllSizes(ui_scale);
    ImGui::GetStyle().TouchExtraPadding = ImVec2(16.0f, 16.0f);
#endif

    return true;
}

void shutdown_imgui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

bool render_iface_ui(TurretRobot& robot, AppConfig& cfg)
{
    bool save_pressed = false;
    ImGui::Begin("Interface Config");
	if (robot.socket.connected)
	{
		ImGui::TextColored(ImVec4(0,1,0,1), "[Connected]");
	}
	else
	{
		ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "[Disconnected]");
	}

	if (ImGui::InputText("IP", robot.socket.ip, sizeof(robot.socket.ip), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		udp_connect(&robot.socket);
	}

	int port = robot.socket.port;
	if (ImGui::InputInt("Port", &port, 0, 0))
	{
		if (port > 0 && port <= 65535)
		{
			robot.socket.port = (uint16_t)port;
			udp_connect(&robot.socket);
		}
	}

	if (ImGui::Button("Save##iface"))
	{
		snprintf(cfg.robot_ip, sizeof(cfg.robot_ip), "%s", robot.socket.ip);
		cfg.robot_port = robot.socket.port;
		save_pressed = true;
	}
	ImGui::Separator();

	if(robot.auto_circles == false)
	{
		if(ImGui::Button("Circles: Enable"))
		{
			robot.auto_circles = true;
		}
	}
	else
	{
		if(ImGui::Button("Circles: Disable"))
		{
			robot.auto_circles = false;
		}
	}

	ImGui::Separator();

	if(ImGui::Button("Laser On"))
	{
		robot.dp_ctl.action_flag = LASER_ON;
		robot.write_laser();
	}
	ImGui::SameLine();
	if(ImGui::Button("Laser Off"))
	{
		robot.dp_ctl.action_flag = LASER_OFF;
		robot.write_laser();
	}
    ImGui::End();
    return save_pressed;
}

void render_telemetry_ui(TurretRobot & robot)
{
    ImGui::Begin("Telemetry");

    ImGui::TextUnformatted("-- CTL Setpoints --");
    ImGui::Text("s0_us:        %d", robot.dp_ctl.s0_us);
    ImGui::Text("s1_us:        %d", robot.dp_ctl.s1_us);
    ImGui::Text("ms:           %u", robot.dp_ctl.ms);
    ImGui::Text("laser_status: %u", robot.dp_ctl.laser_status);
    ImGui::Text("action_flag:  %u", robot.dp_ctl.action_flag);

    ImGui::Separator();

    ImGui::TextUnformatted("-- Periph Values --");
    ImGui::Text("s0_us:        %d", robot.dp_periph.s0_us);
    ImGui::Text("s1_us:        %d", robot.dp_periph.s1_us);
    ImGui::Text("ms:           %u", robot.dp_periph.ms);
    ImGui::Text("laser_status: %u", robot.dp_periph.laser_status);
    ImGui::Text("action_flag:  %u", robot.dp_periph.action_flag);

    ImGui::End();
}

bool render_video_ui(MjpegStream& stream, AppConfig& cfg)
{
    bool save_pressed = false;
    // --- Fullscreen background video ---
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##video_bg", nullptr,
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_NoInputs          |
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoScrollbar);

    GLuint tex = stream.get_texture_id();
    int tex_w  = stream.get_tex_width();
    int tex_h  = stream.get_tex_height();

    if (tex != 0 && tex_w > 0 && tex_h > 0) {
        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;
        float tex_aspect = (float)tex_w / (float)tex_h;
        float win_aspect = win_w / win_h;
        float draw_w, draw_h;
        if (tex_aspect > win_aspect) {
            draw_w = win_w;
            draw_h = win_w / tex_aspect;
        } else {
            draw_h = win_h;
            draw_w = win_h * tex_aspect;
        }
        float off_x = (win_w - draw_w) * 0.5f;
        float off_y = (win_h - draw_h) * 0.5f;
        ImGui::SetCursorPos(ImVec2(off_x, off_y));
        ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(draw_w, draw_h));
    }

    ImGui::End();

    // --- Camera controls panel ---
    ImGui::Begin("Camera");

    MjpegStatus status = stream.get_status();
    switch (status) {
        case MjpegStatus::Idle:         ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "[Idle]");         break;
        case MjpegStatus::Connecting:   ImGui::TextColored(ImVec4(1,1,0,1),           "[Connecting]");   break;
        case MjpegStatus::Streaming:    ImGui::TextColored(ImVec4(0,1,0,1),           "[Streaming]");    break;
        case MjpegStatus::Reconnecting: ImGui::TextColored(ImVec4(1,0.6f,0,1),        "[Reconnecting]"); break;
    }

    ImGui::InputText("Host", cfg.cam_host, sizeof(cfg.cam_host));
    int cam_port = cfg.cam_port;
    if (ImGui::InputInt("Port", &cam_port, 0, 0))
        if (cam_port > 0 && cam_port <= 65535)
            cfg.cam_port = (uint16_t)cam_port;
    ImGui::InputText("Path", cfg.cam_path, sizeof(cfg.cam_path));

    if (status == MjpegStatus::Idle) {
        if (ImGui::Button("Connect"))
            stream.connect(cfg.cam_host, cfg.cam_port, cfg.cam_path);
    } else {
        if (ImGui::Button("Disconnect"))
            stream.disconnect();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save##cam"))
        save_pressed = true;

    ImGui::End();
    return save_pressed;
}
