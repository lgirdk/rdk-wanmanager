/*********************************************************************************
 * Copyright 2023 Liberty Global B.V.
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
 *********************************************************************************/

#include "wanmgr_data.h"
#include "wanmgr_dhcpv6_apis.h"
#include "wanmgr_interface_sm.h"
#include "wanmgr_sysevents.h"
#include "wanmgr_ipc.h"
#include "wanmgr_utils.h"
#include "wanmgr_net_utils.h"
#include <syscfg/syscfg.h>

#include "wanmgr_ipoe_hc_dml.h"
#include "wanmgr_ipoe_hc_apis.h"
#include "wanmgr_ipoe_hc_internal.h"


ANSC_STATUS EnableDisableIPoEHC (int status)
{
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) g_pWanMgrBE->hIPoE_hc;
    WanMgr_Iface_Data_t *pWanDmlIfaceData;

    if (pMyObject == NULL) {
        return ANSC_STATUS_FAILURE;
    }

    pMyObject->Enable = status;
    CcspTraceError(("Going for EnableDisableIPoEHC function ...\n"));

    pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked("erouter0");
    if (pWanDmlIfaceData != NULL)
    {
        DML_WAN_IFACE *pWanDmlIface = &(pWanDmlIfaceData->data);

        if (DmlSetWanIfCfg(pWanDmlIface->uiInstanceNumber, pWanDmlIface) != ANSC_STATUS_SUCCESS)
        {
            CcspTraceError(("%s: Failed \n", __FUNCTION__));
        }

        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS GetNumberOfIPoEActions (int *counter)
{
    char buf[8];

    if (syscfg_get (NULL, "IPOE_number_of_actions", buf, sizeof(buf)) == 0)
    {
        *counter = atoi(buf);
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS SetNumberOfIPoEActions (void)
{
    int counter;

    counter = 0;
    GetNumberOfIPoEActions(&counter);
    counter++;

    if (syscfg_set_u(NULL, "IPOE_number_of_actions", counter) != 0)
    {
        CcspTraceError(("%s %d: syscfg_set IPoE_regular_interval failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

ULONG GetMgtIntfPid (void)
{
     FILE *fp;
     char buffer[12];
     int pid = 0;

     fp = fopen("/tmp/udhcpc.mg0.pid", "r");

     if (fp == NULL) {
        return ANSC_STATUS_FAILURE;
     }

     if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        pid = atoi(buffer);
     }

     fclose(fp);

     return pid;
}

ULONG GetVoiceIntfPid (void)
{
     FILE *fp;
     char buffer[12];
     int pid = 0;

     fp = fopen("/tmp/udhcpc.voip0.pid", "r");

     if (fp == NULL) {
        return ANSC_STATUS_FAILURE;
     }

     if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        pid = atoi(buffer);
     }

     fclose(fp);

     return pid;
}

ANSC_STATUS DmlIPoEGetEnableFromSyscfg (BOOL *pBool)
{
    char buf[12];

    if (syscfg_get (NULL, "IPoE_enable", buf, sizeof(buf)) == 0)
    {
        *pBool = atoi(buf) ? 1 : 0;
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS DmlIPoEGetEnable (BOOL *pBool)
{
    WanMgr_Iface_Data_t *pWanDmlIfaceData;

    pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked("erouter0");
    if (pWanDmlIfaceData != NULL)
    {
        *pBool = pWanDmlIfaceData->data.Wan.EnableIPoE;
        WanMgrDml_GetIfaceData_release("erouter0");
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS DmlIPoEGetIPRegularInterval (ULONG *puLong)
{
    char buf[12];

    if (syscfg_get (NULL, "IPoE_regular_interval", buf, sizeof(buf)) == 0)
    {
        *puLong = atoi(buf);
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS DmlIPoEGetRetryLimit (ULONG *puLong)
{
    char buf[12];

    if (syscfg_get (NULL, "IPoE_retry_limit", buf, sizeof(buf)) == 0)
    {
        *puLong = atoi(buf);
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS DmlIPoEGetRetryInterval (ULONG *puLong)
{
    char buf[12];

    if (syscfg_get (NULL, "IPoE_retry_interval", buf, sizeof(buf)) == 0)
    {
        *puLong = atoi(buf);
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS DmlIPoESetEnable (BOOL bValue)
{
    WanMgr_Iface_Data_t *pWanDmlIfaceData;

    if (syscfg_set(NULL, "IPoE_enable", bValue ? "1" : "0") != 0)
    {
        CcspTraceError(("%s %d: syscfg_set IPoE_enable failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked("erouter0");
    if (pWanDmlIfaceData != NULL)
    {
        pWanDmlIfaceData->data.Wan.EnableIPoE = bValue;
        WanMgrDml_GetIfaceData_release("erouter0");
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS DmlIPoESetIPRegularInterval (ULONG pValue)
{
    if (syscfg_set_u(NULL, "IPoE_regular_interval", pValue) != 0)
    {
        CcspTraceError(("%s %d: syscfg_set IPoE_regular_interval failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS DmlIPoESetRetryLimit (ULONG pValue)
{
    if (syscfg_set_u(NULL, "IPoE_retry_limit", pValue) != 0)
    {
        CcspTraceError(("%s %d: syscfg_set IPoE_retry_limit failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS DmlIPoESetRetryInterval (ULONG pValue)
{
    if (syscfg_set_u(NULL, "IPoE_retry_interval", pValue) != 0)
    {
        CcspTraceError(("%s %d: syscfg_set IPoE_retry_limit failed\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS SaveIPOEHCParamInDB (void)
{
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) g_pWanMgrBE->hIPoE_hc;
    WanMgr_Iface_Data_t *pWanDmlIfaceData;

    if (pMyObject == NULL) {
        return ANSC_STATUS_FAILURE;
    }

    if ( DmlIPoESetRetryInterval(pMyObject->RetryInterval) != ANSC_STATUS_SUCCESS)
    {
        return ANSC_STATUS_FAILURE;
    }

    if ( DmlIPoESetRetryLimit(pMyObject->RetryLimit) != ANSC_STATUS_SUCCESS)
    {
        return ANSC_STATUS_FAILURE;
    }

    if ( DmlIPoESetIPRegularInterval(pMyObject->IPRegularInterval) != ANSC_STATUS_SUCCESS)
    {
        return ANSC_STATUS_FAILURE;
    }

    syscfg_commit();

    pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked("erouter0");
    if (pWanDmlIfaceData != NULL)
    {
        DML_WAN_IFACE *pWanDmlIface = &(pWanDmlIfaceData->data);

        if (DmlSetWanIfCfg(pWanDmlIface->uiInstanceNumber, pWanDmlIface) != ANSC_STATUS_SUCCESS)
        {
            CcspTraceError(("%s: Failed \n", __FUNCTION__));
        }

        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }

    return ANSC_STATUS_SUCCESS;
}
