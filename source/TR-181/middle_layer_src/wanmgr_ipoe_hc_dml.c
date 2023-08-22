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

#include "wanmgr_plugin_main_apis.h"
#include "wanmgr_ipoe_hc_apis.h"
#include "wanmgr_ipoe_hc_internal.h"
#include "ansc_platform.h"

extern WANMGR_BACKEND_OBJ *g_pWanMgrBE;

BOOL IPoEHealthCheck_GetParamBoolValue (ANSC_HANDLE hInsContext, char *ParamName, BOOL* pBool)
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) g_pWanMgrBE->hIPoE_hc;

    if (strcmp(ParamName, "Enable") == 0)
    {
        *pBool = pMyObject->Enable;
        return TRUE;
    }

    return FALSE;
}

BOOL IPoEHealthCheck_SetParamBoolValue (ANSC_HANDLE hInsContext, char *ParamName, BOOL bValue)
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) g_pWanMgrBE->hIPoE_hc;

    if (strcmp(ParamName, "Enable") == 0)
    {
        pMyObject->Enable = bValue;
        DmlIPoESetEnable(bValue);
        return TRUE;
    }

    return FALSE;
}

BOOL IPoEHealthCheck_GetParamUlongValue (ANSC_HANDLE hInsContext, char *ParamName, ULONG *pValue)
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) g_pWanMgrBE->hIPoE_hc;

    if (strcmp(ParamName, "RegularInterval") == 0)
    {
        *pValue = pMyObject->IPRegularInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "RetryInterval") == 0)
    {
        *pValue = pMyObject->RetryInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "Limit") == 0)
    {
        *pValue = pMyObject->RetryLimit;
        return TRUE;
    }

    return FALSE;
}

BOOL IPoEHealthCheck_SetParamUlongValue (ANSC_HANDLE hInsContext, char *ParamName, ULONG Value)
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) g_pWanMgrBE->hIPoE_hc;

    if (strcmp(ParamName, "RegularInterval") == 0)
    {
        if (pMyObject->IPRegularInterval != Value)
        {
            pMyObject->IPRegularInterval = Value;
            pMyObject->CfgChanged = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "RetryInterval") == 0)
    {
        if (pMyObject->RetryInterval != Value)
        {
            pMyObject->RetryInterval = Value;
            pMyObject->CfgChanged = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "Limit") == 0)
    {
        if (pMyObject->RetryLimit != Value)
        {
            pMyObject->RetryLimit = Value;
            pMyObject->CfgChanged = TRUE;
        }
        return TRUE;
    } 

    return FALSE;
}

BOOL IPoEHealthCheck_SetParamStringValue (ANSC_HANDLE hInsContext, char *ParamName, char *pValue, ULONG *pUlSize)
{
    UNREFERENCED_PARAMETER(hInsContext);

    return TRUE;
}

BOOL IPoEHealthCheck_GetParamStringValue (ANSC_HANDLE hInsContext, char *ParamName, char *pValue, ULONG *pUlSize)
{
    UNREFERENCED_PARAMETER(hInsContext);

    return TRUE;
}

BOOL IPoEHealthCheck_Validate (ANSC_HANDLE hInsContext, char *pReturnParamName, ULONG *puLength)
{
    UNREFERENCED_PARAMETER(hInsContext);

    return TRUE;
}

BOOL IPoEHealthCheck_Commit (ANSC_HANDLE hInsContext)
{
    UNREFERENCED_PARAMETER(hInsContext);

    if (SaveIPOEHCParamInDB() == ANSC_STATUS_SUCCESS)
    {
        return TRUE;
    }

    CcspTraceError(("Failed to save IPOE HC changed parameters\n"));
    return FALSE;
}
