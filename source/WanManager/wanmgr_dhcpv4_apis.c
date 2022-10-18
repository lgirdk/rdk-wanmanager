/*
 * If not stated otherwise in this file or this component's LICENSE file the
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
#include "wanmgr_data.h"
#include "wanmgr_dhcpv4_apis.h"
#include "wanmgr_interface_sm.h"
#include "wanmgr_sysevents.h"
#include "wanmgr_ipc.h"
#include "wanmgr_utils.h"
#include "wanmgr_rdkbus_apis.h"
#include "wanmgr_dhcpv4_internal.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <net/if_arp.h>

#include <syscfg/syscfg.h>
#include "dhcpv4c_api.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <cjson/cJSON.h>

#include "ccsp_psm_helper.h"
#include "sysevent/sysevent.h"
#include "dmsb_tr181_psm_definitions.h"

extern int sysevent_fd;
extern token_t sysevent_token;
extern PWAN_DHCPV4_DATA g_pDhcpv4;

extern WANMGR_BACKEND_OBJ* g_pWanMgrBE;

#define COSA_DHCP4_SYSCFG_NAMESPACE NULL

DML_DHCPC_FULL     CH_g_dhcpv4_client[DML_DHCP_MAX_ENTRIES];
COSA_DML_DHCP_OPT  g_dhcpv4_client_sent[DML_DHCP_MAX_ENTRIES][DML_DHCP_MAX_OPT_ENTRIES];
DML_DHCPC_REQ_OPT  g_dhcpv4_client_req[DML_DHCP_MAX_ENTRIES][DML_DHCP_MAX_OPT_ENTRIES];

ULONG          g_Dhcp4ClientNum = 0;
ULONG          g_Dhcp4ClientSentOptNum[DML_DHCP_MAX_ENTRIES] = {0,0,0,0};
ULONG          g_Dhcp4ClientReqOptNum[DML_DHCP_MAX_ENTRIES]  = {0,0,0,0};

// for PSM access
extern ANSC_HANDLE bus_handle;
extern char g_Subsystem[32];

#define P2P_SUB_NET_MASK   "255.255.255.255"
#define DHCP_STATE_UP      "Up"
#define DHCP_STATE_DOWN    "Down"

static ANSC_STATUS wanmgr_dchpv4_get_ipc_msg_info(WANMGR_IPV4_DATA* pDhcpv4Data, ipc_dhcpv4_data_t* pIpcIpv4Data)
{
    if((pDhcpv4Data == NULL) || (pIpcIpv4Data == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }
    memcpy(pDhcpv4Data->ifname, pIpcIpv4Data->dhcpcInterface, BUFLEN_64);
    memcpy(pDhcpv4Data->ip, pIpcIpv4Data->ip, BUFLEN_32);
    memcpy(pDhcpv4Data->mask , pIpcIpv4Data->mask, BUFLEN_32);
    memcpy(pDhcpv4Data->gateway, pIpcIpv4Data->gateway, BUFLEN_32);
    memcpy(pDhcpv4Data->dnsServer, pIpcIpv4Data->dnsServer, BUFLEN_64);
    memcpy(pDhcpv4Data->dnsServer1, pIpcIpv4Data->dnsServer1, BUFLEN_64);

    if( ( TRUE == pIpcIpv4Data->mtuAssigned ) && ( 0 != pIpcIpv4Data->mtuSize ) )
    {
        pDhcpv4Data->mtuSize =  pIpcIpv4Data->mtuSize;
    }
    else
    {
        pDhcpv4Data->mtuSize =  WANMNGR_INTERFACE_DEFAULT_MTU_SIZE;
    }

    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS wanmgr_handle_dhcpv4_event_data(DML_WAN_IFACE* pIfaceData)
{
    if(NULL == pIfaceData)
    {
       return ANSC_STATUS_FAILURE;
    }

    ipc_dhcpv4_data_t* pDhcpcInfo = pIfaceData->IP.pIpcIpv4Data;
    if(NULL == pDhcpcInfo)
    {
       return ANSC_STATUS_BAD_PARAMETER;
    }

    CcspTraceInfo(("%s %d - Enter \n", __FUNCTION__, __LINE__));
    bool IPv4ConfigChanged = FALSE;


    if (strcmp(pIfaceData->IP.Ipv4Data.ip, pDhcpcInfo->ip) ||
      strcmp(pIfaceData->IP.Ipv4Data.mask, pDhcpcInfo->mask) ||
      strcmp(pIfaceData->IP.Ipv4Data.gateway, pDhcpcInfo->gateway) ||
      strcmp(pIfaceData->IP.Ipv4Data.dnsServer, pDhcpcInfo->dnsServer) ||
      strcmp(pIfaceData->IP.Ipv4Data.dnsServer1, pDhcpcInfo->dnsServer1))
    {
        CcspTraceInfo(("%s %d - IPV4 configuration changed \n", __FUNCTION__, __LINE__));
        IPv4ConfigChanged = TRUE;
    }

    char name[64] = {0};
    char value[64] = {0};
    uint32_t up_time = 0;

    /* ipv4_start_time should be set in every v4 packets */
    snprintf(name,sizeof(name),SYSEVENT_IPV4_START_TIME,pDhcpcInfo->dhcpcInterface);
    up_time = WanManager_getUpTime();
    snprintf(value, sizeof(value), "%u", up_time);
    sysevent_set(sysevent_fd, sysevent_token, name, value, 0);

    if (pDhcpcInfo->addressAssigned)
    {
        CcspTraceInfo(("assigned ip=%s netmask=%s gateway=%s dns server=%s,%s leasetime = %d, rebindtime = %d, renewaltime = %d, dhcp state = %s mtu = %d\n",
                     pDhcpcInfo->ip,
                     pDhcpcInfo->mask,
                     pDhcpcInfo->gateway,
                     pDhcpcInfo->dnsServer,
                     pDhcpcInfo->dnsServer1,
                     pDhcpcInfo->leaseTime,
                     pDhcpcInfo->rebindingTime,
                     pDhcpcInfo->renewalTime,
                     pDhcpcInfo->dhcpState,
                     pDhcpcInfo->mtuSize));

        if (IPv4ConfigChanged)
        {
            if (wanmgr_sysevents_ipv4Info_set(pDhcpcInfo, pDhcpcInfo->dhcpcInterface) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("%s %d - Could not store ipv4 data!", __FUNCTION__, __LINE__));
            }
            //Update isIPv4ConfigChanged flag.
            pIfaceData->IP.Ipv4Changed = TRUE;
        }
        else
        {
            CcspTraceInfo(("%s %d - IPV4 optional configuration received \n", __FUNCTION__, __LINE__));
            snprintf(name, sizeof(name), SYSEVENT_IPV4_DS_CURRENT_RATE, pDhcpcInfo->dhcpcInterface);
            snprintf(value, sizeof(value), "%d", pDhcpcInfo->downstreamCurrRate);
            sysevent_set(sysevent_fd, sysevent_token, name, value, 0);

            snprintf(name, sizeof(name), SYSEVENT_IPV4_US_CURRENT_RATE, pDhcpcInfo->dhcpcInterface);
            snprintf(value, sizeof(value), "%d", pDhcpcInfo->upstreamCurrRate);
            sysevent_set(sysevent_fd, sysevent_token, name, value, 0);

            snprintf(name, sizeof(name), SYSEVENT_IPV4_LEASE_TIME, pDhcpcInfo->dhcpcInterface);
            snprintf(value, sizeof(value), "%d", pDhcpcInfo->leaseTime);
            sysevent_set(sysevent_fd, sysevent_token, name, value, 0);

            if (pDhcpcInfo->isTimeOffsetAssigned)
            {
                snprintf(value, sizeof(value), "@%d", pDhcpcInfo->timeOffset);
                sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_TIME_OFFSET, value, 0);
                sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_DHCPV4_TIME_OFFSET, SET, 0);
            }

            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_TIME_ZONE, pDhcpcInfo->timeZone, 0);
