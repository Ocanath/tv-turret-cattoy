#ifndef DARTT_UI_H
#define DARTT_UI_H

#include <SDL.h>
#include "plotting.h"
#include "turret_robot.h"

class SpoolerRobot;

// Initialize ImGui (call after SDL/OpenGL setup)
bool init_imgui(SDL_Window* window, SDL_GLContext gl_context);

// Shutdown ImGui
void shutdown_imgui();

void render_iface_ui(TurretRobot & robot);

#endif // DARTT_UI_H
