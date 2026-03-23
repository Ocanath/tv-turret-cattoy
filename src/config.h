#pragma once
#include <cstdint>

struct AppConfig
{
    char     robot_ip[64];
    uint16_t robot_port;
    char     cam_host[256];
    uint16_t cam_port;
    char     cam_path[256];
};

void config_defaults(AppConfig& cfg);
bool config_load(const char* path, AppConfig& cfg);
bool config_save(const char* path, const AppConfig& cfg);