#ifdef FEATURE_IPOE_HEALTH_CHECK
            pIfaceData->IP.Ipv4Renewed = TRUE;
#endif
        }

        // update current IPv4 data
        wanmgr_dchpv4_get_ipc_msg_info(&(pIfaceData->IP.Ipv4Data), pDhcpcInfo);
        WanManager_UpdateInterfaceStatus(pIfaceData, WANMGR_IFACE_CONNECTION_UP);
    }
    else if (pDhcpcInfo->isExpired)
    {
        CcspTraceInfo(("DHCPC Lease expired!!!!!!!!!!\n"));
        // update current IPv4 data
        wanmgr_dchpv4_get_ipc_msg_info(&(pIfaceData->IP.Ipv4Data), pDhcpcInfo);
        WanManager_UpdateInterfaceStatus(pIfaceData, WANMGR_IFACE_CONNECTION_DOWN);
    }


    if (pIfaceData->IP.pIpcIpv4Data != NULL )
    {
        //free memory
        free(pIfaceData->IP.pIpcIpv4Data);
        pIfaceData->IP.pIpcIpv4Data = NULL;
    }

    return ANSC_STATUS_SUCCESS;
}

void WanMgr_UpdateIpFromCellularMgr (char *dhcpcInterface)
{
    char acTmpReturnValue[256] = {0};
    char acTmpQueryParam[256] = {0};

    if(NULL == dhcpcInterface)
    {
        return;
    }

    //get iface data
    WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(dhcpcInterface);
    if(pWanDmlIfaceData != NULL)
    {
        DML_WAN_IFACE* pIfaceData = &(pWanDmlIfaceData->data);
        //check if previously message was already handled
        if(pIfaceData->IP.pIpcIpv4Data == NULL)
        {
            //allocate
            pIfaceData->IP.pIpcIpv4Data = (ipc_dhcpv4_data_t*) malloc(sizeof(ipc_dhcpv4_data_t));
            if(pIfaceData->IP.pIpcIpv4Data != NULL)
            {
                pIfaceData->IP.pIpcIpv4Data->isExpired = FALSE;
                pIfaceData->IP.pIpcIpv4Data->addressAssigned = TRUE;

                //GET IP address
                snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_IPADDRESS);
                memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                {
                    CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return;
                }
                if (acTmpReturnValue[0] != '\0')
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->ip, acTmpReturnValue, BUFLEN_32);
                }
                else
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                    pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                }
                //get gateway IP
                memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                memset( acTmpQueryParam, 0, sizeof( acTmpQueryParam ) );
                snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_GATEWAY);

                if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                {
                    CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return;
                }
                if (acTmpReturnValue[0] != '\0')
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->gateway, acTmpReturnValue, BUFLEN_32);
                }
                else
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                    pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                }
                //Query for DNS Servers
                memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                memset( acTmpQueryParam, 0, sizeof( acTmpQueryParam ) );
                snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_PRIMARY_DNS);

                if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                {
                    CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return;
                }
                if (acTmpReturnValue[0] != '\0')
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dnsServer, acTmpReturnValue, BUFLEN_64);
                }
                else
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                    pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                }

                memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                memset( acTmpQueryParam, 0, sizeof( acTmpQueryParam ) );
                snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_SECONDARY_DNS);

                if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                {
                    CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return;
                }
                if (acTmpReturnValue[0] != '\0')
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dnsServer1, acTmpReturnValue, BUFLEN_64);
                }
                else
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                    pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                }

                //update iface name 
                strncpy(pIfaceData->IP.pIpcIpv4Data->dhcpcInterface, dhcpcInterface, sizeof(pIfaceData->IP.pIpcIpv4Data->dhcpcInterface) - 1);

                //query mask
                memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                memset( acTmpQueryParam, 0, sizeof( acTmpQueryParam ) );
                snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_SUBNET_MASK);

                if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                {
                    CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return;
                }
                if (acTmpReturnValue[0] != '\0')
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->mask, acTmpReturnValue, BUFLEN_32);
                }
                else
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                    pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                }

                //query mtu size

                memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                memset( acTmpQueryParam, 0, sizeof( acTmpQueryParam ) );
                snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_MTU_SIZE);

                if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                {
                    CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return;
                }
                if (acTmpReturnValue[0] != '\0')
                {
                    pIfaceData->IP.pIpcIpv4Data->mtuSize = atoi(acTmpReturnValue);
                    pIfaceData->IP.pIpcIpv4Data->mtuAssigned = TRUE;
                }
                else
                {
                    strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                    pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                }

                //update Ipv4 data
                wanmgr_handle_dhcpv4_event_data(pIfaceData);
            }    
        }

        //IPv6 data

        memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
        memset( acTmpQueryParam, 0, sizeof( acTmpQueryParam ) );
        snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ),"%s%s", pIfaceData->Phy.Path, CELLULARMGR_IPv6_ADDRESS);

        if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( CELLULARMGR_COMPONENT_NAME, CELLULARMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
        {
            CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
            return;
        }
        if (acTmpReturnValue[0] != '\0')
        {
            //TODO: send ipv6 lease info via pandm. review and modify the code to directly update IPv6 from CellularManager.

            // CELLULARMGR sends us only the ipv6 address, so assigning the default value for rest of the values
            char * fifo_pattern = "dibbler-client add '%s' '1' '\\0' '\\0' '\\0' '\\0' '\\0' 0 '1' '\\0' '\\0' '3600' '7200' ''";
            char ipv6_addr [128] = {0};
            char buff [512] = {0};
            int sock   = -1;
            int bytes  = -1;

            // copy the ipv6_addr to local buff
            strncpy (ipv6_addr, acTmpReturnValue, sizeof(ipv6_addr) - 1);

            // copy the ipv6_addr to string pattern so reading side can parse it
            snprintf(buff, sizeof (buff), fifo_pattern,ipv6_addr);

            // open fifo
            sock = open(CCSP_COMMON_FIFO, O_WRONLY);
            if (sock < 0)
            {
                CcspTraceInfo(("%s %d: Failed to create the socket , error = [%d][%s]\n", __FUNCTION__, __LINE__, errno, strerror(errno)));
                WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                return;
            }

            // write data
            bytes = write(sock, buff, strlen(buff));
            if (bytes == 0)
            {
                CcspTraceInfo(("%s %d: Failed to write in fifo , error = [%d][%s]\n", __FUNCTION__, __LINE__, errno, strerror(errno)));
                close(sock);
                WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                return;
            }

            // close fifo
            close(sock);
        }
    }

    if(pWanDmlIfaceData != NULL)
    {
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }
}

