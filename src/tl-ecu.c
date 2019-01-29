#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>


#include "tl-ecu.h"
#include "tl-net.h"


void Bird_soc_sendbufAdd2(Send_Info *_sendinfo);

U8 get_encryption();
void tbox_delay_reset();
void sendOnePkgData2CAN(U32 canAddrID, U8 *sendData);	


void tbox_upgrade_sw_res(U8* rest_buf, U32 length);
void passthroughCmd2Tbox(U8* rest_buf, U32 length);
void passthroughRes2Tbox(U8* rest_buf, U32 length);



static kal_bool savePkgData(pkg_data_struct pkgData);
static kal_bool sendACKToServer(MSG_CODE_ENUM msg , kal_bool isACKError);
static kal_bool checkDevConected(void);


static STATUS_DOWNLOAD_ENUM downloadStatus;
static pkg_header_struct pkgHeader;
static pkg_data_struct pkgData;
static U8 buf[802];
static U16 bufLen, curPageNum = 0;
static U8 msgType, cmdType;
static U8 ecuParmData[65][8];
static U8 ecuCanDataBuf[ECU_CAN_BUFF_LEN][8];
static U8 dataSend2Cloud[512];
static U8 devID[100];

static U16 dataSend2CloudLen;


static U16 ecuCanDataWriteP = 0;
static U16 ecuCanDataReadP = 0;

static U8 runTimeData[8];
static com_parm_struct curComParm;
static U16 canFrameNum = 0;

kal_bool isDevConnected = FALSE;
static kal_bool isReportConnectFun = TRUE;






static void Delayms(U32 data);
static void SaveToFlashTimerProc(void);
static SW_STATUS_ENUM readSWStatus(void);
static kal_bool writeSWStatus(SW_STATUS_ENUM st);
static kal_bool sendData2Dev(passthrough_data_struct pst, kal_bool isCmd);


void devConnectHeart(void);

extern void tl_net_tbox_connection_packet_output_request(S8 * send_data, U16 len);
extern void tl_serial_request_send_one_can_pkg_data(U32 canAddrID, U8 *sendData);
extern void get_vin_id(S8 *vin);


#define kal_prompt_trace	PRINTF


#if 1
char printf_buf[1024];

void PRINTF(U8 argus, const char *fmt, ...)
{
	if(1)
	{
		va_list args;																									
		va_start(args, fmt);							   
		vsprintf(printf_buf, fmt, args);
		va_end(args);
		puts(printf_buf);
	}
}
#endif

rs_s32 rs_flash_init(void)
{
	return RS_ERR_OK;
}


rs_s32 rs_flash_eraseBlock(rs_u8 *file)
{
	return RS_ERR_OK;
}
rs_s32 rs_flash_writePage(rs_u8 *file, const rs_u8* buffer, rs_s32 len)
{
	return RS_ERR_OK;
}

rs_s32 rs_flash_readPage(rs_u8 *file, rs_u8* buffer, rs_s32 len)
{
	return RS_ERR_OK;
}

FS_HANDLE FS_Open(U8 * file, U8 * rw)
{
	return fopen(file, rw);
}

void FS_Close(FS_HANDLE hFile)
{
	fclose(hFile);
}

void FS_Read(FS_HANDLE hFile,  U8 *readBuf,  U32 recordSize, U32 *ReadLen)
{
	fread(readBuf, 1, recordSize, hFile);
	*ReadLen = recordSize;
}

void FS_Write(FS_HANDLE hFile,  U8 *writeBuf,  U32 recordSize, U32 *WriteLen)
{
	fwrite(writeBuf, 1, recordSize, hFile);
	*WriteLen = recordSize;
}

void FS_Seek(FS_HANDLE hFile , U32 offset , U32 fromwhere)
{
	fseek(hFile, offset, fromwhere);
}

void FS_GetFileSize(FS_HANDLE hFile , U32 *fileLen)
{
    fseek(hFile,0L,SEEK_END);  
    *fileLen = ftell(hFile); 
}

void FS_Delete(U8 * file)
{
	remove(file);
}



/*判断tobx fota升级是否成功*/
void tbox_fota_start(void)
{
	rs_u8 buffer[FLASH_PAGE_SIZE];
	sw_status_struct *sw_status;
	
	if(rs_flash_init()!=RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_init err");
		return;
	}

	rs_flash_readPage(UPGRADE_INFO_ADDR, buffer , FLASH_PAGE_SIZE);
	sw_status = (sw_status_struct *)buffer;

	kal_prompt_trace(MOD_FOTA, "rs_fota_start sw_status = %d reboot_count_after_upgrade = %d" , sw_status->status,sw_status->reboot_count_after_upgrade);

	if(sw_status->status != SW_STATUS_UPGRADE_DONE && sw_status->status != SW_STATUS_RESTORE_DONE)
		return;
		
	if(sw_status->status == SW_STATUS_UPGRADE_DONE)
		sw_status->status =	SW_STATUS_UPGRADE_SUCCESS;

	
	if(sw_status->status == SW_STATUS_RESTORE_DONE)
		sw_status->status =	SW_STATUS_RESTORE_SUCCESS;

	if(rs_flash_eraseBlock(UPGRADE_INFO_ADDR) != RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
		return;
	}
	if(rs_flash_writePage(UPGRADE_INFO_ADDR , buffer, 512) != RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_writePage err");
		return;
	}
}

SW_STATUS_ENUM readSWStatus(void)
{
	rs_u8 buffer[FLASH_PAGE_SIZE];
	sw_status_struct *sw_status;
	
	if(rs_flash_init()!=RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_init err");
		return;
	}

	rs_flash_readPage(UPGRADE_INFO_ADDR, buffer , FLASH_PAGE_SIZE);
	sw_status = (sw_status_struct *)buffer;
	return sw_status->status;

}

kal_bool writeSWStatus(SW_STATUS_ENUM st)
{
	rs_u8 buffer[FLASH_PAGE_SIZE];
	sw_status_struct *sw_status;

	kal_prompt_trace(MOD_FOTA, "writeSWStatus");

	if(rs_flash_init()!=RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_init err");
		return KAL_FALSE;
	}


	rs_flash_readPage(UPGRADE_INFO_ADDR, buffer , FLASH_PAGE_SIZE);
	sw_status = (sw_status_struct *)buffer;
	sw_status->status =	st;
	
	if(rs_flash_eraseBlock(UPGRADE_INFO_ADDR) != RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
		return KAL_FALSE;
	}
	if(rs_flash_writePage(UPGRADE_INFO_ADDR , buffer, 512) != RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_writePage err");
		return KAL_FALSE;
	}
	return KAL_TRUE;

}

//数据发给dev，并且等待dev响应后，反馈给云端
void passthroughCmd2Tbox(U8 *rest_buf, U32 length)
{
	passthrough_data_struct pst;
	kal_bool ret;
	
	kal_prompt_trace(MOD_FOTA, "passthroughCmd2Tbox");
	
	cmdType = MSG_CODE_DEV_CMD;
	memset(ecuParmData, 0xff, 65 * 8);
	canFrameNum = 0;
	dataSend2CloudLen = 0;

	//获取数据
	pst.devType = *rest_buf++;
	pst.devAddr = ((rest_buf[0] << 24) & 0xFF000000) + ((rest_buf[1] << 16) & 0xFF0000) + ((rest_buf[2] << 8) & 0xFF00) + ((rest_buf[3]) & 0xFF) ;
	rest_buf = rest_buf + 4; 
	pst.devIdLenth = *rest_buf++;
	pst.devId = rest_buf;
	rest_buf = rest_buf + pst.devIdLenth;
	pst.comPara = *rest_buf++;
	pst.dataLenth = length - pst.devIdLenth - 7;
	pst.data = rest_buf;

	kal_prompt_trace(MOD_FOTA, "devType=%x,devAddr=%x,devId=%s,comPara=%x", pst.devType , pst.devAddr, pst.devId, pst.comPara);
	
	//检查dev id是否正确，如果不存在发错误应答给云端
	if(pst.data[0] != CMD_DEV_INFO)
	{
		if(strncmp(curComParm.devId, pst.devId, pst.devIdLenth) != 0)
		{
			kal_prompt_trace(MOD_FOTA, "passthroughCmd2Tbox:dev id error");
			dataSend2CloudLen = 0;
			sendACKToServer(MSG_CODE_DEV_CMD, KAL_FALSE);
			return;
		}
	}
	curComParm.devType = pst.devType;
	curComParm.devAddr = pst.devAddr;
	//curComParm.devIdLenth = pst.devIdLenth;
	//if(curComParm.devIdLenth < 100)
	//	memcpy(devID, pst.devId, pst.devIdLenth);
	//curComParm.devId = devID;
	curComParm.comPara = pst.comPara;
	

	if(pst.data[0] == CMD_STATUS_CONSTATUS_REPORT_SET)
	{
		isReportConnectFun = pst.data[3];
	}

	//转成设备数据后发给设备
	ret = sendData2Dev(pst, KAL_TRUE);
	
	//数据格式错误发送错误应答，否则等待dev返回信息后，再做应答。
	if(ret == KAL_FALSE)
	{
		dataSend2CloudLen = 0;
		sendACKToServer(MSG_CODE_DEV_CMD, KAL_FALSE);
	}
	//for test
	sendACKToServer(MSG_CODE_DEV_CMD, KAL_TRUE);
	
}

