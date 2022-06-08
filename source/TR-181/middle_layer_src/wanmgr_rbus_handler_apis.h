/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2021 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#ifndef _WANMGR_RBUS_H_
#define _WANMGR_RBUS_H_
#ifdef RBUS_BUILD_FLAG_ENABLE
#include "ansc_platform.h"
#include <rbus.h>
#include "ccsp_base_api.h"

#define NUM_OF_RBUS_PARAMS                           4
#define WANMGR_CONFIG_WAN_CURRENTACTIVEINTERFACE     "Device.X_RDK_WanManager.CurrentActiveInterface"
#define WANMGR_CONFIG_WAN_CURRENTSTANDBYINTERFACE    "Device.X_RDK_WanManager.CurrentStandbyInterface"
#define WANMGR_CONFIG_WAN_INTERFACEAVAILABLESTATUS   "Device.X_RDK_WanManager.InterfaceAvailableStatus"
#define WANMGR_CONFIG_WAN_INTERFACEACTIVESTATUS      "Device.X_RDK_WanManager.InterfaceActiveStatus"
#define WANMGR_DEVICE_NETWORKING_MODE                "Device.X_RDKCENTRAL-COM_DeviceControl.DeviceNetworkingMode"
#define X_RDK_REMOTE_DEVICECHANGE                    "Device.X_RDK_Remote.DeviceChange"
#define X_RDK_REMOTE_INVOKE                          "Device.X_RDK_Remote.Invoke()"

#define WANMGR_INFACE                                 "Device.X_RDK_WanManager.CPEInterface.{i}."
#define WANMGR_INFACE_TABLE                           "Device.X_RDK_WanManager.CPEInterface"
#define WANMGR_INFACE_PHY_STATUS                      "Device.X_RDK_WanManager.CPEInterface.{i}.Phy.Status"
#define WANMGR_INFACE_WAN_STATUS                      "Device.X_RDK_WanManager.CPEInterface.{i}.Wan.Status"
#define WANMGR_INFACE_WAN_LINKSTATUS                  "Device.X_RDK_WanManager.CPEInterface.{i}.Wan.LinkStatus"
#define WANMGR_INFACE_WAN_ENABLE                      "Device.X_RDK_WanManager.CPEInterface.{i}.Wan.Enable"
#define WANMGR_INFACE_ALIASNAME                       "Device.X_RDK_WanManager.CPEInterface.{i}.AliasName"

typedef enum _IDM_MSG_OPERATION
{
    IDM_SET = 1,
    IDM_GET,
    IDM_SUBS,
    IDM_REQUEST,

}IDM_MSG_OPERATION;

typedef struct _idm_invoke_method_Params
{
    IDM_MSG_OPERATION operation;
    char Mac_dest[18];
    char param_name[128];
    char param_value[2048];
    char pComponent_name[128];
    char pBus_path[128];
    uint timeout;
    enum dataType_e type;
    rbusMethodAsyncHandle_t asyncHandle;
}idm_invoke_method_Params_t;

/***********************************************************************
 WanMgr_IDM_Invoke():
Description:
    Send Invoke request to IDM
Arguments:
    idm_invoke_method_Params_t*

    struct list:
    IDM_MSG_OPERATION operation : DM GET/SET/SUBSCRIBE
    char Mac_dest[18]           : Destination device (identifier) MAC
    char param_name[128]        : DM name
    char param_value[2048]      : DM value
    char pComponent_name[128]   : Destination Component name (EX: eRT.com.cisco.spvtg.ccsp.wanmanager)
    char pBus_path[128]         : Destination Component bus path (EX : /com/cisco/spvtg/ccsp/wanmanager)
    uint timeout                : Timeout for async call back
    enum dataType_e type        : DM data type
    rbusMethodAsyncHandle_t asyncHandle : Async call back handler pointer
Return value:
    ANSC_STATUS

 ***********************************************************************/
ANSC_STATUS WanMgr_IDM_Invoke(idm_invoke_method_Params_t *IDM_request);

ANSC_STATUS WanMgr_Rbus_Init();
ANSC_STATUS WanMgr_Rbus_Exit();
ANSC_STATUS WanMgr_Rbus_String_EventPublish(char *dm_event, void *dm_value);
ANSC_STATUS WanMgr_Rbus_String_EventPublish_OnValueChange(char *dm_event, void *prev_dm_value, void *dm_value);
ANSC_STATUS WanMgr_Rbus_getUintParamValue(char * param, UINT * value);
void WanMgr_Rbus_UpdateLocalWanDb(void);
void WanMgr_Rbus_SubscribeDML(void);
void WanMgr_Rbus_UnSubscribeDML(void);
ANSC_STATUS WanMgr_WanRemoteIfaceConfigure(char *remoteMac);
#endif //RBUS_BUILD_FLAG_ENABLE
#endif
