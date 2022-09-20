/******************************************************************************
*
* Copyright (c) 2010 TP-LINK Technologies CO.,LTD.
* All rights reserved.
*
* FILE NAME  :   ibus.h
* VERSION    :   1.0
* DESCRIPTION:   为进程间通信提供公共的数据类型.
*
* AUTHOR     :   Huangwenzhong <Huangwenzhong@tp-link.net>
* CREATE DATE:   08/13/2015
*
* HISTORY    :
* 01   08/13/2015  Huangwenzhong     Create.
*
******************************************************************************/
#ifndef __IBUS_H__
#define __IBUS_H__
#include <string.h>

#include "ibus_type.h"
#include "ubus_json.h"

#define IBUS_USE_MSGCENTER		(0)
#define IBUS_USE_UBUS			(1)

#define WIFI_BAND_MAX_NUM	(2)

#if  (CONFIG_IBUS_METHOD == IBUS_USE_MSGCENTER)
#include "mc_common.h"
#else
#include "libubus.h"
#endif

//#define CONFIG_IBUS_METHOD			(IBUS_USE_MSGCENTER)

typedef struct _IBUS_HANDLE
{
	void *handle;
}IBUS_HANDLE;

#if (CONFIG_IBUS_METHOD == IBUS_USE_UBUS)
#define IBUS_OBJ_NAME_MAX_LEN		(64)
typedef struct _IBUS_UBUS_SUB
{
	INT8 obj_name[IBUS_OBJ_NAME_MAX_LEN];
	struct ubus_subscriber sub;
}IBUS_UBUS_SUB;
#endif


typedef enum _IBUS_EVENT
{
    IBUS_EVENT_NONE = 0,
    IBUS_EVENT_SMARTIP,
    IBUS_EVENT_DHCP,
    IBUS_EVENT_WIFI,
    IBUS_EVENT_GPIO,
    IBUS_EVENT_TIMERJOB,
    IBUS_EVENT_WPSD,
    IBUS_EVENT_WPSCLI,
    IBUS_EVENT_MAX
}IBUS_EVENT;

typedef enum _IBUS_SMARTIP_ACTION
{
	IBUS_SMARTIP_ACTION_NONE = 0,
	IBUS_SMARTIP_ACTION_DHCP,
	IBUS_SMARTIP_ACTION_WIFI,
	IBUS_SMARTIP_ACTION_IPCHANGE,
	IBUS_SMARTIP_ACTION_MAX
} IBUS_SMARTIP_ACTION;

typedef enum _IBUS_WIFI_ACTION
{
	IBUS_WIFI_ACTION_NONE=0,
	IBUS_WIFI_ACTION_RESTART,
	IBUS_WIFI_ACTION_STOP,
	IBUS_WIFI_ACTION_START_SCAN,
	IBUS_WIFI_ACTION_START_DUAL_SCAN,
	IBUS_WIFI_ACTION_END_SCAN,
	IBUS_WIFI_ACTION_RCV_SCAN_MSG,
	IBUS_WIFI_ACTION_DISCONN_STA,
	IBUS_WIFI_ACTION_ACL,
	IBUS_WIFI_ACTION_WPS_READY,
	IBUS_WIFI_ACTION_HOSTAPD_RESTART,
	IBUS_WIFI_ACTION_MAX
}IBUS_WIFI_ACTION;

typedef enum _IBUS_GPIO_ACTION
{
	IBUS_GPIO_NONE = 0,
	IBUS_GPIO_WPS,
	IBUS_GPIO_MAX
} IBUS_GPIO_ACTION;

typedef enum _IBUS_WPS_CLI_ACTION
{
	IBUS_WPS_CLI_ACTION_NONE = 0,
	IBUS_WPS_CLI_ACTION_CONNECT,
	IBUS_WPS_CLI_ACTION_CANCEL,
	IBUS_WPS_CLI_ACTION_MAX
} IBUS_WPS_CLI_ACTION;

typedef enum _IBUS_WPS_ACTION
{
	IBUS_WPS_ACTION_NONE = 0,
	IBUS_WPS_ACTION_START,
	IBUS_WPS_ACTION_DONE,
	IBUS_WPS_ACTION_MAX
} IBUS_WPS_ACTION;

typedef enum _IBUS_WPS_METHOD
{
	IBUS_WPS_METHOD_NONE = 0,
	IBUS_WPS_METHOD_PBC,
	IBUS_WPS_METHOD_PIN,
	IBUS_WPS_METHOD_MAX
} IBUS_WPS_METHOD;


