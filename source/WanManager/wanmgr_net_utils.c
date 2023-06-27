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
#include <arpa/inet.h> /* for inet_pton */
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <sysevent/sysevent.h>
#include "wanmgr_sysevents.h"
#include "wanmgr_net_utils.h"
#include "wanmgr_rdkbus_utils.h"
#include "wanmgr_dhcpv4_apis.h"
#include "wanmgr_dhcpv6_apis.h"
#include "wanmgr_rdkbus_apis.h"
#include "wanmgr_ssp_internal.h"
#include "ansc_platform.h"
#include <sys/types.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include "platform_hal.h"
#include <sys/sysinfo.h>
#include <syscfg/syscfg.h>
#include "dhcp_client_utils.h"
#include "wanmgr_sysevents.h"
#include <sysevent/sysevent.h>
#include "secure_wrapper.h"
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>

#if defined(FEATURE_464XLAT)
#include <netinet/icmp6.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <netdb.h>
#endif

#define BROADCAST_IP "255.255.255.255"
/* To ignore link local addresses configured as DNS servers,
 * it covers the range 169.254.x.x */
#define LINKLOCAL_RANGE "169.254"
/* To ignore loopback addresses configured as DNS servers,
 * it covers the range 127.x.x.x */
#define LOOPBACK_RANGE "127"

#define BASE_IFNAME_PPPoA "atm0"
#define BASE_IFNAME_PPPoE "vlan101"
#define DEFAULT_IFNAME    "erouter0"
#define MAP_INTERFACE "map0"
#define DSL_INTERFACE "dsl0"
#define PTM_INTERFACE "ptm0"
#define NF_NAT_FTP_MODULE "nf_nat_ftp"
#define ROUTE_TABLE_FILE "/etc/iproute2/rt_tables"

/** Macro to determine if a string parameter is empty or not */
#define IS_EMPTY_STR(s)    ((s == NULL) || (*s == '\0'))

#define DHCPV4C_PID_FILE "/tmp/erouter_dhcp4c.pid"
#define MTU_SIZE (1520)
#define MTU_DEFAULT_SIZE (1500)
#define DHCPV6_OPTION_STR_LEN 288
#define DEFAULT_SER_FIELD_LEN 64
#define DHCPV6_OPTIONS_FILE "/var/skydhcp6_options.txt"
#ifdef FEATURE_IPOE_HEALTH_CHECK
#define DHCP6C_RENEW_PREFIX_FILE    "/tmp/erouter0.dhcpc6c_renew_prefix.conf"
#endif

/** Some defines for selecting which address family (IPv4 or IPv6) we want.
 *  Note: are bits for use in bitmasks (not integers)
 */
#define AF_SELECT_IPV4       0x0001
#define AF_SELECT_IPV6       0x0002

extern ANSC_HANDLE bus_handle;
extern int sysevent_fd;
extern token_t sysevent_token;

#define DATAMODEL_PARAM_LENGTH 256


#if defined (DUID_UUID_ENABLE)
#define DUID_TYPE "0004"  /* duid-type duid-uuid 4 */
#else
#define DUID_TYPE "00:03:"  /* duid-type duid-ll 3 */
#define HW_TYPE "00:01:"    /* hw type is always 1 */
#endif

#define DIBBLER_IPV6_CLIENT_ENABLE      "Device.DHCPv6.Client.%d.Enable"

#define IPMONITOR_WAIT_MAX_TIMEOUT 240
#define IPMONITOR_QUERY_INTERVAL 1

#define MODEM_TABLE_NAME "MODEM"
#define WAN_BRIDGE       "brWAN"

#define SET_MAX_RETRY_COUNT 10 // max. retry count for set requests
/***************************************************************************
 * @brief API used to check the incoming ipv4 address is a valid ipv4 address
 * @param input string contains ipv4 address
 * @return TRUE if its a valid IP address else returned false.
 ****************************************************************************/
static BOOL IsValidIpv4Address(const char *input);

/***************************************************************************
 * @brief API used to check the incoming ip address is a valid one
 * @param ipvx ip address family either v4 or v6
 * @param addr string contains ip address
 * @return TRUE if its a valid IP address else returned false.
 ****************************************************************************/
static BOOL IsZeroIpvxAddress(uint32_t ipvx, const char *addr);

/***************************************************************************
 * @brief API used to check the incoming ipv address is a valid.
 * @param af indicates address family
 * @param address string contains ip address
 * @return TRUE if its a valid IP address else returned false.
 ****************************************************************************/
static BOOL IsValidIpAddress(int32_t af, const char *address);

/***************************************************************************
 * @brief API used to parse ipv6 prefix address
 * @param prefixAddr ipv6 prefix address
 * @param address string contains ip address
 * @param plen holds length of the prefix address
 * @return TRUE if its a valid IP address else returned false.
 ****************************************************************************/
static int ParsePrefixAddress(const char *prefixAddr, char *address, uint32_t *plen);


/***************************************************************************
 * @brief API used to enable/disable dibbler client
 * @param enable boolean contains enable flag
 * @return ANSC_STATUS_SUCCESS if the operation is successful
 * @return ANSC_STATUS_FAILURE if the operation is failure
 ****************************************************************************/
static ANSC_STATUS setDibblerClientEnable(BOOL * enable);

#ifdef _HUB4_PRODUCT_REQ_
/***************************************************************************
 * @brief API used to get ADSL username and password
 * @param Username: ADSL username
 * @param Password: ADSL Password
 * @return TRUE if ADSL Username and Password is read from file else returned false.
 ****************************************************************************/
#define SERIALIZATION_DATA "/tmp/serial.txt"
static ANSC_STATUS GetAdslUsernameAndPassword(char *Username, char *Password);
static int WanManager_CalculatePsidAndV4Index(char *pdIPv6Prefix, int v6PrefixLen, int iapdPrefixLen, int v4PrefixLen, int *psidValue, int *ipv4IndexValue, int *psidLen);
#endif

#if defined(FEATURE_464XLAT)
#define XLAT_INTERFACE "xlat"
int gen_xlat_ipv6_address(char *interface, char *address)
{
	int socket_inet6;
	struct icmp6_filter filter;
	struct sockaddr_in6 inet6Addr;
	socklen_t len = sizeof(inet6Addr);
	int ipv6Result = 0;
	char xlatAddress[512] = {0};
	
	//Check wan interface status 
	socket_inet6  = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	ICMP6_FILTER_SETBLOCKALL(&filter);
	setsockopt(socket_inet6 , IPPROTO_ICMPV6, ICMP6_FILTER, &filter, sizeof(filter));
	setsockopt(socket_inet6 , SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface));
	memset(&inet6Addr, 0, sizeof(inet6Addr));
	inet6Addr.sin6_family = AF_INET6;
	inet6Addr.sin6_addr.s6_addr32[0] = htonl(0x2001);
	inet6Addr.sin6_addr.s6_addr32[1] = htonl(0xdb8);
	if (connect(socket_inet6 , (struct sockaddr*)&inet6Addr, sizeof(inet6Addr)))
	{
		return -1;
	}
	if(getsockname(socket_inet6 , (struct sockaddr*)&inet6Addr, &len))
	{
		return -1;
	}
	
	if(IN6_IS_ADDR_LINKLOCAL(&inet6Addr.sin6_addr))
	{
		return -1;
	}
	
	//Generate clat interface ipv6 address by wan interface.
	struct ipv6_mreq multicastReq  = {inet6Addr.sin6_addr, if_nametoindex(interface)};

	if (IN6_IS_ADDR_LINKLOCAL(&multicastReq.ipv6mr_multiaddr))
	{
		return -1;
	}
	//Create a random ipv6 address
	srandom(multicastReq.ipv6mr_multiaddr.s6_addr32[0] ^ multicastReq.ipv6mr_multiaddr.s6_addr32[1] ^ multicastReq.ipv6mr_multiaddr.s6_addr32[2] ^ multicastReq.ipv6mr_multiaddr.s6_addr32[3]);
	multicastReq.ipv6mr_multiaddr.s6_addr32[2] = random();
	multicastReq.ipv6mr_multiaddr.s6_addr32[3] = random();
	
	if (setsockopt(socket_inet6 , SOL_IPV6, IPV6_JOIN_ANYCAST, &multicastReq, sizeof(multicastReq)))
	{
		return -1;
	}

	inet_ntop(AF_INET6, &multicastReq.ipv6mr_multiaddr, xlatAddress, sizeof(xlatAddress));
	
	snprintf(address,512,"%s",xlat_address);
	
	return 0;
}

int xlat_configure(char *interface, char *xlat_address)
{
	char ipv6address[512] = {0}; 
	char platformPrefix[512] = {0};
	struct addrinfo socketHints = { .ai_family = AF_INET6 }; 
	struct addrinfo *returnInfo;
	int errcode = 1;
	char host[128] = {0};
	char cmd[512] = {0};
	FILE *xlat ;
	char dntInterface[256] = {0};
	int ipv6Result = 0;
	//Get ipv4only.arpa ipv6 address from dns64 server
	snprintf(host,sizeof(host),"ipv4only.arpa");
	//Sometimes fails but works on retry
	errcode = getaddrinfo (host, NULL, &socketHints, &returnInfo);
	if (errcode == 1)
	{
		errcode = getaddrinfo (host, NULL, &socketHints, &returnInfo);
		if (errcode == 1)
		{
			return -1;
		}
	}
	struct sockaddr_in6 *sockin6 = (struct sockaddr_in6*)returnInfo->ai_addr;
	//dns64 will return "ipv4only.arpa" ipv6 address as "ipv6prfix"+"ipv4 address of ipv4only.arpa".
	//ex : 64:ff9b::192.0.0.170 .
	//We need the prefix , and prefix length is /96.
 	inet_ntop(AF_INET6, &sockin6->sin6_addr, platformPrefix, sizeof(platformPrefix));
	strcat(platformPrefix, "/96");
	freeaddrinfo(returnInfo);
	snprintf(dntInterface,sizeof(dntInterface),"%s",interface);
	ipv6Result = gen_xlat_ipv6_address(dntInterface,ipv6address);
	if(ipv6Result < 0)
	{
		return -1;
	}
	
	xlat = fopen("/proc/net/nat46/control", "w");
    if(xlat)
	{
		//Write to nat46 kernel
		//e.g : add xlat config xlat local.style NONE local.v4 /32 local.v6 2607:fb90:ec92:c52c:8274:4d68:66a2:8955/128 remote.style RFC6052 remote.v6 192.0.0.1
		//More detail : https://github.com/ayourtch/nat46/blob/master/nat46/modules/README
		snprintf(cmd,sizeof(cmd),"add %s\nconfig %s local.style NONE local.v4 %s/32 local.v6 %s/128 remote.style RFC6052 remote.v6 %s\n",XLAT_INTERFACE, XLAT_INTERFACE, "192.0.0.1", ipv6address, platformPrefix);
		if(fprintf(xlat,cmd) <0)
		{
			fclose(xlat);
			return -1;
		}
		fclose(xlat);
	}else
	{
		return -1;
	}

	return 0;	
}

int xlat_reconfigure(void)
{
	FILE *nat46 = fopen("/proc/net/nat46/control", "w");
	
	nat46 = fopen("/proc/net/nat46/control", "w");
	if (nat46) {
		fprintf(nat46, "del %s\n", XLAT_INTERFACE);
		fclose(nat46);
	}
	
	return 0;
}
#endif

#ifdef FEATURE_MAPT
/*************************************************************************************
 * @brief checks kernel module loaded.
 * This API calls the proc entry.
 * @param moduleName is the kernal modulename
 * @return RETURN_OK upon success else RETURN_ERR returned
 **************************************************************************************/
int isModuleLoaded(char *moduleName)
{
    char line[BUFLEN_64] = {0};
    int  ret = RETURN_ERR,pclose_ret = 0;
    FILE *file = NULL;

    if (moduleName == NULL)
    {
        return ret;
    }

  

    file = v_secure_popen("r","cat /proc/modules | grep %s", moduleName);

    if(file == NULL)
    {
        CcspTraceError(("[%s][%d]Failed to open  /proc/modules \n", __FUNCTION__, __LINE__));
    }
    else
    {
        if(fgets (line, BUFLEN_64, file) != NULL)
        {
            if(strstr(line, moduleName))
            {
                ret = RETURN_OK;
                CcspTraceInfo(("[%s][%d] %s module is loaded \n", __FUNCTION__, __LINE__, moduleName));
            }
        }
        pclose_ret = v_secure_pclose(file);
	if(pclose_ret !=0)
	{
	    CcspTraceError(("Failed in closing the pipe ret %d\n",pclose_ret));
	}

    }

    return ret;
}
#endif

 /***************************************************************************
 * @brief Thread that sets enable/disable data model of dhcpv6 client
 * @param arg: enable/disable flag to start and stop dhcp6c client
 ****************************************************************************/
static void* Dhcpv6HandlingThread( void *arg );
static int get_index_from_path(const char *path);
static void* DmlHandlePPPCreateRequestThread( void *arg );
static void createDummyWanBridge(char * iface_name);
static void deleteDummyWanBridgeIfExist(char * iface_name);
static INT IsIPObtained(char *pInterfaceName);

