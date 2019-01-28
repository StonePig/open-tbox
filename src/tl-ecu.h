
#define MOD_FOTA 1


#if 0
#define ROM_MAUI_BACKUP_ADDR 	0x6A0000
#define UPGRADE_INFO_ADDR 		0x680000
#define ROM_TBOX_BACKUP_ADDR 	0x600000
#define DL_IMG_ADDR           	0x580000
#define ROM_TBOX_ADDR 			0x480000
#define ROM_MAUI_ADDR			0x28000
#else

#define ROM_MAUI_BACKUP_ADDR 	"ROM_MAUI_BACKUP_ADDR"
#define UPGRADE_INFO_ADDR 		"UPGRADE_INFO_ADDR"
#define ROM_TBOX_BACKUP_ADDR 	"ROM_TBOX_BACKUP_ADDR"
#define DL_IMG_ADDR           		"DL_IMG_ADDR"
#define ROM_TBOX_ADDR 			"ROM_TBOX_ADDR"
#define ROM_MAUI_ADDR			"ROM_MAUI_ADDR"

#endif

#define FS_HANDLE 		FILE *
#define FS_READ_ONLY	"r"
#define FS_READ_WRITE	"w+"


#define FLASH_BLOCK_SIZE 		0x8000
#define FLASH_PAGE_SIZE 		0x200
#define ROM_TBOX_IMG_MAX_SIZE   0x80000



/** Signed 8-bit */
typedef char               rs_s8;

/** Signed 16-bit */
typedef short             rs_s16;

/** Signed 32-bit */
typedef int               rs_s32;

/** Unsigned 8-bit */
typedef unsigned char      rs_u8;

/** Unsigned 16-bit */
typedef unsigned short    rs_u16;

/** Unsigned 32-bit */
typedef unsigned int      rs_u32;


/** Update Agent Boolean Definition */
typedef int              rs_bool;

#ifndef U8
typedef unsigned char U8;
#endif

#ifndef S8
typedef char S8;
#endif


#ifndef U16
typedef unsigned char U16;
#endif


#ifndef MMI_BOOL
#define MMI_BOOL kal_bool
#endif

#ifndef U32
typedef unsigned int U32;
#endif


#define KAL_FALSE FALSE
#define KAL_TRUE TRUE

#define  kal_bool rs_bool




#define RJ_GPS_VERSION2	"TBOX_AG35_V1.00_20190124"

#define  ECU_BOOTLOADER_STARTUP_CMD "12345678"
#define  ECU_QUERY_CAN_DATA_LENGTH	13
#define  ECU_CAN_BUFF_LEN			50


#define DEV_MessageID	0x18F1F002
#define TBOX_MessageID	0x18F1F001


#define RS_ERR_FAILED                      -1  // δ�������ʹ���
#define RS_ERR_OK                           0   // �ɹ�
#define RS_DL_STATE_ERROR                   1   // ��ǰ״̬����
#define RS_SESSION_STATE_ERROR              2   // ÿһ��session��״̬����
#define RS_USER_CANCEL                      3   // �û�ȡ����ǰ����
#define RS_CARD_NOT_INSERT                  4   // SIM��û�в���
#define RS_CARD_NOT_RECOGNIZE               5   // ����ʶ��
#define RS_SYSTEM_BUSY                      6   // ϵͳ��æ
#define RS_IMEI_NOT_WRITTEN					7	// IMEI not written
#define RS_ERR_INVALID_PARAM                9  // ��Ч����


#define SPEC_CHAR 0x23 

#define MAX_SENDBUF_SIZE      		100


typedef enum
{
	SW_STATUS_UPGRADE_NONE,
	SW_STATUS_UPGRADE_SUCCESS,
	SW_STATUS_UPGRADE_FAIL,
	SW_STATUS_UPGRADE_NEED,
	SW_STATUS_UPGRADE_DONE,
	SW_STATUS_RESTORE_NEED,
	SW_STATUS_RESTORE_SUCCESS,
	SW_STATUS_RESTORE_FAIL,
	SW_STATUS_RESTORE_DONE,
	SW_STATUS_ECUUPGRADE_NEED
}SW_STATUS_ENUM;


