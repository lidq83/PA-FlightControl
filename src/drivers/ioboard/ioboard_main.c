/*
 * ioboard.c
 *
 *  Created on: Jun 27, 2018
 *      Author: lidq
 */

#include "ioboard_main.h"

//#define __DEBUG_INFO_

bool _ioboard_should_exit = false;

static int _serial_fd = -1;
static char _dev_name[128] = DEV_NAME;
static int _dev_baudrate = DEV_BAUDRATE;

int ioboard_main(int argc, char *argv[])
{
	int ch;
	int myoptind = 1;
	const char *myoptarg = NULL;
	char *verb = NULL;
	if (argc >= 2)
	{
		verb = argv[1];
	}
	else
	{
		return -1;
	}

	while ((ch = px4_getopt(argc, argv, "d:s:", &myoptind, &myoptarg)) != EOF)
	{
		switch (ch)
		{
			case 'd':
				strcpy(_dev_name, myoptarg);
				break;

			case 's':
				sscanf(myoptarg, "%d", &_dev_baudrate);
				break;

			default:
				break;
		}
	}

	if (strcmp(verb, "start") == 0)
	{
		start(argc, argv);
		return OK;
	}

	if (strcmp(verb, "stop") == 0)
	{
		stop();
		return OK;
	}

	return -1;
}

int start(int argc, char *argv[])
{
	px4_main_t entry_point = (px4_main_t) ioboard_write;
	int task_id = px4_task_spawn_cmd("ioboard", SCHED_DEFAULT, SCHED_PRIORITY_FAST_DRIVER, CONFIG_PTHREAD_STACK_DEFAULT, entry_point, (char * const *) argv);
	return task_id;
}

int stop(void)
{
	_ioboard_should_exit = true;
	usleep(200 * 1000);

	return OK;
}

int ioboard_write(int argc, char *argv[])
{
	_serial_fd = open(_dev_name, O_WRONLY | O_NONBLOCK);
	if (_serial_fd < 0)
	{
		warnx("can not open dev %s.", _dev_name);
		return -1;
	}
	ioboard_set_opt(_serial_fd, _dev_baudrate, 8, 'N', 1);

	ioboard_protocal_init(_serial_fd);

	pthread_t pthddr;
	pthread_create(&pthddr, (const pthread_attr_t*) NULL, (void* (*)(void*)) &ioboard_read, NULL);

	int sub_pwm_output = orb_subscribe(ORB_ID(pwm_output));
	struct pwm_output_s s_pwm_output;
	pwm_out_s s_pwm_out = { 0 };

#ifdef __DEBUG_INFO_
	uint32_t i = 0;
#endif

	while (!_ioboard_should_exit)
	{
		bool updated = false;
		orb_check(sub_pwm_output, &updated);
		if (updated)
		{
			orb_copy(ORB_ID(pwm_output), sub_pwm_output, &s_pwm_output);

			s_pwm_out.num_outputs = s_pwm_output.num_outputs;
			for (int i = 0; i < s_pwm_out.num_outputs; i++)
			{
				s_pwm_out.pwm[i] = s_pwm_output.pwm[i];
			}
			ioboard_protocal_write(&s_pwm_out, DATA_TYPE_PWM_OUTPUT, sizeof(pwm_out_s));

#ifdef __DEBUG_INFO_
			if (i++ % 10 == 0)
			{
				printf("CH %2d [", s_pwm_output.num_outputs);
				for (int i = 0; i < s_pwm_output.num_outputs; i++)
				{
					printf("%5d", s_pwm_output.pwm[i]);
				}
				printf("]\n");
			}
#endif
		}
		usleep(DEV_RATE_W);
	}
	return 0;
}

int ioboard_read(void)
{
	while (!_ioboard_should_exit)
	{
		usleep(DEV_RATE_R);
	}
	return 0;
}

int ioboard_set_opt(int fd, int nSpeed, int nBits, char nEvent, int nStop)
{
	struct termios newtio, oldtio;
	//保存测试现有串口参数设置，在这里如果串口号等出错，会有相关的出错信息
	if (tcgetattr(fd, &oldtio) != 0)
	{
		perror("SetupSerial 1");
		return -1;
	}
	memset(&newtio, 0, sizeof(newtio));
	//步骤一，设置字符大小
	newtio.c_cflag |= CLOCAL | CREAD;
	newtio.c_cflag &= ~CSIZE;
	//设置停止位
	switch (nBits)
	{
		case 7:
			newtio.c_cflag |= CS7;
			break;
		case 8:
			newtio.c_cflag |= CS8;
			break;
	}
	//设置奇偶校验位
	switch (nEvent)
	{
		case 'O': //奇数
			newtio.c_cflag |= PARENB;
			newtio.c_cflag |= PARODD;
			newtio.c_iflag |= (INPCK | ISTRIP);
			break;
		case 'E': //偶数
			newtio.c_iflag |= (INPCK | ISTRIP);
			newtio.c_cflag |= PARENB;
			newtio.c_cflag &= ~PARODD;
			break;
		case 'N': //无奇偶校验位
			newtio.c_cflag &= ~PARENB;
			break;
	}
	switch (nSpeed)
	{
		case 9600:
			cfsetispeed(&newtio, B9600);
			cfsetospeed(&newtio, B9600);
			break;

		case 19200:
			cfsetispeed(&newtio, B19200);
			cfsetospeed(&newtio, B19200);
			break;

		case 57600:
			cfsetispeed(&newtio, B57600);
			cfsetospeed(&newtio, B57600);
			break;

		case 115200:
			cfsetispeed(&newtio, B115200);
			cfsetospeed(&newtio, B115200);
			break;

		case 230400:
			cfsetispeed(&newtio, B230400);
			cfsetospeed(&newtio, B230400);
			break;

		default:
			cfsetispeed(&newtio, B115200);
			cfsetospeed(&newtio, B115200);
			break;
	}

	//设置停止位
	if (nStop == 1)
	{
		newtio.c_cflag &= ~CSTOPB;
	}
	else if (nStop == 2)
	{
		newtio.c_cflag |= CSTOPB;
	}
	//设置等待时间和最小接收字符
	newtio.c_cc[VTIME] = 0;
	newtio.c_cc[VMIN] = 0;
	//处理未接收字符
	tcflush(fd, TCIFLUSH);
	//激活新配置
	if ((tcsetattr(fd, TCSANOW, &newtio)) != 0)
	{
		perror("com set error");
		return -1;
	}
	printf("set done!\n");
	return 0;
}