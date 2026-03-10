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
    ImGui::GetStyle().TouchExtraPadding = ImVec2(8.0f, 8.0f);
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

    ImGui::End();
}

