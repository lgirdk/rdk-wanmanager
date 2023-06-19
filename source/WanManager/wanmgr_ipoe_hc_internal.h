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

#include "wanmgr_ipoe_hc_dml.h"
#include "wanmgr_rdkbus_common.h"

typedef struct _COSA_DATAMODEL_LGI_IPOEHC_CLASS_CONTENT
{
    BASE_CONTENT
    BOOL            Enable;
    long unsigned   IPRegularInterval;
    long unsigned   RetryLimit;
    long unsigned   RetryInterval;
}
COSA_DATAMODEL_LGI_IPOEHC, *PCOSA_DATAMODEL_LGI_IPOEHC;

/*
    Standard function declaration
*/
ANSC_HANDLE WanMgr_IPOE_HC_Create (void);
ANSC_STATUS WanMgr_IPOE_HC_Initialize (ANSC_HANDLE hThisObject);
ANSC_STATUS WanMgr_IPOE_HC_Remove (ANSC_HANDLE hThisObject);