void* IPCPStateChangeHandler (void *arg)
{
    char acTmpReturnValue[256] = {0};
    char acTmpQueryParam[256] = {0};
    char *token = NULL;
    int index = -1;

    const char *dhcpcInterface = (char *) arg;
    if(NULL == dhcpcInterface)
    {
       return (void *)ANSC_STATUS_FAILURE;
    }
    pthread_detach(pthread_self());

    //get iface data
    WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(dhcpcInterface);
    if(pWanDmlIfaceData != NULL)
    {
        DML_WAN_IFACE* pIfaceData = &(pWanDmlIfaceData->data);

        // Get PPP instance number
        sscanf(pIfaceData->PPP.Path, "%*[^0-9]%d", &index);

        //check if previously message was already handled
        if(pIfaceData->IP.pIpcIpv4Data == NULL)
        {
            //allocate
            pIfaceData->IP.pIpcIpv4Data = (ipc_dhcpv4_data_t*) malloc(sizeof(ipc_dhcpv4_data_t));
            if(pIfaceData->IP.pIpcIpv4Data != NULL)
            {
                switch (pIfaceData->PPP.IPCPStatus)
                {
                     case WAN_IFACE_IPCP_STATUS_UP:
                     {
                         pIfaceData->IP.pIpcIpv4Data->isExpired = FALSE;
                         pIfaceData->IP.pIpcIpv4Data->addressAssigned = TRUE;
                         strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_UP, BUFLEN_64);

                         //Query for Local IP address
                         snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ), PPP_IPCP_LOCAL_IPADDRESS, index );
                         memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                         if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                         {
                             CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                             goto EXIT;
                         }
                         if (acTmpReturnValue[0] != '\0')
                         {
                             strncpy (pIfaceData->IP.pIpcIpv4Data->ip, acTmpReturnValue, BUFLEN_32);
                         }
                         else
                         {
                             strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                             pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                         }

                         //Query for Remote IP address
                         snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ), PPP_IPCP_REMOTEIPADDRESS, index );
                         memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                         if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                         {
                             CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                             goto EXIT;
                         }
                         if (acTmpReturnValue[0] != '\0')
                         {
                              strncpy (pIfaceData->IP.pIpcIpv4Data->gateway, acTmpReturnValue, BUFLEN_32);
                         }
                         else
                         {
                             strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, BUFLEN_64);
                             pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                         }
                         //Query for DNS Servers
                         snprintf( acTmpQueryParam, sizeof(acTmpQueryParam ), PPP_IPCP_DNS_SERVERS, index );
                         memset( acTmpReturnValue, 0, sizeof( acTmpReturnValue ) );
                         if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acTmpQueryParam, acTmpReturnValue ) )
                         {
                             CcspTraceError(("%s %d Failed to get param value for paramname %s \n", __FUNCTION__, __LINE__, acTmpQueryParam));
                             goto EXIT;
                         }
                         if (acTmpReturnValue[0] != '\0')
                         {
                             //Return first DNS Server
                             token = strtok(acTmpReturnValue, ",");
                             if (token != NULL)
                             {
                                 strncpy (pIfaceData->IP.pIpcIpv4Data->dnsServer, token,sizeof(pIfaceData->IP.pIpcIpv4Data->dnsServer)-1);
                                 //Return first DNS Server
                                 token = strtok(NULL, ",");
                                 if (token != NULL)
                                 {
                                     strncpy (pIfaceData->IP.pIpcIpv4Data->dnsServer1, token,sizeof(pIfaceData->IP.pIpcIpv4Data->dnsServer1)-1);
                                 }
                             }
                         }

                          strncpy(pIfaceData->IP.pIpcIpv4Data->dhcpcInterface, dhcpcInterface, sizeof(pIfaceData->IP.pIpcIpv4Data->dhcpcInterface) - 1);
                          strncpy(pIfaceData->IP.pIpcIpv4Data->mask, P2P_SUB_NET_MASK, sizeof(pIfaceData->IP.pIpcIpv4Data->mask)-1);
                          wanmgr_handle_dhcpv4_event_data(pIfaceData);
                         break;
                     }
                     case WAN_IFACE_IPCP_STATUS_DOWN:
                     {
                         strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpcInterface, dhcpcInterface, sizeof(pIfaceData->IP.pIpcIpv4Data->dhcpcInterface) - 1);
                         strncpy (pIfaceData->IP.pIpcIpv4Data->dhcpState, DHCP_STATE_DOWN, sizeof(pIfaceData->IP.pIpcIpv4Data->dhcpState)-1);
                         pIfaceData->IP.pIpcIpv4Data->addressAssigned = FALSE;
                         wanmgr_handle_dhcpv4_event_data(pIfaceData);
                         break;
                     }
                }

            }
        }

    }

