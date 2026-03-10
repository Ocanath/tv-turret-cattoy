#include "turret_robot.h"
#include "dartt_init.h"
#include <cstdio>
#include <cstring>
#include <math.h>

TurretRobot::TurretRobot()
{
	ds.address = 0;
	/*TODO: consider allocating heap? */
	ds.ctl_base.buf = (unsigned char *)(&dp_ctl);	//must be assigned
	ds.ctl_base.size = sizeof(dartt_turret_control_t);
	ds.periph_base.buf = (unsigned char *)(&dp_periph);	//must be assigned
	ds.periph_base.size = sizeof(dartt_turret_control_t);
	for(int i = 0; i < sizeof(dartt_turret_control_t); i++)
	{
		ds.ctl_base.buf[i] = 0;
		ds.periph_base.buf[i] = 0;
	}

	ds.msg_type = TYPE_SERIAL_MESSAGE;

	ds.tx_buf.buf = new unsigned char[SERIAL_BUFFER_SIZE];
	ds.tx_buf.size = SERIAL_BUFFER_SIZE - NUM_BYTES_COBS_OVERHEAD;		//DO NOT CHANGE. This is for a good reason. See above note
	ds.tx_buf.len = 0;
	ds.rx_buf.buf = new unsigned char[SERIAL_BUFFER_SIZE];
	ds.rx_buf.size = SERIAL_BUFFER_SIZE - NUM_BYTES_COBS_OVERHEAD;	//DO NOT CHANGE. This is for a good reason. See above note
	ds.rx_buf.len = 0;
	ds.blocking_tx_callback = &tx_blocking;	//todo - figure something out here, cus we can't use the same socket...
	ds.user_context_tx = (void*)(&socket);
	ds.blocking_rx_callback = &rx_blocking;
	ds.user_context_rx = (void*)(&socket);
	ds.timeout_ms = 10;

	laser_on = 0;
	laser_ts = 0;
	auto_circles = false;

	//todo - add imgui interface to this
    snprintf(socket.ip, sizeof(socket.ip), "%s", "192.168.0.204");
	socket.port = 5603;
	udp_connect(&socket);
}



TurretRobot::~TurretRobot()
{
	delete[] ds.tx_buf.buf;
	delete[] ds.rx_buf.buf;
	udp_disconnect(&socket);
}



int TurretRobot::read_write_position(void)
{
	dartt_buffer_t r = {
		.buf  = ds.ctl_base.buf,
		.size = sizeof(uint32_t) * 5,
		.len  = sizeof(uint32_t) * 5
	};
	int rc = dartt_read_multi(&r, &ds);
	if(rc != DARTT_PROTOCOL_SUCCESS)
	{
		printf("Read fail %d\n", rc);
	}
	else
	{
		dartt_buffer_t w_positions = {
			.buf  = ds.ctl_base.buf,
			.size = sizeof(uint32_t)*2,
			.len  = sizeof(uint32_t)*2
		};
		rc = dartt_write_multi(&w_positions, &ds);
		if(rc != DARTT_PROTOCOL_SUCCESS)
		{
			printf("Write fail %d\n", rc);
		}
	}
	return rc;
}

int TurretRobot::do_circles(float time)
{
	float y = sin(time) * 100 + 1200;
	float x = cos(time) * 500 + 1500;
	dp_ctl.s0_us = (int32_t)y;
	dp_ctl.s1_us = (int32_t)x;

	if(time - laser_ts > 10)
	{
		laser_ts = time;
		laser_on = (~laser_on) & 1;
		if(laser_on)
		{
			dp_ctl.action_flag = LASER_ON;
		}
		else
		{
			dp_ctl.action_flag = LASER_OFF;
		}
	}
	return write_laser();
}


int TurretRobot::write_laser(void)
{
	dartt_buffer_t w_laser = {
		.buf  = (unsigned char *)(&dp_ctl.action_flag),
		.size = sizeof(uint32_t),
		.len  = sizeof(uint32_t)
	};
	int rc = dartt_write_multi(&w_laser, &ds);
	if(rc != DARTT_PROTOCOL_SUCCESS)
	{
		printf("LASER WRITE fail %d\n", rc);
	}
	return rc;
}