//数据转发给dev即可
void passthroughRes2Tbox(U8 *rest_buf, U32 length)
{
	passthrough_data_struct pst;
	kal_bool ret;
	
	kal_prompt_trace(MOD_FOTA, "passthroughRes2Tbox");
	
	//msgType = MSG_CODE_DEV_RSD;

	//获取数据
	pst.devType = *rest_buf++;
	pst.devAddr = ((rest_buf[0] << 24) & 0xFF000000) + ((rest_buf[1] << 16) & 0xFF0000) + ((rest_buf[2] << 8) & 0xFF00) + ((rest_buf[3]) & 0xFF) ;
	rest_buf = rest_buf + 4; 
	pst.devIdLenth = *rest_buf++;
	pst.devId = rest_buf;
	rest_buf = rest_buf + pst.devIdLenth;
	pst.comPara = *rest_buf++;
	pst.dataLenth = length - pst.devIdLenth - 7;
	pst.data = rest_buf;

	kal_prompt_trace(MOD_FOTA, "devType=%d,devAddr=%d,devId=%s,comPara=%d", pst.devType , pst.devAddr, pst.devId, pst.comPara);


	//检查dev id是否正确，如果不存在发错误应答给云端
	if(pst.data[0] != CMD_DEV_INFO)
	{
		if(strncmp(curComParm.devId, pst.devId, pst.devIdLenth) != 0)
		{
			kal_prompt_trace(MOD_FOTA, "passthroughRes2Tbox:dev id error");
			dataSend2CloudLen = 0;
			//sendACKToServer(MSG_CODE_DEV_RSD, KAL_FALSE);
			return;
		}
	}
	//转成设备数据后发给设备
	ret = sendData2Dev(pst, KAL_FALSE);

}


//转成can格式的数据后，发送给设备
kal_bool sendData2Dev(passthrough_data_struct pst, kal_bool isCmd)
{
	U8 pkgCanData[8];
	frame_header_struct pkgFrameHeader;
	U8 checkSum;
	U16 i, pkgSize;

	for(i = 0; i < pst.dataLenth; i++)
 		kal_prompt_trace(MOD_FOTA, "%x", pst.data[i]);
	
	//检查数据长度是否正确
	if(pst.dataLenth != *(pst.data + 2) + 3)
	{
		kal_prompt_trace(MOD_FOTA, "sendData2Dev:pst.dataLenth=%d, %d", pst.dataLenth, *(pst.data + 2) + 3);
		return KAL_FALSE;
	}

	//检查应答标识是否一致
	if(isCmd)
	{
		if(*(pst.data + 1) != 0xFE)
		{
			kal_prompt_trace(MOD_FOTA, "sendData2Dev:response flag error");
			return KAL_FALSE;
		}
			
	}

	checkSum = 0;
	checkSum ^= *pst.data;

	for(i = 0; i < pst.dataLenth - 3; i++)
		checkSum ^= *(pst.data + i + 3);
	
	if(pst.devType == DEV_TYPE_CAN)
	{
		//数据单元长度小于等于5
		if(pst.dataLenth - 3 <= 5)
		{
			pkgFrameHeader.frameHeaderBit.CMD_RES = isCmd;
			pkgFrameHeader.frameHeaderBit.END_FRAME = 1;		
			pkgFrameHeader.frameHeaderBit.FRAME_NUM = (pst.dataLenth - 3) + 3; //数据单元长度 + 1个字节帧头 + 1个字节命令字+  1个字节的检验码
			kal_prompt_trace(MOD_FOTA, "sendData2Dev:pst.pkgFrameHeader=%d, ", pkgFrameHeader.frameHeader);

			memset(&pkgCanData[0], 0xFF, 8);
			pkgCanData[0] = pkgFrameHeader.frameHeader; 						//帧头
			pkgCanData[1] = *pst.data;											//命令
			memcpy(&pkgCanData[2], (pst.data + 3), pst.dataLenth - 3); 		//数据单元内容
			pkgCanData[pkgFrameHeader.frameHeaderBit.FRAME_NUM - 1] = checkSum; //校验码

			sendOnePkgData2CAN(TBOX_MessageID, pkgCanData);
			return KAL_TRUE;
		}
		//数据单元长度大于5
		else
		{
			if(pst.dataLenth - 3 <= 12)
				pkgSize = 0;										//只有首帧和最后一帧，没有中间帧数据
			else
				pkgSize = ((pst.dataLenth - 3 - 6 - 6) - 1) / 7 + 1; //去掉首帧和最后一帧以后的帧数

			//首帧数据
			pkgFrameHeader.frameHeaderBit.CMD_RES = isCmd;
			pkgFrameHeader.frameHeaderBit.END_FRAME = 0;		
			pkgFrameHeader.frameHeaderBit.FRAME_NUM = 0;
			kal_prompt_trace(MOD_FOTA, "sendData2Dev:pst.pkgFrameHeader=%d, ", pkgFrameHeader.frameHeader);

			memset(&pkgCanData[0], 0xFF, 8);
			pkgCanData[0] = pkgFrameHeader.frameHeader; 						//帧头
			pkgCanData[1] = *pst.data;											//命令
			memcpy(&pkgCanData[2], (pst.data + 3), 6); 						//数据单元内容

			sendOnePkgData2CAN(TBOX_MessageID, pkgCanData);
			
			//中间帧
			for(i = 0; i < pkgSize; i++)
			{
				pkgFrameHeader.frameHeaderBit.CMD_RES = isCmd;
				pkgFrameHeader.frameHeaderBit.END_FRAME = 0;		
				pkgFrameHeader.frameHeaderBit.FRAME_NUM = i + 1;
				kal_prompt_trace(MOD_FOTA, "sendData2Dev:pst.pkgFrameHeader=%d, ", pkgFrameHeader.frameHeader);

				memset(&pkgCanData[0], 0xFF, 8);
				pkgCanData[0] = pkgFrameHeader.frameHeader; 						//帧头
				memcpy(&pkgCanData[1], (pst.data + 3 + 6 + 7 * i), 7); 		//数据单元内容

				sendOnePkgData2CAN(TBOX_MessageID, pkgCanData);
			}
			
			//结束帧
			pkgFrameHeader.frameHeaderBit.CMD_RES = isCmd;
			pkgFrameHeader.frameHeaderBit.END_FRAME = 1;		
			pkgFrameHeader.frameHeaderBit.FRAME_NUM = (pst.dataLenth - 3) - 6 - pkgSize * 7 + 2;
			kal_prompt_trace(MOD_FOTA, "sendData2Dev:pst.pkgFrameHeader=%d, ", pkgFrameHeader.frameHeader);

			memset(&pkgCanData[0], 0xFF, 8);
			pkgCanData[0] = pkgFrameHeader.frameHeader; 												//帧头
			memcpy(&pkgCanData[1], (pst.data + 3 + 6 + 7 * pkgSize), pkgFrameHeader.frameHeaderBit.FRAME_NUM - 2); 		//数据单元内容
			pkgCanData[pkgFrameHeader.frameHeaderBit.FRAME_NUM - 1] = checkSum; 						//校验码

			sendOnePkgData2CAN(TBOX_MessageID, pkgCanData);

			return KAL_TRUE;	
		}

	}
	if(pst.devType == DEV_TYPE_UART)
	{
		return KAL_FALSE;	//暂时还不能支持uart类型的设备
	}
	else
	{
		return KAL_FALSE;
	}
}



