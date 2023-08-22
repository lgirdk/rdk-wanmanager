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

#include "wanmgr_ipoe_hc_internal.h"
#include "wanmgr_ipoe_hc_apis.h"
#include "wanmgr_ipoe_hc_dml.h"

ANSC_HANDLE WanMgr_IPOE_HC_Create (void)
{
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject;

    pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) AnscAllocateMemory(sizeof(COSA_DATAMODEL_LGI_IPOEHC));

    if (!pMyObject)
    {
        return NULL;
    }

    pMyObject->Create = WanMgr_IPOE_HC_Create;
    pMyObject->Remove = WanMgr_IPOE_HC_Remove;
    pMyObject->Initialize = WanMgr_IPOE_HC_Initialize;

    pMyObject->Initialize ((ANSC_HANDLE) pMyObject);

    return (ANSC_HANDLE) pMyObject;
}

ANSC_STATUS WanMgr_IPOE_HC_Initialize ( ANSC_HANDLE hThisObject )
{
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) hThisObject;

    DmlIPoEGetEnableFromSyscfg(&pMyObject->Enable);
    DmlIPoEGetIPRegularInterval(&pMyObject->IPRegularInterval);
    DmlIPoEGetRetryLimit(&pMyObject->RetryLimit);
    DmlIPoEGetRetryInterval(&pMyObject->RetryInterval); 
    pMyObject->CfgChanged = FALSE;

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS WanMgr_IPOE_HC_Remove ( ANSC_HANDLE hThisObject )
{
    PCOSA_DATAMODEL_LGI_IPOEHC pMyObject = (PCOSA_DATAMODEL_LGI_IPOEHC) hThisObject;

    AnscFreeMemory((ANSC_HANDLE)pMyObject);

    return ANSC_STATUS_SUCCESS;
}
