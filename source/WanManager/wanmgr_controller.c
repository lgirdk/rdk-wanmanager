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

#include "ansc_platform.h"
#include "wanmgr_controller.h"
#include "wanmgr_rdkbus_utils.h"
#include "wanmgr_data.h"
#include "secure_wrapper.h"

ANSC_STATUS WanController_Policy_Change(void)
{
    /* Wan policy changed. Cpe needs a restart! */
    FILE *fp = NULL;
    char value[25] = {0};
    char acOutput[64] = {0};
    int seconds = 30;
    int rebootCount = 1;
    int ret =0;
    WanController_ClearWanConfigurationsInPSM();
    memset(value, 0, sizeof(value));
    fp = v_secure_popen("r","syscfg get X_RDKCENTRAL-COM_LastRebootCounter");
    if (fp == NULL) {
        return ANSC_STATUS_FAILURE;
    }
    v_secure_pclose(fp);

    rebootCount = atoi(value);

    CcspTraceInfo(("Updating the last reboot reason and last reboot counter\n"));
    v_secure_system("syscfg set X_RDKCENTRAL-COM_LastRebootReason Wan_Policy_Change ");
    ret = v_secure_system("syscfg set X_RDKCENTRAL-COM_LastRebootCounter %d ",rebootCount);
    if(ret != 0) {
          CcspTraceWarning(("%s: Failure in executing command via v_secure_system. ret:[%d] \n", __FUNCTION__,ret));
    }
    v_secure_system("syscfg commit");

    while(seconds > 0)
    {
        printf("...(%d)...\n", seconds);
        seconds -= 10;
        sleep(10);
    }

    v_secure_system("/rdklogger/backupLogs.sh true");

    return ANSC_STATUS_SUCCESS;
}

static int WanController_ResetActiveLinkOnAllIface ()
{
    int TotalIfaces = WanMgr_IfaceData_GetTotalWanIface();
    int uiLoopCount;
    for (uiLoopCount = 0; uiLoopCount < TotalIfaces; uiLoopCount++)
    {
        WanMgr_Iface_Data_t*   pWanDmlIfaceData = WanMgr_GetIfaceData_locked(uiLoopCount);
        if(pWanDmlIfaceData != NULL)
        {
            DML_WAN_IFACE* pWanIfaceData = &(pWanDmlIfaceData->data);
            CcspTraceInfo(("%s %d: setting Interface index:%d, ActiveLink = FALSE and saving it in PSM \n", __FUNCTION__, __LINE__, uiLoopCount));
            pWanIfaceData->Wan.ActiveLink = FALSE;
            if (DmlSetWanActiveLinkInPSMDB(uiLoopCount, FALSE) != ANSC_STATUS_SUCCESS)
            {
                WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                return ANSC_STATUS_FAILURE;
            }
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }
    }
    return ANSC_STATUS_SUCCESS;
}