EXIT:
    if(pWanDmlIfaceData != NULL)
    {
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }
    if (arg != NULL)
    {
        free(arg);
    }

    return NULL;
}

static ANSC_STATUS DhcpcDmlScan()
{
    CHAR            tmpBuff[64]    = {0};
    CHAR            tmp[3][64];
    CHAR            ip[32];   
    CHAR            subnet[32];
    CHAR            str_val[64] = {0};
    CHAR            str_key[64] = {0};
    CHAR            dns[3][32]  = {"","",""};
    CHAR            line[128];
    char            *tok;
    int             nmu_dns_server =  0;
    FILE            *fp = NULL;

    PDML_DHCPC_FULL  pEntry = NULL;

    ULONG ulIndex = 0;

    g_Dhcp4ClientNum = 0;

    for (ulIndex = 0; ulIndex < 2; ulIndex++)
    {
        if (ulIndex == 0)
        {
            fp = fopen("/tmp/udhcp.log", "r");
        }       
        else if (ulIndex == 1)
        {

            /*RDKB-6750, CID-33082, free resources before exit*/
            if(fp)
            {
                fclose(fp);
                fp = NULL;
            }

            return ANSC_STATUS_SUCCESS;

        }
        pEntry = &CH_g_dhcpv4_client[ulIndex];

        if (!fp) 
        {
            return ANSC_STATUS_FAILURE;
        }   

        while ( fgets(line, sizeof(line), fp) )
        {         
            memset(str_key, 0, sizeof(str_key));
            memset(str_val, 0, sizeof(str_val));

            tok = strtok( line, ":");
            if(tok) strncpy(str_key, tok, sizeof(str_key)-1 );

            tok = strtok(NULL, ":");
            while( tok && (tok[0] == ' ') ) tok++; /*RDKB-6750, CID-33384, CID-32954; null check before use*/
            if(tok) strncpy(str_val, tok, sizeof(str_val)-1 );

            if ( str_val[ _ansc_strlen(str_val) - 1 ] == '\n' )
            {
                str_val[ _ansc_strlen(str_val) - 1 ] = '\0';
            }

            if( !strcmp(str_key, "interface     ") )
            {
                AnscCopyString( pEntry->Cfg.Interface, str_val);
            }
            else if( !strcmp(str_key, "ip address    ") )
            {
                sscanf(str_val, "%31s", ip);
                AnscWriteUlong(&pEntry->Info.IPAddress.Value, _ansc_inet_addr(ip));
            }
            else if( !strcmp(str_key, "subnet mask   ") )
            {
                sscanf(str_val, "%31s", subnet );
                AnscWriteUlong(&pEntry->Info.SubnetMask.Value, _ansc_inet_addr(subnet));
            }
            else if( !strcmp(str_key, "lease time    ") )
            {
                pEntry->Info.LeaseTimeRemaining = atoi(str_val);
            }
            else if( !strcmp(str_key, "router        ") )
            {
                sscanf(str_val, "%31s", ip ); 
                AnscWriteUlong(&pEntry->Info.IPRouters[0].Value, _ansc_inet_addr(ip) ); 
            }
            else if( !strcmp(str_key, "server id     ") )
            {
                sscanf(str_val, "%31s", ip ); 
                AnscWriteUlong(&pEntry->Info.DHCPServer.Value, _ansc_inet_addr(ip));
            }
            else if( !strcmp(str_key, "dns server    ") )
            {
                nmu_dns_server = 0;               
                char *tok;

                tok = strtok( str_val, " ");
                if(tok) strncpy(dns[0], tok, sizeof(dns[0])-1 ); 
                if( dns[0] ) 
                {
                    ++nmu_dns_server;
                    AnscWriteUlong(&pEntry->Info.DNSServers[0].Value, _ansc_inet_addr(dns[0]));
                }
                while( tok != NULL)
                {
                    tok = strtok(NULL, " ");
                    if(tok) strncpy(dns[nmu_dns_server], tok, sizeof(dns[nmu_dns_server])-1 );
                    if (strlen(dns[nmu_dns_server]) > 1 )
                    {    
                        AnscWriteUlong(&pEntry->Info.DNSServers[nmu_dns_server].Value, _ansc_inet_addr(dns[nmu_dns_server]));
                        ++nmu_dns_server;
                        if( nmu_dns_server > 2) /*RDKB-6750, CID-33057, Fixing Out-of-bounds read of dns[3][] */
                            nmu_dns_server = 2;
                    }
                }
            }
        }

        if ( !_ansc_strncmp(pEntry->Cfg.Interface, DML_DHCP_CLIENT_IFNAME, _ansc_strlen(DML_DHCP_CLIENT_IFNAME)) )
        {
            memset(tmpBuff, 0, sizeof(tmpBuff));
            syscfg_get(NULL, "tr_dhcp4_instance_wan", tmpBuff, sizeof(tmpBuff));
            CH_g_dhcpv4_client[ulIndex].Cfg.InstanceNumber  = atoi(tmpBuff);

            memset(tmpBuff, 0, sizeof(tmpBuff));
            syscfg_get(NULL, "tr_dhcp4_alias_wan", tmpBuff, sizeof(tmpBuff));
            AnscCopyString(CH_g_dhcpv4_client[ulIndex].Cfg.Alias, tmpBuff);
        }
        else if ( !_ansc_strncmp(pEntry->Cfg.Interface, "lan0", _ansc_strlen("lan0")) )
        {
            memset(tmpBuff, 0, sizeof(tmpBuff));
            syscfg_get(NULL, "tr_dhcp4_instance_lan", tmpBuff, sizeof(tmpBuff));
            CH_g_dhcpv4_client[ulIndex].Cfg.InstanceNumber  = atoi(tmpBuff);

            memset(tmpBuff, 0, sizeof(tmpBuff));
            syscfg_get(NULL, "tr_dhcp4_alias_lan", tmpBuff, sizeof(tmpBuff));
            AnscCopyString(CH_g_dhcpv4_client[ulIndex].Cfg.Alias, tmpBuff);
        }

        pEntry->Cfg.PassthroughEnable   = FALSE;

        //AnscCopyString( pEntry->Cfg.PassthroughDHCPPool, "");  
        pEntry->Cfg.bEnabled                     = TRUE;
        //pEntry->Cfg.bRenew                       = FALSE;

        pEntry->Info.Status                      = DML_DHCP_STATUS_Enabled;
        pEntry->Info.DHCPStatus                  = DML_DHCPC_STATUS_Bound;

        pEntry->Info.NumIPRouters                = 1;
        pEntry->Info.NumDnsServers               = nmu_dns_server;
        g_Dhcp4ClientNum++;

    }    
    fclose(fp);
    return ANSC_STATUS_SUCCESS;
}

