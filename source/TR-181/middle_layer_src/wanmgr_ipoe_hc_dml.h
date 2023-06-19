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

BOOL IPoEHealthCheck_GetParamBoolValue (ANSC_HANDLE hInsContext, char *ParamName, BOOL* pBool);
BOOL IPoEHealthCheck_SetParamBoolValue (ANSC_HANDLE hInsContext, char *ParamName, BOOL bValue);
BOOL IPoEHealthCheck_GetParamUlongValue (ANSC_HANDLE hInsContext, char *ParamName, ULONG *pValue);
BOOL IPoEHealthCheck_SetParamUlongValue (ANSC_HANDLE hInsContext, char *ParamName, ULONG value);
BOOL IPoEHealthCheck_GetParamStringValue (ANSC_HANDLE hInsContext, char *ParamName, char *pValue, ULONG *pUlSize);
BOOL IPoEHealthCheck_SetParamStringValue (ANSC_HANDLE hInsContext, char *ParamName, char *pValue, ULONG *pUlSize);
BOOL IPoEHealthCheck_Validate (ANSC_HANDLE hInsContext, char *pReturnParamName, ULONG *puLength);
BOOL IPoEHealthCheck_Commit (ANSC_HANDLE hInsContext);
