#include <stdio.h>
#include <stdlib.h>
#include "datatype.h"


#define MAX_SENDBUF_SIZE      		100 //512
#define MAX_SENDBUF_LEN				100
#define MAX_RECVBUF_SIZE      		500 //512


#define MAX_MUTI_SENDBUF_SIZE      	1000


//设备信息
typedef struct
{
	S8 model[10];
	S8 sw[20];
	S8 iccid[ICCID_SIZE];
	S8 imei[IMEI_SIZE];
	S8 bootMode;
}msg_devinfo_type_t;

//位置信息 ID 2
typedef struct {
						//命令ID	 1
	S8  gps_status;		//定位状态	1
	S8  nsat;			//nsat	1
	S8  hdop;			//HDOP	1
	S8  ew;				//E/W	1
	S8  gps_pos_long[4];//经度值		4
	S8  ns;				//N/S	1
	S8  gps_pos_lati[4];	//纬度值		4
	S8  speed[2];			//速度	2
	S8  gps_pos_alti[4];	//海拔		4
	S8  cog[2];			//方位角	2
	S8  cell_id[4];		//cell-id 信息	4
	S8  rssi;			//RSSI强度	1
	S8  batsoc;			//电池可用电量	1
	S8  batvol[2];			//外接电瓶电压值	2
	S8  acc_status;		//acc状态
}msg_pos_type_t;


//报警信息
typedef struct
{
	S8 alarm_fense_exit_status;					//电子围栏出栏告警
	S8 alarm_fense_enter_status;				//电子围栏入栏告警
	S8 alarm_vib_status;						//震动告警
	S8 alarm_inter_batt_low_status;				//后备电池低电告警
	S8 alarm_ext_batt_low_status;				//外接电瓶低电量告警
	S8 alarm_ext_batt_high_temp_status;			//外接电瓶高温告警
	S8 alarm_ext_batt_disconnet_status;			//外接电瓶断电告警
	S8 alarm_ill_move_status; 					//非法移动告警
	S8 alarm_limit_speed_status;				//超速告警
}msg_alarm_type_t;


typedef enum
{
    SEND_OTHER = 0,
    SEND_DEV_INFO = 1 ,			//开机时发送设备信息
    SEND_GPSPOS = 2,			//位置信息
    SEND_HEART = 3,				//心跳 
    DEV_SETTING = 4,			//配置信息
    SEND_ALARM = 5,
    SEND_LOCATION_IMMEDIATE = 6,
    SEND_STATIONPOS,
    SEND_ALARM_LOW,
    ALARM_POWEROFF,
    ALARM_MOVE,
    ALARM_ZD,
    SEND_SLEEPPOS,
    SEND_SEAL_COUNT,
    SEND_NOREPLY
}send_msg_type;



typedef enum
{
    SEND_STATUS_SENDING = 0,
    SEND_STATUS_SENTED = 1
}send_status_type;



typedef struct
{
    U16 buf_len;/*发送字节长度*/
	U16 send_flow;/*发送流水号*/
    S8	ini_flag;/*标识0可覆盖，1重要数据*/
    S8  send_statue;/*1完成 0未完成*/
	S8  cmd;/*1 cmd, 0 respond*/
    S8  send_times;/*发送次数*/
    S8  send_count;//发送计数次数
    S8  send_type;/*发送指令类型*/
	S8  send_time[6];/*发送时间*/
    S8  send_buf[MAX_SENDBUF_SIZE];//数据
}Send_Info;

typedef struct
{
    S32 g_n_send_index;
    S8 g_n_send_state;
    S8 g_n_rev_state;
    S32 g_n_send_indexAdd;
    Send_Info send_info[MAX_SENDBUF_LEN];
}Socket_Send;




extern msg_devinfo_type_t devInfoMsg2IOT;
extern msg_alarm_type_t alarmMsg2IOT;


U8 sendMsgTaskInit(void);
void sendMsgTask();
void sendOfflineData(void);
U16 sendMsg2IOT(send_msg_type sendMsgType);
void saveOffineData(void);
void giveSem2SendRespond(S8 send_flow);
void giveSem2Send(void);
void add2sendbuf(Send_Info *sendInfo);
S32 getUnsendMsgNum(void);