/*
    Description:
        The API retrieves the number of DHCP clients in the system.
 */
ULONG
WanMgr_DmlDhcpcGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
    return 1;
}

/*
    Description:
        The API retrieves the complete info of the DHCP clients designated by index. The usual process is the caller gets the total number of entries, then iterate through those by calling this API.
    Arguments:
        ulIndex        Indicates the index number of the entry.
        pEntry        To receive the complete info of the entry.
*/
ANSC_STATUS WanMgr_DmlDhcpcGetEntry ( ANSC_HANDLE hContext, ULONG ulIndex, PDML_DHCPC_FULL pEntry )
{
    UNREFERENCED_PARAMETER(hContext);
    if(ulIndex >0)
        return(ANSC_STATUS_FAILURE);
    pEntry->Cfg.InstanceNumber = 1;
    WanMgr_DmlDhcpcGetCfg(hContext, &pEntry->Cfg);
    WanMgr_DmlDhcpcGetInfo(hContext, ulIndex,&pEntry->Info);
    return ANSC_STATUS_SUCCESS;  
}

/*
    Function Name: WanMgr_DmlDhcpcSetValues
    Description:
        The API set the the DHCP clients' instance number and alias to the syscfg DB.
    Arguments:
        ulIndex        Indicates the index number of the entry.
        ulInstanceNumber Client's Instance number 
        pAlias          pointer to the Client's Alias
*/
ANSC_STATUS WanMgr_DmlDhcpcSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
    return ANSC_STATUS_FAILURE;
}