//解析数据单元
void tbox_upgrade_sw_res(U8* rest_buf, U32 length)
{
	U8 specCharNum;
	U16 i, j, k, curSpecCharAddr, prevSpecCharAddr;
	U32 param_pos=0;
	U8 wordchar[3];
	U8 sgchar[2];
	U8 candata[8];
	

	kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res111 %d",length);
	
	if(0)//length<10)
	{
		kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res too small length");
		return;
	}
	if(length>600)
	{
		kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res too large length");
		return;
	}

	
	msgType = *rest_buf++;
	kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res msgType = %d",msgType);

	if(msgType == MSG_CODE_TBOX_PACKAGE_HEAD_CMD || msgType == MSG_CODE_TBOX_PACKAGE_DATA_CMD)
	{
		//去除转义字符
		specCharNum = *rest_buf++;
		kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res specCharNum = %d",specCharNum);
		if(specCharNum>600)
		{
			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res too many special character number");
			return;
		}

		if(specCharNum == 0 )
		{
			memcpy(&buf[0] , &rest_buf[0] , length - 2);
			bufLen = length - 2;
		}
		else
		{
		    bufLen = 0;
			prevSpecCharAddr = 0;
			k = 0;
			for(i = 0; i < specCharNum; i++)
			{
				curSpecCharAddr = rest_buf[i * 2] * 256 + rest_buf[i * 2 + 1];
				kal_prompt_trace(MOD_FOTA,"curSpecCharAddr = %x",curSpecCharAddr);
				for(j = prevSpecCharAddr; j < curSpecCharAddr; j++)
				{
					buf[bufLen++] = rest_buf[specCharNum * 2 + k];
					k++;
					//kal_prompt_trace(MOD_FOTA," %x",buf[bufLen-1]);
				}

				buf[bufLen++] = SPEC_CHAR;
				//kal_prompt_trace(MOD_FOTA," %x",buf[bufLen-1]);
				buf[bufLen++] = SPEC_CHAR;
				//kal_prompt_trace(MOD_FOTA," %x",buf[bufLen-1]);
				prevSpecCharAddr = curSpecCharAddr + 2;
			}
			kal_prompt_trace(MOD_FOTA,"bufLen = %d",bufLen);
			memcpy(&buf[bufLen] , &rest_buf[specCharNum * 2 + k] , length -2 - bufLen);
			bufLen = length - 2;
		}
	}
	else
	{
		memcpy(&buf[0] , &rest_buf[0] , length - 1);
		bufLen = length - 1;
	}
	
	switch(msgType)
	{
		case MSG_CODE_TBOX_UPGRADE_QUERY_CMD:
			//check version is legal
			buf[bufLen] = 0;
			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res sw version = %s",buf);
			downloadStatus = STATUS_DOWNLOAD_TBOX_QUERY_CMD;
			cmdType = MSG_CODE_TBOX_UPGRADE_QUERY_CMD;
			sendACKToServer(MSG_CODE_TBOX_UPGRADE_QUERY_CMD,KAL_TRUE);
			break;
		case MSG_CODE_ECU_UPGRADE_QUERY_CMD:
			//check version is legal
			buf[bufLen] = 0;
			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res sw version = %s",buf);
			downloadStatus = STATUS_DOWNLOAD_ECU_QUERY_CMD;
			cmdType = MSG_CODE_ECU_UPGRADE_QUERY_CMD;
			sendACKToServer(MSG_CODE_ECU_UPGRADE_QUERY_CMD,KAL_TRUE);
			break;
			
		case MSG_CODE_TBOX_PACKAGE_HEAD_CMD:
			if(downloadStatus != STATUS_DOWNLOAD_TBOX_QUERY_CMD && downloadStatus != STATUS_DOWNLOAD_ECU_QUERY_CMD)
			{
				kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res downloadStatus error when recieve MSG_CODE_TBOX_PACKAGE_HEAD_CMD");
				//send error ack
				sendACKToServer(MSG_CODE_TBOX_PACKAGE_HEAD_CMD,KAL_FALSE);
				break;
			}
			
			pkgHeader.totalByte = ((buf[0]<<24)&0xFF000000)|((buf[1]<<16)&0xFF0000)|((buf[2]<<8)&0xFF00)|((buf[3])&0xFF);
			pkgHeader.totalPkgNum = ((buf[4]<<8)&0xFF00)|((buf[5])&0xFF);
			pkgHeader.pkgSize = ((buf[6]<<8)&0xFF00)|((buf[7])&0xFF);
			pkgHeader.checkSum = buf[8];

			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res sw totalByte = %d totalPkgNum = %d pkgSize = %d checkSum = %d",pkgHeader.totalByte, pkgHeader.totalPkgNum, pkgHeader.pkgSize, pkgHeader.checkSum);

			if((pkgHeader.pkgSize > 600) || (pkgHeader.totalPkgNum > 1200) || pkgHeader.totalByte > 512*1024)
			{
				kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res date format error");
				//send error ack
				sendACKToServer(MSG_CODE_TBOX_PACKAGE_HEAD_CMD,KAL_FALSE);
				break;
			}
			cmdType = MSG_CODE_TBOX_PACKAGE_HEAD_CMD;
			//send correct ack
			sendACKToServer(MSG_CODE_TBOX_PACKAGE_HEAD_CMD,KAL_TRUE);
			break;
			
		case MSG_CODE_TBOX_PACKAGE_DATA_CMD:
			if(cmdType != MSG_CODE_TBOX_PACKAGE_HEAD_CMD && cmdType != MSG_CODE_TBOX_PACKAGE_DATA_CMD)
			{
				kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res downloadStatus error when recieve MSG_CODE_TBOX_PACKAGE_DATA_CMD");
				//send error ack
				sendACKToServer(MSG_CODE_TBOX_PACKAGE_DATA_CMD,KAL_FALSE);
				break;
			}

			pkgData.curPkgNum = ((buf[0]<<8)&0xFF00)|((buf[1])&0xFF);
			pkgData.data = &buf[2];
			bufLen = bufLen - 2;

			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res sw pkgData.curPkgNum = %d",pkgData.curPkgNum);
			if(pkgData.curPkgNum > pkgHeader.totalPkgNum - 1)
			{
				kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_rescur PkgNum error");
				//send error ack
				sendACKToServer(MSG_CODE_TBOX_PACKAGE_DATA_CMD,KAL_FALSE);
				break;
			}			
			if(savePkgData(pkgData)==KAL_FALSE)
			{
				kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_rescur PkgNum savePkgData error");
				//send error ack
				sendACKToServer(MSG_CODE_TBOX_PACKAGE_DATA_CMD,KAL_FALSE);
				break;
			}				
			cmdType = MSG_CODE_TBOX_PACKAGE_DATA_CMD;
			//send correct ack
			sendACKToServer(MSG_CODE_TBOX_PACKAGE_DATA_CMD,KAL_TRUE);
			break;

		case MSG_CODE_ECU_SET_LIFETIME_CMD:
			cmdType = MSG_CODE_ECU_SET_LIFETIME_CMD;
			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res MSG_CODE_ECU_SET_LIFETIME_CMD");
			candata[0] = 0xE0;
			candata[1] = 0x00;
			candata[2] = buf[0];
			candata[3] = buf[1];
			candata[4] = buf[2];
			candata[5] = buf[3];
			candata[6] = buf[4];
			candata[7] = 0xFF;
			sendOnePkgData2CAN(TBOX_MessageID, candata);
			//sendACKToServer(MSG_CODE_ECU_SET_LIFETIME_CMD,KAL_TRUE);
			break;
		case MSG_CODE_ECU_QUERY_PARM_CMD:
			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res MSG_CODE_ECU_QUERY_PARM_CMD");
			cmdType = MSG_CODE_ECU_QUERY_PARM_CMD;
			candata[0] = 0xC0;
			candata[1] = 0x00;
			candata[2] = 0xFF;
			candata[3] = 0xFF;
			candata[4] = 0xFF;
			candata[5] = 0xFF;
			candata[6] = 0xFF;
			candata[7] = 0xFF;
			memset(ecuParmData, 0xff, 20 * 8);
			sendOnePkgData2CAN(TBOX_MessageID, candata);
			//sendACKToServer(MSG_CODE_ECU_QUERY_PARM_CMD,KAL_TRUE);
			break;
		default:
			kal_prompt_trace(MOD_FOTA,"tbox_upgrade_sw_res error msg type");
			//send error ack
			sendACKToServer(msgType,KAL_FALSE);
			break;
	}

}