static ANSC_STATUS SetDataModelParamValues( char *pComponent, char *pBus, char *pParamName, char *pParamVal, enum dataType_e type, BOOLEAN bCommit )
{
    CCSP_MESSAGE_BUS_INFO *bus_info              = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
    parameterValStruct_t   param_val[1]          = { 0 };
    char                  *faultParam            = NULL;
    int                    ret                   = 0;

    //Copy Name
    param_val[0].parameterName  = pParamName;
    //Copy Value
    param_val[0].parameterValue = pParamVal;

    //Copy Type
    param_val[0].type           = type;
        ret = CcspBaseIf_setParameterValues(
                                        bus_handle,
                                        pComponent,
                                        pBus,
                                        0,
                                        0,
                                        param_val,
                                        1,
                                        bCommit,
                                        &faultParam
                                       );

    if( ( ret != CCSP_SUCCESS ) && ( faultParam != NULL ) )
    {
        CcspTraceError(("%s-%d Failed to set %s\n",__FUNCTION__,__LINE__,pParamName));
        bus_info->freefunc( faultParam );
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}


static ANSC_STATUS setDibblerClientEnable(BOOL *enable)
{
    INT              index               = 0;
    pthread_t        dibblerThreadId;
    INT              iErrorCode          = -1;

    if (NULL == enable)
    {
        CcspTraceError(("%s %d - Invalid memory \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    BOOL *enable_client = NULL;
    enable_client = (BOOL *) malloc (sizeof(BOOL));
    if (NULL == enable_client)
    {
        CcspTraceError(("%s %d - Failed to reserve memory \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    *enable_client = *enable;

    iErrorCode = pthread_create( &dibblerThreadId, NULL, &Dhcpv6HandlingThread, (void*)enable_client );
    if( 0 != iErrorCode )
    {
        CcspTraceInfo(("%s %d - Dhcpv6HandlingThread thread failed. EC:%d\n", __FUNCTION__, __LINE__, iErrorCode ));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

static void* Dhcpv6HandlingThread( void *arg )
{
    char ParamName[BUFLEN_256] = {0};
    char ParamValue[BUFLEN_256] = {0};

    //detach thread from caller stack
    pthread_detach(pthread_self());

    if( NULL == arg)
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        pthread_exit(NULL);
    }

    BOOL enable_client = *(BOOL *) arg;
    snprintf( ParamName, BUFLEN_256, DIBBLER_IPV6_CLIENT_ENABLE, 1 );
    if(enable_client)
        snprintf( ParamValue, BUFLEN_256, "%s", "true");
    else
        snprintf( ParamValue, BUFLEN_256, "%s", "false");

    SetDataModelParamValues( WAN_COMPONENT_NAME, COMPONENT_PATH_WANMANAGER, ParamName, ParamValue, ccsp_boolean, TRUE );

    CcspTraceInfo(("%s %d Successfully set %d value to %s data model \n", __FUNCTION__, __LINE__, enable_client, ParamName));

    //free memory.
    if (arg)
    {
        free (arg);
        arg = NULL;
    }

    pthread_exit(NULL);
}


int WanManager_Ipv6AddrUtil(char *ifname, Ipv6OperType opr, int preflft, int vallft)
{
    char cmdLine[128] = {0};
    char prefix[BUFLEN_48] = {0};
    char prefixAddr[BUFLEN_48] = {0};

    memset(prefix, 0, sizeof(prefix));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_PREFIX, prefix, sizeof(prefix));

    memset(prefixAddr, 0, sizeof(prefixAddr));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_GLOBAL_IPV6_PREFIX_SET, prefixAddr, sizeof(prefixAddr));

    switch (opr)
    {
        case DEL_ADDR:
        {
            if (strlen(prefix) > 0)
            {
                memset(cmdLine, 0, sizeof(cmdLine));
                snprintf(cmdLine, sizeof(cmdLine), "ip -6 addr del %s/64 dev %s", prefixAddr, ifname);
                if (WanManager_DoSystemActionWithStatus("ip -6 addr del ADDR dev xxxx", cmdLine) != 0)
                    CcspTraceError(("failed to run cmd: %s", cmdLine));

                memset(cmdLine, 0, sizeof(cmdLine));
#if defined(FEATURE_RDKB_CONFIGURABLE_WAN_INTERFACE)
                snprintf(cmdLine, sizeof(cmdLine), "ip -6 route flush match %s ", prefix);
#else
                snprintf(cmdLine, sizeof(cmdLine), "ip -6 route flush %s ", prefix);
#endif
                if (WanManager_DoSystemActionWithStatus("ip -6 route flush PREFIX ", cmdLine) != 0)
                    CcspTraceError(("failed to run cmd: %s", cmdLine));

                CcspTraceInfo(("%s-%d: Successfully del addr and route from Interface %s, prefix=%s, prefixAddr=%s \n",
       	                               __FUNCTION__, __LINE__, ifname, prefix, prefixAddr));

                memset(cmdLine, 0, sizeof(cmdLine));
                snprintf(cmdLine, sizeof(cmdLine), "ip -6 route delete default");
                if (WanManager_DoSystemActionWithStatus("ip -6 route delete default", cmdLine) != 0)
                    CcspTraceError(("failed to run cmd: %s", cmdLine));

            }
            else
            {
                CcspTraceError(("%s-%d: Failed to delete addr and route from Interface %s, prefix=%s, prefixAddr=%s \n",
       	                                __FUNCTION__, __LINE__, ifname, prefix, prefixAddr));
            }
            break;
        }
        case SET_LFT:
        {
            if (strlen(prefixAddr) > 0)
            {
                memset(cmdLine, 0, sizeof(cmdLine));
                snprintf(cmdLine, sizeof(cmdLine), "ip -6 addr change %s dev brlan0 valid_lft %d preferred_lft %d ", prefixAddr, vallft, preflft);
                if (WanManager_DoSystemActionWithStatus("processDhcp6cStateChanged: ip -6 addr change L3IfName", (cmdLine)) != 0)
                    CcspTraceError(("failed to run cmd: %s", cmdLine));

                CcspTraceInfo(("%s-%d: Successfully updated addr from Interface %s, prefixAddr=%s, vallft=%d, preflft=%d \n",
                                       __FUNCTION__, __LINE__, ifname, prefixAddr, vallft, preflft));
            }
	    else
            {
                CcspTraceError(("%s-%d: Failed to update addr from Interface %s, prefixAddr=%s, vallft=%d, preflft=%d \n",
                                        __FUNCTION__, __LINE__, ifname, prefixAddr, vallft, preflft));
            }
            break;
        }
    }

    return 0;
}

uint32_t WanManager_StartDhcpv6Client(char * iface_name)
{
    if (iface_name == NULL)
    {
        CcspTraceError(("%s %d: Invalid args \n", __FUNCTION__, __LINE__));
        return 0;
    }

    uint32_t pid = 0;
    WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(iface_name);

    if(pWanDmlIfaceData != NULL)
    {
        dhcp_params params;
        memset (&params, 0, sizeof(dhcp_params));
        params.ifname = pWanDmlIfaceData->data.Wan.Name;
        params.ifType = pWanDmlIfaceData->data.Wan.IfaceType;

        CcspTraceInfo(("Enter WanManager_StartDhcpv6Client for  %s \n", pWanDmlIfaceData->data.Wan.Name));
        pid = start_dhcpv6_client(&params);
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }

    return pid;
}

/**
 * @brief This function will stop the DHCPV6 client(WAN side) in router
 *
 * @param boolDisconnect : This indicates whether this function called from disconnect context or not.
 *              TRUE (disconnect context) / FALSE (Non disconnect context)
 */
ANSC_STATUS WanManager_StopDhcpv6Client(char * iface_name)
{
    if (iface_name == NULL)
    {
        CcspTraceError(("%s %d: Invalid args \n", __FUNCTION__, __LINE__));
        return 0;
    }

    CcspTraceInfo (("%s %d: Stopping dhcpv6 client for %s\n", __FUNCTION__, __LINE__, iface_name));

    int ret;
    dhcp_params params;

    memset (&params, 0, sizeof(dhcp_params));
    params.ifname = iface_name;

    ret = stop_dhcpv6_client(&params);

    return ret;
}

uint32_t WanManager_StartDhcpv4Client(char * iface_name)
{
    if (iface_name == NULL)
    {
        CcspTraceError(("%s %d: Invalid args \n", __FUNCTION__, __LINE__));
        return 0;
    }

    uint32_t pid = 0;
    WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(iface_name);
    if(pWanDmlIfaceData != NULL)
    {
        dhcp_params params;

        memset (&params, 0, sizeof(dhcp_params));
        params.ifname = pWanDmlIfaceData->data.Wan.Name;
        params.ifType = pWanDmlIfaceData->data.Wan.IfaceType;

#if defined(_HUB4_PRODUCT_REQ_)
        /*TODO:
	 * This is a SKY specific change. This should be moved to hal 
	 *after enabling CONFIGURABLE_WAN_INTERFACE in all SKy devices.
	*/
	if (strncmp(pWanDmlIfaceData->data.Name, "eth", 3) == 0)
	{
            params.opt |= DHCPV4_OPT_43;
	}
#endif
        CcspTraceInfo(("Starting DHCPv4 Client for iface: %s \n", params.ifname));
        pid = start_dhcpv4_client(&params);
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }

    return pid;
}

ANSC_STATUS WanManager_StopDhcpv4Client(char * iface_name, unsigned char IsReleaseNeeded)
{
    if (iface_name == NULL)
    {
        CcspTraceError(("%s %d: Invalid args \n", __FUNCTION__, __LINE__));
        return 0;
    }

    CcspTraceInfo (("%s %d: Stopping dhcpv4 client for %s\n", __FUNCTION__, __LINE__, iface_name));

    dhcp_params params;
    ANSC_STATUS ret;

    memset (&params, 0, sizeof(dhcp_params));
    params.ifname = iface_name;
    params.is_release_required = IsReleaseNeeded;

    ret = stop_dhcpv4_client(&params);

    return ret;
}

void WanUpdateDhcp6cProcessId(char *currentBaseIfName)
{
    INT           wanIndex = -1;
    int processId = 0;
    char cmdLine[BUFLEN_128];
    char out[BUFLEN_128] = {0};

    snprintf(cmdLine, sizeof(cmdLine)-1, "pidof %s", DHCPV6_CLIENT_NAME);
    _get_shell_output(cmdLine, out, sizeof(out));
    CcspTraceError(("%s Updating dibbler client pid %s\n", __func__, out));
    processId = atoi(out);

    WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceDataByName_locked(currentBaseIfName);
    if (pWanDmlIfaceData != NULL)
    {
        pWanDmlIfaceData->data.IP.Dhcp6cPid = processId;		
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }
    else
    {
        CcspTraceError(("%s Failed to get index for %s\n", __FUNCTION__, currentBaseIfName));
        return;
    }
}

#ifdef FEATURE_MAPT
const char *nat44PostRoutingTable = "OUTBOUND_POSTROUTING";
char ipv6AddressString[BUFLEN_256] = " ";
#ifdef FEATURE_MAPT_DEBUG
void WanManager_UpdateMaptLogFile(ipc_mapt_data_t *dhcp6cMAPTMsgBody);
#endif // FEATURE_MAPT_DEBUG
static int WanManager_ConfigureIpv6Sysevents(char *pdIPv6Prefix, char *ipAddressString, int psidValue);
#ifdef NAT46_KERNEL_SUPPORT
static int get_V6_defgateway_wan_interface(char *defGateway);
#endif // NAT46_KERNEL_SUPPORT

static unsigned WanManager_GetMAPTbits(unsigned value, int pos, int num)
{
    return (value >> (pos + 1 - num)) & ~(~0 << num);
}
/*
 *   Returns LAN IP Address in the form X.X.X.X/Y, e.g. 192.168.0.1/24
 */
static ANSC_STATUS WanManager_GetLANIPAddress(char *ipAddress, size_t length)
{
    char lan_ip_address[IP_ADDR_LENGTH] = {0};

    if (syscfg_get(NULL, SYSCFG_LAN_IP_ADDRESS, lan_ip_address, sizeof(lan_ip_address)) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("Failed to get LAN IP address \n"));
        return ANSC_STATUS_FAILURE;
    }

    char lan_subnet_mask[IP_ADDR_LENGTH] = {0};

    if (syscfg_get(NULL, SYSCFG_LAN_NET_MASK, lan_subnet_mask, sizeof(lan_subnet_mask)) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("Failed to get LAN subnet mask \n"));
        return ANSC_STATUS_FAILURE;
    }

    // count number of bits set in subnet mask.
    unsigned int lanSubnetMask = inet_network(lan_subnet_mask);
    unsigned int subnetCount = 0;
    while (lanSubnetMask)
    {
        subnetCount += lanSubnetMask & 1;
        lanSubnetMask = lanSubnetMask >> 1;
    }

    CcspTraceInfo(("LAN IP address/subnet mask = %s/%u \n", lan_ip_address, subnetCount));

    snprintf(ipAddress, length, "%s/%u", lan_ip_address, subnetCount);

    return ANSC_STATUS_SUCCESS;
}

int WanManager_ProcessMAPTConfiguration(ipc_mapt_data_t *dhcp6cMAPTMsgBody, const char *baseIf, const char *vlanIf)
{
    /* IVI_KERNEL_SUPPORT : To Enable IVI sopprted MAPT work flow
     * NAT46_KERNEL_SUPPORT : To Enable NAT46 sopprted MAPT work flow
     * Enable any one of the build flag at a time */
    int ret = RETURN_OK;
    char cmdDMRConfig[BUFLEN_128 + BUFLEN_64];
    char cmdBMRConfig[BUFLEN_256];
    char cmdStartMAPT[BUFLEN_256];
    char cmdStartMAPTMeshBr[BUFLEN_256];
    char cmdDisableMapFiltering[BUFLEN_64];
    int psidValue = 0;
    int ipv4IndexValue = 0;
    char ipAddressString[BUFLEN_32] = "";
    char ipLANAddressString[BUFLEN_32] = "";
    struct in_addr result;
    unsigned char ipAddressBytes[BUFLEN_4];
    unsigned long int ipValue = 0;
    int psidLen = 0;
    char cmdConfigureMTUSize[BUFLEN_64] = "";
    char cmdEnableIpv4Traffic[BUFLEN_64] = "";
    char cmdEnableDefaultIpv4Route[BUFLEN_64] = "";
    char layer2_iface[BUFLEN_32] = {0};

    if (dhcp6cMAPTMsgBody == NULL)
    {
        CcspTraceError(("%s %d: Invalid args. MAPTMsgBody is NULL \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    MaptData_t maptInfo;

    //The sharing ratio cannot be zero, a value of zero means the sharing ratio is 1
    if (dhcp6cMAPTMsgBody->ratio == 0)
        dhcp6cMAPTMsgBody->ratio = 1;

#ifdef FEATURE_MAPT_DEBUG
    WanManager_UpdateMaptLogFile(dhcp6cMAPTMsgBody);
#endif

#if defined(IVI_KERNEL_SUPPORT) || (NAT46_KERNEL_SUPPORT)
    if ((dhcp6cMAPTMsgBody->psidLen > 0) && (dhcp6cMAPTMsgBody->eaLen == 0))
    {
        psidValue = dhcp6cMAPTMsgBody->psid;
        psidLen = dhcp6cMAPTMsgBody->psidLen;
        ipv4IndexValue = 0;
#ifdef FEATURE_MAPT_DEBUG
        MaptInfo("Using psid value from dhcp6c options : %d", psidValue);
        MaptInfo("Using psidLen value from dhcp6c options : %d", psidLen);
        MaptInfo("Using psidOffset value from dhcp6c options : %d", dhcp6cMAPTMsgBody->psidOffset);
#endif
    }
    else
    {
        ret = WanManager_CalculatePsidAndV4Index(dhcp6cMAPTMsgBody->pdIPv6Prefix, dhcp6cMAPTMsgBody->v6Len, dhcp6cMAPTMsgBody->iapdPrefixLen,
                dhcp6cMAPTMsgBody->v4Len, &psidValue, &ipv4IndexValue, &psidLen);
    }

    if (ret != RETURN_OK)
    {
        CcspTraceError(("Error in calculating MAPT PSID value \n"));
#ifdef FEATURE_MAPT_DEBUG
        MaptInfo("Exiting MAPT configuration, MAPT will not be configured, error found in getting PSID value");
#endif
        CcspTraceNotice(("FEATURE_MAPT: MAP-T configuration failed\n"));
        return ret;
    }
#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("mapt: PSID Value: %d, ipv4IndexValue: %d", psidValue, ipv4IndexValue);
#endif

    inet_pton(AF_INET, dhcp6cMAPTMsgBody->ruleIPv4Prefix, &(result));

    ipValue = htonl(result.s_addr) + ipv4IndexValue;

    ipAddressBytes[0] = ipValue & 0xFF;
    ipAddressBytes[1] = (ipValue >> 8) & 0xFF;
    ipAddressBytes[2] = (ipValue >> 16) & 0xFF;
    ipAddressBytes[3] = (ipValue >> 24) & 0xFF;

    //store new ipv4 address
    snprintf(ipAddressString, sizeof(ipAddressString), "%d.%d.%d.%d", ipAddressBytes[3], ipAddressBytes[2], ipAddressBytes[1], ipAddressBytes[0]);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("mapt: ipAddressString:%s", ipAddressString);
#endif

    //get LAN IP Address
    if ((ret = WanManager_GetLANIPAddress(ipLANAddressString, sizeof(ipLANAddressString))) != RETURN_OK)
    {
        CcspTraceError(("Could not get LAN IP Address"));
        return ret;
    }

    if ((ret = WanManager_ConfigureIpv6Sysevents(dhcp6cMAPTMsgBody->pdIPv6Prefix, ipAddressString, psidValue)) != RETURN_OK)
    {
        CcspTraceError(("Failed to configure ipv6Tablerules"));
        return ret;
    }

    /* RM16042: Since erouter0 is vlan interface on top of eth3 ptm, we need
       to first set the MTU size of eth3 to 1520 and then change MTU of erouter0.
       Otherwise we can't configure MTU as we are getting `Numerical result out of range` error.
       Configure eth3 MTU size to 1520.
    */
    if(!strcmp(DSL_INTERFACE, baseIf))
    {
        strcpy(layer2_iface, PTM_INTERFACE);
        snprintf(cmdConfigureMTUSize, sizeof(cmdConfigureMTUSize), "ip link set dev %s mtu %d ", layer2_iface, MTU_SIZE);
    }
    else
    {
        snprintf(cmdConfigureMTUSize, sizeof(cmdConfigureMTUSize), "ip link set dev %s mtu %d ", baseIf, MTU_SIZE);
    }

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("mapt: cmdConfigureMTUSize:%s", cmdConfigureMTUSize);
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("map", cmdConfigureMTUSize)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdConfigureMTUSize, ret));
        return ret;
    }

    /*  Configure erouter0 MTU size. */
    snprintf(cmdConfigureMTUSize, sizeof(cmdConfigureMTUSize), "ip link set dev %s mtu %d ", vlanIf, MTU_SIZE);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("mapt: cmdConfigureMTUSize:%s", cmdConfigureMTUSize);
#endif
    if ((ret = WanManager_DoSystemActionWithStatus("map", cmdConfigureMTUSize)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdConfigureMTUSize, ret));
        return ret;
    }

#endif  // (IVI_KERNEL_SUPPORT) || (NAT46_KERNEL_SUPPORT)

#ifdef NAT46_KERNEL_SUPPORT
    /* We currently have an issue with the netfilter module, as on the MAP-T 16:1 line the
       netfilter ftp module(nf_nat_ftp.ko) is not able to use the assigned ports generated
       from the PSID values.
       The below snippet removes netfitler ftp kernel module when on MAP-T 16:1 line and loads it
       back when we have changed the line type to 1:1 line.
    */
    int isNatFtpModuleLoaded = RETURN_ERR;
    isNatFtpModuleLoaded = isModuleLoaded(NF_NAT_FTP_MODULE);
#ifdef MAPT_NAT46_FTP_ACTIVE_MODE
    char port_range[BUFLEN_1024] = {'\0'};
    char cmd_ftp_load[BUFLEN_1024] = {'\0'};
#endif

    if(dhcp6cMAPTMsgBody->ratio != 1)
    {
        if(isNatFtpModuleLoaded == RETURN_OK)
        {
            if (WanManager_DoSystemActionWithStatus("wanmanager", "rmmod -f /lib/modules/`uname -r`/kernel/net/netfilter/nf_nat_ftp.ko") != RETURN_OK)
            {
                CcspTraceError(("%s %d rmmod:!!!!! Failed to remove nf_nat_ftp.ko !!!!!\n", __FUNCTION__, __LINE__));
            }
            else
            {
                CcspTraceInfo(("%s %d !!!!! nf_nat_ftp.ko module removed !!!!! \n", __FUNCTION__, __LINE__));
            }
        }
#ifdef MAPT_NAT46_FTP_ACTIVE_MODE
        WanManager_CalculateMAPTPortRange(dhcp6cMAPTMsgBody->psidOffset, psidLen, psidValue, port_range);
        if(port_range[0] != '\0')
        {
            snprintf(cmd_ftp_load, BUFLEN_1024, "insmod /lib/modules/`uname -r`/kernel/net/netfilter/nf_nat_ftp.ko port_range_array=%s", port_range);
            if (WanManager_DoSystemActionWithStatus("wanmanager", cmd_ftp_load) != RETURN_OK)
            {
                CcspTraceError(("%s %d insmod: !!!!! Failed to add nf_nat_ftp.ko !!!!! \n", __FUNCTION__, __LINE__));
            }
        }
        else
        {
            CcspTraceInfo(("%s %d MAPT port range is empty \n", __FUNCTION__, __LINE__));
        }
#endif
    }
    else
    {
        if(isNatFtpModuleLoaded != RETURN_OK)
        {
            if (WanManager_DoSystemActionWithStatus("wanmanager", "insmod /lib/modules/`uname -r`/kernel/net/netfilter/nf_nat_ftp.ko") != RETURN_OK)
            {
                CcspTraceError(("%s %d insmod: !!!!! Failed to add nf_nat_ftp.ko !!!!! \n", __FUNCTION__, __LINE__));
            }
            else
            {
                CcspTraceInfo(("%s %d!!!!! nf_nat_ftp.ko added !!!!!\n", __FUNCTION__, __LINE__));
            }
        }
    }
#endif

#ifdef IVI_KERNEL_SUPPORT
    /*Configure Default Ipv4 route*/
    snprintf(cmdEnableDefaultIpv4Route, sizeof(cmdEnableDefaultIpv4Route), "ip -4 r a default dev %s ", vlanIf);
#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("mapt: cmdEnableDefaultIpv4Route:%s", cmdEnableDefaultIpv4Route);
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("map", cmdEnableDefaultIpv4Route)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdEnableDefaultIpv4Route, ret));
        return ret;
    }


    if (dhcp6cMAPTMsgBody->ratio == 1)
        snprintf(cmdDMRConfig, sizeof(cmdDMRConfig), "ivictl -r -d -P %s -R 1 -T", dhcp6cMAPTMsgBody->brIPv6Prefix);
    else
        snprintf(cmdDMRConfig, sizeof(cmdDMRConfig), "ivictl -r -d -P %s -T", dhcp6cMAPTMsgBody->brIPv6Prefix);

#ifdef FEATURE_MAPT_DEBUG
    /*Configure Default Ipv4 route*/
    MaptInfo("### ivictl commands - START ###");
    MaptInfo("ivictl:DMR config:%s", cmdDMRConfig);
#endif //FEATURE_MAPT_DEBUG

    if ((ret = WanManager_DoSystemActionWithStatus("ivictl", cmdDMRConfig)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdDMRConfig, ret));
        return ret;
    }


    snprintf(cmdBMRConfig, sizeof(cmdBMRConfig), "ivictl -r -p %s/%d -P %s -z %d -R %d -T", dhcp6cMAPTMsgBody->ruleIPv4Prefix,dhcp6cMAPTMsgBody->v4Len,
             dhcp6cMAPTMsgBody->ruleIPv6Prefix, dhcp6cMAPTMsgBody->psidOffset, dhcp6cMAPTMsgBody->ratio);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("ivictl:BMR config:%s", cmdBMRConfig);
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("ivictl", cmdBMRConfig)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdBMRConfig, ret));
        return ret;
    }

    // create MAP-T command string
    // On a sharing ratio of 1, since the psid value is zero, we don't require to specify PSID, in the MAP-T command string
    {
        snprintf(cmdStartMAPT, sizeof(cmdStartMAPT), "ivictl -s -i %s -I %s -H -a %s -A %s/%d -P %s -z %d -R %d -T ", ETH_BRIDGE_NAME, vlanIf,
                 ipLANAddressString, ipAddressString, dhcp6cMAPTMsgBody->v4Len, dhcp6cMAPTMsgBody->ruleIPv6Prefix, dhcp6cMAPTMsgBody->psidOffset,
                 dhcp6cMAPTMsgBody->ratio);
#ifdef IVI_MULTI_BRIDGE_SUPPORT
        snprintf(cmdStartMAPTMeshBr, sizeof(cmdStartMAPTMeshBr), "ivictl -s -i %s -I %s -H -a %s/%d -A %s/%d -P %s -z %d -R %d -o %d -T", ETH_MESH_BRIDGE, vlanIf,
                 MESH_BRIP,MESH_BRMASK, ipAddressString, dhcp6cMAPTMsgBody->v4Len, dhcp6cMAPTMsgBody->ruleIPv6Prefix, dhcp6cMAPTMsgBody->psidOffset,
                 dhcp6cMAPTMsgBody->ratio, psidValue);
#endif  //IVI_MULTI_BRIDGE_SUPPORT
    }
    else
    {
        snprintf(cmdStartMAPT, sizeof(cmdStartMAPT), "ivictl -s -i %s -I %s -H -a %s -A %s/%d -P %s -z %d -R %d -T -o %d", ETH_BRIDGE_NAME, vlanIf,
                 ipLANAddressString, ipAddressString, dhcp6cMAPTMsgBody->v4Len, dhcp6cMAPTMsgBody->ruleIPv6Prefix, dhcp6cMAPTMsgBody->psidOffset,
                 dhcp6cMAPTMsgBody->ratio, psidValue);
#ifdef IVI_MULTI_BRIDGE_SUPPORT
        snprintf(cmdStartMAPTMeshBr, sizeof(cmdStartMAPTMeshBr), "ivictl -s -i %s -I %s -H -a %s/%d -A %s/%d -P %s -z %d -R %d -T -o %d", ETH_MESH_BRIDGE, vlanIf,
                MESH_BRIP,MESH_BRMASK, ipAddressString, dhcp6cMAPTMsgBody->v4Len, dhcp6cMAPTMsgBody->ruleIPv6Prefix, dhcp6cMAPTMsgBody->psidOffset,
                dhcp6cMAPTMsgBody->ratio, psidValue);
#endif  //IVI_MULTI_BRIDGE_SUPPORT
    }

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("ivictl:startMapt Command:%s", cmdStartMAPT);
#endif
    ret = WanManager_DoSystemActionWithStatus("ivictl", cmdStartMAPT);

    //Added check since the 'system' command is not returning the expected value
    if (ret == IVICTL_COMMAND_ERROR)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdStartMAPT, ret));
        return ret;
    }