/*
    Description:
        The API adds DHCP client. 
    Arguments:
        pEntry        Caller fills in pEntry->Cfg, except Alias field. Upon return, callee fills pEntry->Cfg.Alias field and as many as possible fields in pEntry->Info.
*/
ANSC_HANDLE WanMgr_DmlDhcpcAddEntry ( ANSC_HANDLE hContext, ULONG pInsNumber )
{
    PDHCPC_CONTEXT_LINK_OBJECT        pCxtLink          = NULL;
    PDML_DHCPC_FULL                 pDhcpc              = NULL;
    PWAN_DHCPV4_DATA                pDhcpv4     = (PWAN_DHCPV4_DATA)g_pWanMgrBE->hDhcpv4;
    ULONG                            i                  = 0;

    UNREFERENCED_PARAMETER(hContext);
    if (pDhcpv4 != NULL)
    {
        pDhcpc    = (PDML_DHCPC_FULL)AnscAllocateMemory( sizeof(DML_DHCPC_FULL) );
        if ( !pDhcpc )
        {
            return NULL; /* return the handle */
        }

        /* Set default value */
        DHCPV4_CLIENT_SET_DEFAULTVALUE(pDhcpc);

        /* Add into our link tree*/
        pCxtLink = (PDHCPC_CONTEXT_LINK_OBJECT)AnscAllocateMemory( sizeof(DHCPC_CONTEXT_LINK_OBJECT) );
        if ( !pDhcpc )
        {
            AnscFreeMemory(pDhcpc);
            return NULL;
        }

        DHCPV4_CLIENT_INITIATION_CONTEXT(pCxtLink)

        pCxtLink->hContext         = (ANSC_HANDLE)pDhcpc;
        pCxtLink->bNew             = TRUE;

        if ( !++pDhcpv4->maxInstanceOfClient )
        {
            pDhcpv4->maxInstanceOfClient = 1;
        }
        pDhcpc->Cfg.InstanceNumber = pDhcpv4->maxInstanceOfClient;
        pCxtLink->InstanceNumber   = pDhcpc->Cfg.InstanceNumber;

        _ansc_sprintf( pDhcpc->Cfg.Alias, "Client%d", pDhcpc->Cfg.InstanceNumber);

        /* Put into our list */
        SListPushEntryByInsNum(&pDhcpv4->ClientList, (PCONTEXT_LINK_OBJECT)pCxtLink);
    
        /* we recreate the configuration because we has new delay_added entry for dhcpv4 */
        WanMgr_Dhcpv4RegSetDhcpv4Info(pDhcpv4);
    }
    return pCxtLink;
}

