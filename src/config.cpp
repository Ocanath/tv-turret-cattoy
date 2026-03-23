#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

void config_defaults(AppConfig& cfg)
{
    snprintf(cfg.robot_ip,  sizeof(cfg.robot_ip),  "192.168.0.204");
    cfg.robot_port = 5603;
    snprintf(cfg.cam_host,  sizeof(cfg.cam_host),  "192.168.0.149");
    cfg.cam_port   = 8081;
    snprintf(cfg.cam_path,  sizeof(cfg.cam_path),  "/?action=stream");
}

bool config_load(const char* path, AppConfig& cfg)
{
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        char*       val = eq + 1;

        // strip trailing newline
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r'))
            val[--vlen] = '\0';

        if      (strcmp(key, "robot_ip")   == 0) snprintf(cfg.robot_ip,  sizeof(cfg.robot_ip),  "%s", val);
        else if (strcmp(key, "robot_port") == 0) cfg.robot_port = (uint16_t)atoi(val);
        else if (strcmp(key, "cam_host")   == 0) snprintf(cfg.cam_host,  sizeof(cfg.cam_host),  "%s", val);
        else if (strcmp(key, "cam_port")   == 0) cfg.cam_port   = (uint16_t)atoi(val);
        else if (strcmp(key, "cam_path")   == 0) snprintf(cfg.cam_path,  sizeof(cfg.cam_path),  "%s", val);
    }

    fclose(f);
    return true;
}

bool config_save(const char* path, const AppConfig& cfg)
{
	
    FILE* f = fopen(path, "w");
    if (!f)
	{
		printf("Save failed\n");
		return false;
	}

    fprintf(f, "robot_ip=%s\n",   cfg.robot_ip);
    fprintf(f, "robot_port=%u\n", cfg.robot_port);
    fprintf(f, "cam_host=%s\n",   cfg.cam_host);
    fprintf(f, "cam_port=%u\n",   cfg.cam_port);
    fprintf(f, "cam_path=%s\n",   cfg.cam_path);

    fclose(f);
	printf("Saved at: %s\n", path);
    return true;
}
