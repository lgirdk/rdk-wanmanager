/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <syscfg.h>

#include "wanmgr_core.h"
#include "wanmgr_sysevents.h"
#include "wanmgr_rdkbus_apis.h"
#include "wanmgr_ipc.h"
#include "wanmgr_controller.h"
#ifdef RBUS_BUILD_FLAG_ENABLE
#include "wanmgr_rbus_handler_apis.h"
#endif //RBUS_BUILD_FLAG_ENABLE

ANSC_STATUS WanMgr_Core_Init(void)
{
    ANSC_STATUS retStatus = ANSC_STATUS_FAILURE;

    //Initialise system messages
    retStatus = WanMgr_SysEvents_Init();
    if(retStatus != ANSC_STATUS_SUCCESS)
    {
        CcspTraceInfo(("%s %d - WanManager failed to initialise!\n", __FUNCTION__, __LINE__ ));
    }

    //Initialise syscfg database
    if (syscfg_init() < 0)
    {
        CcspTraceError(("failed to initialise syscfg"));
    }

    //Starts the IPC thread
    retStatus = WanMgr_StartIpcServer();
    if(retStatus != ANSC_STATUS_SUCCESS)
    {
        CcspTraceInfo(("%s %d - IPC Thread failed to start!\n", __FUNCTION__, __LINE__ ));
    }

#ifdef RBUS_BUILD_FLAG_ENABLE
    //Starts the Rbus Initialize
    retStatus = WanMgr_Rbus_Init();
    if(retStatus != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("%s %d - Rbus Init failed !\n", __FUNCTION__, __LINE__ ));
    }
#endif //RBUS_BUILD_FLAG_ENABLE

    system("netmonitor &");

    return retStatus;
}

ANSC_STATUS WanMgr_Core_Start(void)
{
    ANSC_STATUS retStatus = ANSC_STATUS_SUCCESS;
#ifdef RBUS_BUILD_FLAG_ENABLE
    WanMgr_Rbus_SubscribeDML();
#endif // RBUS_BUILD_FLAG_ENABLE
    //Initialise Policy State Machine
    WanController_Init_StateMachine();

    return retStatus;
}

ANSC_STATUS WanMgr_Core_Finalise(void)
{
    ANSC_STATUS retStatus = ANSC_STATUS_SUCCESS;

    retStatus = WanMgr_SysEvents_Finalise();

    return retStatus;
}
