#ifndef TURRET_ROBOT_H
#define TURRET_ROBOT_H

#include "dartt_sync.h"
#include "tinycsocket.h"
#include "control_interface.h"
#include "dartt_init.h"

class TurretRobot
{
public:
	dartt_turret_control_t dp_ctl;
	dartt_turret_control_t dp_periph;
	dartt_sync_t ds;
	UdpState socket;

	//for circles
	bool auto_circles;
	float laser_ts = 0;
	int laser_on = 0;

	int read_write_position(void);
	int write_laser(void);
	int do_circles(float time);

	TurretRobot();
	~TurretRobot();
};

#endif