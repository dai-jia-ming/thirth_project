/*
 8月1日修改
  */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include "crc_check.h"


//Modbus功能码
#define MODBUS_GET_DIGIG_OP_DATA  (0x01)  		//获取开关量输出当前状态
#define MODBUS_GET_DIGIG_IP_DATA  (0x02)  		//获取开关量输入当前状态
#define MODBUS_GET_ANALOG_OP_DATA (0x03) 	 	//获取模拟量输出当前状态
#define MODBUS_GET_ANALOG_IP_DATA (0x04)  		//获取模拟量输入当前状态

#define MODBUS_SET_DIGIG_OP_DATA 		 (0x05)  	//设置单个开关量输出的值
#define MODBUS_SET_ANALOG_OP_DATA 		 (0x06)  	//设置单个模拟量输出的值
#define MODBUS_SET_MULTI_DIGIG_OP_DATA   (0x15)  	//设置多个开关量输出的值
#define MODBUS_SET_MULTI_ANALOG_OP_DATA  (0x16) 	//设置多个模拟量输出的值


//单路开关量状态
#define DIGIG_ON  (0xFF00) //开
#define DIGIG_OFF (0x0000) //关 


//CRC 生成函数
//unsigned char *puchMsg ; 						/* 用于计算 CRC 的报文 */
//unsigned short usDataLen ;						/* 报文中的字节数 */
unsigned short _CRC16(unsigned char *puchMsg, unsigned short usDataLen) 	/* 函数以 unsigned short 类型返回 CRC */	
{
	unsigned char uchCRCHi = 0xFF ;					/* CRC 的高字节初始化 */
	unsigned char uchCRCLo = 0xFF ; 				/* CRC 的低字节初始化 */
	unsigned uIndex ; 								/* CRC 查询表索引 */
	while (usDataLen--) {							/* 完成整个报文缓冲区 */
		uIndex = uchCRCLo ^ *puchMsg++ ; 				/* 计算 CRC */
		uchCRCLo = uchCRCHi ^ auchCRCHi[uIndex] ;
		uchCRCHi = auchCRCLo[uIndex] ;
	}
	return (uchCRCHi << 8 | uchCRCLo) ;
}



//配置串口通信参数 波特率115200 字长8位 无奇偶校验 1位停止位
int _serial_set_prarm(int serial_fd)
{
	struct termios new_cfg,old_cfg;
	
	if(tcgetattr(serial_fd, &old_cfg) != 0){
		perror("tcgetattr");
		return -1;
	}
	
	bzero(&new_cfg, sizeof(new_cfg));

	new_cfg = old_cfg; 
	cfmakeraw(&new_cfg); 

	cfsetispeed(&new_cfg, B115200); 
	cfsetospeed(&new_cfg, B115200);
	
	new_cfg.c_cflag |= CLOCAL | CREAD;
	
	new_cfg.c_cflag &= ~CSIZE;
	new_cfg.c_cflag |= CS8;

	new_cfg.c_cflag &= ~PARENB;

	new_cfg.c_cflag &= ~CSTOPB;
	
	tcflush( serial_fd,TCIOFLUSH);
	new_cfg.c_cc[VTIME] = 0;
	new_cfg.c_cc[VMIN] = 1;
	tcflush ( serial_fd, TCIOFLUSH);
	
	tcsetattr( serial_fd ,TCSANOW,&new_cfg);
	return 0;
}

//串口初始化,返回串口描述符
int MODBUS_serial_init(char *Dev_path)
{
	int serial_fd = open(Dev_path,O_RDWR);
	if(serial_fd<0){
		perror("open serial failed!\n");
		return -1;
	}
	_serial_set_prarm(serial_fd);
	return serial_fd;
}

void MODBUS_serial_close(int serial_fd)
{
	close(serial_fd);
}