#ifdef IVI_MULTI_BRIDGE_SUPPORT
#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("ivictl:startMapt Command:%s", cmdStartMAPTMeshBr);
#endif
    ret = WanManager_DoSystemActionWithStatus("ivictl", cmdStartMAPTMeshBr);
    if (ret == IVICTL_COMMAND_ERROR)
    {
       CcspTraceError(("Failed to run: %s:%d", cmdStartMAPTMeshBr, ret));
       return ret;
    }
#endif  //IVI_MULTI_BRIDGE_SUPPORT
#endif  //IVI_KERNEL_SUPPORT

    snprintf(cmdDisableMapFiltering, sizeof(cmdDisableMapFiltering), "echo 0 > /proc/sys/net/ipv4/conf/%s/rp_filter", vlanIf);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("mapt: Disable RP Filtering:%s", cmdDisableMapFiltering);
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("mapt", cmdDisableMapFiltering)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdDisableMapFiltering, ret));
        return ret;
    }

#ifdef NAT46_KERNEL_SUPPORT
    char cmdinterfaceCreate[BUFLEN_512];
    char cmdInterfaceConfig[BUFLEN_1024];
    char cmdInterfaceUp[BUFLEN_512];
    char cmdInterfaceDefaultRouteDefault[BUFLEN_512];
    char cmdInterfaceDefaultRoDel[BUFLEN_512];
    char defaultGatewayV6[BUFLEN_128];
    char cmdInterfaceMTU1[BUFLEN_512];
    char cmdInterfaceMTU2[BUFLEN_512];
    char cmdInterfaceMTU3[BUFLEN_512];

    if(get_V6_defgateway_wan_interface(defaultGatewayV6) == RETURN_ERR)
    {
        return RETURN_ERR;
    }

    snprintf(cmdinterfaceCreate , sizeof(cmdinterfaceCreate), "echo add %s > /proc/net/nat46/control", MAP_INTERFACE);
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdinterfaceCreate)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdinterfaceCreate, ret));
        return ret;
    }

    snprintf(cmdInterfaceConfig , sizeof(cmdInterfaceConfig), "echo config %s local.style MAP local.v4 %s/%d local.v6 %s local.ea-len %d local.psid-offset %d remote.v4 0.0.0.0/0 remote.v6 %s remote.style RFC6052 remote.ea-len 0 remote.psid-offset 0 debug -1 > /proc/net/nat46/control",
    MAP_INTERFACE, dhcp6cMAPTMsgBody->ruleIPv4Prefix, dhcp6cMAPTMsgBody->v4Len, dhcp6cMAPTMsgBody->ruleIPv6Prefix, dhcp6cMAPTMsgBody->eaLen, dhcp6cMAPTMsgBody->psidOffset, dhcp6cMAPTMsgBody->brIPv6Prefix);
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceConfig)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceConfig, ret));
        return ret;
    }

    snprintf(cmdInterfaceUp , sizeof(cmdInterfaceUp), "ip link set %s up", MAP_INTERFACE);
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceUp)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceUp, ret));
        return ret;
    }

    snprintf(cmdInterfaceDefaultRoDel , sizeof(cmdInterfaceDefaultRoDel), "ip route del default");
    snprintf(cmdConfigureMTUSize, sizeof(cmdConfigureMTUSize), "ip link set dev %s mtu %d ", MAP_INTERFACE, MTU_SIZE);
    snprintf(cmdInterfaceMTU1, sizeof(cmdInterfaceMTU1), "echo %d > /proc/sys/net/ipv6/conf/%s/mtu", MTU_DEFAULT_SIZE, vlanIf);
    snprintf(cmdInterfaceMTU2, sizeof(cmdInterfaceMTU2), "ip -6 ro change default via %s dev %s mtu %d", defaultGatewayV6, vlanIf, MTU_DEFAULT_SIZE) ;
    snprintf(cmdEnableIpv4Traffic, sizeof(cmdEnableIpv4Traffic), "ip ro rep default dev %s mtu %d", MAP_INTERFACE, MTU_DEFAULT_SIZE) ;
    snprintf(cmdInterfaceMTU3, sizeof(cmdInterfaceMTU3), "ip -6 ro rep %s via %s dev %s mtu %d", dhcp6cMAPTMsgBody->brIPv6Prefix, defaultGatewayV6, vlanIf, MTU_SIZE);
    snprintf(cmdInterfaceDefaultRouteDefault , sizeof(cmdInterfaceDefaultRouteDefault), "ip -6 ro rep %s/128 dev %s mtu %d", ipv6AddressString, MAP_INTERFACE, MTU_SIZE);