typedef enum _IBUS_WPS_STATUS
{
	IBUS_WPS_STATUS_INIT = 0,
	IBUS_WPS_STATUS_ASSOCIATED,
	IBUS_WPS_STATUS_OK,
	IBUS_WPS_STATUS_MSG_ERR,
	IBUS_WPS_STATUS_TIMEOUT,
	IBUS_WPS_STATUS_SENDM2,
	IBUS_WPS_STATUS_SENDM7,
	IBUS_WPS_STATUS_MSGDONE,
	IBUS_WPS_STATUS_PBCOVERLAP,
	IBUS_WPS_STATUS_FIND_PBC_AP,
	IBUS_WPS_STATUS_ASSOCIATING,
	IBUS_WPS_STATUS_SCAN_AP,
	IBUS_WPS_STATUS_SILENT,		/* MC_WS_SILENT must be last one */
	IBUS_WPS_STATUS_MAX
} IBUS_WPS_STATUS;

typedef struct _IBUS_DHCPC_INFO
{
	U32 ip;
	U32 mask;
	U32 gw;
	U32 dns[2];	/* 2 dns is enough */
}IBUS_DHCPC_INFO;

typedef struct _IBUS_ENEVT_DATA_SMARTIP
{
	IBUS_SMARTIP_ACTION action;
	union {
		U32 detect_result; /*dhcpc detects dhcps result:0 success, 1 fail*/
		U32 wifi_status;   /*wifi_status:0 no change, 1 connected->disconnected, 2 disconnected->connected, 3 rootap changed*/
		U32 lanip_changed; /*for smartip pub msg:0 lanip no change, 1 lanip changed*/
	} status;
	IBUS_DHCPC_INFO payload; /*the payload of msg*/
}IBUS_EVENT_DATA_SMARTIP;

typedef struct _IBUS_EVENT_DATA_WIFI
{
	U32 band;
	INT8 region[8];
	IBUS_WIFI_ACTION action;
}IBUS_EVENT_DATA_WIFI;


typedef struct _IBUS_EVENT_DATA_TIMERJOB
{
	U16 timerjob_status;		/* timerjob status:0 led off, 1 led on */
}IBUS_EVENT_DATA_TIMERJOB;

typedef struct _IBUS_EVENT_DATA_WPSD
{
	IBUS_WPS_ACTION action;
	U16 wps_status[WIFI_BAND_MAX_NUM];
}IBUS_EVENT_DATA_WPSD;

typedef struct _IBUS_EVENT_DATA_WPS_CLI
{
	IBUS_WPS_CLI_ACTION action;
	IBUS_WPS_METHOD method;
	U8 pin[8];
}IBUS_EVENT_DATA_WPS_CLI;

typedef struct _IBUS_EVENT_DATA
{
    IBUS_EVENT event;
 
    union {
        /* add something */
        /* example: MC_EVENT_DATA_SMARTIP */
		IBUS_EVENT_DATA_SMARTIP 	data_smartip;
		IBUS_EVENT_DATA_WIFI 		data_wifi;
		IBUS_EVENT_DATA_TIMERJOB 	data_timerjob;
		IBUS_EVENT_DATA_WPSD		data_wpsd;
		IBUS_EVENT_DATA_WPS_CLI		data_wps_cli;
    }data;

}IBUS_EVENT_DATA;

typedef enum _IBUS_ROLE
{
    IBUS_ROLE_NONE = 0,
    IBUS_ROLE_PUBLISHER,   /* publisher  */
    IBUS_ROLE_SUBSCRIBER,      /* subscriber */
    IBUS_ROLE_MAX
}IBUS_ROLE;

#if  (CONFIG_IBUS_METHOD == IBUS_USE_MSGCENTER)
#include "mc_common.h"
static void ibus_convert_from_mc_data(const MC_EVENT_DATA *mc_data, IBUS_EVENT_DATA *ibus_data)
{
	int len = 0;
	if ((mc_data == NULL) || (ibus_data))
	{
		return;
	}

	ibus_data->event = mc_data->event;

	len = (sizeof(ibus_data->data) > sizeof(mc_data->data)) ? 
				sizeof(mc_data->data) : sizeof(ibus_data->data);
	memcpy(&(ibus_data->data), &(mc_data->data), len);
}
#endif


#endif /* __COMM_H__ */


