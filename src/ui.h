#ifndef DARTT_UI_H
#define DARTT_UI_H

#include <SDL.h>
#include "plotting.h"
#include "turret_robot.h"
#include "mjpeg_stream.h"
#include "config.h"

class SpoolerRobot;

// Initialize ImGui (call after SDL/OpenGL setup)
bool init_imgui(SDL_Window* window, SDL_GLContext gl_context);

// Shutdown ImGui
void shutdown_imgui();

// Returns true when the Save button is pressed
bool render_iface_ui(TurretRobot& robot, AppConfig& cfg);
void render_telemetry_ui(TurretRobot& robot);
bool render_video_ui(MjpegStream& stream, AppConfig& cfg);

#endif // DARTT_UI_H