#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("--- map_nat46: Configuration Starts ---");
    MaptInfo("map_nat46: %s", cmdinterfaceCreate);
    MaptInfo("map_nat46: %s", cmdInterfaceConfig);
    MaptInfo("map_nat46: %s", cmdInterfaceUp);
    MaptInfo("map_nat46: %s", cmdInterfaceDefaultRoDel);
    MaptInfo("map_nat46: %s", cmdConfigureMTUSize);
    MaptInfo("map_nat46: %s", cmdInterfaceMTU1);
    MaptInfo("map_nat46: %s", cmdInterfaceMTU2);
    MaptInfo("map_nat46: %s", cmdEnableIpv4Traffic);
    MaptInfo("map_nat46: %s", cmdInterfaceMTU3);
    MaptInfo("map_nat46: %s", cmdInterfaceDefaultRouteDefault);
    MaptInfo("--- map_nat46: Configuration Ends ---");
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceDefaultRoDel)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceDefaultRoDel, ret));
        return ret;
    }
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdConfigureMTUSize)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdConfigureMTUSize, ret));
        return ret;
    }
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceMTU1)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceMTU1, ret));
        return ret;
    }
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceMTU2)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceMTU2, ret));
        return ret;
    }
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdEnableIpv4Traffic)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdEnableIpv4Traffic, ret));
        return ret;
    }
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceMTU3)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceMTU3, ret));
        return ret;
    }
    if ((ret = WanManager_DoSystemActionWithStatus("map_nat46", cmdInterfaceDefaultRouteDefault)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdInterfaceDefaultRouteDefault, ret));
        return ret;
    }
#endif  // NAT46_KERNEL_SUPPORT

#if defined(IVI_KERNEL_SUPPORT) || (NAT46_KERNEL_SUPPORT)
    /**
     * Firewall rules are changed to utopia firewall
     * To update the firewall rules, required the MAPT specific values, so
     * updated sysvents and restart firewall.
     * First set MAPT_CONFIG_FLAG to set, so firewall can add rules for the MAPT configuration. */
    memset(&maptInfo, 0, sizeof(maptInfo));
    strncpy(maptInfo.maptConfigFlag, SET, sizeof(maptInfo.maptConfigFlag));
    strncpy(maptInfo.ruleIpAddressString, dhcp6cMAPTMsgBody->ruleIPv4Prefix, sizeof(maptInfo.ruleIpAddressString));
    strncpy(maptInfo.ruleIpv6AddressString, dhcp6cMAPTMsgBody->ruleIPv6Prefix, sizeof(maptInfo.ruleIpv6AddressString));
    strncpy(maptInfo.brIpv6PrefixString, dhcp6cMAPTMsgBody->brIPv6Prefix, sizeof(maptInfo.brIpv6PrefixString));
    strncpy(maptInfo.ipAddressString, ipAddressString, sizeof(maptInfo.ipAddressString));
    strncpy(maptInfo.ipv6AddressString, ipv6AddressString, sizeof(maptInfo.ipv6AddressString));
    maptInfo.psidValue = psidValue;
    maptInfo.psidLen = psidLen;
    maptInfo.ratio = dhcp6cMAPTMsgBody->ratio;
    maptInfo.psidOffset = dhcp6cMAPTMsgBody->psidOffset;
    maptInfo.eaLen = dhcp6cMAPTMsgBody->eaLen;
    maptInfo.maptAssigned = TRUE;
    maptInfo.mapeAssigned = FALSE;
    maptInfo.isFMR = dhcp6cMAPTMsgBody->isFMR;;
    maptInfo.v4Len = dhcp6cMAPTMsgBody->v4Len;

    if ((ret = maptInfo_set(&maptInfo)) != RETURN_OK)
    {
        CcspTraceError(("Failed to set sysevents for MAPT feature to set firewall rules \n"));
        return ret;
    }
#endif  // (IVI_KERNEL_SUPPORT) || (NAT46_KERNEL_SUPPORT)

    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);

#if defined(IVI_KERNEL_SUPPORT)
    snprintf(cmdEnableIpv4Traffic, sizeof(cmdEnableIpv4Traffic), "ip ro rep default dev %s", vlanIf);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("ivictl:cmdEnableIpv4Traffic:%s", cmdEnableIpv4Traffic);
#endif
    if ((ret = WanManager_DoSystemActionWithStatus("ivictl", cmdEnableIpv4Traffic)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdEnableIpv4Traffic, ret));
        return ret;
    }
#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("### ivictl commands - ENDS  ###");
#endif
#endif //IVI_KERNEL_SUPPORT

    WanManager_DisplayMAPTFeatureStatus();

    CcspTraceNotice(("FEATURE_MAPT: MAP-T configuration done\n"));
    return RETURN_OK;
}

void WanManager_DisplayMAPTFeatureStatus(void)
{
    char isEnabled[BUFLEN_8] = {0};

    if(syscfg_get(NULL, "upnp_igd_enabled", isEnabled, BUFLEN_8) == 0)
    {
        if ( '1' == isEnabled[0] )
        {
            CcspTraceInfo(("%s - MAP-T_enabled_UPnP_IGD_enabled\n", __FUNCTION__));
        }
        else
        {
            CcspTraceInfo(("%s - MAP-T_enabled_UPnP_IGD_disabled\n", __FUNCTION__));
        }
    }
    memset(isEnabled, 0, BUFLEN_8);

    if (syscfg_get(NULL, "dmz_enabled", isEnabled, BUFLEN_8) == 0)
    {
        if ( '1' == isEnabled[0] )
        {
            CcspTraceInfo(("%s - MAP-T_enabled_DMZ_enabled\n", __FUNCTION__));
        }
        else
        {
            CcspTraceInfo(("%s - MAP-T_enabled_DMZ_disabled\n", __FUNCTION__));
        }
    }
    memset(isEnabled, 0, BUFLEN_8);

    if (syscfg_get(NULL, "CosaNAT::port_forward_enabled", isEnabled, BUFLEN_8) == 0)
    {
        if ( '1' == isEnabled[0] )
        {
            CcspTraceInfo(("%s - MAP-T_enabled_Port_Forwarding_enabled\n", __FUNCTION__));
        }
        else
        {
            CcspTraceInfo(("%s - MAP-T_enabled_Port_Forwarding_disabled\n", __FUNCTION__));
        }
    }
    memset(isEnabled, 0, BUFLEN_8);

    if (syscfg_get(NULL, "CosaNAT::port_trigger_enabled", isEnabled, BUFLEN_8) == 0)
    {
        if ( '1' == isEnabled[0] )
        {
            CcspTraceInfo(("%s - MAP-T_enabled_Port_Triggering_enabled\n", __FUNCTION__));
        }
        else
        {
            CcspTraceInfo(("%s - MAP-T_enabled_Port_Triggering_disabled\n", __FUNCTION__));
        }
    }
    memset(isEnabled, 0, BUFLEN_8);

    if (syscfg_get(NULL, "mgmt_wan_httpaccess_ert", isEnabled, BUFLEN_8)== 0)
    {
        if ( '1' == isEnabled[0] )
        {
            CcspTraceInfo(("%s - MAP-T_enabled_User_Remote_Mgt_http_enabled\n", __FUNCTION__));
        }
        else
        {
            CcspTraceInfo(("%s - MAP-T_enabled_User_Remote_Mgt_http_disabled\n", __FUNCTION__));
        }
    }
    else
    {
        if (syscfg_get(NULL, "mgmt_wan_httpaccess", isEnabled, BUFLEN_8)== 0)
        {
            if ( '1' == isEnabled[0] )
            {
                CcspTraceInfo(("%s - MAP-T_enabled_User_Remote_Mgt_http_enabled\n", __FUNCTION__));
            }
            else
            {
                CcspTraceInfo(("%s - MAP-T_enabled_User_Remote_Mgt_http_disabled\n", __FUNCTION__));
            }
        }
    }
    memset(isEnabled, 0, BUFLEN_8);

    if (syscfg_get(NULL, "mgmt_wan_httpsaccess", isEnabled, BUFLEN_8) == 0)
    {
        if ( '1' == isEnabled[0] )
        {
            CcspTraceInfo(("%s - MAP-T_enabled_User_Remote_Mgt_https_enabled\n", __FUNCTION__));
        }
        else
        {
            CcspTraceInfo(("%s - MAP-T_enabled_User_Remote_Mgt_https_disabled\n", __FUNCTION__));
        }
    }
}

#ifdef NAT46_KERNEL_SUPPORT
static int get_V6_defgateway_wan_interface(char *defGateway)
{
    int ret = RETURN_OK,pclose_ret = 0;
    char line[BUFLEN_1024] = {0};
    struct in6_addr in6Addr;
    FILE *fp;

    fp = v_secure_popen("r","ip -6 route show default | grep default | awk '{print $3}'");

    if (fp)
    {
        if (fgets(line, sizeof(line), fp) != NULL)
        {
            char *token = strtok(line, "\n");
            if (token)
            {
                if (inet_pton (AF_INET6, token, &in6Addr) <= 0)
                {
                    CcspTraceError(("Invalid ipv6 address=%s \n", token));
                    ret = RETURN_ERR;
                    return ret;
                }
                strncpy(defGateway, token, BUFLEN_128);
                CcspTraceNotice(("IPv6 Default GW address  = %s \n", defGateway));
            }
            else
            {
                CcspTraceError(("Could not parse ipv6 gw addr\n"));
                ret = RETURN_ERR;
            }
        }
        else
        {
            CcspTraceError(("Could not read ipv6 gw addr \n"));
            ret = RETURN_ERR;
        }
        pclose_ret = v_secure_pclose(fp);
	if(pclose_ret !=0)
        {
            CcspTraceError(("Failed in closing the pipe ret %d\n",pclose_ret));
        }

    }
    else
    {
        CcspTraceError(("Failed to get the default gw address \n"));
        ret = RETURN_ERR;
    }

    return ret;
}
#endif  // NAT46_KERNEL_SUPPORT

