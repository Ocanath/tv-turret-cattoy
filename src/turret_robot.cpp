#include "turret_robot.h"
#include "dartt_init.h"
#include <cstdio>
#include <cstring>

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