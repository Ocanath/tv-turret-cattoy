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