static int WanManager_CalculatePsidAndV4Index(char *pdIPv6Prefix, int v6PrefixLen, int iapdPrefixLen, int v4PrefixLen, int *psidValue, int *ipv4IndexValue, int *psidLen)
{

    int ret = RETURN_OK;
    int ipv4BitIndexLen = 0, psidBitIndexLen = 0, tempShift = 0;
    int EAlen = 0, EAStartByte = 0, EAStartByteShift = 0,  startV4Offset = 0;
    int EAStartPos = 0;
    unsigned long EAbytes = 0;
    struct in6_addr in6Addr;
    
    ipv4BitIndexLen = 32 - v4PrefixLen;
    startV4Offset  = v6PrefixLen;
    EAlen = iapdPrefixLen - startV4Offset;
    psidBitIndexLen = EAlen - ipv4BitIndexLen;

    if (iapdPrefixLen <= startV4Offset)
    {
        CcspTraceError(("Error: Invalid dhcpc6 option,iapdPrefixLen(%d) has a value which <= startV4Offset(%d) \n", iapdPrefixLen, startV4Offset));
	ret = RETURN_ERR;
	return ret;
    }

    if (inet_pton (AF_INET6, pdIPv6Prefix, &in6Addr) <= 0)
    {
        CcspTraceError(("Invalid ipv6 address=%s \n", pdIPv6Prefix));
	ret = RETURN_ERR;
	return ret;
    }

    EAStartByte = startV4Offset / 8;
    EAStartByteShift = startV4Offset % 8;

    if(EAlen <= 8)
    {
        EAbytes = (in6Addr.s6_addr[EAStartByte]) ;
        tempShift = 8;
    }
    else if(EAlen <= 16)
    {
        EAbytes = (in6Addr.s6_addr[EAStartByte] << 8) | (in6Addr.s6_addr[EAStartByte + 1]);
        tempShift = 16;
    }
    else if(EAlen <= 24)
    {
        EAbytes = (in6Addr.s6_addr[EAStartByte] << 16) | (in6Addr.s6_addr[EAStartByte + 1] << 8 ) |
              (in6Addr.s6_addr[startV4Offset / 8 + 2]);
        tempShift = 24;
    }
    else if(EAlen <= 32)
    {
        EAbytes = (in6Addr.s6_addr[EAStartByte] << 24) | (in6Addr.s6_addr[EAStartByte + 1] << 16 ) |
              (in6Addr.s6_addr[EAStartByte + 2] << 8) | (in6Addr.s6_addr[EAStartByte + 3])  ;
        tempShift = 32;
    }
    else if(EAlen <= 40)
    {
        EAbytes = ((unsigned long)(in6Addr.s6_addr[EAStartByte]) << 32) |((unsigned long)(in6Addr.s6_addr[EAStartByte + 1]) << 24) | 
              (in6Addr.s6_addr[EAStartByte + 2] << 16 ) | (in6Addr.s6_addr[EAStartByte + 3] << 8) | 
	      (in6Addr.s6_addr[EAStartByte + 4]);
        tempShift = 40;
    }
    else if(EAlen <= 48)
    {
        EAbytes = ((unsigned long)(in6Addr.s6_addr[EAStartByte]) << 40) |((unsigned long)(in6Addr.s6_addr[EAStartByte + 1]) << 32) |
              (in6Addr.s6_addr[EAStartByte + 2] << 24) | (in6Addr.s6_addr[EAStartByte + 3] << 16 ) |
              (in6Addr.s6_addr[EAStartByte + 4] << 8) | (in6Addr.s6_addr[EAStartByte + 5])  ;
        tempShift = 48;
    }

    EAStartPos = tempShift - EAStartByteShift -1;

    if(EAStartByteShift) {
        EAbytes <<= 8;
        EAbytes |= (in6Addr.s6_addr[EAStartByte + (tempShift/8)]);
	EAStartPos += 8;
    }

    *ipv4IndexValue = WanManager_GetMAPTbits(EAbytes, EAStartPos, ipv4BitIndexLen);
    *psidValue = WanManager_GetMAPTbits(EAbytes, EAStartPos - ipv4BitIndexLen, psidBitIndexLen);
    *psidLen = psidBitIndexLen;

    return ret;
}

static int WanManager_ConfigureIpv6Sysevents(char *pdIPv6Prefix, char *ipAddressString, int psidValue)
{
    int ret = RETURN_OK;
    struct in6_addr in6Addr;
    unsigned char ipAddressBytes[BUFLEN_4];
    struct in_addr result;
    unsigned int ipValue = 0;

    inet_pton(AF_INET, ipAddressString, &(result));

    ipValue = htonl(result.s_addr);

    ipAddressBytes[0] = ipValue & 0xFF;
    ipAddressBytes[1] = (ipValue >> 8) & 0xFF;
    ipAddressBytes[2] = (ipValue >> 16) & 0xFF;
    ipAddressBytes[3] = (ipValue >> 24) & 0xFF;

    if (inet_pton(AF_INET6, pdIPv6Prefix, &in6Addr) <= 0)
    {
        CcspTraceError(("Invalid ipv6 address=%s", pdIPv6Prefix));
        ret = RETURN_ERR;
        return ret;
    }

    snprintf(ipv6AddressString, sizeof(ipv6AddressString), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", in6Addr.s6_addr[0], in6Addr.s6_addr[1], in6Addr.s6_addr[2], in6Addr.s6_addr[3], in6Addr.s6_addr[4], in6Addr.s6_addr[5], in6Addr.s6_addr[6], in6Addr.s6_addr[7], 0x0, 0x0, ipAddressBytes[3], ipAddressBytes[2], ipAddressBytes[1], ipAddressBytes[0], 0x0, psidValue);

    return ret;
}

int WanManager_ResetMAPTConfiguration(const char *baseIf, const char *vlanIf)
{
    char cmdConfigureMTUSize[BUFLEN_64] = "";
    int ret = RETURN_OK;

    /* RM16042: Since we have configures MTU size to 1520 for the MAPT functionality,
     * we need to reconfigure it back to 1500 when we reset from MAPT configuration.
     * Reconfigure eth3 mtu size to 1500. */
    snprintf(cmdConfigureMTUSize, sizeof(cmdConfigureMTUSize), "ip link set dev %s mtu %d ", baseIf, MTU_DEFAULT_SIZE);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("ivictl:cmdConfigureMTUSize:%s", cmdConfigureMTUSize);
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("mapt", cmdConfigureMTUSize)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdConfigureMTUSize, ret));
        return ret;
    }

    /*  ReConfigure erouter0 MTU size to 1500 */
    snprintf(cmdConfigureMTUSize, sizeof(cmdConfigureMTUSize), "ip link set dev %s mtu %d ", vlanIf, MTU_DEFAULT_SIZE);

#ifdef FEATURE_MAPT_DEBUG
    MaptInfo("ivictl:cmdConfigureMTUSize:%s", cmdConfigureMTUSize);
#endif

    if ((ret = WanManager_DoSystemActionWithStatus("mapt", cmdConfigureMTUSize)) < RETURN_OK)
    {
        CcspTraceError(("Failed to run: %s:%d", cmdConfigureMTUSize, ret));
        return ret;
    }

    /* Reset MAPT configuration.
     * All the firewall rules should remove once this called.
     * To do this we have reset the value for sysevent field
     * `mapt_configure_flag` and restart the firewall. */
    //Reset MAP sysevent parameters
    maptInfo_reset();
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
    CcspTraceNotice(("FEATURE_MAPT: MAP-T configuration cleared\n"));
    return RETURN_OK;
}

#ifdef FEATURE_MAPT_DEBUG
void WanManager_UpdateMaptLogFile(ipc_mapt_data_t *dhcp6cMAPTMsgBody)
{

    MaptInfo("#### MAP-T Options DHCP - START ####");
    MaptInfo("ruleIPv6Prefix:%s", dhcp6cMAPTMsgBody->ruleIPv6Prefix);
    MaptInfo("ruleIPv4Prefix:%s", dhcp6cMAPTMsgBody->ruleIPv4Prefix);
    MaptInfo("brIPv6Prefix:%s", dhcp6cMAPTMsgBody->brIPv6Prefix);
    MaptInfo("pdIPv6Prefix:%s", dhcp6cMAPTMsgBody->pdIPv6Prefix);
    MaptInfo("iapdPrefixLen:%d", dhcp6cMAPTMsgBody->iapdPrefixLen);
    MaptInfo("psidOffset:%d", dhcp6cMAPTMsgBody->psidOffset);
    MaptInfo("psidLen:%d", dhcp6cMAPTMsgBody->psidLen);
    MaptInfo("psid:%d", dhcp6cMAPTMsgBody->psid);
    MaptInfo("ratio:%d", dhcp6cMAPTMsgBody->ratio);
    MaptInfo("v4Len:%d", dhcp6cMAPTMsgBody->v4Len);
    MaptInfo("v6Len:%d", dhcp6cMAPTMsgBody->v6Len);
    MaptInfo("eaLen:%d", dhcp6cMAPTMsgBody->eaLen);
    MaptInfo("#### MAP-T Options DHCP - END ####");

    CcspTraceInfo(("SKYWANMGR:MAP-T_OPTIONS:%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d",
                 dhcp6cMAPTMsgBody->ruleIPv6Prefix,
                 dhcp6cMAPTMsgBody->ruleIPv4Prefix,
                 dhcp6cMAPTMsgBody->brIPv6Prefix,
                 dhcp6cMAPTMsgBody->pdIPv6Prefix,
                 dhcp6cMAPTMsgBody->iapdPrefixLen,
                 dhcp6cMAPTMsgBody->psidOffset,
                 dhcp6cMAPTMsgBody->psidLen,
                 dhcp6cMAPTMsgBody->psid,
                 dhcp6cMAPTMsgBody->ratio,
                 dhcp6cMAPTMsgBody->v4Len,
                 dhcp6cMAPTMsgBody->v6Len,
                 dhcp6cMAPTMsgBody->eaLen));
}
#endif /* FEATURE_MAPT_DEBUG */

#endif /* FEATURE_MAPT */

