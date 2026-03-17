#include "ui.h"
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

void render_iface_ui(TurretRobot & robot)
{
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

void render_video_ui(MjpegStream& stream)
{
    ImGui::Begin("Video Feed");

    // Status indicator
    MjpegStatus status = stream.get_status();
    switch (status) {
        case MjpegStatus::Idle:
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[Idle]");
            break;
        case MjpegStatus::Connecting:
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[Connecting]");
            break;
        case MjpegStatus::Streaming:
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[Streaming]");
            break;
        case MjpegStatus::Reconnecting:
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[Reconnecting]");
            break;
    }

    // Connection inputs
    static char s_host[256] = "192.168.0.149";
    static char s_path[256] = "/?action=stream";
    static int  s_port      = 8081;

    ImGui::InputText("Host", s_host, sizeof(s_host));
    ImGui::InputInt("Port", &s_port, 0, 0);
    ImGui::InputText("Path", s_path, sizeof(s_path));

    // Connect / Disconnect button
    if (status == MjpegStatus::Idle) {
        if (ImGui::Button("Connect")) {
            if (s_port > 0 && s_port <= 65535)
                stream.connect(s_host, (uint16_t)s_port, s_path);
        }
    } else {
        if (ImGui::Button("Disconnect"))
            stream.disconnect();
    }

    ImGui::Separator();

    // Video display
    GLuint tex = stream.get_texture_id();
    int tex_w  = stream.get_tex_width();
    int tex_h  = stream.get_tex_height();

    float avail_w = ImGui::GetContentRegionAvail().x;

    if (tex != 0 && tex_w > 0 && tex_h > 0) {
        float aspect    = (float)tex_h / (float)tex_w;
        float display_w = avail_w;
        float display_h = display_w * aspect;
        ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(display_w, display_h));
    } else {
        ImGui::Dummy(ImVec2(avail_w, avail_w * 9.0f / 16.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - avail_w * 9.0f / 16.0f - ImGui::GetStyle().ItemSpacing.y);
        ImGui::TextDisabled("  No signal");
    }

    ImGui::End();
}
