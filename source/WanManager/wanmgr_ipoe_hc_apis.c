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
    char buf[8] = {0};

    if (syscfg_get(NULL, "IPoE_enable", buf, sizeof(buf)) == 0)
    {
        *pBool = (strcmp(buf, "1") == 0) ? TRUE : FALSE;
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

ANSC_STATUS DmlIPoESetEnable(BOOL bValue)
{
    ANSC_STATUS ret = ANSC_STATUS_FAILURE;
    int uiLoopCount;
    CONNECTIVITY_CHECK_TYPE type = bValue ? WAN_CONNECTIVITY_TYPE_IHC : WAN_CONNECTIVITY_TYPE_NO_CHECK;
    int TotalIfaces = WanMgr_IfaceData_GetTotalWanIface();

    CcspTraceInfo(("%s: IPoE Health Check %s\n", __FUNCTION__, bValue ? "ENABLED" : "DISABLED"));

    for (uiLoopCount = 0; uiLoopCount < TotalIfaces; uiLoopCount++)
    {
        WanMgr_Iface_Data_t *pWanDmlIfaceData = WanMgr_GetIfaceData_locked(uiLoopCount);
        if (pWanDmlIfaceData != NULL)
        {
            DML_WAN_IFACE *pWanIfaceData = &(pWanDmlIfaceData->data);

            for (int virIf_id = 0; virIf_id < pWanIfaceData->NoOfVirtIfs; virIf_id++)
            {
                DML_VIRTUAL_IFACE *pVirtIf = WanMgr_getVirtualIfaceById(pWanIfaceData->VirtIfList, virIf_id);

                if (!pVirtIf->Enable)
                {
                    CcspTraceInfo(("%s: Skipping disabled Interface: %s\n", __FUNCTION__, pVirtIf->Name));
                    continue;
                }

                if ((type != pVirtIf->IP.ConnectivityCheckType) &&
                    ((type != WAN_CONNECTIVITY_TYPE_NO_CHECK) ||
                     (pVirtIf->IP.ConnectivityCheckType == WAN_CONNECTIVITY_TYPE_IHC)))
                {
                    CcspTraceInfo(("%s: Setting IPoE Health Check to %s for Interface: %s\n", __FUNCTION__, bValue ? "ENABLED" : "DISABLED", pVirtIf->Name));

                    WanMgr_SetConnectivityCheckTypeToPSM(pVirtIf, type);
                    pVirtIf->IP.WCC_TypeChanged = TRUE;
                    pVirtIf->IP.ConnectivityCheckType = type;
                    ret = ANSC_STATUS_SUCCESS;
                }
                else
                {
                    CcspTraceInfo(("%s: No change needed for Interface: %s\n", __FUNCTION__, pVirtIf->Name));
                }
            }

            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }
    }

    if(ret == ANSC_STATUS_SUCCESS)
    {
        if (syscfg_set(NULL, "IPoE_enable", bValue ? "1" : "0") != 0)
        {
            CcspTraceError(("%s %d: syscfg_set IPoE_enable failed\n", __FUNCTION__, __LINE__));
            ret = ANSC_STATUS_FAILURE;
        }
    }

    return ret;
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

    if (pMyObject->CfgChanged)
    {
        // Restart process
        Wan_restart_ipoe_hc();
        pMyObject->CfgChanged = FALSE;
    }

    return ANSC_STATUS_SUCCESS;
}