int IsValidDnsServer(int32_t af, const char *nameServer)
{
    if ((nameServer == NULL) || (nameServer[0] == '\0'))
    {
        CcspTraceError(("%s %d - invalid argument \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    if(af == AF_INET)
    {
        if(!(IsValidIpAddress(af, nameServer)))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
        if(IsZeroIpvxAddress(AF_SELECT_IPV4,nameServer))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
        if(!(strncmp(BROADCAST_IP, nameServer, strlen(BROADCAST_IP))))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
        if(!(strncmp(LINKLOCAL_RANGE, nameServer, strlen(LINKLOCAL_RANGE))))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
        if(!(strncmp(LOOPBACK_RANGE, nameServer, strlen(LOOPBACK_RANGE))))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
    }
    else
    {
        if(!(IsValidIpAddress(af, nameServer)))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
        if(IsZeroIpvxAddress(AF_SELECT_IPV6, nameServer))
        {
            CcspTraceError(("%s %d - invalid nameserver: %s \n",__FUNCTION__,__LINE__, nameServer));
            return RETURN_ERR;
        }
    }
    return RETURN_OK;
}


int WanManager_GetBCastFromIpSubnetMask(const char* inIpStr, const char* inSubnetMaskStr, char *outBcastStr)
{
   struct in_addr ip;
   struct in_addr subnetMask;
   struct in_addr bCast;
   int ret = RETURN_OK;

   if (inIpStr == NULL || inSubnetMaskStr == NULL || outBcastStr == NULL)
   {
      return RETURN_ERR;
   }

   ip.s_addr = inet_addr(inIpStr);
   subnetMask.s_addr = inet_addr(inSubnetMaskStr);
   bCast.s_addr = ip.s_addr | ~subnetMask.s_addr;
   strncpy(outBcastStr, inet_ntoa(bCast),strlen(inet_ntoa(bCast))+1);

   return ret;
}

int WanManager_DelDefaultGatewayRoute(DEVICE_NETWORKING_MODE DeviceNwMode, BOOL DeviceNwModeChange, const WANMGR_IPV4_DATA* pIpv4Info)
{
    char cmd[BUFLEN_128]={0};
    int ret = RETURN_OK;
    DEVICE_NETWORKING_MODE ModeConfigToDelete;


    if (DeviceNwModeChange == TRUE)
    {
        if (DeviceNwMode == GATEWAY_MODE)
        {
            // DeviceNwMode changed from MODEM_MODE to GATEWAY_MODE
            CcspTraceInfo(("%s %d: DeviceNwMode changed from MODEM_MODE->GATEWAY_MODE. So deleting MODEM_MODE def routes\n", __FUNCTION__, __LINE__));
            ModeConfigToDelete = MODEM_MODE;
        }
        else
        {
            // DeviceNwMode changed from GATEWAY_MODE to MODEM_MODE
            CcspTraceInfo(("%s %d: DeviceNwMode changed from GATEWAY_MODE->MODEM_MODE. So deleting GATEWAY_MODE def route\n", __FUNCTION__, __LINE__));
            ModeConfigToDelete = GATEWAY_MODE;
        }
    }
    else
    {
        // No change is DeviceNwMode, so delete config for current DeviceNwMode
        ModeConfigToDelete =  DeviceNwMode;
    }


    if (ModeConfigToDelete == GATEWAY_MODE)
    {
        /* delete default gateway first before add  */
        snprintf(cmd, sizeof(cmd), "route del default 2>/dev/null");
        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", cmd);
    }
    else
    {
        snprintf(cmd, sizeof(cmd), "ip rule flush table %s", MODEM_TABLE_NAME);
        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", cmd);

        memset (cmd, 0 , sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "ip ro flush table %s", MODEM_TABLE_NAME);
        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", cmd);

        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", "ip route flush cache");
    }

    return ret;
}

int WanManager_AddDefaultGatewayRoute(DEVICE_NETWORKING_MODE DeviceNwMode, const WANMGR_IPV4_DATA* pIpv4Info)
{
    char cmd[BUFLEN_128]={0};
    int ret = RETURN_OK;

    if (DeviceNwMode == GATEWAY_MODE)
    {
        CcspTraceInfo(("%s %d: Device in Gateway Mode. So configure default route in main routing table\n", __FUNCTION__, __LINE__));
        /* Sets default gateway route entry */
        /* For IPoE, always use gw IP address. */
        if (IsValidIpv4Address(pIpv4Info->gateway) && !(IsZeroIpvxAddress(AF_SELECT_IPV4, pIpv4Info->gateway)))
        {
            snprintf(cmd, sizeof(cmd), "route add default gw %s dev %s", pIpv4Info->gateway, pIpv4Info->ifname);
            WanManager_DoSystemAction("SetUpDefaultSystemGateway:", cmd);
            CcspTraceInfo(("%s %d - The default gateway route entries set!, cmd(%s)\n",__FUNCTION__,__LINE__, cmd));
        }
    }
    else if (DeviceNwMode == MODEM_MODE)   
    {
        CcspTraceInfo(("%s %d: Device in MODEM Mode. So configure default route in user defined MODEM routing table\n", __FUNCTION__, __LINE__));

        // add rule for packets from brWan to lookup MODEM table
        memset (cmd, 0, sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "ip rule add iif %s table %s", WAN_BRIDGE, MODEM_TABLE_NAME);
        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", cmd);

        // add route to extender gateway via extender wan interface
        memset (cmd, 0, sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "ip route add default via %s dev %s table MODEM", pIpv4Info->gateway, pIpv4Info->ifname, WAN_BRIDGE, MODEM_TABLE_NAME);
        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", cmd);

        // put the previous rule/route to action
        WanManager_DoSystemAction("SetUpDefaultSystemGateway:", "ip route flush cache");

    }

    return ret;
}

static BOOL IsValidIpv4Address(const char* input)
{
   BOOL ret = TRUE;
   char *pToken = NULL;
   char *pLast = NULL;
   char buf[BUFLEN_32];
   uint32_t i, num;

   if (input == NULL || strlen(input) < 7 || strlen(input) > 15)
   {
      return FALSE;
   }

   /* need to copy since strtok_r updates string */
   memset(buf, '\0', BUFLEN_32);
   strncpy(buf, input, sizeof(buf)-1);

   /* IP address has the following format
      xxx.xxx.xxx.xxx where x is decimal number */
   pToken = strtok_r(buf, ".", &pLast);
   num = strtoul(pToken, NULL, 10);
   if (num > 255)
   {
      ret = FALSE;
   }
   else
   {
      for ( i = 0; i < 3; i++ )
      {
         pToken = strtok_r(NULL, ".", &pLast);

         num = strtoul(pToken, NULL, 10);
         if (num > 255)
         {
            ret = FALSE;
            break;
         }
      }
   }

   return ret;
}

static BOOL IsZeroIpvxAddress(uint32_t ipvx, const char *addr)
{
   if (IS_EMPTY_STR(addr))
   {
      return TRUE;
   }

   /*
    * Technically, the ::/0 is not an all zero address, but it is used by our
    * routing code to specify the default route.  See Wikipedia IPv6_address
    */
   if (((ipvx & AF_SELECT_IPV4) && !strcmp(addr, "0.0.0.0")) ||
       ((ipvx & AF_SELECT_IPV6) &&
           (!strcmp(addr, "0:0:0:0:0:0:0:0") ||
            !strcmp(addr, "::") ||
            !strcmp(addr, "::/128") ||
            !strcmp(addr, "::/0")))  )
   {
      return TRUE;
   }

   return FALSE;
}

static BOOL IsValidIpAddress(int32_t af, const char* address)
{
   if ( IS_EMPTY_STRING(address) ) return FALSE;
   if (af == AF_INET6)
   {
      struct in6_addr in6Addr;
      uint32_t plen;
      char   addr[IP_ADDR_LENGTH];

      if (ParsePrefixAddress(address, addr, &plen) != ANSC_STATUS_SUCCESS)
      {
         CcspTraceInfo(("Invalid ipv6 address=%s", address));
         return FALSE;
      }

      if (inet_pton(AF_INET6, addr, &in6Addr) <= 0)
      {
         CcspTraceInfo(("Invalid ipv6 address=%s", address));
         return FALSE;
      }

      return TRUE;
   }
   else
   {
      if (af == AF_INET)
      {
         return IsValidIpv4Address(address);
      }
      else
      {
         return FALSE;
      }
   }
}

static int ParsePrefixAddress(const char *prefixAddr, char *address, uint32_t *plen)
{
   int ret = RETURN_OK;
   char *tmpBuf;
   char *separator;
   uint32_t len;

   if (prefixAddr == NULL || address == NULL || plen == NULL)
   {
      return RETURN_ERR;
   }

   *address = '\0';
   *plen    = 128;

   len = strlen(prefixAddr);

   if ((tmpBuf = malloc(len+1)) == NULL)
   {
      CcspTraceError(("%s %d - alloc of %d bytes failed",__FUNCTION__,__LINE__, len));
      ret = RETURN_ERR;
   }
   else
   {
      memcpy(tmpBuf, prefixAddr, len + 1);
      separator = strchr(tmpBuf, '/');
      if (separator != NULL)
      {
         /* break the string into two strings */
         *separator = 0;
         separator++;
         while ((isspace(*separator)) && (*separator != 0))
         {
            /* skip white space after comma */
            separator++;
         }

         *plen = atoi(separator);
      }

      if (strlen(tmpBuf) < BUFLEN_40 && *plen <= 128)
      {
         strncpy(address, tmpBuf, strlen(tmpBuf) + 1);
      }
      else
      {
         ret = RETURN_ERR;
      }
      free(tmpBuf);
   }

   return ret;

}

static ANSC_STATUS getIfAddr6(const char *ifname , uint32_t addrIdx,
                      char *ipAddr, uint32_t *ifIndex, uint32_t *prefixLen, uint32_t *scope, uint32_t *ifaFlags)
{
    int   ret = ANSC_STATUS_FAILURE;
    FILE     *fp;
    uint32_t   count = 0;
    char     line[BUFLEN_64];

    *ipAddr = '\0';

    if ((fp = fopen("/proc/net/if_inet6", "r")) == NULL)
    {
        CcspTraceInfo(("failed to open /proc/net/if_inet6"));
        return ANSC_STATUS_FAILURE;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        /* remove the carriage return char */
        line[strlen(line)-1] = '\0';

        if (strstr(line, ifname) != NULL)
        {
            char *addr, *ifidx, *plen, *scp, *flags, *devname;
            char *nextToken = NULL;

            /* the first token in the line is the ip address */
            addr = strtok_r(line, " ", &nextToken);

            /* the second token is the Netlink device number (interface index) in hexadecimal */
            ifidx = strtok_r(NULL, " ", &nextToken);
            if (ifidx == NULL)
            {
                CcspTraceInfo(("Invalid /proc/net/if_inet6 line"));
                ret = ANSC_STATUS_FAILURE;
                break;
            }

            /* the third token is the Prefix length in hexadecimal */
            plen = strtok_r(NULL, " ", &nextToken);
            if (plen == NULL)
            {
                CcspTraceInfo(("Invalid /proc/net/if_inet6 line"));
                ret = ANSC_STATUS_FAILURE;
                break;
            }

            /* the forth token is the Scope value */
            scp = strtok_r(NULL, " ", &nextToken);
            if (scp == NULL)
            {
                CcspTraceInfo(("Invalid /proc/net/if_inet6 line"));
                ret = ANSC_STATUS_FAILURE;
                break;
            }

            /* the fifth token is the ifa flags */
            flags = strtok_r(NULL, " ", &nextToken);
            if (flags == NULL)
            {
                CcspTraceInfo(("Invalid /proc/net/if_inet6 line"));
                ret = ANSC_STATUS_FAILURE;
                break;
            }

            /* the sixth token is the device name */
            devname = strtok_r(NULL, " ", &nextToken);
            if (devname == NULL)
            {
                CcspTraceInfo(("Invalid /proc/net/if_inet6 line"));
                ret = ANSC_STATUS_FAILURE;
                break;
            }
            else
            {
                if (strcmp(devname, ifname) != 0)
                {
                    continue;
                }
                else if (count == addrIdx)
                {
                    int32_t   i;
                    char     *p1, *p2;

                    *ifIndex   = strtoul(ifidx, NULL, 16);
                    *prefixLen = strtoul(plen, NULL, 16);
                    *scope     = strtoul(scp, NULL, 16);
                    *ifaFlags  = strtoul(flags, NULL, 16);

                    /* insert a colon every 4 digits in the address string */
                    p2 = ipAddr;
                    for (i = 0, p1 = addr; *p1 != '\0'; i++)
                    {
                        if (i == 4)
                        {
                            i = 0;
                            *p2++ = ':';
                        }
                        *p2++ = *p1++;
                    }
                    *p2 = '\0';

                    ret = ANSC_STATUS_SUCCESS;
                    break;   /* done */
                }
                else
                {
                    count++;
                }
            }
        }
    }  /* while */

    fclose(fp);

    return ret;

}  /* End of getIfAddr6() */

ANSC_STATUS WanManager_getGloballyUniqueIfAddr6(const char *ifname, char *ipAddr, uint32_t *prefixLen)
{
   uint32_t addrIdx=0;
   uint32_t netlinkIndex=0;
   uint32_t scope=0;
   uint32_t ifaflags=0;
   int ret= ANSC_STATUS_SUCCESS;

   while (ANSC_STATUS_SUCCESS == ret)
   {
      ret = getIfAddr6(ifname, addrIdx, ipAddr, &netlinkIndex,
                              prefixLen, &scope, &ifaflags);
      if ((ANSC_STATUS_SUCCESS == ret) && (0 == scope))  // found it
         return ANSC_STATUS_SUCCESS;

      addrIdx++;
   }

   return ANSC_STATUS_FAILURE;
}


#ifdef FEATURE_802_1P_COS_MARKING
void Marking_UpdateInitValue(ANSC_HANDLE hInsContext, ULONG ulIfInstanceNumber, ULONG nIndex,DML_MARKING* pmark)
{
    PSINGLE_LINK_ENTRY  pSListEntry = NULL;

    WanMgr_Iface_Data_t* pIfaceDmlEntry = (WanMgr_Iface_Data_t*) hInsContext;
    if(pIfaceDmlEntry != NULL)
    {
        WanMgr_Iface_Data_t* pWanDmlIfaceData = WanMgr_GetIfaceData_locked(ulIfInstanceNumber);
        if(pWanDmlIfaceData != NULL)
        {
            DML_WAN_IFACE* pWanDmlIface = &(pWanDmlIfaceData->data);

            /* check the parameter name and set the corresponding value */
            pSListEntry       = AnscSListGetEntryByIndex(&(pWanDmlIface->Marking.MarkingList), nIndex);
            if ( pSListEntry )
            {
                CONTEXT_MARKING_LINK_OBJECT* pCxtLink      = ACCESS_CONTEXT_MARKING_LINK_OBJECT(pSListEntry);
                DML_MARKING*                    p_Marking  = pCxtLink->hContext;
                pCxtLink->bNew  = FALSE;
                /*Copy values from pmark to p_Marking */
                strcpy(p_Marking->Alias,pmark->Alias);
                p_Marking->SKBPort = pmark->SKBPort;
                p_Marking->SKBMark = pmark->SKBMark;
                p_Marking->InstanceNumber = pmark->InstanceNumber;
                p_Marking->EthernetPriorityMark = pmark->EthernetPriorityMark;
                CcspTraceInfo(("%s %d - copied to dml [%s,%u,%u,%d]\n",__FUNCTION__,__LINE__,p_Marking->Alias,p_Marking->SKBPort,p_Marking->SKBMark,p_Marking->EthernetPriorityMark));
            }
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }
    }
}

ANSC_HANDLE WanManager_AddIfaceMarking(DML_WAN_IFACE* pWanDmlIface, ULONG* pInsNumber)
{
    DATAMODEL_MARKING*              pDmlMarking     = (DATAMODEL_MARKING*) &(pWanDmlIface->Marking);
    DML_MARKING*                    p_Marking       = NULL;
    CONTEXT_MARKING_LINK_OBJECT*    pMarkingCxtLink = NULL;

    //Verify limit of the marking table
    if( WAN_IF_MARKING_MAX_LIMIT < pDmlMarking->ulNextInstanceNumber )
    {
        CcspTraceError(("%s %d - Failed to add Marking entry due to maximum limit(%d)\n",__FUNCTION__,__LINE__,WAN_IF_MARKING_MAX_LIMIT));
        return NULL;
    }

    p_Marking       = (DML_MARKING*)AnscAllocateMemory(sizeof(DML_MARKING));
    pMarkingCxtLink = (CONTEXT_MARKING_LINK_OBJECT*)AnscAllocateMemory(sizeof(CONTEXT_MARKING_LINK_OBJECT));
    if((p_Marking == NULL) || (pMarkingCxtLink == NULL))
    {
        if( NULL != pMarkingCxtLink )
        {
          AnscFreeMemory(pMarkingCxtLink);
          pMarkingCxtLink = NULL;
        }

        if( NULL != p_Marking )
        {
          AnscFreeMemory(p_Marking);
          p_Marking = NULL;
        }
        return NULL;
    }


    /* now we have this link content */
    pMarkingCxtLink->hContext = (ANSC_HANDLE)p_Marking;
    pMarkingCxtLink->bNew     = TRUE;

    pMarkingCxtLink->InstanceNumber  = pDmlMarking->ulNextInstanceNumber;
    *pInsNumber                      = pDmlMarking->ulNextInstanceNumber;

    //Assign actual instance number
    p_Marking->InstanceNumber = pDmlMarking->ulNextInstanceNumber;

    pDmlMarking->ulNextInstanceNumber++;

    //Assign WAN interface instance for reference
    p_Marking->ulWANIfInstanceNumber = pWanDmlIface->uiInstanceNumber;

    //Initialise all marking members
    memset( p_Marking->Alias, 0, sizeof( p_Marking->Alias ) );
    p_Marking->SKBPort = 0;
    p_Marking->SKBMark = 0;
    p_Marking->EthernetPriorityMark = -1;

    SListPushMarkingEntryByInsNum(&pDmlMarking->MarkingList, (PCONTEXT_LINK_OBJECT)pMarkingCxtLink);

    return (ANSC_HANDLE)pMarkingCxtLink;
}


#endif /* * FEATURE_802_1P_COS_MARKING */

ANSC_STATUS WanManager_CreatePPPSession(DML_WAN_IFACE* pInterface)
{
    pthread_t pppThreadId;
    INT iErrorCode;
    char wan_iface_name[10] = {0};

    /* Remove erouter0 dummy wan bridge if exists */
    deleteDummyWanBridgeIfExist(pInterface->Wan.Name);
    if (pInterface->PPP.LinkType == WAN_IFACE_PPP_LINK_TYPE_PPPoA)
    {
        strncpy(wan_iface_name, BASE_IFNAME_PPPoA, strlen(BASE_IFNAME_PPPoA));
    }
    else if (pInterface->PPP.LinkType == WAN_IFACE_PPP_LINK_TYPE_PPPoE)
    {
        strncpy(wan_iface_name, BASE_IFNAME_PPPoE, strlen(BASE_IFNAME_PPPoE));
    }
    else
    {
        strncpy(wan_iface_name, DEFAULT_IFNAME, strlen(DEFAULT_IFNAME));
    }    
    if (syscfg_set_commit(NULL, SYSCFG_WAN_INTERFACE_NAME, wan_iface_name) != 0)
    {
        CcspTraceError(("%s %d - syscfg_set failed to set Interafce=%s \n", __FUNCTION__, __LINE__, wan_iface_name ));
    }else{
        CcspTraceInfo(("%s %d - syscfg_set successfully to set Interafce=%s \n", __FUNCTION__, __LINE__, wan_iface_name ));
    }

    iErrorCode = pthread_create( &pppThreadId, NULL, &DmlHandlePPPCreateRequestThread, (void*)pInterface );
    if( 0 != iErrorCode )
    {
        CcspTraceInfo(("%s %d - Failed to start VLAN refresh thread EC:%d\n", __FUNCTION__, __LINE__, iErrorCode ));
        return ANSC_STATUS_FAILURE;
    }
    return ANSC_STATUS_SUCCESS;
}

static void* DmlHandlePPPCreateRequestThread( void *arg )
{
    char acSetParamName[DATAMODEL_PARAM_LENGTH] = {0};
    char acSetParamValue[DATAMODEL_PARAM_LENGTH] = {0};
    char adslPassword[DATAMODEL_PARAM_LENGTH] = {0};
    char adslUserName[DATAMODEL_PARAM_LENGTH] = {0};
    INT  iPPPInstance = -1;

    DML_WAN_IFACE* pInterface = (DML_WAN_IFACE *) arg;

    if( NULL == pInterface )
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        return (void *)ANSC_STATUS_FAILURE;
    }

    pthread_detach(pthread_self());

    //Create PPP Interface
    if( -1 == iPPPInstance )
    {
        char  acTableName[ 128 ] = { 0 };
        INT   iNewTableInstance  = -1;

        snprintf( acTableName, sizeof(acTableName)-1, "%s", PPP_INTERFACE_TABLE );
        if ( CCSP_SUCCESS != CcspBaseIf_AddTblRow (
             bus_handle,
             PPPMGR_COMPONENT_NAME,
             PPPMGR_DBUS_PATH,
             0,  /* session id */
             acTableName,
             &iNewTableInstance
           ) )
       {
            CcspTraceError(("%s Failed to add table %s\n", __FUNCTION__,acTableName));
            return (void *)ANSC_STATUS_FAILURE;
       }

       //Assign new instance
       iPPPInstance = iNewTableInstance;
    }

    CcspTraceInfo(("%s %d PPP Interface Instance:%d\n",__FUNCTION__, __LINE__, iPPPInstance));
    snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s%d.", PPP_INTERFACE_TABLE, iPPPInstance);
    CcspTraceInfo(("%s %d Set ppp path to %s \n", __FUNCTION__,__LINE__ ,acSetParamValue ));
    strncpy(pInterface->PPP.Path, acSetParamValue,sizeof(pInterface->PPP.Path)-1);
    WanMgr_SetRestartWanInfo(WAN_PPP_PATH_PARAM_NAME, pInterface->uiIfaceIdx, acSetParamValue);

    //Set Lower Layer
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_LOWERLAYERS, iPPPInstance );
    snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", pInterface->Phy.Path );
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_string, FALSE );

    //Set Alias
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_ALIAS, iPPPInstance );
    snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", pInterface->Wan.Name );
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_string, FALSE );

    CcspTraceError(("%s Going to call GetAdslUsernameAndPassword \n", __FUNCTION__ ));

