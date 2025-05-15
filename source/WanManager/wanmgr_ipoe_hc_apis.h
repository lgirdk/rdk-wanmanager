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

#include "ansc_platform.h"

//Getter
ANSC_STATUS DmlIPoEGetEnable (BOOL *pBool);
ANSC_STATUS DmlIPoEGetIPRegularInterval (ULONG *puLong);
ANSC_STATUS DmlIPoEGetRetryLimit (ULONG *puLong);
ANSC_STATUS DmlIPoEGetRetryInterval (ULONG *puLong);
ULONG GetMgtIntfPid (void);
ULONG GetVoiceIntfPid (void);

//Setter
ANSC_STATUS DmlIPoESetEnable (BOOL Bool);
ANSC_STATUS DmlIPoESetRetryLimit (ULONG puLong);
ANSC_STATUS DmlIPoESetIPRegularInterval (ULONG pValue);
ANSC_STATUS DmlIPoESetRetryInterval (ULONG puLong);

//Commit
ANSC_STATUS SaveIPOEHCParamInDB (void);