/* WanController_Start_StateMachine() */
ANSC_STATUS WanController_Start_StateMachine(DML_WAN_POLICY swan_policy)
{
    ANSC_STATUS retStatus = ANSC_STATUS_FAILURE;
    DML_WAN_POLICY wan_policy = FIXED_MODE;
    int iErrorCode = 0;
    BOOLEAN WanEnable = TRUE;
    BOOLEAN WanPolicyChanged = FALSE;
    BOOLEAN WanStateMachineRunning = FALSE;
    CcspTraceInfo(("%s %d \n", __FUNCTION__, __LINE__ ));

    // event handler
    int n = 0;
    struct timeval tv;

    while(1)
    {
        /* Wait up to 500 milliseconds */
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        n = select(0, NULL, NULL, NULL, &tv);
        if (n < 0)
        {
            /* interrupted by signal or something, continue */
            continue;
        }

        //Get Policy
        WanMgr_Config_Data_t* pWanConfigData = WanMgr_GetConfigData_locked();
        if(pWanConfigData != NULL)
        {
            DML_WANMGR_CONFIG* pWanConfig = &(pWanConfigData->data);

            WanEnable = pWanConfig->Enable;
            WanStateMachineRunning = pWanConfig->Interface_SM_Running;
            wan_policy = pWanConfig->Policy;
            WanPolicyChanged = pWanConfig->PolicyChanged;
            if((pWanConfig->PolicyChanged) && (WanStateMachineRunning == FALSE))
            {
                pWanConfig->PolicyChanged = FALSE;
                CcspTraceInfo((" %s %d PolicyChanged reset \n", __FUNCTION__, __LINE__ ));
            }           

            WanMgrDml_GetConfigData_release(pWanConfigData);
        }

        if(WanEnable == FALSE || WanStateMachineRunning == TRUE)
        {
            continue;
        }

        if(WanPolicyChanged)
        {
            WanController_ResetActiveLinkOnAllIface();
#ifdef FEATURE_RDKB_WAN_UPSTREAM
            /* The changes are required only for the policies that are not handling
             * hardware configurations on its own. It is ineffective if we do not intend
             * to use those policies.
             */
            if(wan_policy != AUTOWAN_MODE && wan_policy != PARALLEL_SCAN)  // Add policy that does not need reset hw configurations.
            {
                WanMgr_RdkBus_setEthernetUpstream(TRUE);
            }
#endif
        }

#ifdef FEATURE_RDKB_AUTO_PORT_SWITCH
        WanMgr_SetPortCapabilityForEthIntf(wan_policy);
#endif  //FEATURE_RDKB_AUTO_PORT_SWITCH

        //Starts wan controller threads
        switch (wan_policy) {
            case FIXED_MODE:
                retStatus = WanMgr_Policy_FixedModePolicy();
                break;

            case FIXED_MODE_ON_BOOTUP:
                retStatus = WanMgr_Policy_FixedModeOnBootupPolicy();
                break;

            case PRIMARY_PRIORITY:
                retStatus = WanMgr_Policy_PrimaryPriorityPolicy();
                break;

            case PRIMARY_PRIORITY_ON_BOOTUP:
                retStatus = WanMgr_Policy_PrimaryPriorityOnBootupPolicy();
                break;

            case MULTIWAN_MODE:
                break;

            case AUTOWAN_MODE: 
#if defined (_XB6_PRODUCT_REQ_) || defined (_CBR2_PRODUCT_REQ_)
                retStatus = WanMgr_Policy_AutoWan();
#else
                retStatus = WanMgr_FailOverThread();
#endif
                break;

            case PARALLEL_SCAN:
#if defined(FEATURE_RDKB_CONFIGURABLE_WAN_INTERFACE)
                retStatus = WanMgr_FailOverThread();
                break;
#else
                CcspTraceError(("%s: PARALLEL_SCAN Not supported. Falling back to AUTOWAN \n",__FUNCTION__));
#endif

            default: //If policy is not recognised use AUTOWAN_MODE
                pWanConfigData = WanMgr_GetConfigData_locked();
                if(pWanConfigData != NULL)
                {
                    DML_WANMGR_CONFIG* pWanConfig = &(pWanConfigData->data);
                    CcspTraceError(("%s: policy Not supported. Falling back to AUTOWAN \n",__FUNCTION__));
                    pWanConfig->Policy = AUTOWAN_MODE;
                    WanMgrDml_GetConfigData_release(pWanConfigData);
                }
                break;
        }
    }
    if( ANSC_STATUS_SUCCESS != retStatus )
    {
        CcspTraceInfo(("%s %d Error: Failed to start State Machine Thread error code: %d \n", __FUNCTION__, __LINE__, retStatus ));
        retStatus = ANSC_STATUS_SUCCESS;
    }

    WanController_Policy_Change();

    return retStatus;
}

/* WanController_Init_StateMachine */
ANSC_STATUS WanController_Init_StateMachine(void)
{
    int iTotalInterfaces = 0;
    DML_WAN_POLICY wan_policy;

    CcspTraceInfo(("%s %d \n", __FUNCTION__, __LINE__ ));

    // Get the configured wan policy
    if(WanMgr_RdkBus_getWanPolicy(&wan_policy) != ANSC_STATUS_SUCCESS) {
        CcspTraceInfo(("%s %d  Error: WanController_getWanPolicy() failed \n", __FUNCTION__, __LINE__ ));
        return ANSC_STATUS_FAILURE;
    }

    if(WanController_Start_StateMachine(wan_policy) != ANSC_STATUS_SUCCESS) {
        CcspTraceInfo(("%s %d Error: WanController_Start_StateMachine failed \n", __FUNCTION__, __LINE__ ));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS WanMgr_Controller_PolicyCtrlInit(WanMgr_Policy_Controller_t* pWanPolicyCtrl)
{
    ANSC_STATUS retStatus = ANSC_STATUS_FAILURE;

    if(pWanPolicyCtrl != NULL)
    {
        pWanPolicyCtrl->WanEnable = FALSE;
        pWanPolicyCtrl->activeInterfaceIdx = -1;
        pWanPolicyCtrl->selSecondaryInterfaceIdx = -1;
        pWanPolicyCtrl->pWanActiveIfaceData = NULL;
        memset(&(pWanPolicyCtrl->SelectionTimeOutStart), 0, sizeof(struct timespec));
        memset(&(pWanPolicyCtrl->SelectionTimeOutEnd), 0, sizeof(struct timespec));
        pWanPolicyCtrl->InterfaceSelectionTimeOut = 0;
        pWanPolicyCtrl->TotalIfaces = 0;
        pWanPolicyCtrl->WanOperationalMode = -1;
        pWanPolicyCtrl->GroupIfaceList = 0;
        pWanPolicyCtrl->GroupInst = 0;
        pWanPolicyCtrl->GroupChanged = FALSE;
        pWanPolicyCtrl->ResetActiveInterface = FALSE;
        retStatus = ANSC_STATUS_SUCCESS;
    }

   return retStatus;
}