#ifdef _HUB4_PRODUCT_REQ_
    if (GetAdslUsernameAndPassword(adslUserName, adslPassword) != ANSC_STATUS_SUCCESS )
    {
        CcspTraceError(("%s Failed to get ADSL username and password \n", __FUNCTION__ ));
    }
#endif
    CcspTraceError(("%s adslusername = %s adslpassword = %s \n", __FUNCTION__, adslUserName, adslPassword));
    //Set Username
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_USERNAME, iPPPInstance );
    snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", adslUserName );
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_string, TRUE );

    //Set Password
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_PASSWORD, iPPPInstance );
    snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", adslPassword );
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_string, TRUE );

    //Set IPCPEnable
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_IPCP_ENABLE, iPPPInstance );
    if (pInterface->PPP.IPCPEnable == TRUE)
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "true" );
    }
    else
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "false" );
    }
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_boolean, TRUE );

    //Set IPv6CPEnable
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_IPV6CP_ENABLE, iPPPInstance );
    if (pInterface->PPP.IPV6CPEnable == TRUE)
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "true" );
    }
    else
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "false" );
    }
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_boolean, TRUE );

    //Set LinkType
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_LINKTYPE, iPPPInstance );
    if (pInterface->PPP.LinkType == WAN_IFACE_PPP_LINK_TYPE_PPPoA)
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "PPPoA" );
    }
    else if (pInterface->PPP.LinkType == WAN_IFACE_PPP_LINK_TYPE_PPPoE)
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "PPPoE" );
    }
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_string, TRUE );

    //Set PPP Enable
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_ENABLE, iPPPInstance );
    if (pInterface->PPP.Enable == TRUE)
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "true" );
    }
    else
    {
        snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "false" );
    }
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_boolean, TRUE );


    CcspTraceInfo(("%s %d Successfully created PPP %s interface \n", __FUNCTION__,__LINE__, pInterface->Wan.Name ));

    //Clean exit
    pthread_exit(NULL);

    return NULL;
}