//读取从机数据, 函数调用后需要手动free 返回的char *内存
//在获取数字量开关时，data_count代表开关量个数，在获取模拟量时，data_count代表要读的寄存器，获取一个模拟量需要读取2个寄存器
//函数执行成功返回数据的首地址
char *MODBUS_get_slave_state(int serial_fd, char slave_addr, char function_code, short data_addr, short data_count)
{
	char request_frame[8] = {0};  //存储询问帧报文
	char *answ_frame = NULL;      //指向应答帧报文
	char *answ_data = NULL;		  //指向具体请求到数据
	short answ_frame_length = 0;  //记录应答帧长度
	
	//组装报文
	snprintf(request_frame, 8, "%c%c%c%c%c%c", slave_addr, \
			function_code, data_addr>>8, data_addr&0x00FF, data_count>>8, data_count&0x00FF); 
	//计算组装CRC码
	short CRC_code = _CRC16((unsigned char*)request_frame, 6);
	*((short *)&request_frame[6]) = CRC_code; 
#if 1
	int i = 0;
	for(i = 0; i<8; i++)
		printf(":%#x ", request_frame[i]);
	printf("\n");
#endif 
	//发送询问帧
	if(write(serial_fd, request_frame, 8) <= 0){
		perror("write request_frame error");
		return NULL;
	}
	//根据功能码确定应答帧的长度
	switch(function_code){
		case MODBUS_GET_DIGIG_OP_DATA:
				answ_frame_length = 5+data_count/8; 
				if(data_count%8>0)
					answ_frame_length += 1;
		case MODBUS_GET_ANALOG_OP_DATA:
				answ_frame_length = 5+data_count*2;
	}
	//读取应答数据帧
	answ_frame = malloc(answ_frame_length);
	read(serial_fd,answ_frame, answ_frame_length);
	//提取CRC码
	CRC_code = *((short *)(answ_frame+(answ_frame_length-2)));
	//校验CRC码
	if(_CRC16((unsigned char*)answ_frame, answ_frame_length-2) != CRC_code){
		printf("校验出错\n");
		return NULL;
	}
	//从报文提取具体数据
	answ_data = malloc(answ_frame_length-5);
	strncpy(answ_data, answ_frame+3, answ_frame_length-5);
	free(answ_frame);
	return answ_data;
}


//设置从机一个或多个开关输出状态
//单路开关量状态
//states 可选择： DIGIG_ON  (0xFF00) //开
//				  DIGIG_OFF (0x0000) //关 
//DIGIG_count  :  要设置的开关量个数  
int MODBUS_set_slave_state(int serial_fd, char slave_addr, short data_addr, short states, short DIGIG_count)
{
	
	char *request_frame = NULL;  //指向存储询问帧报文
	char *answ_frame = NULL;
	char fram_length = 0;
	
	if(DIGIG_count == 1)//设置单个开关量
	{
		fram_length = 8;
		request_frame = malloc(fram_length);
		//组装报文 从站地址+功能码+起始地址高位+起始地址低位+数据+校验和CRC
		snprintf(request_frame, 6, "%c%c%c%c%c%c", slave_addr, \
				MODBUS_SET_DIGIG_OP_DATA, data_addr>>8, data_addr&0x00FF, states>>8, states&0x00FF); 
		//计算组装CRC码
		short CRC_code = _CRC16((unsigned char*)request_frame, 6);
		*((short *)&request_frame[6]) = CRC_code;
	}
	else if(DIGIG_count > 1) //设置多个开关量
	{
		fram_length = 11;
		request_frame = malloc(fram_length);
		//组装报文 从站地址+功能码 起始地址高位+起始地址低位+寄存器个数高位+寄存器个数低位+字节计数+数据+校验和CRC
		snprintf(request_frame, 11, "%c%c%c%c%c%c%c%c%c", slave_addr, \
				MODBUS_SET_MULTI_DIGIG_OP_DATA, data_addr>>8, data_addr&0x00FF, data_count>>8, data_count&0x00FF, 0x02, states>>8, states&0x00FF); 
		//计算组装CRC码
		short CRC_code = _CRC16((unsigned char*)request_frame, 6);
		*((short *)&request_frame[9]) = CRC_code;
	}
	//发送报文
	write(serial_fd, request_frame, fram_length);
	//获取8字节的应答帧
	answ_frame = request_frame;
	read(serial_fd, answ_frame, 8);
	//校验CRC码
	CRC_code = *((short *)(answ_frame+6));
	if(_CRC16((unsigned char*)answ_frame, 6) != CRC_code){
		printf("校验出错\n");
		return -1;
	}
	
	if(DIGIG_count == 1)
	{
		//比对应答帧是否和询问帧是否一样，判断是否设置成功
	}
	if(DIGIG_count > 1)
	{
		//检查应答帧是否正确，判断是否设置成功
	}
	
}

//设置从机一个或多个模拟输出状态




int main()
{
	int i = 0;
	int MODBUS_ser_fd;
	
	MODBUS_ser_fd = MODBUS_serial_init("/dev/tty2");
	if(MODBUS_ser_fd == -1)
	{
		printf("串口打开失败\n");
		return -1;
	}

	//获取10个开关量输出状态
	char *MODBUS_data_point = MODBUS_get_slave_state(MODBUS_ser_fd, 2, MODBUS_GET_DIGIG_OP_DATA, 0x2020, 8);
	if(MODBUS_data_point != NULL)
	{
		printf("data_point = ");
		for(; i<8; i++)
			printf("-%#x ", *MODBUS_data_point++);
		printf("\n");
		free(MODBUS_data_point);
	}
	else	
		printf("获取失败\n");

	MODBUS_serial_close(MODBUS_ser_fd);
}

