static kal_bool sendACKToServer(MSG_CODE_ENUM msg, kal_bool isACKCorrect)
{
    Send_Info _send;
	U16 i;
	U8 check_code;
	U8 *strbegin, *strend;
	S8 vin[20];

    _send.buf_len=0;
    _send.ini_flag=0;
    _send.send_flow=0;
    //_send.send_type=BIRD_SOC_SEND_NOREPLY;
    //memset(_send.send_buf, 0, MAX_BIRD_SENDBUF_SIZE);
    kal_prompt_trace(MOD_FOTA,"sendACKToServer isACKCorrect = %d  %x", isACKCorrect, msg);

	_send.send_buf[_send.buf_len++] = SPEC_CHAR;
	_send.send_buf[_send.buf_len++] = SPEC_CHAR;
	_send.send_buf[_send.buf_len++] = 0xC2;
	
	if(isACKCorrect)
		_send.send_buf[_send.buf_len++] = 0x01;
	else
		_send.send_buf[_send.buf_len++] = 0x02;

	get_vin_id(vin);

	memcpy(&_send.send_buf[_send.buf_len] , vin, 17);
	
	_send.buf_len = 21;
	_send.send_buf[_send.buf_len++] = get_encryption();

	if(msg == MSG_CODE_TBOX_PACKAGE_DATA_CMD)
	{

		_send.send_buf[_send.buf_len++] = 0;
		_send.send_buf[_send.buf_len++] = 3;
		_send.send_buf[_send.buf_len++] = msg;
		_send.send_buf[_send.buf_len++] = pkgData.curPkgNum/256;
		_send.send_buf[_send.buf_len++] = pkgData.curPkgNum%256;
	}
	else if(msg == MSG_CODE_TBOX_PACKAGE_HEAD_CMD)
	{
		_send.send_buf[_send.buf_len++] = 0;
		_send.send_buf[_send.buf_len++] = 1;
		_send.send_buf[_send.buf_len++] = msg;
	}
	else if(msg == MSG_CODE_TBOX_UPGRADE_QUERY_CMD)
	{
		_send.send_buf[_send.buf_len++] = (strlen((S8 *)RJ_GPS_VERSION2) + 1) / 256;
		_send.send_buf[_send.buf_len++] = (strlen((S8 *)RJ_GPS_VERSION2) + 1) % 256;
		_send.send_buf[_send.buf_len++] = msg;
		memcpy(&_send.send_buf[_send.buf_len], (S8 *)RJ_GPS_VERSION2, strlen((S8 *)RJ_GPS_VERSION2));
		_send.buf_len = _send.buf_len + strlen((S8 *)RJ_GPS_VERSION2);
	}
	else if(msg == MSG_CODE_ECU_SET_LIFETIME_CMD)
	{
		_send.send_buf[_send.buf_len++] = 0;
		_send.send_buf[_send.buf_len++] = 10;
		_send.send_buf[_send.buf_len++] = msg;
		_send.send_buf[_send.buf_len++] = 0;
		memcpy(&_send.send_buf[_send.buf_len], &runTimeData[0], 3);
		_send.buf_len = _send.buf_len + 3;
		_send.send_buf[_send.buf_len++] = 0;
		memcpy(&_send.send_buf[_send.buf_len], &runTimeData[3], 3);
		_send.buf_len = _send.buf_len + 3;
		_send.send_buf[_send.buf_len++] = isACKCorrect;
	}
	else if(msg == MSG_CODE_ECU_QUERY_PARM_CMD && isACKCorrect)
	{
		_send.send_buf[_send.buf_len++] = (ECU_QUERY_CAN_DATA_LENGTH * 8 + 1)/256;
		_send.send_buf[_send.buf_len++] = (ECU_QUERY_CAN_DATA_LENGTH * 8 + 1)%256;
		_send.send_buf[_send.buf_len++] = msg;
		memcpy(&_send.send_buf[_send.buf_len], &ecuParmData[0][0], ECU_QUERY_CAN_DATA_LENGTH * 8); // for test
		_send.buf_len = _send.buf_len + ECU_QUERY_CAN_DATA_LENGTH * 8;

	}
	else if(msg == MSG_CODE_DEV_CMD)
	{
		_send.send_buf[2] = 0xC3;

		//解析错误码  ：0xFF + 命令字 ->          命令字 + 错误应答 + 数据单元长度为0，总包的应答改为错误应答。
		if(dataSend2Cloud[0] == CMD_STATUS_ERROR_RSP) // 获取设备ID
		{
			_send.send_buf[3] = 0x02;   			//错误应答
			dataSend2Cloud[0] = dataSend2Cloud[1];  //命令字
			dataSend2Cloud[1] = 0x02;				//错误应答
			dataSend2Cloud[2] = 0;					//数据单元长度
			dataSend2CloudLen = 3;
		}

		if(dataSend2Cloud[0] == CMD_DEV_INFO) // 获取设备ID
		{
			curComParm.devId = devID;
			strbegin = strstr((S8 *)dataSend2Cloud + 3, (S8 *)"/") + 1;
			strbegin = strstr((S8 *)strbegin, (S8 *)"/") + 1;
			strend = strstr((S8 *)strbegin, (S8 *)"/");
			curComParm.devIdLenth = strend - strbegin;
			if(curComParm.devIdLenth < 100)
				memcpy(curComParm.devId, strbegin, curComParm.devIdLenth);
			kal_prompt_trace(MOD_FOTA,"sendACKToServer devIdLenth = %d" , curComParm.devIdLenth);
		}
				
		_send.send_buf[_send.buf_len++] = (dataSend2CloudLen + curComParm.devIdLenth + 6 + 1) / 256;
		_send.send_buf[_send.buf_len++] = (dataSend2CloudLen + curComParm.devIdLenth + 6 + 1) % 256;		
		//数据单元
		_send.send_buf[_send.buf_len++] = curComParm.devType;							//第三方设备通信接口类型
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 24) & 0xFF;			//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 16) & 0xFF;			//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 8) & 0xFF;				//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 0) & 0xFF;				//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = curComParm.devIdLenth;						//第三方设备ID长度
		memcpy(&_send.send_buf[_send.buf_len], curComParm.devId, curComParm.devIdLenth); //第三方设备ID
		_send.buf_len = _send.buf_len + curComParm.devIdLenth;
		_send.send_buf[_send.buf_len++] = curComParm.comPara;							//传输参数

		//透传数据单元
		memcpy(&_send.send_buf[_send.buf_len], dataSend2Cloud, dataSend2CloudLen); 
		_send.buf_len = _send.buf_len + dataSend2CloudLen;

		//dev上报的连接状态
		//if(dataSend2Cloud[0] == CMD_STATUS_CONSTATUS_REPORT_QUERY)
		//	return;

		msgType = MSG_CODE_NONE;
	}
	else if(msg == MSG_CODE_DEV_RSD)
	{
		_send.send_buf[2] = 0xC4;
		_send.send_buf[3] = 0xFE;	//应答标识为主发方
		_send.send_buf[_send.buf_len++] = (dataSend2CloudLen + curComParm.devIdLenth + 6 + 1) / 256;
		_send.send_buf[_send.buf_len++] = (dataSend2CloudLen + curComParm.devIdLenth + 6 + 1) % 256;
		
		//数据单元
		_send.send_buf[_send.buf_len++] = curComParm.devType;							//第三方设备通信接口类型
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 24) & 0xFF;			//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 16) & 0xFF;			//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 8) & 0xFF;				//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = (curComParm.devAddr >> 0) & 0xFF;				//第三方设备的设备地址;
		_send.send_buf[_send.buf_len++] = curComParm.devIdLenth;						//第三方设备ID长度
		memcpy(&_send.send_buf[_send.buf_len], curComParm.devId, curComParm.devIdLenth); //第三方设备ID
		_send.buf_len = _send.buf_len + curComParm.devIdLenth;
		_send.send_buf[_send.buf_len++] = curComParm.comPara;							//传输参数

		//透传数据单元
		memcpy(&_send.send_buf[_send.buf_len], dataSend2Cloud, dataSend2CloudLen); 
		_send.buf_len = _send.buf_len + dataSend2CloudLen;

		//msgType = MSG_CODE_NONE;
	}
	else
	{
		_send.send_buf[_send.buf_len++] = (U8 )((bufLen + 1)/256);
		_send.send_buf[_send.buf_len++] = (U8 )((bufLen + 1)%256);
		_send.send_buf[_send.buf_len++] = msg;
		memcpy(&_send.send_buf[_send.buf_len] , buf , bufLen);
		_send.buf_len = _send.buf_len + bufLen;
	}	

	//校验码
	check_code = 0;
	for (i = 2; i < _send.buf_len; i++)
		check_code ^= _send.send_buf[i];
	_send.send_buf[_send.buf_len++] = check_code;

    Bird_soc_sendbufAdd2(&_send);
}