/*
    Description:
        The API removes the designated DHCP client entry. 
    Arguments:
        pAlias        The entry is identified through Alias.
*/
ANSC_STATUS
WanMgr_DmlDhcpcDelEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    return ANSC_STATUS_FAILURE;
}

/*
Description:
    The API re-configures the designated DHCP client entry. 
Arguments:
    pAlias        The entry is identified through Alias.
    pEntry        The new configuration is passed through this argument, even Alias field can be changed.
*/

ANSC_STATUS
WanMgr_DmlDhcpcSetCfg
    (
        ANSC_HANDLE                 hContext,
        PDML_DHCPC_CFG         pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pCfg);
    ULONG                           index  = 0;
    /*don't allow to change dhcp configuration*/ 

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcGetCfg
    (
        ANSC_HANDLE                 hContext,
        PDML_DHCPC_CFG         pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    ULONG       i = 0;
    char ifname[32] = {0};

    if ( !pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    sprintf(pCfg->Alias,"eRouter");
    pCfg->bEnabled = TRUE;
    pCfg->InstanceNumber = 1;
    if(dhcpv4c_get_ert_ifname(ifname))
        pCfg->Interface[0] = 0;
    else
        snprintf(pCfg->Interface, sizeof(pCfg->Interface)-1, "%s", ifname);
    pCfg->PassthroughEnable = TRUE;
    pCfg->PassthroughDHCPPool[0] = 0;

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
WanMgr_DmlDhcpcGetInfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PDML_DHCPC_INFO        pInfo
    )
{
    UNREFERENCED_PARAMETER(hContext);
    ULONG                           index  = 0;
    ANSC_STATUS  rc;
    dhcpv4c_ip_list_t address;
    int i;

    if ( (!pInfo) || (ulInstanceNumber != 1) ){
        return ANSC_STATUS_FAILURE;
    }

    pInfo->Status = DML_DHCP_STATUS_Enabled;
    dhcpv4c_get_ert_fsm_state((int*)&pInfo->DHCPStatus);
    dhcpv4c_get_ert_ip_addr((unsigned int*)&pInfo->IPAddress.Value);
    dhcpv4c_get_ert_mask((unsigned int*)&pInfo->SubnetMask.Value);
    pInfo->NumIPRouters = 1;
    dhcpv4c_get_ert_gw((unsigned int*)&pInfo->IPRouters[0].Value);
    address.number = 0;
    dhcpv4c_get_ert_dns_svrs(&address);
    pInfo->NumDnsServers = address.number;
    if (pInfo->NumDnsServers > DML_DHCP_MAX_ENTRIES)
    {
        CcspTraceError(("!!! Max DHCP Entry Overflow: %d",address.number));
        pInfo->NumDnsServers = DML_DHCP_MAX_ENTRIES; // Fail safe
    }
    for(i=0; i< pInfo->NumDnsServers;i++)
        pInfo->DNSServers[i].Value = address.addrs[i];
    dhcpv4c_get_ert_remain_lease_time((unsigned int*)&pInfo->LeaseTimeRemaining);
    dhcpv4c_get_ert_dhcp_svr((unsigned int*)&pInfo->DHCPServer);

    return ANSC_STATUS_SUCCESS;
}

/*
    Description:
        The API initiates a DHCP client renewal. 
    Arguments:
        pAlias        The entry is identified through Alias.
*/
ANSC_STATUS
WanMgr_DmlDhcpcRenew
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if(ulInstanceNumber != 1)
        return(ANSC_STATUS_FAILURE);
#ifndef _HUB4_PRODUCT_REQ_
    system("sysevent set dhcp_client-renew");
#endif
    return(ANSC_STATUS_SUCCESS);
}