//���ݵ�Ԫ����
typedef enum
{
	MSG_CODE_TBOX_UPGRADE_QUERY_CMD = 0x00,
	MSG_CODE_ECU_UPGRADE_QUERY_CMD = 0x01,
	MSG_CODE_TBOX_PACKAGE_HEAD_CMD = 0x02,
	MSG_CODE_TBOX_PACKAGE_DATA_CMD = 0x03,
	MSG_CODE_ECU_SET_LIFETIME_CMD = 0x04,
	MSG_CODE_ECU_QUERY_PARM_CMD = 0x05,
	MSG_CODE_DEV_CMD = 0x06,				//���豸��������
	MSG_CODE_DEV_RSD = 0x07,				//�豸��������������ƶ�
	MSG_CODE_NONE = 0xFF
}MSG_CODE_ENUM;

typedef enum
{
	STATUS_DOWNLOAD_TBOX_NONE = 0x00,
	STATUS_DOWNLOAD_TBOX_QUERY_CMD = 0x01,	
	STATUS_DOWNLOAD_ECU_QUERY_CMD = 0x02,
	STATUS_DOWNLOAD_TBOX_PACKAGE_HEAD_CMD = 0x03,
	STATUS_DOWNLOAD_TBOX_PACKAGE_DATA_CMD = 0x04
}STATUS_DOWNLOAD_ENUM;




typedef struct
{
	U16 buf_len;
	U16 send_flow;
	S8	ini_flag;
	S8  send_statue;
	S8  cmd;/*1 cmd, 0 respond*/
	S8  send_times;
	S8  send_count;
	S8  send_type;
	S8  send_time[6];
	S8  send_buf[MAX_SENDBUF_SIZE];
}Send_Info;




typedef struct
{
	SW_STATUS_ENUM status;
	U8 reboot_count_after_upgrade;
	U32 imgLen;
}sw_status_struct;

//��ְ���Ϣ��ʽ
typedef struct
{
	U32 totalByte;
	U16 totalPkgNum;
	U16 pkgSize;
	U8 checkSum;
}pkg_header_struct;


//��ְ����ݸ�ʽ
typedef struct
{
	U16 curPkgNum;
	U8 *data;
}pkg_data_struct;


//�ӿ�����
typedef enum
{
	DEV_TYPE_CAN = 0x01,
	DEV_TYPE_UART = 0x02,
	DEV_TYPE_OTHER
}DEV_TYPE_ENUM;



//͸�����ݿ��Ƶ�Ԫ���ݸ�ʽ
typedef struct
{
	U8  devType;	//�������豸ͨ�Žӿ�����
	U32 devAddr;	//�������豸���豸��ַ
	U8  devIdLenth;	//�������豸ID����
	U8* devId;		//�������豸ID
	U8  comPara;	//�������
	U16 dataLenth;	//͸�����ݳ���
	U8* data;		//͸������ָ��
}passthrough_data_struct;


//����������ݸ�ʽ
typedef struct
{
	U8  devType;	//�������豸ͨ�Žӿ�����
	U32 devAddr;	//�������豸���豸��ַ
	U8  devIdLenth;	//�������豸ID����
	U8* devId;		//�������豸ID
	U8  comPara;	//�������
}com_parm_struct;



typedef union
{
	U8 frameHeader;
	struct
	{
		U8 FRAME_NUM	:6;			//��ʾ��ǰ֡��֡�ţ���Χ0-63���������֡��־Ϊ1�����ʾΪ���һ֡��ʵ�����ݳ��ȣ�����֡ͷ��
		U8 END_FRAME	:1;			//�Ƿ����֡��0���������һ֡������1����ʾ��ǰΪ���һ֡������
		U8 CMD_RES		:1;			//1������֡��0��Ӧ��֡����������֡�������������ͷ�����Ӧ����Ϣ
    }frameHeaderBit;    
}frame_header_struct;


typedef enum
{
	CMD_DEV_INFO = 0xA1,
	CMD_RUMTIME_SET = 0xA2,
	CMD_RUMTIME_QUERY = 0xA3,
	CMD_STATUS_QUERY = 0xA4,
	CMD_ERROR_REPORT = 0xA5,
	CMD_STATUS_CONSTATUS_REPORT_SET = 0xA6,
	CMD_STATUS_CONSTATUS_REPORT_QUERY = 0xA7,
	CMD_STATUS_ERROR_RSP = 0xFF
}cmd_type_enum;


void tbox_fota_start(void);





