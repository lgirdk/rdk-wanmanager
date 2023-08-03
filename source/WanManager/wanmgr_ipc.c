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

/* ---- Include Files ---------------------------------------- */
#include <sys/un.h>
#include <errno.h>
#include <sysevent/sysevent.h>
#include "wanmgr_ipc.h"
#include "wanmgr_data.h"
#include "wanmgr_sysevents.h"
#include "wanmgr_dhcpv4_apis.h"

#ifdef FEATURE_IPOE_HEALTH_CHECK
#include "wanmgr_ipoe_hc_apis.h"
#define MAX_ALLOWED_FORCED_RENEW 5
#endif

#define WANMGR_MAX_IPC_PROCCESS_TRY             5
#define WANMGR_IPC_PROCCESS_TRY_WAIT_TIME       30000 //us


typedef struct _WanIpcCtrl_t_
{
    INT interfaceIdx;
} WanIpcCtrl_t;


static int   ipcListenFd;   /* Unix domain IPC listening socket fd */
extern int sysevent_fd;
extern token_t sysevent_token;


/* ---- Private Functions ------------------------------------ */
#ifdef FEATURE_IPOE_HEALTH_CHECK
static ANSC_STATUS Wan_StopDhcpIPv4(char * ifName);
static ANSC_STATUS Wan_ForceRenewDhcpIPv4(char * ifName);
static ANSC_STATUS Wan_StopDhcpIPv6(char * ifName);
#endif /*FEATURE_IPOE_HEALTH_CHECK*/



/* ---- Private Variables ------------------------------------ */



//ANSC_STATUS WanManager_sendIpcMsgToClient_AndGetReplyWithTimeout(ipc_msg_payload_t * payload)
//{
//    return ANSC_STATUS_SUCCESS;
//}
//
//
//ANSC_STATUS WanManager_sendIpcMsgToClient(ipc_msg_payload_t * payload)
//{
//    return ANSC_STATUS_SUCCESS;
//}


static ANSC_STATUS WanMgr_IpcNewIpv4Msg(ipc_dhcpv4_data_t* pNewIpv4Msg)
{
    ANSC_STATUS retStatus = ANSC_STATUS_FAILURE;
    INT try = 0;

    while((retStatus != ANSC_STATUS_SUCCESS) && (try < WANMGR_MAX_IPC_PROCCESS_TRY))
    {
        //get iface data
        WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(pNewIpv4Msg->dhcpcInterface);
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
                    // copy data
                    memcpy(pIfaceData->IP.pIpcIpv4Data, pNewIpv4Msg, sizeof(ipc_dhcpv4_data_t));
                    retStatus = ANSC_STATUS_SUCCESS;
                }
            }

            //release lock
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }

        if(retStatus != ANSC_STATUS_SUCCESS)
        {
            try++;
            usleep(WANMGR_IPC_PROCCESS_TRY_WAIT_TIME);
        }
    }

    return retStatus;
}


static ANSC_STATUS WanMgr_IpcNewIpv6Msg(ipc_dhcpv6_data_t* pNewIpv6Msg)
{
    ANSC_STATUS retStatus = ANSC_STATUS_FAILURE;
    INT try = 0;

    while((retStatus != ANSC_STATUS_SUCCESS) && (try < WANMGR_MAX_IPC_PROCCESS_TRY))
    {
        //get iface data
        WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(pNewIpv6Msg->ifname);
        if(pWanDmlIfaceData != NULL)
        {
            DML_WAN_IFACE* pIfaceData = &(pWanDmlIfaceData->data);

            //check if previously message was already handled
            if(pIfaceData->IP.pIpcIpv6Data == NULL)
            {
                //allocate
                pIfaceData->IP.pIpcIpv6Data = (ipc_dhcpv6_data_t*) malloc(sizeof(ipc_dhcpv6_data_t));
                if(pIfaceData->IP.pIpcIpv6Data != NULL)
                {
                    // copy data
                    memcpy(pIfaceData->IP.pIpcIpv6Data, pNewIpv6Msg, sizeof(ipc_dhcpv6_data_t));
                    retStatus = ANSC_STATUS_SUCCESS;
                }
            }

            //release lock
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }

        if(retStatus != ANSC_STATUS_SUCCESS)
        {
            try++;
            usleep(WANMGR_IPC_PROCCESS_TRY_WAIT_TIME);
        }
    }

    return retStatus;
}

