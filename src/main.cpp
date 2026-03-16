#include <cstdio>

#define TINYCSOCKET_IMPLEMENTATION

// Platform headers (must come before GL on Windows)
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#endif

// tinycsocket (must come before SDL - SDL redefines main to SDL_main)
#include "tinycsocket.h"

// SDL2
#include <SDL.h>

// OpenGL
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

// ImGui
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

// byte-stuffing
#include "cobs.h"
#include "PPP.h"

// dartt-protocol
#include "dartt.h"
#include "dartt_sync.h"
#include "checksum.h"

// App
#include "ui.h"
#include "dartt_init.h"
#include "plotting.h"

#include <algorithm>
#include <string>

#include "turret_robot.h"
#include "cobs.h"




int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	// Drag-and-drop state

	// Touch-as-mouse hint (needed for Android touch input via SDL_GetMouseState)
#ifdef __ANDROID__
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif

	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
	{
		printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
		return -1;
	}

	// GL attributes
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
#ifdef __ANDROID__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	// Create window
	Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
	window_flags |= SDL_WINDOW_FULLSCREEN;
#endif
	SDL_Window* window = SDL_CreateWindow(
		"Ability Hand Tester",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		1280, 720,
		window_flags
	);
	
	if (!window) 
	{
		printf("Window creation failed: %s\n", SDL_GetError());
		return -1;
	}

	// Create GL context
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	if (!gl_context) 
	{
		printf("GL context creation failed: %s\n", SDL_GetError());
		return -1;
	}
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1); // VSync

	// Initialize ImGui
	if (!init_imgui(window, gl_context)) 
	{
		printf("ImGui initialization failed\n");
		return -1;
	}

	Plotter plot;

	int width = 0;
	int height = 0;
	SDL_GetWindowSize(window, &width, &height);
	plot.init(width, height);

	if (tcs_lib_init() != TCS_SUCCESS)
	{
		printf("Failed to initialize tinycsocket\n");
	}
	else
	{
		printf("Initialize tinycsocket library success\n");
	}


	TurretRobot robot;
	robot.dp_ctl.s0_us = 1500;
	robot.dp_ctl.s1_us = 1500;

	// Main loop
	bool running = true;
	bool do_pctl = false;
	uint8_t prev_space_state = 0;
	while (running)
	{
		// Poll events
		SDL_Event event;
		while (SDL_PollEvent(&event)) 
		{
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
			{
				running = false;
			}
			if (event.type == SDL_WINDOWEVENT &&
				event.window.event == SDL_WINDOWEVENT_CLOSE &&
				event.window.windowID == SDL_GetWindowID(window))
			{
				running = false;
			}
			if (!robot.auto_circles &&
				event.type == SDL_MOUSEBUTTONUP &&
				event.button.button == SDL_BUTTON_RIGHT)
			{
				robot.laser_on = !robot.laser_on;
				robot.dp_ctl.action_flag = robot.laser_on ? LASER_ON : LASER_OFF;
				if(robot.laser_on)
					printf("Laser: On\n");
				else
					printf("Laser: Off\n");
				robot.write_laser();
			}
		}

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();


		if(robot.auto_circles)
		{
			robot.do_circles(plot.sys_sec);
		}
		else
		{
			// manual mode: left mouse held -> map mouse pos to servo positions
			int mx, my;
			uint32_t mouse_buttons = SDL_GetMouseState(&mx, &my);
			if (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT))
			{
				float hw = ((float)(plot.window_width))/2.f;
				float hh = ((float)(plot.window_height))/2.f;
				float mxf = ((float)mx - hw)/hw;
				float myf = -((float)my - hh)/hh;
				// Map [0, w-1] -> [800, 2300] for s1_us (x axis)
				// Map [0, h-1] -> [800, 2300] for s0_us (y axis)
				robot.dp_ctl.s1_us = (int32_t)(mxf*750.f + 1550.f);
				robot.dp_ctl.s0_us = (int32_t)(myf*750.f + 1550.f);
				printf("(%d, %d)\n", robot.dp_ctl.s1_us, robot.dp_ctl.s0_us);
			}
		}
		robot.read_write_position();
		robot.laser_on = (bool)robot.dp_periph.laser_status;



		SDL_GetWindowSize(window, &plot.window_width, &plot.window_height);
		plot.sys_sec = (float)(((double)SDL_GetTicks64())/1000.);

		render_iface_ui(robot);
		render_telemetry_ui(robot);
		// Render
		ImGui::Render();
		int display_w, display_h;
		SDL_GL_GetDrawableSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		plot.render();	//must position here
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(window);
	}

	// Save UI settings back to config
	// save_dartt_config("config.json", config);

	// Cleanup — teardown GL resources before destroying the context
	plot.teardown_gl_resources();
	shutdown_imgui();
	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