/*
 *  DHCP Client Send/Req Option
 *
 *  The options are managed on top of a DHCP client,
 *  which is identified through pClientAlias
 */

static BOOL WanMgr_DmlDhcpcWriteOptions(ULONG ulClientInstanceNumber)
{
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    return FALSE;
}


ULONG
WanMgr_DmlDhcpcGetNumberOfSentOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    return 0;
}

ANSC_STATUS
WanMgr_DmlDhcpcGetSentOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        ULONG                       ulIndex,
        PCOSA_DML_DHCP_OPT          pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}


ANSC_STATUS
WanMgr_DmlDhcpcGetSentOptionbyInsNum
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        PCOSA_DML_DHCP_OPT          pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    ULONG index   = 0;
    ULONG i     = 0;

    for(i = 0; i < g_Dhcp4ClientNum; i++)
    {
        if ( CH_g_dhcpv4_client[i].Cfg.InstanceNumber ==  ulClientInstanceNumber)
        {
            for( index = 0;  index < g_Dhcp4ClientSentOptNum[i]; index++)
            {
                if ( pEntry->InstanceNumber == g_dhcpv4_client_sent[i][index].InstanceNumber )
                {
                    AnscCopyMemory( pEntry, &g_dhcpv4_client_sent[i][index], sizeof(COSA_DML_DHCP_OPT));
                    return ANSC_STATUS_SUCCESS;
                }
            } 
        }
    }

    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcSetSentOptionValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcAddSentOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        PCOSA_DML_DHCP_OPT          pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}



ANSC_STATUS
WanMgr_DmlDhcpcDelSentOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        ULONG                       ulInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    return ANSC_STATUS_FAILURE;
}


ANSC_STATUS
WanMgr_DmlDhcpcSetSentOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        PCOSA_DML_DHCP_OPT          pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}

/*
 *  DHCP Client Send/Req Option
 *
 *  The options are managed on top of a DHCP client,
 *  which is identified through pClientAlias
 */
ULONG
WanMgr_DmlDhcpcGetNumberOfReqOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    return 0;
}

ANSC_STATUS
WanMgr_DmlDhcpcGetReqOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        ULONG                       ulIndex,
        PDML_DHCPC_REQ_OPT     pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcGetReqOptionbyInsNum
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        PDML_DHCPC_REQ_OPT     pEntry
    )
{
   UNREFERENCED_PARAMETER(hContext);
   UNREFERENCED_PARAMETER(ulClientInstanceNumber);
   UNREFERENCED_PARAMETER(pEntry);
   return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcSetReqOptionValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
    return ANSC_STATUS_FAILURE;
}


ANSC_STATUS
WanMgr_DmlDhcpcAddReqOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        PDML_DHCPC_REQ_OPT     pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcDelReqOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        ULONG                       ulInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
WanMgr_DmlDhcpcSetReqOption
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulClientInstanceNumber,
        PDML_DHCPC_REQ_OPT          pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulClientInstanceNumber);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}