ANSC_STATUS WanManager_DeletePPPSession(DML_WAN_IFACE* pInterface)
{
    char acSetParamName[256] = {0};
    char acSetParamValue[256] = {0};
    INT  iPPPInstance = -1;

    if( NULL == pInterface )
    {
        CcspTraceError(("%s Invalid Memory\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    memset( acSetParamName, 0, DATAMODEL_PARAM_LENGTH );
    memset( acSetParamValue, 0, DATAMODEL_PARAM_LENGTH );

    iPPPInstance = get_index_from_path(pInterface->PPP.Path);
    if (iPPPInstance == -1)
    {
        CcspTraceInfo(("%s %d PPP path is invalid \n",__FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    //Set PPP Enable
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_ENABLE, iPPPInstance );
    snprintf( acSetParamValue, DATAMODEL_PARAM_LENGTH, "%s", "false" );
    WanMgr_RdkBus_SetParamValues( PPPMGR_COMPONENT_NAME, PPPMGR_DBUS_PATH, acSetParamName, acSetParamValue, ccsp_boolean, TRUE );

    //Delete PPP Instance
    snprintf( acSetParamName, DATAMODEL_PARAM_LENGTH, PPP_INTERFACE_INSTANCE, iPPPInstance );
    if ( CCSP_SUCCESS != CcspBaseIf_DeleteTblRow (
                            bus_handle,
                            PPPMGR_COMPONENT_NAME,
                            PPPMGR_DBUS_PATH,
                            0, /* session id */
                            acSetParamName))
     {
         CcspTraceError(("%s Failed to delete table %s\n", __FUNCTION__, acSetParamName));
         return ANSC_STATUS_FAILURE;
     }

    CcspTraceInfo(("%s %d Successfully deleted PPP interface \n", __FUNCTION__,__LINE__ ));

    sleep(2);

    /* Create a dummy wan bridge */
    if (syscfg_set_commit(NULL, SYSCFG_WAN_INTERFACE_NAME, DEFAULT_IFNAME) != 0)
    {
        CcspTraceError(("%s %d - syscfg_set failed to set Interafce=%s \n", __FUNCTION__, __LINE__, DEFAULT_IFNAME ));
    }else{
        CcspTraceInfo(("%s %d - syscfg_set successfully to set Interafce=%s \n", __FUNCTION__, __LINE__, DEFAULT_IFNAME ));
    }
    createDummyWanBridge(pInterface->Wan.Name);

    return ANSC_STATUS_SUCCESS;
}

int WanManager_AddGatewayRoute(const WANMGR_IPV4_DATA* pIpv4Info)
{
    char cmd[BUFLEN_128]={0};
    int ret = RETURN_OK;
    FILE *fp = NULL;
    
    /* delete gateway first before add  */
    snprintf(cmd, sizeof(cmd), "route del %s dev %s", pIpv4Info->gateway, pIpv4Info->ifname);
    WanManager_DoSystemAction("SetUpSystemGateway:", cmd);

    /* Sets gateway route entry */
    if (IsValidIpv4Address(pIpv4Info->gateway) && !(IsZeroIpvxAddress(AF_SELECT_IPV4, pIpv4Info->gateway)))
    {
        snprintf(cmd, sizeof(cmd), "ip route add %s dev %s", pIpv4Info->gateway, pIpv4Info->ifname);
        WanManager_DoSystemAction("SetUpSystemGateway:", cmd);
        CcspTraceInfo(("%s %d - The gateway route entries set!\n",__FUNCTION__,__LINE__));
    }

    return ret;
}

static int get_index_from_path(const char *path)
{
    int index = -1;

    if(path == NULL)
    {
        return -1;
    }

    sscanf(path, "%*[^0-9]%d", &index);
    return index;
}

#ifdef _HUB4_PRODUCT_REQ_
static ANSC_STATUS GetAdslUsernameAndPassword(char *Username, char *Password)
{
    int ret= ANSC_STATUS_SUCCESS;
    FILE *fp = NULL;
    char line[BUFLEN_128] = {0};

    if (Username == NULL || Password == NULL)
    {
        return ANSC_STATUS_FAILURE;
    }
    if (fp = fopen(SERIALIZATION_DATA, "r"))
    {
        while(fgets(line, sizeof(line), fp)!=NULL)
        {
            if(strstr(line,"ADSLUSER"))
            {
                char * ch = strrchr(line,'=');
                strncpy(Username, ch+1, BUFLEN_64);
                Username[strcspn(Username, "\n\r")] = 0;
            }
            if(strstr(line,"ADSLPASS"))
            {
                char * ch = strrchr(line,'=');
                strncpy(Password, ch+1, BUFLEN_64);
                Password[strcspn(Password, "\n\r")] = 0;
            }
        }
        if(fp != NULL)
        {
            fclose(fp);
            fp = NULL;
        }
    }
    else
    {
        ret = ANSC_STATUS_FAILURE;
    }

    return ret;
}
#endif

static void createDummyWanBridge(char * iface_name)
{
    char wan_mac[64] = {'\0'};
    char file_path[64] = {0};
    FILE *fp_mac_addr_table = NULL;
    int ret = 0;

    snprintf(file_path, sizeof(file_path)-1, "/sys/class/net/%s/address", iface_name);
    fp_mac_addr_table = fopen(file_path, "r");
    if(fp_mac_addr_table == NULL)
    {
        CcspTraceError(("Failed to open mac address table"));
    }
    else
    {
        ret = fread(wan_mac, sizeof(wan_mac),1, fp_mac_addr_table);
        fclose(fp_mac_addr_table);
    }

    ret = v_secure_system("brctl addbr %s", iface_name);
    if(ret != 0) {
          CcspTraceWarning(("%s: Failure in executing command via v_secure_system. ret:[%d] \n", __FUNCTION__,ret));
    }

    ret = v_secure_system("ip link set dev %s address %s", iface_name, wan_mac);
    if(ret != 0) {
          CcspTraceWarning(("%s:Failure in executing command via v_secure_system. ret:[%d] \n",__FUNCTION__,ret));
    }

    return;
}

static void deleteDummyWanBridgeIfExist(char * iface_name)
{
    char resultBuff[256];
    FILE *fp = NULL;
    int ret = 0;
    memset(resultBuff, '\0', sizeof(resultBuff));
    fp = v_secure_popen("r","ip -d link show %s | tail -n +2 | grep bridge", iface_name);
    if (fp != NULL)
    {
        fgets(resultBuff, sizeof(resultBuff), fp);
        if (resultBuff[0] == '\0')
        {
            // Empty result. No bridge found.
            CcspTraceInfo(("%s bridge interface is not exists in the system \n", iface_name));
        }
        else
        {
            CcspTraceInfo(("%s bridge interface is found in the system, so delete it \n", iface_name));
            /* Down the interface before we delete it. */
            ret = v_secure_system("ifconfig %s down", iface_name);
	    if(ret != 0) {
                CcspTraceWarning(("%s:Failure in executing command via v_secure_system. ret:[%d] \n",__FUNCTION__,ret));
            }
            ret = v_secure_system("brctl delbr %s", iface_name);
	    if(ret != 0) {
                CcspTraceWarning(("%s:Failure in executing command via v_secure_system. ret:[%d] \n",__FUNCTION__,ret));
            }

        }
        ret = v_secure_pclose(fp);
	if(ret !=0)
	{
	    CcspTraceError(("%s: Failed in closing pipe ret %d \n",__FUNCTION__,ret));
	}
    }

    return;
}

ANSC_STATUS WanManager_CheckGivenPriorityExists(INT IfIndex, UINT uiTotalIfaces, INT priority, BOOL *Status)
{
    ANSC_STATUS             retStatus    = ANSC_STATUS_SUCCESS;
    INT                     uiLoopCount   = 0;
    INT                     wan_if_count = 0;

    *Status = FALSE;
    if ( uiTotalIfaces <= 0 )
    {
        CcspTraceError(("%s Invalid Parameter\n", __FUNCTION__ ));
        return ANSC_STATUS_FAILURE;
    }

    for( uiLoopCount = 0; uiLoopCount < uiTotalIfaces; uiLoopCount++ )
    {
        WanMgr_Iface_Data_t*   pWanDmlIfaceData = WanMgr_GetIfaceData_locked(uiLoopCount);
        if(pWanDmlIfaceData != NULL)
        {
            DML_WAN_IFACE* pWanIfaceData = &(pWanDmlIfaceData->data);
            if( uiLoopCount == IfIndex )
            {
                WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                continue;
            }
            if(pWanIfaceData->Wan.Priority == priority)
            {
                *Status = TRUE;
                WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                return retStatus;
            }
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }
    }

    return retStatus;
}

ANSC_STATUS WanManager_CheckGivenTypeExists(INT IfIndex, UINT uiTotalIfaces, DML_WAN_IFACE_TYPE priorityType, INT priority, BOOL *Status)
{
    ANSC_STATUS             retStatus    = ANSC_STATUS_SUCCESS;
    INT                     uiLoopCount   = 0;
    INT                     wan_if_count = 0;

    if ( uiTotalIfaces <= 0 )
    {
        CcspTraceError(("%s Invalid Parameter\n", __FUNCTION__ ));
        return ANSC_STATUS_FAILURE;
    }

    for( uiLoopCount = 0; uiLoopCount < uiTotalIfaces; uiLoopCount++ )
    {
        WanMgr_Iface_Data_t*   pWanDmlIfaceData = WanMgr_GetIfaceData_locked(uiLoopCount);
        if(pWanDmlIfaceData != NULL)
        {
            DML_WAN_IFACE* pWanIfaceData = &(pWanDmlIfaceData->data);
            if( uiLoopCount == IfIndex )
            {
                WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                continue;
            }
            if(pWanIfaceData->Wan.Type == priorityType)
            {
                if ( pWanIfaceData->Wan.Priority == priority)
                {
                    *Status = TRUE;
                    WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
                    return retStatus;
                }
            }
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }
    }

    return retStatus;
}

static INT IsIPObtained(char *pInterfaceName)
{
    char buff[256] = {0};
    FILE *fp = NULL;
    int pclose_ret = 0;
    if (!pInterfaceName)
    {
        return 0;
    }

    /* Validate IPv4 Connection on WAN interface */
    memset(buff,0,sizeof(buff));

    /* Open the command for reading. */
    fp = v_secure_popen("r","ip addr show %s |grep -i 'inet ' |awk '{print $2}' |cut -f2 -d:", pInterfaceName);
    if (fp == NULL)
    {
        CcspTraceError(("%s-%d : Error popen\n", __FUNCTION__, __LINE__));
    }
    else
    {
        /* Read the output a line at a time - output it. */
        if (fgets(buff, 50, fp) != NULL)
        {
            CcspTraceInfo(("%s-%d : assigned IP(%s), On Interface(%s) \n", __FUNCTION__, __LINE__, buff, pInterfaceName));
        }
        /* close */
        pclose_ret = v_secure_pclose(fp);
	if(pclose_ret !=0)
        {
            CcspTraceError(("Failed in closing the pipe ret %d\n",pclose_ret));
        }

        if(buff[0] != 0)
        {
            CcspTraceInfo(("%s %d - IP obtained %s\n", __FUNCTION__, __LINE__,pInterfaceName));
            return 1;
        }
    }

    /* Validate IPv6 Connection on WAN interface */
    
   
    memset(buff,0,sizeof(buff));

    /* Open the command for reading. */
    fp = v_secure_popen("r","ip addr show %s |grep -i 'inet6 ' |grep -i 'Global' |awk '{print $2}'", pInterfaceName);
    if (fp == NULL)
    {
        CcspTraceError(("%s-%d : Error popen\n", __FUNCTION__, __LINE__));
    }
    else
    {
        /* Read the output a line at a time - output it. */
        if (fgets(buff, 50, fp) != NULL)
        {
            CcspTraceInfo(("%s-%d : assigned IP(%s), On Interface(%s) \n", __FUNCTION__, __LINE__, buff, pInterfaceName));
        }
        /* close */
        pclose_ret = v_secure_pclose(fp);
	if(pclose_ret !=0)
        {
            CcspTraceError(("Failed in closing the pipe ret %d\n",pclose_ret));
        }

        if(buff[0] != 0)
        {
            CcspTraceInfo(("%s %d - IP obtained %s \n", __FUNCTION__, __LINE__,pInterfaceName));
            return 2;
        }
    }
    return 0;
}


void* ThreadWanMgr_MonitorAndUpdateIpStatus( void *arg )
{
    UINT counter = 0;
    UINT  *puIndex = NULL;
    INT ipstatus = 0;
    DML_WAN_IFACE* pFixedInterface = NULL;

    pthread_detach(pthread_self());

    if ( NULL == arg )
    {
       CcspTraceError(("%s %d Invalid buffer\n", __FUNCTION__,__LINE__));
       //Cleanup current thread when exit
       pthread_exit(NULL);
    }
    puIndex = (UINT*)arg;
    CcspTraceInfo(("%s %d index %d\n", __FUNCTION__,__LINE__,*puIndex));
    while(1)
    {
       WanMgr_Iface_Data_t*   pWanDmlIfaceData = WanMgr_GetIfaceData_locked(*puIndex);
        if (!pWanDmlIfaceData)
        {
            break;
        }
        pFixedInterface = &pWanDmlIfaceData->data;
        if (!pFixedInterface)
        {
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
            break;
        }
        ipstatus = IsIPObtained(pFixedInterface->Wan.Name);
        if (ipstatus != 0)
        {
            if (ipstatus == 1)
            {
                pFixedInterface->IP.Ipv4Status = WAN_IFACE_IPV4_STATE_UP;
            }
            if (ipstatus == 2)
            {
                pFixedInterface->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_UP;
            }
            CcspTraceInfo(("%s-%d : IP Monitor Successful, Interface(%s) \n", __FUNCTION__, __LINE__, pFixedInterface->Wan.Name));
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
            break;
        }
        if (counter >= IPMONITOR_WAIT_MAX_TIMEOUT)
        {
            pFixedInterface->IP.Ipv4Status = WAN_IFACE_IPV4_STATE_DOWN;
            pFixedInterface->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_DOWN;
            CcspTraceWarning(("%s-%d : IP Monitor timer expire, Interface(%s) \n", __FUNCTION__, __LINE__, pFixedInterface->Wan.Name));
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
            break;
        }
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        sleep(IPMONITOR_QUERY_INTERVAL);
        counter += IPMONITOR_QUERY_INTERVAL;
    }
    free(puIndex);
    return arg;
}

INT WanMgr_StartIpMonitor(UINT iface_index)
{
    INT  iErrorCode     = 0;
    pthread_t ipMonitorThreadId;

    UINT *pIndex = malloc(sizeof(UINT));
    if (pIndex)
    {
        *pIndex = iface_index;
        iErrorCode = pthread_create( &ipMonitorThreadId, NULL, &ThreadWanMgr_MonitorAndUpdateIpStatus, (void*)pIndex );
    }
    return iErrorCode;
}
#ifdef MAPT_NAT46_FTP_ACTIVE_MODE
void WanManager_CalculateMAPTPortRange(int offset, int psidLen, int psid, char* port_range)
{
    int a = 0, m = 0, contigous_port = 0, ratio = 0;
    int index = 0, len = 0;
    int initialPortValue = 0, finalPortValue = 0;
    char single_port_range[BUFLEN_32]={0};

    if (offset == 0)
        offset = 6;

    a = (1 << offset);
    m = 16 - (psidLen + offset);
    contigous_port = (1 << m);
    ratio = 16 - offset;

    for(index=1; index < (a); index++)
    {
        initialPortValue = (index<<ratio) + (psid <<(m));
        finalPortValue   = initialPortValue + (contigous_port - 1);
        snprintf(single_port_range, BUFLEN_32, "%d,%d,", initialPortValue, finalPortValue);
        strncat(port_range, single_port_range, BUFLEN_32);
        memset(single_port_range, 0, BUFLEN_32);
    }
    len = strlen(port_range);
    port_range[len - 1] = '\0';
}
#endif

bool IsDefaultRoutePresent(char *IfaceName, bool IsV4)
{
    int pclose_ret = 0;
    bool ret = false;
    char line[BUFLEN_1024] = {0};
    FILE *fp;

    if ((strlen(IfaceName) <=0 ) || (IfaceName[0] == '\0'))
    {
        CcspTraceError(("%s-%d : Interface Name is Null\n", __FUNCTION__, __LINE__));
        return false;
    }

    if (IsV4)
    {
        fp = v_secure_popen("r","ip -4 route show default | grep default | awk '{print $5}'");
    }
    else
    {
        fp = v_secure_popen("r","ip -6 route show default | grep default | awk '{print $5}'");
    }
    if (fp)
    {
        if (fgets(line, sizeof(line), fp) != NULL)
        {
            char *token = strtok(line, "\n");
            if (token)
            {
                CcspTraceError(("%s-%d : Default Route Iface Name(%s)/Expected Iface Name(%s) \n", __FUNCTION__, __LINE__, token, IfaceName));
                if ((strcmp(IfaceName, token) == 0))
                {
                    CcspTraceInfo(("%s-%d : Default %s Route found for Interface(%s) \n", __FUNCTION__, __LINE__, (IsV4? "IPv4":"IPv6"), IfaceName));
                    ret = true;
                }
            }
            else
            {
                CcspTraceError(("%s-%d : Failed to Parse Route, Expected Iface Name(%s) \n", __FUNCTION__, __LINE__, IfaceName));
                ret = false;
            }
        }
        else
        {
            CcspTraceError(("%s-%d : Could not read default route \n", __FUNCTION__, __LINE__));
            ret = false;
        }
        pclose_ret = v_secure_pclose(fp);
        if(pclose_ret !=0)
        {
            CcspTraceError(("%s-%d : Failed in closing the pipe ret %d \n", __FUNCTION__, __LINE__, pclose_ret));
        }
    }
    else
    {
        CcspTraceError(("%s-%d : Failed to get the default route \n", __FUNCTION__, __LINE__));
        ret = false;
    }

    return ret;
}

bool WanManager_IsNetworkInterfaceAvailable( char *IfaceName ) 
{
    int skfd = -1;
    struct ifreq ifr = {0};

    if( NULL == IfaceName )
    {
       return FALSE;
    }
  
    AnscCopyString(ifr.ifr_name, IfaceName);
    
    skfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(skfd == -1)
       return FALSE;
   
    if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
        if (errno == ENODEV) {
            close(skfd); 
            return FALSE;
        }
    }

    close(skfd);
    return TRUE;
}

int WanMgr_RdkBus_AddIntfToLanBridge (char * PhyPath, BOOL AddToBridge)
{
    if (PhyPath == NULL)
    {
        CcspTraceError(("%s %d: Invalid args...\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }

    if (strstr(PhyPath, "Ethernet") == NULL)
    {
        CcspTraceInfo(("%s %d: Interface is not Ethernet based, so LAN Bridge operation required\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_SUCCESS;
    }

    int retry_count = SET_MAX_RETRY_COUNT;
    char param_name[BUFLEN_256] = {0};
    char param_value[BUFLEN_256] = {0};
    parameterValStruct_t varStruct[1] = {0};

    strncpy(param_name, PhyPath, sizeof(param_name)-1);
    strncat(param_name, ETH_ADDTOLANBRIDGE_DM_SUFFIX, sizeof(param_name) - strlen(param_name));
    if (AddToBridge)
    {
        strncpy(param_value, "true", sizeof(param_value));
    }
    else
    {
        strncpy(param_value, "false", sizeof(param_value));
    }

    while (retry_count--)
    {
        if (WanMgr_RdkBus_SetParamValues(ETH_COMPONENT_NAME, ETH_COMPONENT_PATH, param_name, param_value, ccsp_boolean, TRUE ) == ANSC_STATUS_SUCCESS)
        {
            CcspTraceInfo(("%s %d: Succesfully set %s = %s\n", __FUNCTION__, __LINE__, param_name, param_value));
            return ANSC_STATUS_SUCCESS;
        }
        usleep(500000);
    }

    CcspTraceError(("%s %d: unable to set %s as %s\n", __FUNCTION__, __LINE__, param_name, param_value));
    return ANSC_STATUS_FAILURE;

}