/*

保存数据到DL_IMG区
*/
static kal_bool savePkgData(pkg_data_struct pkg)
{
	U16 pkgSize, pageNum;
	U32 i, j;
	kal_bool isLastPkg;
	U32 pkgAddr;
	FS_HANDLE fileHandle = -1;
	U32  WriteLen, fileLen, ReadLen;
	U8 check_code, tmp[FLASH_PAGE_SIZE];
	sw_status_struct *sw_status;
	
	if(pkg.curPkgNum == pkgHeader.totalPkgNum - 1)
	{
		pkgSize = bufLen;
		//检查合法性
		//软件包总字节数 - 拆分包大小 * （拆分包总数 ? 1）
		if(pkgSize != pkgHeader.totalByte - pkgHeader.pkgSize * (pkgHeader.totalPkgNum - 1))
		{
			kal_prompt_trace(MOD_FOTA,"savePkgData error the last pkgSize = %d" , pkgSize);
			return KAL_FALSE;
		}
		isLastPkg = KAL_TRUE;
	}
	else	
	{
		pkgSize = pkgHeader.pkgSize;
		//检查合法性
		if(pkgSize != bufLen)
		{
			kal_prompt_trace(MOD_FOTA,"savePkgData error pkgSize = %d bufLen = %d" , pkgSize, bufLen);
			return KAL_FALSE;
		}
		isLastPkg = KAL_FALSE;
	}
#if 0	
	//还没有保存完毕要收到新的数据时
	if(curPageNum)
	{
		kal_prompt_trace(MOD_FOTA,"savePkgData error curPageNum = %d" , curPageNum);
		return KAL_FALSE;
	}
#endif	
	if(pkg.curPkgNum == 0)
		FS_Delete(DL_IMG_FILE_NAME);
	
	//保存到文件系统中
	//fileHandle = FS_Open(L"C:\\dl_img.dat",FS_CREATE|FS_READ_WRITE);
	fileHandle = FS_Open(DL_IMG_FILE_NAME, FS_READ_WRITE);
	if (fileHandle == NULL)
    {
		kal_prompt_trace(MOD_FOTA,"savePkgData error fileHandle = %d" , fileHandle);
		return KAL_FALSE;
    }
	
	pkgAddr = pkg.curPkgNum * pkgHeader.pkgSize;
	FS_Seek(fileHandle , pkgAddr , 0);
	FS_Write(fileHandle, pkg.data , pkgSize, (U32 *)&WriteLen);
	kal_prompt_trace(MOD_FOTA,"savePkgData WriteLen = %d pkgSize = %d" , WriteLen , pkgSize);
	FS_Close(fileHandle);


	//收到最后一个包
	if(isLastPkg)
	{
		kal_prompt_trace(MOD_FOTA,"savePkgData is the last pkg");
		//检查checksum
		fileHandle = FS_Open(DL_IMG_FILE_NAME, FS_READ_ONLY);
		if (fileHandle == NULL)
	    {
			kal_prompt_trace(MOD_FOTA,"savePkgData error fileHandle = %d" , fileHandle);
			return KAL_FALSE;
	    }
		FS_GetFileSize(fileHandle , &fileLen);
		kal_prompt_trace(MOD_FOTA,"savePkgData fileLen = %d , pkgHeader.totalByte = %d" , fileLen , pkgHeader.totalByte);
		if(fileLen != pkgHeader.totalByte)
	    {
			//kal_prompt_trace(MOD_FOTA,"savePkgData error fileLen = %d , pkgHeader.totalByte = %d" , fileLen , pkgHeader.totalByte);
			FS_Close(fileHandle);
			return KAL_FALSE;
	    }		
		check_code = 0;
		for(i = 0 ; i < fileLen ; i++)
		{
			FS_Seek(fileHandle, i, 0);
			FS_Read(fileHandle, (void *)tmp, 1, &ReadLen);
			check_code ^= tmp[0];
		}
		if(check_code != pkgHeader.checkSum)
	    {
			kal_prompt_trace(MOD_FOTA,"savePkgData error check_code = %d , pkgHeader.checkSum = %d" , check_code , pkgHeader.checkSum);
			FS_Close(fileHandle);
			return KAL_FALSE;
	    }

		//保存到flash中
		if(rs_flash_init()!=RS_ERR_OK)
		{
			kal_prompt_trace(MOD_FOTA, "rs_flash_init err");
			FS_Close(fileHandle);
			return KAL_FALSE;
		}
		for(i = 0; i < ROM_TBOX_IMG_MAX_SIZE / FLASH_BLOCK_SIZE; i++)
		{
			if(rs_flash_eraseBlock(DL_IMG_ADDR + i * FLASH_BLOCK_SIZE) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
				FS_Close(fileHandle);
				return KAL_FALSE;
			}
		}

		curPageNum = 0;
#if 1		
		pageNum = fileLen / FLASH_PAGE_SIZE;
		if(fileLen % FLASH_PAGE_SIZE != 0)
			pageNum++;
		
		for(i = 0 ; i < pageNum ; i++)
		{
			FS_Seek(fileHandle, i * FLASH_PAGE_SIZE, 0);
			//if(i == pageNum - 1 && fileLen % FLASH_PAGE_SIZE != 0)
			//	FS_Read(fileHandle, (void *)tmp, fileLen % FLASH_PAGE_SIZE, &ReadLen);
			//else
				FS_Read(fileHandle, (void *)tmp, FLASH_PAGE_SIZE, &ReadLen);
			//for(j=0;j<FLASH_PAGE_SIZE/8;j++)
			//	kal_prompt_trace(MOD_FOTA,"tmp  %x,%x,%x,%x,%x,%x,%x,%x ReadLen = %d", tmp[0+j*8], tmp[1+j*8], tmp[2+j*8], tmp[3+j*8], tmp[4+j*8], tmp[5+j*8], tmp[6+j*8], tmp[7+j*8], ReadLen);
			//通过can数据发送给ECU
			if(0)//cmdType == MSG_CODE_ECU_UPGRADE_QUERY_CMD)
			{
				kal_prompt_trace(MOD_FOTA, "send can data to ecu starting");
				for(j = 0 ; j < FLASH_PAGE_SIZE / 8 ; j++)
				{
					sendOnePkgData2CAN(TBOX_MessageID, &tmp[j * 8]);
				}
				//Delayms(10);
			}
			kal_prompt_trace(MOD_FOTA,"page num = %d" , i);
			if(rs_flash_writePage(DL_IMG_ADDR + i * FLASH_PAGE_SIZE , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
			{
				FS_Close(fileHandle);
				return KAL_FALSE;
			}				
		}

		//写入TBOX固件待升级标志，重启
		if(cmdType == MSG_CODE_TBOX_UPGRADE_QUERY_CMD)
		{
			kal_prompt_trace(MOD_FOTA,"write tbox upgrade flag");
			sw_status = (sw_status_struct *)tmp;
			sw_status->status =	SW_STATUS_UPGRADE_NEED;
			sw_status->reboot_count_after_upgrade = 0;
			sw_status->imgLen = fileLen;
			if(rs_flash_eraseBlock(UPGRADE_INFO_ADDR) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
				FS_Close(fileHandle);
				return KAL_FALSE;
			}
			if(rs_flash_writePage(UPGRADE_INFO_ADDR , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_writePage err");
				FS_Close(fileHandle);
				return KAL_FALSE;
			}
			tbox_delay_reset(); // 10S后重启
		}
		
		//写入ECU固件待升级标志
		if(cmdType == MSG_CODE_ECU_UPGRADE_QUERY_CMD)
		{
			kal_prompt_trace(MOD_FOTA,"write ecu upgrade flag");
			sw_status = (sw_status_struct *)tmp;
			sw_status->status =	SW_STATUS_ECUUPGRADE_NEED;
			sw_status->reboot_count_after_upgrade = 0;
			sw_status->imgLen = fileLen;
			if(rs_flash_eraseBlock(UPGRADE_INFO_ADDR) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
				FS_Close(fileHandle);
				return KAL_FALSE;
			}
			kal_prompt_trace(MOD_FOTA,"savePkgData error fileHandle = %d" , fileHandle);
			if(rs_flash_writePage(UPGRADE_INFO_ADDR , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_writePage err");
				FS_Close(fileHandle);
				return KAL_FALSE;
			}
		}
#endif
	}

	FS_Close(fileHandle);
	return KAL_TRUE;
}

void SendUpgradeDataToECUTimerProc(void)
{
	U16 pageNum, j;
	U8  tmp[FLASH_PAGE_SIZE];
	sw_status_struct *sw_status;
	
	
	//StopTimer(BIRD_COUNTOR_TIMEOUT_TIMER);
	
	if(rs_flash_init()!=RS_ERR_OK)
	{
		kal_prompt_trace(MOD_FOTA, "rs_flash_init err");
		return;
	}

	rs_flash_readPage(UPGRADE_INFO_ADDR, tmp , FLASH_PAGE_SIZE);
	sw_status = (sw_status_struct *)tmp;

	pageNum = (sw_status->imgLen - 1) / FLASH_PAGE_SIZE + 1;

	//通过can数据发送给ECU
	if(rs_flash_readPage(DL_IMG_ADDR + curPageNum * FLASH_PAGE_SIZE , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
	{
		return;
	}

	kal_prompt_trace(MOD_FOTA, "send can data to ecu starting");
	for(j = 0 ; j < FLASH_PAGE_SIZE / 8 ; j++)
	{
		sendOnePkgData2CAN(TBOX_MessageID, &tmp[j * 8]);
	}
	//Delayms(10);

	kal_prompt_trace(MOD_FOTA,"curPageNum = %d pageNum = %d" , curPageNum , pageNum);

	curPageNum++;
	if(curPageNum == pageNum)
	{
		kal_prompt_trace(MOD_FOTA,"write ecu upgrade flag");
		writeSWStatus(SW_STATUS_UPGRADE_NONE);
		curPageNum = 0;
	}
	else
	{
		;//StartTimer(BIRD_COUNTOR_TIMEOUT_TIMER, 300, SendUpgradeDataToECUTimerProc);
	}
	
}

void SaveToFlashTimerProc(void)
{
	U16 pkgSize, pageNum, j;
	kal_bool isLastPkg;
	U32 pkgAddr;
	FS_HANDLE fileHandle = -1;
	U32  WriteLen, fileLen, ReadLen;
	U8 check_code, tmp[FLASH_PAGE_SIZE];
	sw_status_struct *sw_status;

	//StopTimer(BIRD_COUNTOR_TIMEOUT_TIMER);

	fileHandle = FS_Open(DL_IMG_FILE_NAME, FS_READ_ONLY);
	if (fileHandle ==NULL)
    {
    	curPageNum = 0;
		kal_prompt_trace(MOD_FOTA,"savePkgData error fileHandle = %d" , fileHandle);
		return;
    }
	FS_GetFileSize(fileHandle , &fileLen);


	pageNum = fileLen / FLASH_PAGE_SIZE;
	if(fileLen % FLASH_PAGE_SIZE != 0)
		pageNum++;

	FS_Seek(fileHandle, curPageNum * FLASH_PAGE_SIZE, 0);
	//if(i == pageNum - 1 && fileLen % FLASH_PAGE_SIZE != 0)
	//	FS_Read(fileHandle, (void *)tmp, fileLen % FLASH_PAGE_SIZE, &ReadLen);
	//else
		FS_Read(fileHandle, (void *)tmp, FLASH_PAGE_SIZE, &ReadLen);
	//for(j=0;j<FLASH_PAGE_SIZE/8;j++)
	//	kal_prompt_trace(MOD_FOTA,"tmp	%x,%x,%x,%x,%x,%x,%x,%x ReadLen = %d", tmp[0+j*8], tmp[1+j*8], tmp[2+j*8], tmp[3+j*8], tmp[4+j*8], tmp[5+j*8], tmp[6+j*8], tmp[7+j*8], ReadLen);
	//通过can数据发送给ECU
	if(cmdType == MSG_CODE_ECU_UPGRADE_QUERY_CMD)
	{
		kal_prompt_trace(MOD_FOTA, "send can data to ecu starting");
		for(j = 0 ; j < FLASH_PAGE_SIZE / 8 ; j++)
		{
			sendOnePkgData2CAN(TBOX_MessageID, &tmp[j * 8]);
		}
		//Delayms(10);
	}
	kal_prompt_trace(MOD_FOTA,"curPageNum = %d pageNum = %d" , curPageNum , pageNum);
	if(rs_flash_writePage(DL_IMG_ADDR + curPageNum * FLASH_PAGE_SIZE , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
	{
		FS_Close(fileHandle);
		return;
	}				

	curPageNum++;
	if(curPageNum == pageNum)
	{
		//写入TBOX固件待升级标志，重启
		if(cmdType == MSG_CODE_TBOX_UPGRADE_QUERY_CMD)
		{
			kal_prompt_trace(MOD_FOTA,"write tbox upgrade flag");
			sw_status = (sw_status_struct *)tmp;
			sw_status->status = SW_STATUS_UPGRADE_NEED;
			sw_status->reboot_count_after_upgrade = 0;
			if(rs_flash_eraseBlock(UPGRADE_INFO_ADDR) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
				FS_Close(fileHandle);
				return;
			}
			if(rs_flash_writePage(UPGRADE_INFO_ADDR , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_writePage err");
				FS_Close(fileHandle);
				return;
			}
			tbox_delay_reset(); // 10S后重启
		}
		
		//写入ECU固件待升级标志
		if(cmdType == MSG_CODE_ECU_UPGRADE_QUERY_CMD)
		{
			kal_prompt_trace(MOD_FOTA,"write ecu upgrade flag");
			sw_status = (sw_status_struct *)tmp;
			sw_status->status = SW_STATUS_ECUUPGRADE_NEED;
			sw_status->reboot_count_after_upgrade = 0;
			if(rs_flash_eraseBlock(UPGRADE_INFO_ADDR) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_eraseBlock err");
				FS_Close(fileHandle);
				return;
			}
			if(rs_flash_writePage(UPGRADE_INFO_ADDR , tmp, FLASH_PAGE_SIZE) != RS_ERR_OK)
			{
				kal_prompt_trace(MOD_FOTA, "rs_flash_writePage err");
				FS_Close(fileHandle);
				return;
			}
		}
		curPageNum = 0;
	}
	else
	{
		;//StartTimer(BIRD_COUNTOR_TIMEOUT_TIMER, 300, SaveToFlashTimerProc);
	}
	
	FS_Close(fileHandle);
}

void devDisconnected(void)
{
	isDevConnected = KAL_FALSE;
	checkDevConected();
}


kal_bool checkDevConected(void)
{
	static kal_bool isPreConnStatus = 0;

	if(isDevConnected != isPreConnStatus)
	{
		isPreConnStatus = isDevConnected;
		dataSend2CloudLen = 0;
		dataSend2Cloud[dataSend2CloudLen++] = CMD_STATUS_CONSTATUS_REPORT_QUERY;
		dataSend2Cloud[dataSend2CloudLen++] = 0xFE;
		dataSend2Cloud[dataSend2CloudLen++] = 0x01;
		dataSend2Cloud[dataSend2CloudLen++] = isDevConnected;
		if(isReportConnectFun)
			sendACKToServer(MSG_CODE_DEV_RSD,KAL_TRUE);
		return KAL_TRUE;
	}
	return KAL_FALSE;
}


void devConnectHeart(void)
{
	U8 pkgCanData[8];
	
	pkgCanData[0] = 0xC3;
	pkgCanData[1] = 0xA7;
	pkgCanData[2] = 0xA7;
	pkgCanData[3] = 0xFF;
	pkgCanData[4] = 0xFF;
	pkgCanData[5] = 0xFF;
	pkgCanData[6] = 0xFF;
	pkgCanData[7] = 0xFF;
	if(isReportConnectFun)
		sendOnePkgData2CAN(TBOX_MessageID, pkgCanData);
	
	//Rj_stop_timer(BIRD_ECU_CONNECT_TIMEROUT_CHECK_TIMER);
	//Rj_start_timer(BIRD_ECU_CONNECT_TIMEROUT_CHECK_TIMER, 5 * 1000, devDisconnected, NULL);
	//Rj_stop_timer(BIRD_ECU_CONNECT_TIMER);
	//Rj_start_timer(BIRD_ECU_CONNECT_TIMER, 1 * 60 * 1000, devConnectHeart,NULL);
}

kal_bool canTboxProc(U32 id, U8 *data)
{	
	//if(id != DEV_MessageID)
	if(id != curComParm.devAddr)
		return KAL_FALSE;

	kal_prompt_trace(MOD_FOTA, "canTboxProc");

	memcpy(&ecuCanDataBuf[ecuCanDataWriteP][0], data, 8);
	ecuCanDataWriteP++;
	if(ecuCanDataWriteP > ECU_CAN_BUFF_LEN - 1)
		ecuCanDataWriteP = 0;
	
	//bird_send_message(MSG_ID_CAN_RX_CHECK4ECU);
	return KAL_TRUE;
}


void can_rx_data_check4ecu(void)
{
	U8 *data, checkSum;
	U16 i, j, len;
	frame_header_struct frmHeader;

	//kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu");

	//checkDevConected(isDevConnected);

	data = &ecuCanDataBuf[ecuCanDataReadP][0];
		ecuCanDataReadP++;
	if(ecuCanDataReadP > ECU_CAN_BUFF_LEN - 1)
		ecuCanDataReadP = 0;

	//Delayms(100);
	//cmdType = MSG_CODE_ECU_QUERY_PARM_CMD; // for test

	kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu data[0] = %d",data[0]);

	//检测到ECU bootloader启动
	if(strcmp(data, ECU_BOOTLOADER_STARTUP_CMD)==0)
	{
		if(readSWStatus()==SW_STATUS_ECUUPGRADE_NEED)
		{
			//发送数据到ECU上
			kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: send ecu upgrade data");
			curPageNum = 0;
			;//StartTimer(BIRD_COUNTOR_TIMEOUT_TIMER, 100, SendUpgradeDataToECUTimerProc);	
			return;
		}
		return;
	}
	//检测到ECU发来的存活期设置反馈数据
	if(data[0] == 0xE0 && cmdType == MSG_CODE_ECU_SET_LIFETIME_CMD)
	{
		kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: lifetime = %d",data[2]*256*256+data[3]*256+data[4]);
		kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: runtime = %d",data[5]*256*256+data[6]*256+data[7]);
		memcpy(runTimeData, &data[2], 6);
		
		if(data[1] == 1 || data[1] == 2)
			sendACKToServer(MSG_CODE_ECU_SET_LIFETIME_CMD,data[1]);
		else
			sendACKToServer(MSG_CODE_ECU_SET_LIFETIME_CMD,KAL_FALSE);
		return;
	}
	
	//检测到ECU发来的监控数据帧
	if(data[0] == 0xC0 && cmdType == MSG_CODE_ECU_QUERY_PARM_CMD)
	{
		kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: frame id = %d",data[1]);
		if(data[1]>20)
		{
			kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: too large frame id");
			sendACKToServer(MSG_CODE_ECU_QUERY_PARM_CMD,KAL_FALSE);
			return;
		}

		for(i = 0; i < ECU_QUERY_CAN_DATA_LENGTH; i++)
		{
			//kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: ecuParmData = %d , %d",ecuParmData[i][0] ,ecuParmData[i][1]);
			if(ecuParmData[i][0] == 0xFF || ecuParmData[i][1]==data[1])
			{
				memcpy(&ecuParmData[i][0], data, 8);
				kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: i = %d",i);
				if(i==ECU_QUERY_CAN_DATA_LENGTH - 1)
				{
					sendACKToServer(MSG_CODE_ECU_QUERY_PARM_CMD,KAL_TRUE);
					memset(ecuParmData, 0xff, 20 * 8);
				}	
				return;
			}
		}
		return;
	}

	frmHeader.frameHeader = data[0];
	//如果是命令帧，说明是dev主动发起的,  如果有错误，反馈给DEV，如果正确反馈给云端。
	if(frmHeader.frameHeaderBit.CMD_RES == 1)
	{
		kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: res");

		//cmdType = MSG_CODE_DEV_RSD;
		//结束帧
		if(frmHeader.frameHeaderBit.END_FRAME)
		{
			cmdType = MSG_CODE_NONE;
			memcpy(&ecuParmData[canFrameNum][0], &data[0], 8);
			
			canFrameNum++;
			
			//检查数据完整性
			for(i = 0; i < canFrameNum - 1; i++)
			{
				frmHeader.frameHeader = ecuParmData[i][0];
				if(frmHeader.frameHeaderBit.FRAME_NUM != i)
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error FRAME_NUM = %d,i = %d",frmHeader.frameHeaderBit.FRAME_NUM, i);
					//dataSend2CloudLen = 0;
					//sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					canFrameNum = 0;
					return;
				}
			}
			frmHeader.frameHeader = ecuParmData[0][0];
			dataSend2CloudLen = 0;
			//拼接帧---只有1帧
			if(canFrameNum == 1)
			{
				dataSend2Cloud[dataSend2CloudLen++] = ecuParmData[0][1];	//命令标识
				dataSend2Cloud[dataSend2CloudLen++] = 0xFE;					//应答标识
				len = frmHeader.frameHeaderBit.FRAME_NUM - 3;
				if(len > 5) // 不能大于5个字节
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error len = %d", len);
					//dataSend2Cloud[1] = 0x02;
					//dataSend2CloudLen = 0;
					//sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					canFrameNum = 0;
					return;					
				}
				dataSend2Cloud[dataSend2CloudLen++] = len;
				memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[0][2], len);
				dataSend2CloudLen = dataSend2CloudLen + len;

				//检查checksum
				checkSum = 0;
				checkSum ^= dataSend2Cloud[0];
				for(j = 0; j < len; j++)
					checkSum ^= dataSend2Cloud[j + 3];

				if(checkSum != ecuParmData[0][2 + len])
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error checkSum = %d %d", checkSum, ecuParmData[0][2+len]);
					//dataSend2Cloud[1] = 0x02;
					//dataSend2CloudLen = 0;
					//sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					canFrameNum = 0;
					return;					
				}
				canFrameNum = 0;
				
				//如果是dev连接心跳则不上报云端。
				if(dataSend2Cloud[0] == CMD_STATUS_CONSTATUS_REPORT_QUERY)
				{
					isDevConnected = TRUE;
					checkDevConected();
					//Rj_stop_timer(BIRD_ECU_CONNECT_TIMEROUT_CHECK_TIMER);
					return;
				}
				sendACKToServer(MSG_CODE_DEV_RSD,KAL_TRUE);
				return;
			}
			//拼接帧---多帧
			else if(canFrameNum <= 65)
			{
				//首帧
				dataSend2Cloud[dataSend2CloudLen++] = ecuParmData[0][1];	//命令标识
				dataSend2Cloud[dataSend2CloudLen++] = 0xFE;					//应答标识
				dataSend2Cloud[dataSend2CloudLen++] = 0x01;					//数据单元长度, 先空着
				memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[0][2], 6);
				dataSend2CloudLen = dataSend2CloudLen + 6;

				//中间帧
				for(i = 0; i < canFrameNum - 2; i++)
				{
					memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[i + 1][1], 7);
					//test
					//kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu:  i = %x %x %x", ecuParmData[i + 1][1],i,dataSend2Cloud[dataSend2CloudLen + i * 7]);
					dataSend2CloudLen = dataSend2CloudLen + 7;
				}
				
				//test
				//for(j = 0; j < dataSend2CloudLen; j++)
				//	kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error2 checkSum = %x %x", dataSend2Cloud[j],canFrameNum);
				
				//最后一帧
				frmHeader.frameHeader = ecuParmData[canFrameNum - 1][0];				
				len = frmHeader.frameHeaderBit.FRAME_NUM - 2; 								//最后一帧的数据单元长度
								
				if(len > 6) // 最后一帧不能大于6个字节
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error len1 = %d", len);
					//dataSend2Cloud[1] = 0x02;
					//dataSend2CloudLen = 0;
					//sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					canFrameNum = 0;
					return;					
				}
				
				dataSend2Cloud[2] = len + (canFrameNum - 2) * 7 + 6;		//数据单元长度
				
				memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[canFrameNum - 1][1], len);
				dataSend2CloudLen = dataSend2CloudLen + len;

				//检查checksum
				checkSum = 0;
				checkSum ^= dataSend2Cloud[0];
				for(j = 0; j < dataSend2Cloud[2]; j++)
				{
					checkSum ^= dataSend2Cloud[j + 3];
					//kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error2 checkSum = %x %x", checkSum, dataSend2Cloud[j + 3]);
				}
				if(checkSum != ecuParmData[canFrameNum - 1][1+len])
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error2 checkSum = %d %d", checkSum, ecuParmData[canFrameNum - 1][1+len]);
					//dataSend2Cloud[1] = 0x02;
					//dataSend2CloudLen = 0;
					//sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					canFrameNum = 0;
					return;					
				}
				canFrameNum = 0;
				sendACKToServer(MSG_CODE_DEV_RSD,KAL_TRUE);
				canFrameNum = 0;
				return;
			}	
		}
		else
		{
			canFrameNum = frmHeader.frameHeaderBit.FRAME_NUM;
			kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: canFrameNum = %d", canFrameNum);
			if(canFrameNum > 63)
			{
				//cmdType = MSG_CODE_NONE;
				//dataSend2CloudLen = 0;
				//sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
				canFrameNum = 0;
				return;
			}
			memcpy(&ecuParmData[canFrameNum][0], &data[0], 8);
			canFrameNum++;
		}
		return;
	}
	//检测到dev发来的响应帧数据, 把结果上报给云端。
	if(cmdType == MSG_CODE_DEV_CMD)
	{
		kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: MSG_CODE_DEV_CMD");
		frmHeader.frameHeader = data[0];
		//结束帧
		if(frmHeader.frameHeaderBit.END_FRAME)
		{
			cmdType = MSG_CODE_NONE;
			memcpy(&ecuParmData[canFrameNum][0], &data[0], 8);
			canFrameNum++;
			
			//检查数据完整性
			for(i = 0; i < canFrameNum - 1; i++)
			{
				frmHeader.frameHeader = ecuParmData[i][0];
				if(frmHeader.frameHeaderBit.FRAME_NUM != i)
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error FRAME_NUM = %d,i = %d",frmHeader.frameHeaderBit.FRAME_NUM, i);
					dataSend2CloudLen = 0;
					canFrameNum = 0;
					sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					return;
				}
			}
			frmHeader.frameHeader = ecuParmData[0][0];
			dataSend2CloudLen = 0;
			//拼接帧---只有1帧
			if(canFrameNum == 1)
			{
				dataSend2Cloud[dataSend2CloudLen++] = ecuParmData[0][1];	//命令标识
				dataSend2Cloud[dataSend2CloudLen++] = 0x01;					//应答标识
				len = frmHeader.frameHeaderBit.FRAME_NUM - 3;
				if(len > 5) // 不能大于5个字节
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error len = %d", len);
					dataSend2Cloud[1] = 0x02;
					dataSend2CloudLen = 0;
					canFrameNum = 0;
					sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					return;					
				}
				dataSend2Cloud[dataSend2CloudLen++] = len;
				memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[0][2], len);
				dataSend2CloudLen = dataSend2CloudLen + len;

				//检查checksum
				checkSum = 0;
				checkSum ^= dataSend2Cloud[0];
				for(j = 0; j < len; j++)
					checkSum ^= dataSend2Cloud[j + 3];

				if(checkSum != ecuParmData[0][2 + len])
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error checkSum = %d %d", checkSum, ecuParmData[0][2+len]);
					dataSend2Cloud[1] = 0x02;
					dataSend2CloudLen = 0;
					canFrameNum = 0;
					sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					return;					
				}
				sendACKToServer(MSG_CODE_DEV_CMD,KAL_TRUE);
				canFrameNum = 0;
				return;
			}
			//拼接帧---多帧
			else if(canFrameNum <= 65)
			{
				//首帧
				dataSend2Cloud[dataSend2CloudLen++] = ecuParmData[0][1];	//命令标识
				dataSend2Cloud[dataSend2CloudLen++] = 0x01;					//应答标识
				dataSend2Cloud[dataSend2CloudLen++] = 0x01;					//数据单元长度, 先空着
				memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[0][2], 6);
				dataSend2CloudLen = dataSend2CloudLen + 6;

				//中间帧
				for(i = 0; i < canFrameNum - 2; i++)
				{
					memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[i + 1][1], 7);
					dataSend2CloudLen = dataSend2CloudLen + 7;
				}
				
				//最后一帧
				frmHeader.frameHeader = ecuParmData[canFrameNum - 1][0];				
				len = frmHeader.frameHeaderBit.FRAME_NUM - 2; 								//最后一帧的数据单元长度
								
				if(len > 6) // 最后一帧不能大于6个字节
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error len1 = %d", len);
					dataSend2Cloud[1] = 0x02;
					dataSend2CloudLen = 0;
					canFrameNum = 0;
					sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					return;					
				}
				
				dataSend2Cloud[2] = len + (canFrameNum - 2) * 7 + 6;		//数据单元长度
				
				memcpy(&dataSend2Cloud[dataSend2CloudLen], &ecuParmData[canFrameNum - 1][1], len);
				dataSend2CloudLen = dataSend2CloudLen + len;

				//检查checksum
				checkSum = 0;
				checkSum ^= dataSend2Cloud[0];
				for(j = 0; j < dataSend2Cloud[2]; j++)
					checkSum ^= dataSend2Cloud[j + 3];

				if(checkSum != ecuParmData[canFrameNum - 1][1+len])
				{
					kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: error2 checkSum = %d %d", checkSum, ecuParmData[canFrameNum - 1][1+len]);
					dataSend2Cloud[1] = 0x02;
					dataSend2CloudLen = 0;
					canFrameNum = 0;
					sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
					return;					
				}
				canFrameNum = 0;
				sendACKToServer(MSG_CODE_DEV_CMD,KAL_TRUE);
				return;
			}	
		}
		else
		{
			canFrameNum = frmHeader.frameHeaderBit.FRAME_NUM;
			kal_prompt_trace(MOD_FOTA, "can_rx_data_check4ecu: canFrameNum = %d", canFrameNum);
			if(canFrameNum > 63)
			{
				cmdType = MSG_CODE_NONE;
				dataSend2CloudLen = 0;
				canFrameNum = 0;
				sendACKToServer(MSG_CODE_DEV_CMD,KAL_FALSE);
				return;
			}
			memcpy(&ecuParmData[canFrameNum][0], &data[0], 8);
			canFrameNum++;
		}
		return;
	}
	return;
}



static void Delayms(U32 data)
{
	U32 i;
	while(data--)
	{
		for(i=0;i<90000;i++){}
	}
}

void Bird_soc_sendbufAdd2(Send_Info *_sendinfo)
{
	tl_net_tbox_connection_packet_output_request(_sendinfo->send_buf, _sendinfo->buf_len);
}

U8 get_encryption()
{
	return  1;
}
void tbox_delay_reset()
{

}
void sendOnePkgData2CAN(U32 canAddrID, U8 *sendData)
{
	tl_serial_request_send_one_can_pkg_data(canAddrID, sendData);
}