ANSC_STATUS WanMgr_SetInterfaceStatus(char *ifName, wanmgr_iface_status_t state)
{
    WanMgr_Iface_Data_t* pWanDmlIfaceData = NULL;
    DML_WAN_IFACE*          pIfaceData = NULL;

    pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(ifName);
    if(pWanDmlIfaceData != NULL)
    {
        pIfaceData = &(pWanDmlIfaceData->data);
        WanManager_UpdateInterfaceStatus(pIfaceData, state);
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        return ANSC_STATUS_SUCCESS;
    }
    return ANSC_STATUS_FAILURE;
}

#ifdef FEATURE_IPOE_HEALTH_CHECK
ANSC_STATUS Wan_restart_ipoe_hc (void)
{
     if (DmlIPoESetEnable(false) == ANSC_STATUS_FAILURE)
           return ANSC_STATUS_FAILURE;

     sleep(1);

     if (DmlIPoESetEnable(true) == ANSC_STATUS_FAILURE)
           return ANSC_STATUS_FAILURE;

     return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS WanMgr_IpcNewIhcMsg(ipc_ihc_data_t *pIhcMsg)
{
    if (pIhcMsg == NULL)
    {
        CcspTraceError(("[%s-%d] Failed to process IPoE v6 Echo Renew Event \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    char conn_status[BUFLEN_16] = {0};

    switch(pIhcMsg->msgType)
    {
        case IPOE_MSG_IHC_ECHO_RENEW_IPV6:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_RENEW_IPV6 from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            if (Wan_ForceRenewDhcpIPv6(pIhcMsg->ifName) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to process IPoE v6 Echo Renew Event \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        case IPOE_MSG_IHC_ECHO_RENEW_IPV4:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_RENEW_IPV4 from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            if (Wan_ForceRenewDhcpIPv4(pIhcMsg->ifName) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to process IPoE v6 Echo Renew Event \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        case IPOE_MSG_IHC_ECHO_RENEW_MGMT:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_RENEW from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            if (Wan_ForceRenewVlan(pIhcMsg->ifName) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to process IPoE mg0 Echo Renew Event \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        case IPOE_MSG_IHC_ECHO_RENEW_VOIP:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_RENEW from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            if (Wan_ForceRenewVlan(pIhcMsg->ifName) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to process IPoE mg0 Echo Renew Event \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        case IPOE_MSG_IHC_RESTART_DUO_BINDING_ERROR:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_RESTART_DUO_BINDING_ERROR from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            ANSC_STATUS err = Wan_restart_ipoe_hc();
            if (err != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to restart all interfaces \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        case IPOE_MSG_IHC_ECHO_IPV6_UP:
            /* Check if we get the IPv6 connection UP message from the
               IPOE Health Check, in case the IPV6 status is DOWN set
               it to UP.
               */
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_IPV6_UP from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_IPV6_CONNECTION_STATE, conn_status, sizeof(conn_status));
            if(strcmp(conn_status, WAN_STATUS_DOWN) == 0)
            {
                CcspTraceInfo(("Setting IPV6 Connection state to UP \n"));
                sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV6_CONNECTION_STATE, WAN_STATUS_UP, 0);
            }
            break;
        case IPOE_MSG_IHC_ECHO_IPV4_UP:
            /* Check if we get the IPv4 connection UP message from the
               IPOE Health Check, in case the IPv4 status is DOWN, set
               it to UP.
               */
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_IPV4_UP from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_IPV4_CONNECTION_STATE, conn_status, sizeof(conn_status));
            if(strcmp(conn_status, WAN_STATUS_DOWN) == 0)
            {
                CcspTraceInfo(("Setting IPv4 Connection state to UP \n"));
                sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_CONNECTION_STATE, WAN_STATUS_UP, 0);
            }
            break;
        case IPOE_MSG_IHC_ECHO_FAIL_IPV4:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_FAIL_IPV4 from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            if(Wan_StopDhcpIPv4(pIhcMsg->ifName) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to process IPoE v4 Echo Fail Event \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        case IPOE_MSG_IHC_ECHO_FAIL_IPV6:
            CcspTraceInfo(("[%s-%d] Received IPOE_MSG_IHC_ECHO_FAIL_IPV6 from IHC for intf: %s \n", __FUNCTION__, __LINE__, pIhcMsg->ifName));
            if(Wan_StopDhcpIPv6(pIhcMsg->ifName) != ANSC_STATUS_SUCCESS)
            {
                CcspTraceError(("[%s-%d] Failed to process IPoE v6 Echo Fail Event \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
            }
            break;
        default:
            CcspTraceError(("[%s-%d] Invalid message type \n", __FUNCTION__, __LINE__));
            return ANSC_STATUS_FAILURE;
    }
    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS Wan_StopDhcpIPv4(char *ifName)
{

    /* Kill DHCPv4 client */
    if (WanManager_StopDhcpv4Client(ifName,TRUE) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceInfo(("Failed to kill DHCPv4 Client \n"));
    }

    return WanMgr_SetInterfaceStatus(ifName, WANMGR_IFACE_CONNECTION_DOWN);
}

static ANSC_STATUS Wan_ForceRenewDhcpIPv4(char *ifName)
{

    /*send triggered renew request to DHCPC*/
    if (WanManager_IsApplicationRunning(DHCPV4_CLIENT_NAME) == TRUE)
    {
        int pid = util_getPidByName(DHCPV4_CLIENT_NAME);
        CcspTraceInfo(("sending SIGUSR1 to %s[pid=%d], this will let the %s to send renew packet out \n", DHCPV4_CLIENT_NAME, pid, DHCPV4_CLIENT_NAME));
        util_signalProcess(pid, SIGUSR1);
    }
    return WanMgr_SetInterfaceStatus(ifName, WANMGR_IFACE_CONNECTION_DOWN);
}

ANSC_STATUS Wan_ForceRenewVlan(char *ifName)
{
    ULONG pid = 0;
    int actions_counter = 0;

    if (GetNumberOfIPoEActions(&actions_counter) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceInfo(("Not able to get the total number of renew request coming from ipoe hc process .. continue anyway ..\n"));
    }

    if (actions_counter > MAX_ALLOWED_FORCED_RENEW)
    {
        CcspTraceInfo(("Too many requests received by ipoe hc to renew IP .. ignore this request.\n"));
        return ANSC_STATUS_FAILURE;
    }

    if (strcmp(ifName, "mg0") == 0)
    {
        pid = GetMgtIntfPid();
    }
    else
    {
        pid = GetVoiceIntfPid();
    }

    if (pid != 0)
    {
        CcspTraceInfo(("sending SIGUSR1 to %s[pid=%d], this will let the %s to send renew packet out \n", DHCPV4_CLIENT_NAME, pid, DHCPV4_CLIENT_NAME));
        util_signalProcess(pid, SIGUSR1);
        SetNumberOfIPoEActions(ifName);
    }
    else
    {
        CcspTraceError(("Failed to get pid of interface %s !! \n"));
    }

    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS Wan_StopDhcpIPv6(char *ifName)
{

    /* Kill DHCPv6 client */
    if (WanManager_StopDhcpv6Client(ifName) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceInfo(("Failed to kill DHCPv6 Client \n"));
    }

    return WanMgr_SetInterfaceStatus(ifName, WANMGR_IFACE_CONNECTION_IPV6_DOWN);
}
#endif

ANSC_STATUS Wan_ForceRenewDhcpIPv6(char *ifName)
{

    /*send triggered renew request to DHCPv6C*/
    WanManager_StopDhcpv6Client(ifName);
    WanMgr_SetInterfaceStatus(ifName, WANMGR_IFACE_CONNECTION_IPV6_DOWN);
    CcspTraceInfo(("Dhcpv6 client has been killed. Restart it \n"));
    sleep(1);
    WanManager_StartDhcpv6Client(ifName);

    return  ANSC_STATUS_SUCCESS; 
}

static void* IpcServerThread( void *arg )
{

    //detach thread from caller stack
    pthread_detach(pthread_self());

    // local variables
    BOOL bRunning = TRUE;

    int bytes = 0;
    int msg_size = sizeof(ipc_msg_payload_t);
    ipc_msg_payload_t ipc_msg;
    memset (&ipc_msg, 0, sizeof(ipc_msg_payload_t));

    while (bRunning)
    {
        bytes = nn_recv(ipcListenFd, (ipc_msg_payload_t *)&ipc_msg, msg_size, 0);
        if ((bytes == msg_size))
        {
            switch(ipc_msg.msg_type)
            {
                case DHCPC_STATE_CHANGED:
                    if (WanMgr_IpcNewIpv4Msg(&(ipc_msg.data.dhcpv4)) != ANSC_STATUS_SUCCESS)
                    {
                        CcspTraceError(("[%s-%d] Failed to proccess DHCPv4 state change message \n", __FUNCTION__, __LINE__));
                    }
                    break;
                case DHCP6C_STATE_CHANGED:
                    if (WanMgr_IpcNewIpv6Msg(&(ipc_msg.data.dhcpv6)) != ANSC_STATUS_SUCCESS)
                    {
                        CcspTraceError(("[%s-%d] Failed to proccess DHCPv6 state change message \n", __FUNCTION__, __LINE__));
                    }
                    break;
#ifdef FEATURE_IPOE_HEALTH_CHECK
                case IHC_STATE_CHANGE:
                    if (WanMgr_IpcNewIhcMsg(&(ipc_msg.data.ihcData)) != ANSC_STATUS_SUCCESS)
                    {
                        CcspTraceError(("[%s-%d] Failed to proccess IHC state change message \n", __FUNCTION__, __LINE__));
                    }
                    break;
#endif
                default:
                        CcspTraceError(("[%s-%d] Invalid  Message sent to Wan Manager\n", __FUNCTION__, __LINE__));
            }
        }
        else
        {
            CcspTraceError(("[%s-%d] message size unexpected\n", __FUNCTION__, __LINE__));
        }
    }

    pthread_exit(NULL);
}

static ANSC_STATUS IpcServerInit()
{
    ANSC_STATUS ret = ANSC_STATUS_SUCCESS;
    uint32_t i;

    if ((ipcListenFd = nn_socket(AF_SP, NN_PULL)) < 0)
    {
        CcspTraceError(("Error: nn_socket failed[%s]\n",nn_strerror(nn_errno ())));
        return ANSC_STATUS_FAILURE;
    }
    if ((i = nn_bind(ipcListenFd, WAN_MANAGER_ADDR)) < 0)
    {
        CcspTraceError(("Error: nn_bind failed[%s]\n",nn_strerror(nn_errno ())));
        nn_close(ipcListenFd);
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

#ifdef FEATURE_IPOE_HEALTH_CHECK
ANSC_STATUS WanMgr_SendMsgToIHC (ipoe_msg_type_t msgType, char *ifName)
{
    int sock = -1;
    int conn = -1;

    ipc_ihc_data_t msgBody;
    memset (&msgBody, 0, sizeof(ipc_ihc_data_t));
    msgBody.msgType = msgType;
    if (msgType == IPOE_MSG_WAN_CONNECTION_IPV6_UP)
    {
        // V6 UP Message needs Wan V6 IP
        char* pattern = NULL;
        char ipv6_address[INET6_ADDRSTRLEN] = {0};

        syscfg_get(NULL, SYSCFG_FIELD_IPV6_ADDRESS, ipv6_address, sizeof(ipv6_address));
        if( *ipv6_address == '\0'|| (0 == strncmp(ipv6_address, "(null)", strlen("(null)"))))
        {
            CcspTraceError(("[%s-%d] Unable to get ipv6_address..\n",  __FUNCTION__, __LINE__));
            return ANSC_STATUS_FAILURE;
        }

        strncpy(msgBody.ipv6Address, ipv6_address, sizeof(ipv6_address));
        CcspTraceInfo(("[%s-%d] Sending IPOE_MSG_WAN_CONNECTION_IPV6_UP msg with addr :%s\n", __FUNCTION__, __LINE__, msgBody.ipv6Address));
    }
    else if (msgType == IPOE_MSG_WAN_CONNECTION_UP)
    {
        char ipv4_wan_address[IP_ADDR_LENGTH] = {0};
        char sysevent_param_name[BUFLEN_64] = {0};
        snprintf(sysevent_param_name, sizeof(sysevent_param_name), SYSEVENT_IPV4_IP_ADDRESS, ifName);
        sysevent_get(sysevent_fd, sysevent_token, sysevent_param_name, ipv4_wan_address, sizeof(ipv4_wan_address));
        if(ipv4_wan_address == NULL  || *ipv4_wan_address == '\0'|| (0 == strncmp(ipv4_wan_address, "(null)", strlen("(null)"))))
        {
            CcspTraceError(("[%s-%d] Unable to get ipv4_erouter0_ipaddr..\n",  __FUNCTION__, __LINE__));
            return ANSC_STATUS_FAILURE;
        }
        strncpy(msgBody.ipv4Address, ipv4_wan_address, sizeof(ipv4_wan_address));
        CcspTraceInfo(("[%s-%d] Sending IPOE_MSG_WAN_CONNECTION_UP msg with addr :%s\n", __FUNCTION__, __LINE__, msgBody.ipv4Address));
    }
    strncpy(msgBody.ifName, ifName, IFNAME_LENGTH-1);

    CcspTraceInfo(("[%s-%d] Sending msg = %d for interface %s  \n", __FUNCTION__, __LINE__, msgType, ifName));

    int bytes = 0;
    int msgSize = sizeof(ipc_ihc_data_t);

    sock = nn_socket(AF_SP, NN_PUSH);
    if (sock < 0)
    {
        CcspTraceError(("[%s-%d] nn_socket failed\n"));
        return ANSC_STATUS_FAILURE;
    }

    conn = nn_connect(sock, IHC_IPC_ADDR);
    if (conn < 0)
    {
        CcspTraceError(("[%s-%d] Failed to connect to the IPoE HEalth Check IPC socket \n", __FUNCTION__, __LINE__));
        nn_close (sock);
        return ANSC_STATUS_FAILURE;
    }
    bytes = nn_send(sock, (char *) &msgBody, msgSize, 0);
    if (bytes < 0)
    {
        CcspTraceError(("[%s-%d] Failed to send data to IPoE HEalth Check error=[%d][%s] \n", __FUNCTION__, __LINE__,errno, strerror(errno)));
        nn_close (sock);
        return ANSC_STATUS_FAILURE;
    }

    CcspTraceInfo(("[%s-%d] Successfully send %d bytes over nano msg  \n", __FUNCTION__, __LINE__,bytes));
    nn_close (sock);
    return ANSC_STATUS_SUCCESS;
}
#endif


ANSC_STATUS WanMgr_StartIpcServer()
{
    pthread_t ipcThreadId;
    ANSC_STATUS retStatus = ANSC_STATUS_FAILURE;
    int ret = -1;

    if(IpcServerInit() != ANSC_STATUS_SUCCESS)
    {
        CcspTraceInfo(("Failed to initialise IPC messaging"));
        return -1;
    }

    //create thread
    ret = pthread_create( &ipcThreadId, NULL, &IpcServerThread, NULL );

    if( 0 != ret )
    {
        CcspTraceInfo(("%s %d - Failed to start IPC Thread Error:%d\n", __FUNCTION__, __LINE__, ret));
    }
    else
    {
        CcspTraceInfo(("%s %d - IPC Thread Started Successfully\n", __FUNCTION__, __LINE__));
        retStatus = ANSC_STATUS_SUCCESS;
    }
    return retStatus ;
}

ANSC_STATUS WanMgr_CloseIpcServer(void)
{
    //nn_close(ipcListenFd);

    return ANSC_STATUS_SUCCESS ;
}
