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

#include <unistd.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <sys/syscall.h>
#include "wanmgr_interface_sm.h"
#include "wanmgr_utils.h"
#include "platform_hal.h"
#include "wanmgr_sysevents.h"
#include "wanmgr_ipc.h"
#include "wanmgr_rdkbus_utils.h"
#include "wanmgr_data.h"
#include "wanmgr_interface_sm.h"
#include "wanmgr_platform_events.h"
#include "wanmgr_net_utils.h"
#include "wanmgr_dhcpv4_apis.h"
#include "wanmgr_dhcpv6_apis.h"


#define LOOP_TIMEOUT 50000 // timeout in milliseconds. This is the state machine loop interval
#define RESOLV_CONF_FILE "/etc/resolv.conf"
#define LOOPBACK "127.0.0.1"

#ifdef FEATURE_IPOE_HEALTH_CHECK
#define IPOE_HEALTH_CHECK_V4_STATUS "ipoe_health_check_ipv4_status"
#define IPOE_HEALTH_CHECK_V6_STATUS "ipoe_health_check_ipv6_status"
#define IPOE_STATUS_FAILED "failed"
#endif

#if defined(FEATURE_IPOE_HEALTH_CHECK) && defined(IPOE_HEALTH_CHECK_LAN_SYNC_SUPPORT)
extern lanState_t lanState;
#endif

#if defined(FEATURE_464XLAT)
typedef enum
{
    XLAT_ON = 10,
    XLAT_OFF
} XLAT_State_t;
static XLAT_State_t xlat_state_get(void);
#endif


/*WAN Manager States*/
static eWanState_t wan_state_configuring_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_validating_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_obtaining_ip_addresses(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_standby(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_ipv4_leased(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_ipv6_leased(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_dual_stack_active(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
#ifdef FEATURE_MAPT
static eWanState_t wan_state_mapt_active(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
#endif //FEATURE_MAPT
static eWanState_t wan_state_refreshing_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_deconfiguring_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_state_exit(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);

/*WAN Manager Transitions*/
static eWanState_t wan_transition_start(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_physical_interface_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_wan_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_wan_validated(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_refreshing_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_wan_refreshed(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_ipv4_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_ipv4_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_ipv6_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_ipv6_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_dual_stack_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_standby(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_standby_deconfig_ips(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);

#ifdef FEATURE_MAPT
static eWanState_t wan_transition_mapt_feature_refresh(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_mapt_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
static eWanState_t wan_transition_mapt_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);
extern int mapt_feature_enable_changed;
#endif //FEATURE_MAPT
static eWanState_t wan_transition_exit(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl);

/***************************************************************************
 * @brief API used to check the incoming nameserver is valid
 * @param af indicates ip address family
 * @param nameserver dns namserver name
 * @return RETURN_OK if execution successful else returned error.
 ****************************************************************************/
int IsValidDnsServer(int32_t af, const char *nameServer);

/********************************************************************************
 * @brief Configure IPV4 configuration on the interface.
 * This API calls the HAL routine to configure ipv4.
 * @param pWanIfaceCtrl Interface state machine data - iface and config wan data
 * @return RETURN_OK upon success else returned error code.
 *********************************************************************************/
static int wan_setUpIPv4(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl);

/********************************************************************************
 * @brief Unconfig IPV4 configuration on the interface.
 * This API calls the HAL routine to unconfig ipv4.
 * @param pWanIfaceCtrl Interface state machine data - iface and config wan data
 * @return RETURN_OK upon success else returned error code.
 *********************************************************************************/
static int wan_tearDownIPv4(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl);

/*************************************************************************************
 * @brief Configure IPV6 configuration on the interface.
 * This API calls the HAL routine to config ipv6.
 * @param pWanIfaceCtrl Interface state machine data - iface and config wan data
 * @return  RETURN_OK upon success else returned error code.
 **************************************************************************************/
static int wan_setUpIPv6(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl);

/*************************************************************************************
 * @brief Unconfig IPV6 configuration on the interface.
 * This API calls the HAL routine to unconfig ipv6.
 * @param pWanIfaceCtrl Interface state machine data - iface and config wan data
 * @return RETURN_OK upon success else returned error code.
 **************************************************************************************/
static int wan_tearDownIPv6(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl);

/**************************************************************************************
 * @brief Update DNS configuration into /etc/resolv.conf
 * @param pWanIfaceCtrl Interface state machine data - iface and config wan data
 * @param addIPv4 boolean flag indicates whether IPv4 DNS data needs to be update
 * @param addIPv6 boolean flag indicates whether IPv6 DNS data needs to be update
 * @return RETURN_OK upon success else ERROR code returned
 **************************************************************************************/
int wan_updateDNS(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl, BOOL addIPv4, BOOL addIPv6);

/**************************************************************************************
 * @brief Clear the DHCP client data stored.
 * It should be used to clear the old data before start a new DHCP client.
 * @param Interface data structure
 * @return ANSC_STATUS_SUCCESS upon success else ANSC_STATUS_FAILURE
 **************************************************************************************/
static ANSC_STATUS WanManager_ClearDHCPData(DML_WAN_IFACE* pInterface);

/*************************************************************************************
 * @brief Check IPv6 address assigned to interface or not.
 * This API internally checks ipv6 prefix being set, received valid gateway and
 * lan ipv6 address ready to use.
 * @return RETURN_OK on success else RETURN_ERR
 *************************************************************************************/
static int checkIpv6AddressAssignedToBridge();

/*************************************************************************************
 * @brief Check IPv6 address is ready to use or not
 * @return RETURN_OK on success else RETURN_ERR
 *************************************************************************************/
static int checkIpv6LanAddressIsReadyToUse();

#ifdef FEATURE_MAPT

/*************************************************************************************
 * @brief Enable mapt configuration on the interface.
 * This API calls the HAL routine to Enable mapt.
 * @return RETURN_OK upon success else ERROR code returned
 **************************************************************************************/
static int wan_setUpMapt();

/*************************************************************************************
 * @brief Disable mapt configuration on the interface.
 * This API calls the HAL routine to disable mapt.
 * @return RETURN_OK upon success else ERROR code returned
 **************************************************************************************/
static int wan_tearDownMapt();

/*************************************************************************************
 * @brief checks kernel module loaded.
 * This API calls the proc entry.
 * @param moduleName is the kernal modulename
 * @return RETURN_OK upon success else RETURN_ERR returned
 **************************************************************************************/

#define NAT46_MODULE "nat46"
#define MTU_DEFAULT_SIZE (1500)
#define MAP_INTERFACE "map0"

static int wan_setUpMapt()
{
    int ret = RETURN_OK;

#if defined(IVI_KERNEL_SUPPORT)
    if (WanManager_DoSystemActionWithStatus("wanmanager", "insmod /lib/modules/`uname -r`/extra/ivi.ko") != RETURN_OK)
    {
        CcspTraceError(("%s %d -insmod: Failed to add ivi.ko \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }
#elif defined(NAT46_KERNEL_SUPPORT)
    if ((isModuleLoaded(NAT46_MODULE) != RETURN_OK) && (WanManager_DoSystemActionWithStatus("wanmanager", "insmod /lib/modules/`uname -r`/extra/nat46.ko zero_csum_pass=1") != RETURN_OK))
    {
        CcspTraceError(("%s %d -insmod: Failed to add nat46.ko \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }
#endif //IVI_KERNEL_SUPPORT

    return ret;
}

static int wan_tearDownMapt()
{
    int ret = RETURN_OK;
    FILE *file;
    char line[BUFLEN_64];
    char cmd[BUFLEN_128] = {0};

#if defined(IVI_KERNEL_SUPPORT)
    file = popen("cat /proc/modules | grep ivi","r");

    if( file == NULL) {
        CcspTraceError(("[%s][%d]Failed to open  /proc/modules \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }
    else {
        if( fgets (line, BUFLEN_64, file) !=NULL ) {
            if( strstr(line, "ivi")) {
                if (WanManager_DoSystemActionWithStatus("wanmanager", "ivictl -q") != RETURN_OK)
                {
                    CcspTraceError(("%s %d ivictl: Failed to stop \n", __FUNCTION__, __LINE__));
                    ret = RETURN_ERR;
                }
                else
                {
                    CcspTraceInfo(("%s %d ivictl stopped successfully\n", __FUNCTION__, __LINE__));
                }

                if (WanManager_DoSystemActionWithStatus("wanmanager", "rmmod -f /lib/modules/`uname -r`/extra/ivi.ko") != RETURN_OK)
                {
                    CcspTraceError(("%s %d rmmod: Failed to remove ivi.ko \n", __FUNCTION__, __LINE__));
                    ret = RETURN_ERR;
                }
                else
                {
                    CcspTraceInfo(("%s %d ivi.ko removed\n", __FUNCTION__, __LINE__));
                }
            }
        }
        pclose(file);
    }
#elif defined(NAT46_KERNEL_SUPPORT)
    snprintf(cmd, BUFLEN_128, "echo del %s > /proc/net/nat46/control", MAP_INTERFACE);
    if ((isModuleLoaded(NAT46_MODULE) == RETURN_OK) && (WanManager_DoSystemActionWithStatus("wanmanager", cmd) != RETURN_OK))
    {
        CcspTraceError(("%s %d Clear nat46 configurations Failed \n", __FUNCTION__, __LINE__));
    }
    else
    {
        CcspTraceInfo(("%s %d Clear nat46 configurations Success \n", __FUNCTION__, __LINE__));
    }
#endif //IVI_KERNEL_SUPPORT

    return ret;
}
#endif

/************************************************************************************
 * @brief Get the status of Interface State Machine.
 * @return TRUE on running else FALSE
 ************************************************************************************/
BOOL WanMgr_Get_ISM_RunningStatus(UINT idx)
{
    BOOL status = FALSE;
    WanMgr_Config_Data_t* pWanConfigData = WanMgr_GetConfigData_locked();
    if(pWanConfigData != NULL)
    {
        DML_WANMGR_CONFIG* pWanConfig = &(pWanConfigData->data);
        if (pWanConfig->ISMStatus & (1UL << idx))
        {
            status = TRUE;
        }
        else
        {
            status = FALSE;
        }
        WanMgrDml_GetConfigData_release(pWanConfigData);
    }
    return status;
}

/************************************************************************************
 * @brief Update the status of Interface State Machine.
 ************************************************************************************/
void WanMgr_Set_ISM_RunningStatus(UINT idx, bool status)
{
    WanMgr_Config_Data_t* pWanConfigData = WanMgr_GetConfigData_locked();
    if(pWanConfigData != NULL)
    {
        DML_WANMGR_CONFIG* pWanConfig = &(pWanConfigData->data);

        if (status)
        {
            pWanConfig->ISMStatus |= (1UL << idx);
        }
        else
        {
            pWanConfig->ISMStatus &= ~(1UL << idx);
        }

        WanMgrDml_GetConfigData_release(pWanConfigData);
    }
}

#if defined(FEATURE_464XLAT)
static XLAT_State_t xlat_state_get(void)
{
	char buf[BUFLEN_128];
	char status[BUFLEN_128]={0};
	

	if (syscfg_get(NULL, "xlat_status", status, sizeof(status))!=0)
	{
		if(strcmp(status,"up")==0)
		{
			CcspTraceInfo(("%s %d - 464 xlat is on\n", __FUNCTION__, __LINE__));
			return XLAT_ON;
		}
		else
		{
			CcspTraceInfo(("%s %d - 464 xlat is off\n", __FUNCTION__, __LINE__));
			return XLAT_OFF;
		}
	}else
	{
		return XLAT_OFF;
	}
}
#endif

/*********************************************************************************/
/**************************** ACTIONS ********************************************/
/*********************************************************************************/
void WanManager_UpdateInterfaceStatus(DML_WAN_IFACE* pIfaceData, wanmgr_iface_status_t iface_status)
{
    CcspTraceInfo(("ifName: %s, link: %s, ipv4: %s, ipv6: %s\n", ((pIfaceData != NULL) ? pIfaceData->Wan.Name : "NULL"),
                   ((iface_status == WANMGR_IFACE_LINK_UP) ? "UP" : (iface_status == WANMGR_IFACE_LINK_DOWN) ? "DOWN" : "N/A"),
                   ((iface_status == WANMGR_IFACE_CONNECTION_UP) ? "UP" : (iface_status == WANMGR_IFACE_CONNECTION_DOWN) ? "DOWN" : "N/A"),
                   ((iface_status == WANMGR_IFACE_CONNECTION_IPV6_UP) ? "UP" : (iface_status == WANMGR_IFACE_CONNECTION_IPV6_DOWN) ? "DOWN" : "N/A")
                ));

    if(pIfaceData == NULL)
    {
        return;
    }

    switch (iface_status)
    {
        case WANMGR_IFACE_LINK_UP:
        {
            break;
        }
        case WANMGR_IFACE_LINK_DOWN:
        {
            break;
        }
        case WANMGR_IFACE_CONNECTION_UP:
        {
#if defined(FEATURE_RDKB_CONFIGURABLE_WAN_INTERFACE)
            /*Set Generic sysevents only if Interface is active. Else will be configured by ISM when interface is selected. */
            if(pIfaceData->SelectionStatus == WAN_IFACE_ACTIVE)
            {
                if (pIfaceData->IP.Ipv4Data.isTimeOffsetAssigned)
                {
                    char value[64] = {0};
                    snprintf(value, sizeof(value), "@%d", pIfaceData->IP.Ipv4Data.timeOffset);
                    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_TIME_OFFSET, value, 0);
                    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_DHCPV4_TIME_OFFSET, SET, 0);
                }

                sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_TIME_ZONE, pIfaceData->IP.Ipv4Data.timeZone, 0);
            }
#endif
            pIfaceData->IP.Ipv4Status = WAN_IFACE_IPV4_STATE_UP;
            break;
        }
        case WANMGR_IFACE_CONNECTION_DOWN:
        {
            pIfaceData->IP.Ipv4Status = WAN_IFACE_IPV4_STATE_DOWN;
            pIfaceData->IP.Ipv4Changed = FALSE;
#ifdef FEATURE_IPOE_HEALTH_CHECK
            pIfaceData->IP.Ipv4Renewed = FALSE;
#endif
            strncpy(pIfaceData->IP.Ipv4Data.ip, "", sizeof(pIfaceData->IP.Ipv4Data.ip));
            break;
        }
        case WANMGR_IFACE_CONNECTION_IPV6_UP:
        {
            pIfaceData->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_UP;
            break;
        }
        case WANMGR_IFACE_CONNECTION_IPV6_DOWN:
        {
            pIfaceData->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_DOWN;
            pIfaceData->IP.Ipv6Changed = FALSE;
#ifdef FEATURE_IPOE_HEALTH_CHECK
            pIfaceData->IP.Ipv6Renewed = FALSE;
#endif
            pIfaceData->MAP.MaptStatus = WAN_IFACE_MAPT_STATE_DOWN;     // reset MAPT flag
            pIfaceData->MAP.MaptChanged = FALSE;                        // reset MAPT flag
            strncpy(pIfaceData->IP.Ipv6Data.address, "", sizeof(pIfaceData->IP.Ipv6Data.address));
            strncpy(pIfaceData->IP.Ipv6Data.pdIfAddress, "", sizeof(pIfaceData->IP.Ipv6Data.pdIfAddress));
            strncpy(pIfaceData->IP.Ipv6Data.sitePrefix, "", sizeof(pIfaceData->IP.Ipv6Data.sitePrefix));
            strncpy(pIfaceData->IP.Ipv6Data.nameserver, "", sizeof(pIfaceData->IP.Ipv6Data.nameserver));
            strncpy(pIfaceData->IP.Ipv6Data.nameserver1, "", sizeof(pIfaceData->IP.Ipv6Data.nameserver1));
            wanmgr_sysevents_ipv6Info_init(); // reset the sysvent/syscfg fields
            break;
        }
#ifdef FEATURE_MAPT
        case WANMGR_IFACE_MAPT_START:
        {
            pIfaceData->MAP.MaptStatus = WAN_IFACE_MAPT_STATE_UP;
            CcspTraceInfo(("mapt: %s \n",
                   ((iface_status == WANMGR_IFACE_MAPT_START) ? "UP" : (iface_status == WANMGR_IFACE_MAPT_STOP) ? "DOWN" : "N/A")));

            break;
        }
        case WANMGR_IFACE_MAPT_STOP:
        {
            pIfaceData->MAP.MaptStatus = WAN_IFACE_MAPT_STATE_DOWN;     // reset MAPT flag
            pIfaceData->MAP.MaptChanged = FALSE;                        // reset MAPT flag
            CcspTraceInfo(("mapt: %s \n",
                   ((iface_status == WANMGR_IFACE_MAPT_START) ? "UP" : (iface_status == WANMGR_IFACE_MAPT_STOP) ? "DOWN" : "N/A")));

            break;
        }
#endif
        default:
            /* do nothing */
            break;
    }

    return;
}

static ANSC_STATUS WanMgr_Send_InterfaceRefresh(DML_WAN_IFACE* pInterface)
{
    DML_WAN_IFACE*      pWanIface4Thread = NULL;
    pthread_t           refreshThreadId;
    INT                 iErrorCode = -1;

    if(pInterface == NULL)
    {
         return ANSC_STATUS_FAILURE;
    }

    if((pInterface->Wan.Refresh == TRUE) &&
       (pInterface->Wan.LinkStatus == WAN_IFACE_LINKSTATUS_UP))
    {
        //Allocate memory for interface struct
        pWanIface4Thread = (DML_WAN_IFACE*)malloc(sizeof(DML_WAN_IFACE));
        if( NULL == pWanIface4Thread )
        {
            CcspTraceError(("%s %d Failed to allocate memory\n", __FUNCTION__, __LINE__));
            return ANSC_STATUS_FAILURE;
        }

        //Copy WAN interface structure for thread
        memset( pWanIface4Thread, 0, sizeof(DML_WAN_IFACE));
        memcpy( pWanIface4Thread, pInterface, sizeof(DML_WAN_IFACE) );

        //WAN refresh thread
        iErrorCode = pthread_create( &refreshThreadId, NULL, &WanMgr_RdkBus_WanIfRefreshThread, (void*)pWanIface4Thread );
        if( 0 != iErrorCode )
        {
         CcspTraceError(("%s %d - Failed to start WAN refresh thread EC:%d\n", __FUNCTION__, __LINE__, iErrorCode ));
         return ANSC_STATUS_FAILURE;
        }
    }

    return ANSC_STATUS_SUCCESS;
}

int wan_updateDNS(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl, BOOL addIPv4, BOOL addIPv6)
{

    if ((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        CcspTraceError(("%s %d - Invalid args \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    DEVICE_NETWORKING_MODE deviceMode = pWanIfaceCtrl->DeviceNwMode;
    DML_WAN_IFACE * pInterface = pWanIfaceCtrl->pIfaceData;

    bool valid_dns = FALSE;
    int ret = RETURN_OK;

#if defined(_LG_OFW_)

    char cmd[256];
    char syseventParam[128];

    if (deviceMode == MODEM_MODE)
    {
        // DNS nameserves should not be configured in MODEM mode
        remove(RESOLV_CONF_FILE);
    }

    snprintf(cmd,sizeof(cmd),"resolvconf -d %s.udhcpc",pInterface->Wan.Name);
    system(cmd);

    if (addIPv4)
    {
        char buf[256] = {'\0'};
        char dns[256] = {'\0'};
        char *tok = NULL;
        int idx = 0;

        snprintf(dns, sizeof(dns), "%s", pInterface->IP.Ipv4Data.dnsServer);
        tok = strtok(dns, " ");

        while (tok)
        {
            if (IsValidDnsServer(AF_INET, tok) == RETURN_OK)
            {
                snprintf(syseventParam, sizeof(syseventParam), SYSEVENT_IPV4_WANIFNAME_DNS_INDEX, pInterface->Wan.Name, idx);
                sysevent_set(sysevent_fd, sysevent_token, syseventParam, tok, 0);
                snprintf(syseventParam, sizeof(syseventParam), SYSEVENT_FIELD_IPV4_DNS_INDEX, idx++);
                sysevent_set(sysevent_fd, sysevent_token, syseventParam, tok, 0);

                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s ", tok);

                CcspTraceInfo(("%s %d: adding nameserver %s >> %s\n", __FUNCTION__, __LINE__, tok, RESOLV_CONF_FILE));
                snprintf(cmd,sizeof(cmd),"echo nameserver %s | resolvconf -a %s.udhcpc", tok, pInterface->Wan.Name);
                system(cmd);

                valid_dns = TRUE;
            }

            tok = strtok(NULL, " ");
        }

        if (valid_dns) {
            buf[strlen(buf)-1] = '\0';
            sysevent_set(sysevent_fd, sysevent_token, "wan_dhcp_dns", buf, 0);

            snprintf(buf, sizeof(buf), "%d", idx);
            snprintf(syseventParam, sizeof(syseventParam), SYSEVENT_IPV4_DNS_NUMBER, pInterface->Wan.Name);
            sysevent_set(sysevent_fd, sysevent_token, syseventParam, buf, 0);
        }

        if (strlen(pInterface->IP.Ipv4Data.domainName) > 0)
        {
            // Update domain name in resolv.conf
            CcspTraceInfo(("%s %d: adding domainname  %s >> %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv4Data.domainName, RESOLV_CONF_FILE));
            snprintf(cmd, sizeof(cmd), "echo -n domain '%s' | resolvconf -a %s.udhcpc", pInterface->IP.Ipv4Data.domainName, pInterface->Wan.Name);
            system(cmd);
        }
    }

    if (addIPv6)
    {
        // v6 DNS1
        if(IsValidDnsServer(AF_INET6, pInterface->IP.Ipv6Data.nameserver) == RETURN_OK)
        {
            // v6 DNS1 is valid
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_PRIMARY, pInterface->IP.Ipv6Data.nameserver, 0);
            valid_dns = TRUE;
        }
        else
        {
            // v6 DNS1 is invalid
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_PRIMARY, "", 0);
        }

        // v6 DNS2
        if(IsValidDnsServer(AF_INET6, pInterface->IP.Ipv6Data.nameserver1) == RETURN_OK)
        {
            // v6 DNS2 is valid
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_SECONDARY, pInterface->IP.Ipv6Data.nameserver1, 0);
            valid_dns = TRUE;
        }
        else
        {
            // v6 DNS2 is invalid
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_SECONDARY, "", 0);
        }
    }

    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_DHCP_SERVER_RESTART, NULL, 0);

    if (valid_dns == TRUE)
    {
        CcspTraceInfo(("%s %d - Active domainname servers set!\n", __FUNCTION__,__LINE__));
    }
    else
    {
        CcspTraceInfo(("%s %d - No valid nameserver is available, adding loopback address for nameserver\n", __FUNCTION__,__LINE__));
        snprintf(cmd,sizeof(cmd),"echo nameserver %s | resolvconf -a %s.udhcpc",LOOPBACK,pInterface->Wan.Name);
        system(cmd);
    }

#else

    FILE *fp = NULL;
    char syseventParam[BUFLEN_128]={0};

    char v4nameserver1[BUFLEN_64];
    char v4nameserver2[BUFLEN_64];
    char v6nameserver1[BUFLEN_128];
    char v6nameserver2[BUFLEN_128]; 

    if (deviceMode == GATEWAY_MODE)
    {
        if((fp = fopen(RESOLV_CONF_FILE, "w+")) == NULL)
        {
            CcspTraceError(("%s %d - Open %s error!\n", __FUNCTION__, __LINE__, RESOLV_CONF_FILE));
            return RETURN_ERR;
        }
    }

    // save the current configured nameserver address
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV4_DNS_PRIMARY, v4nameserver1, sizeof(v4nameserver1));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV4_DNS_SECONDARY, v4nameserver2, sizeof(v4nameserver2));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_PRIMARY, v6nameserver1, sizeof(v6nameserver1));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_SECONDARY, v6nameserver2, sizeof(v6nameserver2));

    CcspTraceInfo(("%s %d: v4nameserver1 = %s v4nameserver2 = %s\n", __FUNCTION__, __LINE__, v4nameserver1, v4nameserver2));
    CcspTraceInfo(("%s %d: v6nameserver1 = %s v6nameserver2 = %s\n", __FUNCTION__, __LINE__, v6nameserver1, v6nameserver2));

    if (addIPv4)
    {
        snprintf(syseventParam, sizeof(syseventParam), SYSEVENT_IPV4_WANIFNAME_DNS_PRIMARY, pInterface->Wan.Name);

        // v4 DNS1
        if(IsValidDnsServer(AF_INET, pInterface->IP.Ipv4Data.dnsServer) == RETURN_OK)
        {
            // v4 DNS1 is a valid
            if (fp != NULL)
            {
                // GATEWAY Mode
                CcspTraceInfo(("%s %d: adding nameserver %s >> %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv4Data.dnsServer, RESOLV_CONF_FILE));
                fprintf(fp, "nameserver %s\n", pInterface->IP.Ipv4Data.dnsServer);
            }
            sysevent_set(sysevent_fd, sysevent_token, syseventParam, pInterface->IP.Ipv4Data.dnsServer, 0);
            CcspTraceInfo(("%s %d: new v4 DNS Server = %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv4Data.dnsServer));
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV4_DNS_PRIMARY, pInterface->IP.Ipv4Data.dnsServer, 0);
            valid_dns = TRUE;
        }
        else
        {
            // v4 DNS1 is a invalid
            sysevent_set(sysevent_fd, sysevent_token, syseventParam, "", 0);
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV4_DNS_PRIMARY, "", 0);
        }

        // v4 DNS2
        snprintf(syseventParam, sizeof(syseventParam), SYSEVENT_IPV4_WANIFNAME_DNS_SECONDARY, pInterface->Wan.Name);
        if(IsValidDnsServer(AF_INET, pInterface->IP.Ipv4Data.dnsServer1) == RETURN_OK)
        {
            // v4 DNS2 is a valid
            if (fp != NULL)
            {
                // GATEWAY Mode
                CcspTraceInfo(("%s %d: adding nameserver %s >> %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv4Data.dnsServer1, RESOLV_CONF_FILE));
                fprintf(fp, "nameserver %s\n", pInterface->IP.Ipv4Data.dnsServer1);
            }
            CcspTraceInfo(("%s %d: new v4 DNS Server = %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv4Data.dnsServer1));
            sysevent_set(sysevent_fd, sysevent_token, syseventParam, pInterface->IP.Ipv4Data.dnsServer1, 0);
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV4_DNS_SECONDARY, pInterface->IP.Ipv4Data.dnsServer1, 0);
            if (valid_dns == TRUE)
            {
                snprintf(syseventParam, sizeof(syseventParam), SYSEVENT_IPV4_DNS_NUMBER, pInterface->Wan.Name);
                sysevent_set(sysevent_fd, sysevent_token, syseventParam, SYSEVENT_IPV4_NO_OF_DNS_SUPPORTED, 0);
            }
            valid_dns = TRUE;
        }
        else
        {
            // v4 DNS2 is a invalid
            sysevent_set(sysevent_fd, sysevent_token, syseventParam, "", 0);
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV4_DNS_SECONDARY, "", 0);
        }
    }

    if (addIPv6)
    {
        // v6 DNS1
        if(IsValidDnsServer(AF_INET6, pInterface->IP.Ipv6Data.nameserver) == RETURN_OK)
        {
            // v6 DNS1 is valid
            if (fp != NULL)
            {
                // GATEWAY Mode
                CcspTraceInfo(("%s %d: adding nameserver %s >> %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv6Data.nameserver, RESOLV_CONF_FILE));
                fprintf(fp, "nameserver %s\n", pInterface->IP.Ipv6Data.nameserver);
            }
            CcspTraceInfo(("%s %d: new v6 DNS Server = %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv6Data.nameserver));
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_PRIMARY, pInterface->IP.Ipv6Data.nameserver, 0);
            valid_dns = TRUE;
        }
        else
        {
            // v6 DNS1 is invalid
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_PRIMARY, "", 0);
        }

        // v6 DNS2
        if(IsValidDnsServer(AF_INET6, pInterface->IP.Ipv6Data.nameserver1) == RETURN_OK)
        {
            // v6 DNS2 is valid
            if (fp != NULL)
            {
                CcspTraceInfo(("%s %d: adding nameserver %s >> %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv6Data.nameserver1, RESOLV_CONF_FILE));
                fprintf(fp, "nameserver %s\n", pInterface->IP.Ipv6Data.nameserver1);
            }
            CcspTraceInfo(("%s %d: new v6 DNS Server = %s\n", __FUNCTION__, __LINE__, pInterface->IP.Ipv6Data.nameserver1));
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_SECONDARY, pInterface->IP.Ipv6Data.nameserver1, 0);
            valid_dns = TRUE;
        }
        else
        {
            // v6 DNS2 is invalid
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_DNS_SECONDARY, "", 0);
        }
    }

    if ( strcmp(pInterface->IP.Ipv4Data.dnsServer, v4nameserver1) || strcmp(pInterface->IP.Ipv4Data.dnsServer1, v4nameserver2)
        || strcmp(pInterface->IP.Ipv6Data.nameserver, v6nameserver1) || strcmp (pInterface->IP.Ipv6Data.nameserver1, v6nameserver2))
    {
        // new and curr nameservers are differen, so apply configuration
        CcspTraceInfo(("%s %d: Setting %s\n", __FUNCTION__, __LINE__, SYSEVENT_DHCP_SERVER_RESTART));
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_DHCP_SERVER_RESTART, NULL, 0);
    }
    else
    {
        CcspTraceInfo(("%s %d: No change not Setting %s\n", __FUNCTION__, __LINE__, SYSEVENT_DHCP_SERVER_RESTART));
    }

    if (valid_dns == TRUE)
    {
        CcspTraceInfo(("%s %d - Active domainname servers set!\n", __FUNCTION__,__LINE__));
    }
    else
    {
        CcspTraceError(("%s %d - No valid nameserver is available, adding loopback address for nameserver\n", __FUNCTION__,__LINE__));
        if (fp != NULL)
        {
            fprintf(fp, "nameserver %s \n", LOOPBACK);
        }
    }

    if (fp != NULL)
    {
        fclose(fp);
    }

#endif

    return ret;
}

static int checkIpv6LanAddressIsReadyToUse()
{
    char buffer[BUFLEN_256] = {0};
    FILE *fp_dad   = NULL;
    FILE *fp_route = NULL;
    int address_flag   = 0;
    int dad_flag       = 0;
    int route_flag     = 0;
    struct ifaddrs *ifap = NULL;
    struct ifaddrs *ifa  = NULL;
    char addr[INET6_ADDRSTRLEN] = {0};
    int i;

    /* We need to check the interface has got an IPV6-prefix , beacuse P-and-M can send
    the same event when interface is down, so we ensure send the UP event only
    when interface has an IPV6-prefix.
    */
    if (!getifaddrs(&ifap)) {
        for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if(strncmp(ifa->ifa_name,ETH_BRIDGE_NAME, strlen(ETH_BRIDGE_NAME)))
                continue;
            if (ifa->ifa_addr->sa_family != AF_INET6)
                continue;
            getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6), addr,
                    sizeof(addr), NULL, 0, NI_NUMERICHOST);
            if((strncmp(addr + (strlen(addr) - 3), "::1", 3) == 0)){
                address_flag = 1;
                break;
            }
        }//for loop
        freeifaddrs(ifap);
    }//getifaddr close

    if(address_flag == 0) {
        CcspTraceError(("%s %d address_flag Failed\n", __FUNCTION__, __LINE__));
        return -1;
    }
    /* Check Duplicate Address Detection (DAD) status. The way it works is that
       after an address is added to an interface, the operating system uses the
       Neighbor Discovery Protocol to check if any other host on the network
       has the same address. The whole process will take around 3 to 4 seconds
       to complete. Also we need to check and ensure that the gateway has
       a valid default route entry.
    */
    for(i=0; i<15; i++) {
        buffer[0] = '\0';
        if(dad_flag == 0) {
            if ((fp_dad = popen("ip address show dev brlan0 tentative", "r"))) {
                if(fp_dad != NULL) {
                    fgets(buffer, BUFLEN_256, fp_dad);
                    if(strlen(buffer) == 0 ) {
                        dad_flag = 1;
                    }
                    pclose(fp_dad);
                }
            }
        }

        if(route_flag == 0) {
            buffer[0] = '\0';
            if ((fp_route = popen("ip -6 ro | grep default", "r"))) {
                if(fp_route != NULL) {
                    fgets(buffer, BUFLEN_256, fp_route);
                    if(strlen(buffer) > 0 ) {
                        route_flag = 1;
                    }
                    pclose(fp_route);
                }
            }
        }

        if(dad_flag == 0 || route_flag == 0) {
            sleep(1);
        }
        else {
            break;
       }
    }

    if(dad_flag == 0 || route_flag == 0) {
        CcspTraceError(("%s %d dad_flag[%d] route_flag[%d] Failed \n", __FUNCTION__, __LINE__,dad_flag,route_flag));
        return -1;
    }

    return 0;
}

static int checkIpv6AddressAssignedToBridge()
{
    char lanPrefix[BUFLEN_128] = {0};
    int ret = RETURN_ERR;

    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_GLOBAL_IPV6_PREFIX_SET, lanPrefix, sizeof(lanPrefix));

    if(strlen(lanPrefix) > 0)
    {
        CcspTraceInfo(("%s %d lanPrefix[%s] \n", __FUNCTION__, __LINE__,lanPrefix));
        if (checkIpv6LanAddressIsReadyToUse() == 0)
        {
            ret = RETURN_OK;
        }
    }

    return ret;
}

static int wan_setUpIPv4(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl)
{

    if ((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        CcspTraceError(("%s %d - Invalid args \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    int ret = RETURN_OK;
    char cmdStr[BUFLEN_128 + IP_ADDR_LENGTH] = {0};
    char bCastStr[IP_ADDR_LENGTH] = {0};
    char line[BUFLEN_64] = {0};
    char buf[BUFLEN_32] = {0};
    char *cp = NULL;
    FILE *fp = NULL;


    DEVICE_NETWORKING_MODE DeviceNwMode = pWanIfaceCtrl->DeviceNwMode;
    DML_WAN_IFACE * pInterface = pWanIfaceCtrl->pIfaceData;

    /** Setup IPv4: such as
     * "ifconfig eth0 10.6.33.165 netmask 255.255.255.192 broadcast 10.6.33.191 up"
     */
     if (wanmgr_set_Ipv4Sysevent(&pInterface->IP.Ipv4Data, pWanIfaceCtrl->DeviceNwMode) != ANSC_STATUS_SUCCESS)
     {
         CcspTraceError(("%s %d - Could not store ipv4 data!", __FUNCTION__, __LINE__));
     }

    if (WanManager_GetBCastFromIpSubnetMask(pInterface->IP.Ipv4Data.ip, pInterface->IP.Ipv4Data.mask, bCastStr) != RETURN_OK)
    {
        CcspTraceError((" %s %d - bad address %s/%s \n",__FUNCTION__,__LINE__, pInterface->IP.Ipv4Data.ip, pInterface->IP.Ipv4Data.mask));
        return RETURN_ERR;
    }

    snprintf(cmdStr, sizeof(cmdStr), "ifconfig %s %s netmask %s broadcast %s mtu %u",
             pInterface->IP.Ipv4Data.ifname, pInterface->IP.Ipv4Data.ip, pInterface->IP.Ipv4Data.mask, bCastStr, pInterface->IP.Ipv4Data.mtuSize);
    CcspTraceInfo(("%s %d -  IP configuration = %s \n", __FUNCTION__, __LINE__, cmdStr));
    WanManager_DoSystemAction("setupIPv4:", cmdStr);

    snprintf(cmdStr, sizeof(cmdStr), "sendarp -s %s -d %s", ETH_BRIDGE_NAME, ETH_BRIDGE_NAME);
    WanManager_DoSystemAction("setupIPv4", cmdStr);

    /** Need to manually add route if the connection is PPP connection*/
    if (pInterface->PPP.Enable == TRUE)
    {
        if (WanManager_AddGatewayRoute(&pInterface->IP.Ipv4Data) != RETURN_OK)
        {
            CcspTraceError(("%s %d - Failed to set up system gateway", __FUNCTION__, __LINE__));
        }
    }

    /** configure DNS */
    if (RETURN_OK != wan_updateDNS(pWanIfaceCtrl, TRUE, (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)))
    {
        CcspTraceError(("%s %d - Failed to configure IPv4 DNS servers \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }
    else
    {
        CcspTraceInfo(("%s %d -  IPv4 DNS servers configures successfully \n", __FUNCTION__, __LINE__));
    }

        /** Set default gatway. */
    if (WanManager_AddDefaultGatewayRoute(DeviceNwMode, &pInterface->IP.Ipv4Data) != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to set up default system gateway", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }

    /** Update required sysevents. */

    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_CONNECTION_STATE, WAN_STATUS_UP, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_IPV4_LINK_STATE, WAN_STATUS_UP, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_IPADDR, pInterface->IP.Ipv4Data.ip, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_SUBNET, pInterface->IP.Ipv4Data.mask, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_STATE, WAN_STATUS_UP, 0);
    if ((fp = fopen("/proc/uptime", "rb")) == NULL)
    {
        return RETURN_ERR;
    }
    if (fgets(line, sizeof(line), fp) != NULL)
    {
        if ((cp = strchr(line, ',')) != NULL)
            *cp = '\0';
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_START_TIME, line, 0);
    }
    fclose(fp);

    if (strstr(pInterface->Phy.Path, "Ethernet"))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_ETHWAN_INITIALIZED, "1", 0);
    }
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, buf, sizeof(buf));
    if (strcmp(buf, WAN_STATUS_STARTED))
    {
        int  uptime = 0;
        char buffer[64] = {0};

        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_START, "", 0);
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, WAN_STATUS_STARTED, 0);
        CcspTraceInfo(("%s %d - wan-status event set to started \n", __FUNCTION__, __LINE__));

        if(pInterface->IP.Ipv4Data.ifname[0] != '\0')
        {
            syscfg_set_string(SYSCFG_WAN_INTERFACE_NAME, pInterface->IP.Ipv4Data.ifname);
        }
        wanmgr_services_restart();
        //Get WAN uptime
        WanManager_GetDateAndUptime( buffer, &uptime );
        LOG_CONSOLE("%s [tid=%ld] v4: Wan_init_complete for interface index %d at %d\n", buffer, syscall(SYS_gettid), pWanIfaceCtrl->interfaceIdx, uptime);

        system("print_uptime \"Waninit_complete\"");
        system("print_uptime \"boot_to_wan_uptime\"");
    }

    /* Firewall restart. */
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
    return ret;
}


static int wan_tearDownIPv4(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl)
{
    if ((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        CcspTraceError(("%s %d - Invalid args \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    int ret = RETURN_OK;
    char cmdStr[BUFLEN_64] = {0};
    char buf[BUFLEN_32] = {0};

    DEVICE_NETWORKING_MODE DeviceNwMode = pWanIfaceCtrl->DeviceNwMode;
    DML_WAN_IFACE * pInterface = pWanIfaceCtrl->pIfaceData;

        /** Reset IPv4 DNS configuration. */
    if (RETURN_OK != wan_updateDNS(pWanIfaceCtrl, FALSE, (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)))
    {
        CcspTraceError(("%s %d - Failed to unconfig IPv4 DNS servers \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }
    else
    {
        CcspTraceInfo(("%s %d -  IPv4 DNS servers unconfig successfully \n", __FUNCTION__, __LINE__));
    }

    /* Need to remove the network from the routing table by
    * doing "ifconfig L3IfName 0.0.0.0"
    * wanData->ipv4Data.ifname is Empty.
    */
    snprintf(cmdStr, sizeof(cmdStr), "ifconfig %s 0.0.0.0", pInterface->Wan.Name);
    if (WanManager_DoSystemActionWithStatus("wan_tearDownIPv4: ifconfig L3IfName 0.0.0.0", (cmdStr)) != 0)
    {
        CcspTraceError(("%s %d - failed to run cmd: %s", __FUNCTION__, __LINE__, cmdStr));
        ret = RETURN_ERR;
    }

    if (WanManager_DelDefaultGatewayRoute(DeviceNwMode, pWanIfaceCtrl->DeviceNwModeChanged, &pInterface->IP.Ipv4Data) != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to Del default system gateway", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }

    /* ReSet the required sysevents. */
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV4_CONNECTION_STATE, WAN_STATUS_DOWN, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_IPV4_LINK_STATE, WAN_STATUS_DOWN, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_STATE, WAN_STATUS_DOWN, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_START_TIME, "0", 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_IPADDR, "0.0.0.0", 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_SUBNET, "255.255.255.0", 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
    if (strstr(pInterface->Phy.Path, "Ethernet"))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_ETHWAN_INITIALIZED, "0", 0);
    }

    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, buf, sizeof(buf));
    if ((strcmp(buf, WAN_STATUS_STOPPED) != 0) && (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_DOWN))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, WAN_STATUS_STOPPED, 0);
        CcspTraceInfo(("%s %d - wan-status event set to stopped \n", __FUNCTION__, __LINE__));
    }

    return ret;
}

static int wan_setUpIPv6(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl)
{

    if ((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        CcspTraceError(("%s %d - Invalid args \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    int ret = RETURN_OK;
    char buf[BUFLEN_32] = {0};

    DML_WAN_IFACE * pInterface = pWanIfaceCtrl->pIfaceData;

    if (pInterface == NULL)
    {
        CcspTraceError(("%s %d - Invalid memory \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    /** Reset IPv6 DNS configuration. */
    if (wan_updateDNS(pWanIfaceCtrl, (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP), TRUE) != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to configure IPv6 DNS servers \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }
    else
    {
        CcspTraceInfo(("%s %d -  IPv6 DNS servers configured successfully \n", __FUNCTION__, __LINE__));
    }

    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV6_CONNECTION_STATE, WAN_STATUS_UP, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_RADVD_RESTART, NULL, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_DHCP_SERVER_RESTART, NULL, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);

    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, buf, sizeof(buf));
    if (strcmp(buf, WAN_STATUS_STARTED))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_START, "", 0);
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, WAN_STATUS_STARTED, 0);
        CcspTraceInfo(("%s %d - wan-status event set to started \n", __FUNCTION__, __LINE__));

        int  uptime = 0;
        char buffer[64] = {0};

        //Get WAN uptime
        WanManager_GetDateAndUptime( buffer, &uptime );
        LOG_CONSOLE("%s [tid=%ld] v6: Wan_init_complete for interface index %d at %d\n", buffer, syscall(SYS_gettid), pWanIfaceCtrl->interfaceIdx, uptime);

        system("print_uptime \"Waninit_complete\"");
        system("print_uptime \"boot_to_wan_uptime\"");

        /* Set the current WAN Interface name */
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_IFNAME, pInterface->IP.Ipv6Data.ifname, 0);
        if(pInterface->IP.Ipv6Data.ifname[0] != '\0')
        {
            syscfg_set_string(SYSCFG_WAN_INTERFACE_NAME, pInterface->IP.Ipv6Data.ifname);
        }
        wanmgr_services_restart();
    }

    return ret;
}

static int wan_tearDownIPv6(WanMgr_IfaceSM_Controller_t * pWanIfaceCtrl)
{

    if ((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        CcspTraceError(("%s %d - Invalid args \n", __FUNCTION__, __LINE__));
        return RETURN_ERR;
    }

    int ret = RETURN_OK;
    char buf[BUFLEN_32] = {0};

    DML_WAN_IFACE * pInterface = pWanIfaceCtrl->pIfaceData;

    /** Reset IPv6 DNS configuration. */
    if (RETURN_OK == wan_updateDNS(pWanIfaceCtrl, (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP), FALSE))
    {
        CcspTraceInfo(("%s %d -  IPv6 DNS servers unconfig successfully \n", __FUNCTION__, __LINE__));
    }
    else
    {
        CcspTraceError(("%s %d - Failed to unconfig IPv6 DNS servers \n", __FUNCTION__, __LINE__));
        ret = RETURN_ERR;
    }

    /** Unconfig IPv6. */
    if ( WanManager_Ipv6AddrUtil(ETH_BRIDGE_NAME,DEL_ADDR,0,0) < 0)
    {
        AnscTraceError(("%s %d -  Failed to remove inactive address \n", __FUNCTION__,__LINE__));
    }

    // Reset sysvevents.
    char previousPrefix[BUFLEN_48] = {0};
    char previousPrefix_vldtime[BUFLEN_48] = {0};
    char previousPrefix_prdtime[BUFLEN_48] = {0};
    /* set ipv6 down sysevent notification. */
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_PREFIX, previousPrefix, sizeof(previousPrefix));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_PREFIXVLTIME, previousPrefix_vldtime, sizeof(previousPrefix_vldtime));
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_PREFIXPLTIME, previousPrefix_prdtime, sizeof(previousPrefix_prdtime));
    if (strncmp(previousPrefix, "", BUFLEN_48) != 0)
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_PREVIOUS_IPV6_PREFIX, previousPrefix, 0);
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_PREVIOUS_IPV6_PREFIXVLTIME, previousPrefix_vldtime, 0);
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_PREVIOUS_IPV6_PREFIXPLTIME, previousPrefix_prdtime, 0);
    }
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_IPV6_PREFIX, "", 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIELD_TR_EROUTER_DHCPV6_CLIENT_PREFIX, "", 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_IPV6_CONNECTION_STATE, WAN_STATUS_DOWN, 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_GLOBAL_IPV6_PREFIX_SET, "", 0);
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);

    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, buf, sizeof(buf));
    if ((strcmp(buf, WAN_STATUS_STOPPED) != 0) && (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_DOWN))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_STATUS, WAN_STATUS_STOPPED, 0);
        CcspTraceInfo(("%s %d - wan-status event set to stopped \n", __FUNCTION__, __LINE__));
    }

    return ret;
}

#ifdef FEATURE_IPOE_HEALTH_CHECK
static ANSC_STATUS WanMgr_IfaceSM_IHC_Init(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    CcspTraceInfo(("[%s:%d] Init IHC details in Intf StateMachine data\n", __FUNCTION__, __LINE__));
    if (pWanIfaceCtrl == NULL)
    {
        CcspTraceError(("%s %d - Invalid args\n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }
    pWanIfaceCtrl->IhcPid = 0;
    pWanIfaceCtrl->IhcV4Status = IHC_STOPPED;
    pWanIfaceCtrl->IhcV6Status = IHC_STOPPED;
    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS WanManager_StopIHC(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if (pWanIfaceCtrl == NULL)
    {
        CcspTraceError(("%s %d - invalid args \n", __FUNCTION__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }
    CcspTraceInfo(("[%s:%d] Stopping IHC App\n", __FUNCTION__, __LINE__));
    if (WanManager_StopIpoeHealthCheckService(pWanIfaceCtrl->IhcPid) == ANSC_STATUS_FAILURE)
    {
        CcspTraceError(("%s %d - Failed to kill IHC process interface %s \n", __FUNCTION__, __LINE__, pWanIfaceCtrl->pIfaceData->Wan.Name));
        return ANSC_STATUS_FAILURE;
    }
    WanMgr_IfaceSM_IHC_Init(pWanIfaceCtrl);
    return ANSC_STATUS_SUCCESS;
}
#endif


/* WanManager_ClearDHCPData */
/* This function must be used only with the mutex locked */
static ANSC_STATUS WanManager_ClearDHCPData(DML_WAN_IFACE* pInterface)
{
    if(pInterface == NULL)
    {
        return ANSC_STATUS_FAILURE;
    }

    memset(pInterface->IP.Path, 0, sizeof(pInterface->IP.Path));

    /* DHCPv4 client */
    pInterface->IP.Ipv4Status = WAN_IFACE_IPV4_STATE_DOWN;
    pInterface->IP.Ipv4Changed = FALSE;
#ifdef FEATURE_IPOE_HEALTH_CHECK
    pInterface->IP.Ipv4Renewed = FALSE;
#endif
    memset(&(pInterface->IP.Ipv4Data), 0, sizeof(WANMGR_IPV4_DATA));
    pInterface->IP.Dhcp4cPid = 0;
    if(pInterface->IP.pIpcIpv4Data != NULL)
    {
        free(pInterface->IP.pIpcIpv4Data);
        pInterface->IP.pIpcIpv4Data = NULL;
    }

    /* DHCPv6 client */
    pInterface->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_DOWN;
    pInterface->IP.Ipv6Changed = FALSE;
#ifdef FEATURE_IPOE_HEALTH_CHECK
    pInterface->IP.Ipv6Renewed = FALSE;
#endif
    memset(&(pInterface->IP.Ipv6Data), 0, sizeof(WANMGR_IPV6_DATA));
    pInterface->IP.Dhcp6cPid = 0;
    if(pInterface->IP.pIpcIpv6Data != NULL)
    {
        free(pInterface->IP.pIpcIpv6Data);
        pInterface->IP.pIpcIpv6Data = NULL;
    }

    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/************************** TRANSITIONS ******************************************/
/*********************************************************************************/
static eWanState_t wan_transition_start(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    int  uptime = 0;
    char buffer[64] = {0};

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    pInterface->IP.Ipv4Status = WAN_IFACE_IPV4_STATE_DOWN;
    pInterface->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_DOWN;
    pInterface->MAP.MaptStatus = WAN_IFACE_MAPT_STATE_DOWN;
    pInterface->DSLite.Status = WAN_IFACE_DSLITE_STATE_DOWN;

    pInterface->Wan.Status = WAN_IFACE_STATUS_INITIALISING;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    /*TODO: Upstream should not be set for Remote Interface, for More info, refer RDKB-42676*/
    if (pInterface->Wan.IfaceType != REMOTE_IFACE)
    {
        if( pInterface->Wan.LinkStatus == WAN_IFACE_LINKSTATUS_DOWN )
            pInterface->Wan.LinkStatus = WAN_IFACE_LINKSTATUS_CONFIGURING;
        if (WanMgr_RdkBus_updateInterfaceUpstreamFlag(pInterface->Phy.Path, TRUE) != ANSC_STATUS_SUCCESS)
        {
            CcspTraceError(("%s - Failed to set Upstream data model, exiting interface state machine\n", __FUNCTION__));
            return WAN_STATE_EXIT;
        }
        //Update Link status if wanmanager restarted.
        WanMgr_RestartGetLinkStatus(pInterface);
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION START\n", __FUNCTION__, __LINE__, pInterface->Name));

    WanMgr_Set_ISM_RunningStatus(pInterface->uiIfaceIdx, TRUE);

    WanManager_GetDateAndUptime( buffer, &uptime );
    LOG_CONSOLE("%s [tid=%ld] Wan_init_start:%d\n", buffer, syscall(SYS_gettid), uptime);

    system("print_uptime \"Waninit_start\"");

    /* TODO: Need to handle crash recovery */
    return WAN_STATE_CONFIGURING_WAN;
}

static eWanState_t wan_transition_physical_interface_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;


#ifdef FEATURE_MAPT
    if(pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_UP)
    {
        wan_transition_mapt_down(pWanIfaceCtrl);
    }
#endif

    if(pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
    {
        wan_transition_ipv6_down(pWanIfaceCtrl);
    }

    if(pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
    {
        wan_transition_ipv4_down(pWanIfaceCtrl);
    }

    if (pInterface->PPP.Enable == TRUE)
    {
        /* Stops DHCPv6 client */
        WanManager_StopDhcpv6Client(pInterface->Wan.Name); // release dhcp lease
        pInterface->IP.Dhcp6cPid = 0;

        /* Delete PPP session */
        WanManager_DeletePPPSession(pInterface);
    }
    else if(pInterface->Wan.EnableDHCP == TRUE)
    {
        /* Stops DHCPv4 client */
        if (pInterface->IP.Dhcp4cPid > 0)
        {
            CcspTraceInfo(("%s %d: Stopping DHCP v4\n", __FUNCTION__, __LINE__));
            WanManager_StopDhcpv4Client(pInterface->Wan.Name, TRUE); // release dhcp lease
            pInterface->IP.Dhcp4cPid = 0;
        }

        /* Stops DHCPv6 client */
        if (pInterface->IP.Dhcp6cPid > 0)
        {
            CcspTraceInfo(("%s %d: Stopping DHCP v6\n", __FUNCTION__, __LINE__));
            WanManager_StopDhcpv6Client(pInterface->Wan.Name); // release dhcp lease
            pInterface->IP.Dhcp6cPid = 0;
        }

#ifdef FEATURE_IPOE_HEALTH_CHECK
        if (pWanIfaceCtrl->IhcPid > 0)
        {
            WanManager_StopIHC(pWanIfaceCtrl);
        }
#endif  // FEATURE_IPOE_HEALTH_CHECK
    }

    if (pInterface->Wan.IfaceType != REMOTE_IFACE)
    {
        //Check Upstream value
        char dmQuery[BUFLEN_256] = {0};
        char dmValue[BUFLEN_256] = {0};
        char *upstreamsuffix;

        if (strstr(pInterface->Phy.Path, "CableModem") != NULL)
            upstreamsuffix = CMAGENT_UPSTREAM_NAME;
        else
            upstreamsuffix = UPSTREAM_DM_SUFFIX;

        snprintf(dmQuery, sizeof(dmQuery), "%s%s", pInterface->Phy.Path, upstreamsuffix);

        if ( ANSC_STATUS_FAILURE == WanMgr_RdkBus_GetParamValueFromAnyComp (dmQuery, dmValue))
        {
            CcspTraceError(("%s-%d: %s, Failed to get param value\n", __FUNCTION__, __LINE__, dmQuery));
        }

        if(strncmp(dmValue, "false", sizeof(dmValue)))
        {
            CcspTraceInfo(("%s %d: Sending Upstream disbale to Interface %s\n", __FUNCTION__, __LINE__, pInterface->Name));
            WanMgr_RdkBus_updateInterfaceUpstreamFlag(pInterface->Phy.Path, FALSE);
        }
        else
        {
            // Upstream is already false. WanInterface component could be restarted and not in current. Delete VLAN link and change the state.
            if (pInterface->PPP.Enable != TRUE && (strstr(pInterface->Phy.Path, "Cellular") == NULL))
            {
                CcspTraceWarning(("%s %d: Upstream is already disabled. Delete VLAN link %s\n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                WanMgr_RdkBusDeleteVlanLink(pInterface);
            }
            pInterface->Wan.LinkStatus = WAN_IFACE_LINKSTATUS_DOWN;
        }
    }

    /* VLAN link is not created yet if LinkStatus is CONFIGURING. Change it to down. */
    if( pInterface->Wan.LinkStatus == WAN_IFACE_LINKSTATUS_CONFIGURING )
    {
        CcspTraceInfo(("%s %d: LinkStatus is still CONFIGURING. Set to down\n", __FUNCTION__, __LINE__));
        pInterface->Wan.LinkStatus = WAN_IFACE_LINKSTATUS_DOWN;
    }
    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION DECONFIGURING WAN\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_DECONFIGURING_WAN;
}

static eWanState_t wan_transition_wan_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;


    pInterface->Wan.Status = WAN_IFACE_STATUS_VALIDATING;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION VALIDATING WAN\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_VALIDATING_WAN;
}

static void stopDhcpClients(DML_WAN_IFACE *pInterface)
{
    char buf[8];
    syscfg_get(NULL, "last_erouter_mode", buf, sizeof(buf));

#ifdef _LG_MV2_PLUS_
    if (atoi(buf) != 2 && atoi(buf) != 0) // Dont try to stop DHCPv4 client on IPv6 only mode and bridge mode
#else
    if (atoi(buf) != 2) // Dont try to stop DHCPv4 client on IPv6 only
#endif
    {
        if (WanManager_StopDhcpv4Client(pInterface, TRUE) == ANSC_STATUS_SUCCESS)
        {
            CcspTraceInfo(("DHCP v4 client on interface %s was successfully terminated \n", pInterface->Wan.Name));
        }
    }

#ifdef _LG_MV2_PLUS_
    if (atoi(buf) != 1 && atoi(buf) != 0) // Dont try to stop DHCPv6 client on IPv4 only mode and bridge mode
#else
    if (atoi(buf) != 1) // Dont try to stop DHCPv6 client on IPv4 only mode
#endif
    {
        if (WanManager_StopDhcpv6Client(pInterface) == ANSC_STATUS_SUCCESS)
        {
            CcspTraceInfo(("DHCP v6 client on interface %s was successfully terminated \n", pInterface->Wan.Name));
        }
    }
}

static void startDhcpClients (DML_WAN_IFACE *pInterface)
{
    char buf[8];
    int erouter_mode;

    syscfg_get(NULL, "last_erouter_mode", buf, sizeof(buf));
    erouter_mode = atoi(buf);

#ifdef _LG_MV2_PLUS_
    // Only Request the erouter0 IP in router mode. Don't request the erouter0 IP in bridge mode.
    if (erouter_mode != 2 && erouter_mode != 0) // Dont run udhcpc in IPV6 only mode and bridge mode
#else
    // For now request both the IP in bridge mode. Need clarification in requirements for bridgemode erouter0 IP
    if (erouter_mode != 2) // Dont run udhcpc in IPV6 only
#endif
    {
        /* Start DHCPv4 client */
        pInterface->IP.Dhcp4cPid = WanManager_StartDhcpv4Client(pInterface->Wan.Name);
        CcspTraceInfo(("%s %d - Started dhcpc on interface %s, dhcpv4_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp4cPid));
    }

#ifdef _LG_MV2_PLUS_
    if (erouter_mode != 1 && erouter_mode != 0) // Dont run dibbler in IPv4 only mode and bridge mode
#else
    if (erouter_mode != 1) // Dont run dibbler in IPv4 only
#endif
    {
        /* Start DHCPv6 Client */
        pInterface->IP.Dhcp6cPid = WanManager_StartDhcpv6Client(pInterface->Wan.Name);
        CcspTraceInfo(("%s %d - Started dhcpv6 client on interface %s, dhcpv6_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp6cPid));
    }
}

static eWanState_t wan_transition_wan_validated(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    /* Clear DHCP data */
    WanManager_ClearDHCPData(pInterface);

    if( pInterface->PPP.Enable == TRUE )
    {
        if(WanMgr_RestartUpdatePPPinfo(pInterface) == TRUE)
        {
            CcspTraceInfo(("%s %d - Interface '%s' - Already PPP session is running. \n", __FUNCTION__, __LINE__, pInterface->Name));
        }
        else
        {
            WanManager_CreatePPPSession(pInterface);
        }
    }
    else if (pInterface->Wan.EnableDHCP == TRUE)
    {
        // DHCPv4v6 is enabled

        // Check if we have any running DHCP v4/v6 clients to be stopped
        stopDhcpClients(pInterface);
        startDhcpClients(pInterface);

    }else if(pInterface->Wan.EnableDHCP == FALSE)
    {
        if(strstr(pInterface->Phy.Path, "Cellular") != NULL)
        {
            WanMgr_UpdateIpFromCellularMgr(pInterface->Wan.Name);
        }
    }


    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION OBTAINING IP ADDRESSES\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_OBTAINING_IP_ADDRESSES;
}

static eWanState_t wan_transition_refreshing_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if(pInterface->PPP.Enable == TRUE)
    {
        if(!(pInterface->PPP.IPCPStatus == WAN_IFACE_IPCP_STATUS_UP &&
                    pInterface->PPP.IPV6CPStatus == WAN_IFACE_IPV6CP_STATUS_UP ))
        {
            return WAN_STATE_REFRESHING_WAN;
        }
        /* Stops DHCPv6 client */
        WanManager_StopDhcpv6Client(pInterface->Wan.Name); // release dhcp lease
        pInterface->IP.Dhcp6cPid = 0;

        /* Delete PPP session */
        WanManager_DeletePPPSession(pInterface);
    }
    else if (pInterface->Wan.EnableDHCP == TRUE)
    {
        /* Stops DHCPv4 client */
        WanManager_StopDhcpv4Client(pInterface->Wan.Name, FALSE); // no release dhcp lease
        pInterface->IP.Dhcp4cPid = 0;

        /* Stops DHCPv6 client */
        WanManager_StopDhcpv6Client(pInterface->Wan.Name); // release dhcp lease
        pInterface->IP.Dhcp6cPid = 0;

    /* Sets Ethernet.Link.{i}.X_RDK_Refresh to TRUE in VLAN & Bridging Manager
       in order to refresh the WAN link */
    if(WanMgr_Send_InterfaceRefresh(pInterface) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("%s %d - Interface '%s' - Sending Refresh message failed\n", __FUNCTION__, __LINE__, pInterface->Name));
    }

    if (pInterface->Wan.IfaceType != REMOTE_IFACE)
        pInterface->Wan.LinkStatus = WAN_IFACE_LINKSTATUS_CONFIGURING;
    }
    pInterface->Wan.Refresh = FALSE;

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION REFRESHING WAN\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_REFRESHING_WAN;
}

static eWanState_t wan_transition_wan_refreshed(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    /* Clear DHCP data */
    WanManager_ClearDHCPData(pInterface);

    if( pInterface->PPP.Enable == TRUE )
    {
        WanManager_CreatePPPSession(pInterface);
    }
    else if ( pInterface->Wan.EnableDHCP == TRUE )
    {
        startDhcpClients(pInterface);
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION OBTAINING IP ADDRESSES\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_OBTAINING_IP_ADDRESSES;
}

static eWanState_t wan_transition_ipv4_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    ANSC_STATUS ret;
    char buf[BUFLEN_128] = {0};

    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    /* successfully got the v4 lease from the interface, so lets mark it validated */
    pInterface->Wan.Status = WAN_IFACE_STATUS_UP;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    /* Configure IPv4. */
    ret = wan_setUpIPv4(pWanIfaceCtrl);
    if (ret != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to configure IPv4 successfully \n", __FUNCTION__, __LINE__));
    }

#ifdef FEATURE_IPOE_HEALTH_CHECK
        if (pInterface->PPP.Enable == FALSE)
        {
            if ((pInterface->Wan.EnableIPoE == TRUE) && (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STOPPED))
            {
                WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
                pWanIfaceCtrl->IhcV4Status = IHC_STARTED;
            }
        }
#endif
    /* Force reset ipv4 state global flag. */
    pInterface->IP.Ipv4Changed = FALSE;
#ifdef FEATURE_IPOE_HEALTH_CHECK
    pInterface->IP.Ipv4Renewed = FALSE;
#endif

    Update_Interface_Status();
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_SERVICE_STATUS, buf, sizeof(buf));
    if (strcmp(buf, WAN_STATUS_STARTED))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_SERVICE_STATUS, WAN_STATUS_STARTED, 0);
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
    }

    memset(buf, 0, BUFLEN_128);
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_IPV6_CONNECTION_STATE, buf, sizeof(buf));

    if(pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP && !strcmp(buf, WAN_STATUS_UP))
    {
        CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION DUAL STACK ACTIVE\n", __FUNCTION__, __LINE__, pInterface->Name));
        return WAN_STATE_DUAL_STACK_ACTIVE;
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION IPV4 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_IPV4_LEASED;
}

static eWanState_t wan_transition_ipv4_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    char buf[BUFLEN_128] = {0};

    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP ||
        pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
        ((pInterface->Wan.RefreshDHCP == TRUE) && (pInterface->Wan.EnableDHCP == FALSE)))
    {
        // Stopping DHCPv4 client, so we can send a unicast DHCP Release packet
        CcspTraceInfo(("%s %d: Stopping DHCP v4\n", __FUNCTION__, __LINE__));
        WanManager_StopDhcpv4Client(pInterface->Wan.Name, TRUE);
        pInterface->IP.Dhcp4cPid = 0;
    }
    else
    {
        /* Collect if any zombie process. */
        WanManager_DoCollectApp(DHCPV4_CLIENT_NAME);

        // start DHCPv4 client if it is not running, MAP-T not configured and PPP Disable scenario.
        if ((WanManager_IsApplicationRunning(DHCPV4_CLIENT_NAME) != TRUE) && (pInterface->PPP.Enable == FALSE) &&
            (!(pInterface->Wan.EnableMAPT == TRUE && (pInterface->SelectionStatus == WAN_IFACE_ACTIVE) && 
            (pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_UP))))
        {
            pInterface->IP.Dhcp4cPid = WanManager_StartDhcpv4Client(pInterface->Wan.Name);
            CcspTraceInfo(("%s %d - SELFHEAL - Started dhcpc on interface %s, dhcpv4_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp4cPid));
        }
    }
    WanManager_UpdateInterfaceStatus (pInterface, WANMGR_IFACE_CONNECTION_DOWN);

    if (wan_tearDownIPv4(pWanIfaceCtrl) != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to tear down IPv4 for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
    }

#ifdef FEATURE_IPOE_HEALTH_CHECK
        if (pInterface->PPP.Enable == FALSE)
        {
            if((pInterface->Wan.EnableIPoE) && (pWanIfaceCtrl->IhcPid > 0))
            {
                WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_DOWN, pInterface->Wan.Name);
                pWanIfaceCtrl->IhcV4Status = IHC_STOPPED;
            }
        }
#endif

    wanmgr_sysevents_ipv4Info_init(pInterface->Wan.Name, pWanIfaceCtrl->DeviceNwMode); // reset the sysvent/syscfg fields

    Update_Interface_Status();
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_IPV6_CONNECTION_STATE, buf, sizeof(buf));

    if(pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP && !strcmp(buf, WAN_STATUS_UP))
    {
        CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION IPV6 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));
        return WAN_STATE_IPV6_LEASED;
    }
    if(pWanIfaceCtrl->eCurrentState == WAN_STATE_DUAL_STACK_ACTIVE)
    {
        CcspTraceInfo(("%s %d - Interface '%s' - WAN_STATE_DUAL_STACK_ACTIVE->TRANSITION IPV6 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));
        return WAN_STATE_IPV6_LEASED;
    }

    /* RDKB-46612 - Empty set caused the cujo firewall rules to currupt and led to IHC IDLE.
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_IFNAME, "", 0);*/

    pInterface->Wan.Status = WAN_IFACE_STATUS_VALIDATING;
    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION OBTAINING IP ADDRESSES\n", __FUNCTION__, __LINE__, pInterface->Name));
    return WAN_STATE_OBTAINING_IP_ADDRESSES;
}

#if defined(FEATURE_464XLAT)
int sc_myPipe(char *command, char **output)
{
    FILE *fp;
    char buf[1024];
	char *pt = NULL;
    int len=0;
    *output=malloc(3);
    strcpy(*output, "");
    if((fp=popen(command, "r"))==NULL)
	{
        return(-2);
	}
    while((fgets(buf, sizeof(buf), fp)) != NULL){
        len=strlen(*output)+strlen(buf);
        if((*output=realloc(*output, (sizeof(char) * (len+1))))==NULL)
        {
            pclose(fp);
            return(-1);
        }
        strcat(*output, buf);
    }
	if ((pt = strchr(*output, '\n')) != NULL)
	{
		*pt = '\0';
	}

    pclose(fp);
    return len;
}

void xlat_process_start(char *interface)
{
	char cmd[512] = {0};
	char xlatcfg_output[256] = {0};
	char *kernel_path = NULL;
	int ret = 0;
	
	sc_myPipe("uname -r",&kernel_path);
	snprintf(cmd,sizeof(cmd),"insmod /lib/modules/%s/extra/nat46.ko",kernel_path);
	free(kernel_path);
	system(cmd);
	ret = xlat_configure(interface, xlatcfg_output);
	if(ret == 0 || strlen(xlatcfg_output) > 3)
	{
		system("ifconfig xlat up");
		sleep(1);
		system("ip ro del default");
		system("ip route add default dev xlat");
		system("echo 200 464xlat >> /etc/iproute2/rt_tables");
		system("ip -6 rule del from all lookup local");
		system("ip -6 rule add from all lookup local pref 1");
		snprintf(cmd,sizeof(cmd),"ip -6 rule add to %s lookup 464xlat pref 0",xlatcfg_output);
		system(cmd);
		snprintf(cmd,sizeof(cmd),"ip -6 route add %s dev xlat proto static metric 1024 pref medium table 464xlat",xlatcfg_output);
		system(cmd);
		syscfg_set(NULL, "xlat_status", "up");
        syscfg_set(NULL, "xlat_addrdss", xlatcfg_output);
		system("sync;sync");
		syscfg_commit();
		snprintf(cmd,sizeof(cmd),"sysevent set firewall-restart 0");
		system(cmd);
	}else
	{
		system("rmmod nat46");
	}
}

void xlat_process_stop(char *interface)
{
	char cmd[256] = {0};
	char xlat_addr[128] = {0};
	
	system("ip route del default dev xlat");
	snprintf(cmd,sizeof(cmd),"ip ro add default dev %s scope link",interface);
	system(cmd);
	system("iptables -t nat -D POSTROUTING  -o xlat -j SNAT --to-source  192.0.0.1");
	system("ifconfig xlat down");
	xlat_reconfigure();
	system("rmmod nat46");
	if (syscfg_get(NULL, "xlat_addrdss", xlat_addr, sizeof(xlat_addr))!=0)
	{
		if(strlen(xlat_addr) > 3)
		{
			snprintf(cmd,sizeof(cmd),"ip -6 rule del to %s lookup 464xlat",xlat_addr);
			system(cmd);
		}
	}
	system("ip -6 rule del from all lookup local");
	system("ip -6 rule add from all lookup local pref 0");
	system("syscfg unset xlat_status");
	system("syscfg unset xlat_addrdss");
	system("syscfg commit");
}
#endif

static eWanState_t wan_transition_ipv6_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    ANSC_STATUS ret;
    char buf[BUFLEN_128] = {0};
#if defined(FEATURE_464XLAT)
	XLAT_State_t xlat_status;
#endif
	

    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    /* successfully got the v6 lease from the interface, so lets mark it validated */
    pInterface->Wan.Status = WAN_IFACE_STATUS_UP;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    /* Configure IPv6. */
    ret = wan_setUpIPv6(pWanIfaceCtrl);
    if (ret != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to configure IPv6 successfully \n", __FUNCTION__, __LINE__));
    }
#ifdef FEATURE_IPOE_HEALTH_CHECK
        if (pInterface->PPP.Enable == FALSE)
        {
           if ((pInterface->Wan.EnableIPoE) && (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STOPPED))
           {
               WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
               pWanIfaceCtrl->IhcV6Status = IHC_STARTED;
           }
        }
#endif
    Update_Interface_Status();
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_WAN_SERVICE_STATUS, buf, sizeof(buf));
    if (strcmp(buf, WAN_STATUS_STARTED))
    {
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_WAN_SERVICE_STATUS, WAN_STATUS_STARTED, 0);
        sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
    }
#if defined(FEATURE_464XLAT)
	CcspTraceInfo(("%s %d - 464xlat STARTING \n", __FUNCTION__, __LINE__));
	xlat_status = xlat_state_get();
	CcspTraceInfo(("%s %d - xlat_status = %d \n", __FUNCTION__, __LINE__,xlat_status));
	if(xlat_status == XLAT_OFF)
	{
		CcspTraceInfo(("%s %d - START 464xlat\n", __FUNCTION__, __LINE__));
		xlat_process_start(pInterface->Wan.Name);
	}
	else
	{
		CcspTraceInfo(("%s %d - RESTART 464xlat\n", __FUNCTION__, __LINE__));
		xlat_process_stop(pInterface->Wan.Name);
		xlat_process_start(pInterface->Wan.Name);
	}
	CcspTraceInfo(("%s %d -  464xlat END\n", __FUNCTION__, __LINE__));
#endif
    memset(buf, 0, BUFLEN_128);
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_IPV4_CONNECTION_STATE, buf, sizeof(buf));

    if( pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP && !strcmp(buf, WAN_STATUS_UP))
    {
        CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION DUAL STACK ACTIVE\n", __FUNCTION__, __LINE__, pInterface->Name));
        return WAN_STATE_DUAL_STACK_ACTIVE;
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION IPV6 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));
    return WAN_STATE_IPV6_LEASED;
}

static eWanState_t wan_transition_ipv6_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    char buf[BUFLEN_128] = {0};
#if defined(FEATURE_464XLAT)
	XLAT_State_t xlat_status;
#endif

    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP ||
        pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
        ((pInterface->Wan.RefreshDHCP == TRUE) && (pInterface->Wan.EnableDHCP == FALSE)))
    {
        // Stopping DHCPv6 client, so we can send a unicast DHCP Release packet
        CcspTraceInfo(("%s %d: Stopping DHCP v6\n", __FUNCTION__, __LINE__));
        WanManager_StopDhcpv6Client(pInterface->Wan.Name);
        pInterface->IP.Dhcp6cPid = 0;
    }
    else
    {
        /* Collect if any zombie process. */
        WanManager_DoCollectApp(DHCPV6_CLIENT_NAME);

        if (WanManager_IsApplicationRunning(DHCPV6_CLIENT_NAME) != TRUE)
        {
            /* Start DHCPv6 Client */
            CcspTraceInfo(("%s %d - Starting dibbler-client on interface %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            pInterface->IP.Dhcp6cPid = WanManager_StartDhcpv6Client(pInterface->Wan.Name);
            CcspTraceInfo(("%s %d - SELFHEAL - Started dibbler-client on interface %s, dhcpv6_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp6cPid));
        }
    }

    WanManager_UpdateInterfaceStatus (pInterface, WANMGR_IFACE_CONNECTION_IPV6_DOWN);

    if (wan_tearDownIPv6(pWanIfaceCtrl) != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to tear down IPv6 for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
    }
#if defined(FEATURE_464XLAT)
	xlat_status = xlat_state_get();
	if(xlat_status == XLAT_ON)
	{
		CcspTraceInfo(("%s %d - STOP 464xlat\n", __FUNCTION__, __LINE__));
		xlat_process_stop(pInterface->Wan.Name);
	}
#endif
#ifdef FEATURE_IPOE_HEALTH_CHECK
        if ( pInterface->PPP.Enable == FALSE )
        {
            if ((pInterface->Wan.EnableIPoE) && (pWanIfaceCtrl->IhcPid > 0))
            {
                WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_DOWN, pInterface->Wan.Name);
                pWanIfaceCtrl->IhcV6Status = IHC_STOPPED;
            }
        }
#endif

    Update_Interface_Status();
    sysevent_get(sysevent_fd, sysevent_token, SYSEVENT_IPV4_CONNECTION_STATE, buf, sizeof(buf));

    if(pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP && !strcmp(buf, WAN_STATUS_UP))
    {
        CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION IPV4 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));
        return WAN_STATE_IPV4_LEASED;
    }
    if(pWanIfaceCtrl->eCurrentState == WAN_STATE_DUAL_STACK_ACTIVE)
    {
        CcspTraceInfo(("%s %d - Interface '%s' - WAN_STATE_DUAL_STACK_ACTIVE->TRANSITION IPV4 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));
        return WAN_STATE_IPV4_LEASED;
    }

    /* RDKB-46612 - Empty set caused the cujo firewall rules to currupt and led to IHC IDLE.
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_CURRENT_WAN_IFNAME, "", 0);*/

    pInterface->Wan.Status = WAN_IFACE_STATUS_VALIDATING;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION OBTAINING IP ADDRESSES\n", __FUNCTION__, __LINE__, pInterface->Name));
    return WAN_STATE_OBTAINING_IP_ADDRESSES;

}

static eWanState_t wan_transition_dual_stack_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    pInterface->Wan.Status = WAN_IFACE_STATUS_VALIDATING;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    wan_transition_ipv4_down(pWanIfaceCtrl);
    wan_transition_ipv6_down(pWanIfaceCtrl);

    CcspTraceInfo(("%s %d - TRANSITION OBTAINING IP ADDRESSES\n", __FUNCTION__, __LINE__));
    return WAN_STATE_OBTAINING_IP_ADDRESSES;
}

#ifdef FEATURE_MAPT
static eWanState_t wan_transition_mapt_feature_refresh(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    wan_transition_ipv6_down(pWanIfaceCtrl);

    /* DHCPv6 client */
    pInterface->IP.Ipv6Status = WAN_IFACE_IPV6_STATE_DOWN;
    pInterface->IP.Ipv6Changed = FALSE;
#ifdef FEATURE_IPOE_HEALTH_CHECK
    pInterface->IP.Ipv6Renewed = FALSE;
#endif
    memset(&(pInterface->IP.Ipv6Data), 0, sizeof(WANMGR_IPV6_DATA));
    pInterface->IP.Dhcp6cPid = 0;
    if(pInterface->IP.pIpcIpv6Data != NULL)
    {
        free(pInterface->IP.pIpcIpv6Data);
        pInterface->IP.pIpcIpv6Data = NULL;
    }


    if(pInterface->PPP.Enable == TRUE)
    {
        /* Delete PPP session */
        WanManager_DeletePPPSession(pInterface);

        /* Create PPP session */
        WanManager_CreatePPPSession(pInterface);
    }
    else if (pInterface->Wan.EnableDHCP == TRUE)
    {
        int i = 0;
        /* Release and Stops DHCPv6 client */
        system("touch /tmp/dhcpv6_release");
        WanManager_StopDhcpv6Client(pInterface->Wan.Name);
        pInterface->IP.Dhcp6cPid = 0;

        for(i= 0; i < 10; i++)
        {
            if (WanManager_IsApplicationRunning(DHCPV6_CLIENT_NAME) == TRUE)
            {
                // Before starting a V6 client, it may take some time to get the REPLAY for RELEASE from Previous V6 client.
                // So wait for 1 to 10 secs for the process of Release & Kill the existing client
                sleep(1);
            }
            else
            {
                /* Start DHCPv6 Client */
                CcspTraceInfo(("%s %d - Staring dibbler-client on interface %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                pInterface->IP.Dhcp6cPid = WanManager_StartDhcpv6Client(pInterface->Wan.Name);
                CcspTraceInfo(("%s %d - Started dibbler-client on interface %s, dhcpv6_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp6cPid));
            }
        }
    }

    CcspTraceInfo(("%s %d - TRANSITION OBTAINING IP ADDRESSES\n", __FUNCTION__, __LINE__));

    return WAN_STATE_OBTAINING_IP_ADDRESSES;
}

static eWanState_t wan_transition_mapt_up(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    ANSC_STATUS ret;
    char buf[BUFLEN_128] = {0};
    char cmdEnableIpv4Traffic[BUFLEN_256] = {'\0'};

    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    /* Configure IPv6. */
    ret = wan_setUpMapt();
    if (ret != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to configure MAP-T successfully \n", __FUNCTION__, __LINE__));
    }

    if (WanManager_ProcessMAPTConfiguration(&(pInterface->MAP.dhcp6cMAPTparameters), pInterface->Name, pInterface->IP.Ipv6Data.ifname) != RETURN_OK)
    {
        CcspTraceError(("%s %d - Error processing MAP-T Parameters \n", __FUNCTION__, __LINE__));
        pInterface->MAP.MaptStatus = WAN_IFACE_MAPT_STATE_DOWN;
        return WAN_STATE_MAPT_ACTIVE;
    }

    pInterface->MAP.MaptChanged = FALSE;

    /* if V4 data already recieved, let it configure */
    if((pInterface->IP.Ipv4Changed == TRUE) && (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP))
    {
        wan_transition_ipv4_up(pWanIfaceCtrl);
    }

    if (pInterface->IP.Dhcp4cPid > 0)
    {
        /* Stops DHCPv4 client on this interface */
        WanManager_StopDhcpv4Client(pInterface->Wan.Name, TRUE);
        pInterface->IP.Dhcp4cPid = 0;
    }

    /* if V4 already configured, let it teardown */
    if((pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP))
    {
        wan_transition_ipv4_down(pWanIfaceCtrl);

#if defined(IVI_KERNEL_SUPPORT)
        snprintf(cmdEnableIpv4Traffic,sizeof(cmdEnableIpv4Traffic),"ip ro rep default dev %s", pInterface->Wan.Name);
#elif defined(NAT46_KERNEL_SUPPORT)
        snprintf(cmdEnableIpv4Traffic, sizeof(cmdEnableIpv4Traffic), "ip ro rep default dev %s mtu %d", MAP_INTERFACE, MTU_DEFAULT_SIZE);
#endif
#ifdef FEATURE_MAPT_DEBUG
        MaptInfo("mapt: default route after v4 teardown:%s",cmdEnableIpv4Traffic);
#endif
        if (WanManager_DoSystemActionWithStatus("mapt:", cmdEnableIpv4Traffic) < RETURN_OK)
        {
            CcspTraceError(("%s %d - Failed to run: %s \n", __FUNCTION__, __LINE__, cmdEnableIpv4Traffic));
        }
    }

    if( pInterface->PPP.Enable == TRUE )
    {
        WanManager_DeletePPPSession(pInterface);
    }

    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION WAN_STATE_MAPT_ACTIVE\n", __FUNCTION__, __LINE__, pInterface->Name));
    return WAN_STATE_MAPT_ACTIVE;
}

static eWanState_t wan_transition_mapt_down(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    char buf[BUFLEN_128] = {0};

    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    WanManager_UpdateInterfaceStatus (pInterface, WANMGR_IFACE_MAPT_STOP);

    if (wan_tearDownMapt() != RETURN_OK)
    {
        CcspTraceError(("%s %d - Failed to tear down MAP-T for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
    }

    if (WanManager_ResetMAPTConfiguration(pInterface->Name, pInterface->Wan.Name) != RETURN_OK)
    {
        CcspTraceError(("%s %d Error resetting MAP-T configuration", __FUNCTION__, __LINE__));
    }

    /* Clear DHCPv4 client */
    WanManager_UpdateInterfaceStatus (pInterface, WANMGR_IFACE_CONNECTION_DOWN);
    memset(&(pInterface->IP.Ipv4Data), 0, sizeof(WANMGR_IPV4_DATA));
    pInterface->IP.Dhcp4cPid = 0;

    if(pInterface->IP.pIpcIpv4Data != NULL)
    {
        free(pInterface->IP.pIpcIpv4Data);
        pInterface->IP.pIpcIpv4Data = NULL;
    }

    if(pInterface->Phy.Status ==  WAN_IFACE_PHY_STATUS_UP)
    {
        if( pInterface->PPP.Enable == FALSE )
        {
            pInterface->IP.Dhcp4cPid = WanManager_StartDhcpv4Client(pInterface->Wan.Name);
            CcspTraceInfo(("%s %d - Started dhcpc on interface %s, pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp4cPid));
        }
        else
        {
            WanManager_CreatePPPSession(pInterface);
        }
    }

    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);

    CcspTraceInfo(("%s %d - Interface '%s' - TRANSITION IPV6 LEASED\n", __FUNCTION__, __LINE__, pInterface->Name));
    return WAN_STATE_IPV6_LEASED;
}
#endif //FEATURE_MAPT

static eWanState_t wan_transition_exit(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    pInterface->Wan.Status = WAN_IFACE_STATUS_DISABLED;

    pInterface->Wan.Refresh = FALSE;

    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    Update_Interface_Status();
    CcspTraceInfo(("%s %d - Interface '%s' - EXITING STATE MACHINE\n", __FUNCTION__, __LINE__, pInterface->Name));

    return WAN_STATE_EXIT;
}

static eWanState_t wan_transition_standby(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
    {
        pInterface->Wan.Status = WAN_IFACE_STATUS_STANDBY;
        pInterface->IP.Ipv4Changed = FALSE;

    }
    if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
    {
        pInterface->Wan.Status = WAN_IFACE_STATUS_STANDBY;
        pInterface->IP.Ipv6Changed = FALSE;
    }
    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    Update_Interface_Status();
    CcspTraceInfo(("%s %d - TRANSITION WAN_STATE_STANDBY\n", __FUNCTION__, __LINE__));
    return WAN_STATE_STANDBY;
}

static eWanState_t wan_transition_standby_deconfig_ips(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

#ifdef FEATURE_MAPT
    if(pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_UP)
    {
        CcspTraceInfo(("%s %d - Deconfiguring MAP-T for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        if (wan_tearDownMapt() != RETURN_OK)
        {
            CcspTraceError(("%s %d - Failed to tear down MAP-T for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }

        if (WanManager_ResetMAPTConfiguration(pInterface->Name, pInterface->Wan.Name) != RETURN_OK)
        {
            CcspTraceError(("%s %d Error resetting MAP-T configuration", __FUNCTION__, __LINE__));
        }
    }
#endif
    if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
    {
        CcspTraceInfo(("%s %d - Deconfiguring Ipv4 for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        if (wan_tearDownIPv4(pWanIfaceCtrl) != RETURN_OK)
        {
            CcspTraceError(("%s %d - Failed to tear down IPv4 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }

    if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
    {
        CcspTraceInfo(("%s %d - Deconfiguring Ipv6 for %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        if (wan_tearDownIPv6(pWanIfaceCtrl) != RETURN_OK)
        {
            CcspTraceError(("%s %d - Failed to tear down IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
    pInterface->Wan.Status = WAN_IFACE_STATUS_STANDBY;
    if (pWanIfaceCtrl->interfaceIdx != -1)
    {
        WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
    }

    Update_Interface_Status();
     CcspTraceInfo(("%s %d - TRANSITION WAN_STATE_STANDBY\n", __FUNCTION__, __LINE__));
    return WAN_STATE_STANDBY;
}

/*********************************************************************************/
/**************************** STATES *********************************************/
/*********************************************************************************/
static eWanState_t wan_state_configuring_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }

    if (pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_UP )
    {
        return wan_transition_wan_up(pWanIfaceCtrl);
    }

    return WAN_STATE_CONFIGURING_WAN;
}

static eWanState_t wan_state_validating_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }

    if (pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_CONFIGURING )
    {
        /* TODO: We'll need to call a transition that stops any running validation
           processes before returning to the CONFIGURING WAN state */
        return WAN_STATE_CONFIGURING_WAN;
    }

    /* Needs to ensure WAN interface name before going to validation */
    if ( ( pInterface->Wan.Name[0] == '\0' ) || ( strlen(pInterface->Wan.Name) == 0 ) )
    {
       return WAN_STATE_VALIDATING_WAN;
    }

    /* TODO: Waits for every running validation process to complete, then checks the results */

    return wan_transition_wan_validated(pWanIfaceCtrl);
}

static eWanState_t wan_state_obtaining_ip_addresses(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }

    if ( pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN )
    {
        if (pInterface->Wan.IfaceType != REMOTE_IFACE)
            pInterface->Wan.LinkStatus =  WAN_IFACE_LINKSTATUS_CONFIGURING;
        Update_Interface_Status();
        return WAN_STATE_CONFIGURING_WAN;
    }

    if ( pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_CONFIGURING ||
            pInterface->Wan.Refresh == TRUE)
    {
        return wan_state_refreshing_wan(pWanIfaceCtrl);
    }

    if ((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.RefreshDHCP == TRUE))
    {
        if (pInterface->Wan.EnableDHCP == TRUE)
        {
            // EnableDHCP changed to TRUE
            if (pInterface->IP.Dhcp4cPid <= 0)
            {
                pInterface->IP.Dhcp4cPid = WanManager_StartDhcpv4Client(pInterface->Wan.Name);
                CcspTraceInfo(("%s %d - Started dhcpc on interface %s, dhcpv4_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp4cPid));
            }
            if (pInterface->IP.Dhcp6cPid <= 0)
            {
                pInterface->IP.Dhcp6cPid = WanManager_StartDhcpv6Client(pInterface->Wan.Name);
                CcspTraceInfo(("%s %d - Started dhcpc on interface %s, dhcpv6_pid %d \n", __FUNCTION__, __LINE__, pInterface->Wan.Name, pInterface->IP.Dhcp6cPid));
            }
        }
        else
        {
            // EnableDHCP changes to FALSE 
            if (pInterface->IP.Dhcp4cPid > 0)
            {
                CcspTraceInfo(("%s %d: Stopping DHCP v4\n", __FUNCTION__, __LINE__));
                WanManager_StopDhcpv4Client(pInterface->Wan.Name, FALSE); // no release dhcp lease
                pInterface->IP.Dhcp4cPid = 0;
            }

            if (pInterface->IP.Dhcp6cPid > 0)
            {
                CcspTraceInfo(("%s %d: Stopping DHCP v6\n", __FUNCTION__, __LINE__));
                WanManager_StopDhcpv6Client(pInterface->Wan.Name); // release dhcp lease
                pInterface->IP.Dhcp6cPid = 0;
            }

            if ((pInterface->IP.Dhcp4cPid == 0) && (pInterface->IP.Dhcp6cPid == 0))
            {
                pInterface->Wan.Status = WAN_IFACE_STATUS_VALIDATING;
                if (pWanIfaceCtrl->interfaceIdx != -1)
                {
                    WanMgr_Publish_WanStatus(pWanIfaceCtrl->interfaceIdx);
                }
            }
        }
        pInterface->Wan.RefreshDHCP = FALSE;

        return WAN_STATE_OBTAINING_IP_ADDRESSES;
    }

    if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
    {
        if (pInterface->SelectionStatus == WAN_IFACE_ACTIVE)
        {
            return wan_transition_ipv4_up(pWanIfaceCtrl);
        }
        else
        {
            return wan_transition_standby(pWanIfaceCtrl);
        }
    }
    else if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
    {
        if (pInterface->SelectionStatus == WAN_IFACE_ACTIVE)
        {
            if(pInterface->IP.Ipv6Changed == TRUE)
            {
                /* Set sysevents to trigger P&M */
                if (setUpLanPrefixIPv6(pInterface) != RETURN_OK)
                {
                    CcspTraceError((" %s %d - Failed to configure IPv6 prefix \n", __FUNCTION__, __LINE__));
                }
                /* Reset isIPv6ConfigChanged  */
                pInterface->IP.Ipv6Changed = FALSE;
                return WAN_STATE_OBTAINING_IP_ADDRESSES;
            }
            if (checkIpv6AddressAssignedToBridge() == RETURN_OK)
            {
                return wan_transition_ipv6_up(pWanIfaceCtrl);
            }
            else
            {
                wanmgr_Ipv6Toggle();
            }
        }
        else
        {
            return wan_transition_standby(pWanIfaceCtrl);
        }
    }
#ifdef FEATURE_MAPT
    else if (pInterface->Wan.EnableMAPT == TRUE &&
            pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
            mapt_feature_enable_changed == TRUE &&
            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_DOWN)
    {
        if (TRUE == wanmanager_mapt_feature())
        {
            mapt_feature_enable_changed = FALSE;
            return wan_transition_mapt_feature_refresh(pWanIfaceCtrl);
        }
    }
#endif

    return WAN_STATE_OBTAINING_IP_ADDRESSES;
}

static eWanState_t wan_state_standby(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    eWanState_t ret;
    static BOOL BridgeWait = FALSE;
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }
    else if (pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
             (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_DOWN &&
             pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_DOWN) ||
             ((pInterface->Wan.RefreshDHCP == TRUE) && (pInterface->Wan.EnableDHCP == FALSE)))
    {
        return WAN_STATE_OBTAINING_IP_ADDRESSES;
    }
    else if (pInterface->SelectionStatus == WAN_IFACE_ACTIVE)
    {
        if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
        {
            if (!BridgeWait)
            {
                if (setUpLanPrefixIPv6(pInterface) == RETURN_OK)
                {
                    BridgeWait = TRUE;
                    CcspTraceInfo((" %s %d - configure IPv6 prefix \n", __FUNCTION__, __LINE__));
                }
            }
            if (checkIpv6AddressAssignedToBridge() == RETURN_OK)
            {
                BridgeWait = FALSE;
                ret = wan_transition_ipv6_up(pWanIfaceCtrl);
                pInterface->IP.Ipv6Changed = FALSE;
                CcspTraceInfo((" %s %d - IPv6 Address Assigned to Bridge Yet.\n", __FUNCTION__, __LINE__));
            }
            else
            {
                wanmgr_Ipv6Toggle();
            }
        }
        if (pInterface->IP.Ipv6Status != WAN_IFACE_IPV6_STATE_UP || !BridgeWait)
        {
            if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
            {
                ret = wan_transition_ipv4_up(pWanIfaceCtrl);
            }
            return ret;
        }
    }
    else
    {
        BridgeWait = FALSE;
        if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
        {
            pInterface->IP.Ipv4Changed = FALSE;
        }
        if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
        {
            pInterface->IP.Ipv6Changed = FALSE;
        }

    }
    return WAN_STATE_STANDBY;
}

static eWanState_t wan_state_ipv4_leased(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;


#ifdef FEATURE_IPOE_HEALTH_CHECK
    if ((pInterface->Wan.EnableIPoE == TRUE) && (pInterface->PPP.Enable == FALSE))
    {
        // IHC is enabled
        if ( pWanIfaceCtrl->IhcPid <= 0 )
        {
            // IHC enabled but not running, So Starting IHC
            UINT IhcPid = 0;
            IhcPid = WanManager_StartIpoeHealthCheckService(pInterface->Wan.Name);
            if (IhcPid > 0)
            {
                pWanIfaceCtrl->IhcPid = IhcPid;
                CcspTraceInfo(("%s %d - Starting IPoE Health Check pid - %u for interface %s \n", 
                                __FUNCTION__, __LINE__, pWanIfaceCtrl->IhcPid, pInterface->Wan.Name));
            }
            else
            {
                CcspTraceError(("%s %d - Failed to start IPoE Health Check for interface %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        if ( (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STOPPED) )
        {
            // sending v4 UP event to IHC, IHC will starts to send BFD v4 packets to BNG
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
            pWanIfaceCtrl->IhcV4Status = IHC_STARTED;
        }
    }
    else if (pWanIfaceCtrl->IhcPid > 0)
    {
        // IHC is disabled, but is still running, so stop it
        WanManager_StopIHC(pWanIfaceCtrl);
    }
#endif // FEATURE_IPOE_HEALTH_CHECK

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_DOWN ||
             pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
            ((pInterface->Wan.RefreshDHCP == TRUE) && (pInterface->Wan.EnableDHCP == FALSE)))    // EnableDHCP changes to FALSE
    {
        return wan_transition_ipv4_down(pWanIfaceCtrl);
    }
    else if ((pInterface->SelectionStatus != WAN_IFACE_ACTIVE) || (pWanIfaceCtrl->DeviceNwModeChanged == TRUE))
    {
        return wan_transition_standby_deconfig_ips(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv4Changed == TRUE)
    {
        if (wan_tearDownIPv4(pWanIfaceCtrl) == RETURN_OK)
        {
            if (wan_setUpIPv4(pWanIfaceCtrl) == RETURN_OK)
            {
#ifdef FEATURE_IPOE_HEALTH_CHECK
                if ((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                        (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STARTED))
                {
                    WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
                }
#endif
                pInterface->IP.Ipv4Changed = FALSE;
                CcspTraceInfo(("%s %d - Successfully updated IPv4 configure Changes for %s Interface \n",
                            __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
            else
            {
                CcspTraceError(("%s %d - Failed to configure IPv4 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        else
        {
            CcspTraceError(("%s %d - Failed to tear down IPv4 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
    else if (pInterface->Wan.Refresh == TRUE)
    {
        return wan_state_refreshing_wan(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_UP)
    {
        if(pInterface->IP.Ipv6Changed == TRUE)
        {
            /* Set sysevents to trigger P&M */
            if (setUpLanPrefixIPv6(pInterface) != RETURN_OK)
            {
                CcspTraceError((" %s %d - Failed to configure IPv6 prefix \n", __FUNCTION__, __LINE__));
            }
            /* Reset isIPv6ConfigChanged  */
            pInterface->IP.Ipv6Changed = FALSE;
            return WAN_STATE_IPV4_LEASED;
        }
        if (checkIpv6AddressAssignedToBridge() == RETURN_OK)
        {
            return wan_transition_ipv6_up(pWanIfaceCtrl);
        }
        else
        {
            wanmgr_Ipv6Toggle();
        }
    }
#ifdef FEATURE_IPOE_HEALTH_CHECK
    else if (pInterface->IP.Ipv4Renewed == TRUE)
    {
        char IHC_V4_status[BUFLEN_16] = {0};
        sysevent_get(sysevent_fd, sysevent_token, IPOE_HEALTH_CHECK_V4_STATUS, IHC_V4_status, sizeof(IHC_V4_status));
        if((strcmp(IHC_V4_status, IPOE_STATUS_FAILED) == 0) && (pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STARTED))
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
        }
        pInterface->IP.Ipv4Renewed = FALSE;
    }
#endif
#ifdef FEATURE_MAPT
    else if (pInterface->Wan.EnableMAPT == TRUE &&
            pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
            mapt_feature_enable_changed == TRUE &&
            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_DOWN)
    {
        if (TRUE == wanmanager_mapt_feature())
        {
            mapt_feature_enable_changed = FALSE;
            return wan_transition_mapt_feature_refresh(pWanIfaceCtrl);
        }
    }
#endif

    return WAN_STATE_IPV4_LEASED;
}

static eWanState_t wan_state_ipv6_leased(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

#ifdef FEATURE_IPOE_HEALTH_CHECK
    if ((pInterface->Wan.EnableIPoE == TRUE) && (pInterface->PPP.Enable == FALSE))
    {
        // IHC is enabled
        if ( pWanIfaceCtrl->IhcPid <= 0 )
        {
            // IHC enabled but not running, So Starting IHC
            UINT IhcPid = 0;
            IhcPid = WanManager_StartIpoeHealthCheckService(pInterface->Wan.Name);
            if (IhcPid > 0)
            {
                pWanIfaceCtrl->IhcPid = IhcPid;
                CcspTraceInfo(("%s %d - Starting IPoE Health Check pid - %u for interface %s \n", 
                                __FUNCTION__, __LINE__, pWanIfaceCtrl->IhcPid, pInterface->Wan.Name));
            }
            else
            {
                CcspTraceError(("%s %d - Failed to start IPoE Health Check for interface %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        if ((pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STOPPED))
        {
            // sending v6 UP event to IHC, IHC starts to send BFD v6 packts to BNG
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
            pWanIfaceCtrl->IhcV6Status = IHC_STARTED;
        }
    }
    else if (pWanIfaceCtrl->IhcPid > 0)
    {
        // IHC is disabled, but is still running, so stop it
        WanManager_StopIHC(pWanIfaceCtrl);
    }
#endif 

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_DOWN ||
             pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
            ((pInterface->Wan.RefreshDHCP == TRUE) && (pInterface->Wan.EnableDHCP == FALSE)))    // EnableDHCP changes to FALSE
    {
        return wan_transition_ipv6_down(pWanIfaceCtrl);
    }
    else if ((pInterface->SelectionStatus != WAN_IFACE_ACTIVE)  || (pWanIfaceCtrl->DeviceNwModeChanged == TRUE))
    {
        return wan_transition_standby_deconfig_ips(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv6Changed == TRUE)
    {
        if (wan_tearDownIPv6(pWanIfaceCtrl) == RETURN_OK)
        {
            if (setUpLanPrefixIPv6(pInterface) == RETURN_OK)
            {
                if (wan_setUpIPv6(pWanIfaceCtrl) == RETURN_OK)
                {
#ifdef FEATURE_IPOE_HEALTH_CHECK
                    if ((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) && 
                        (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
                    {
                        WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
                    }
#endif
                    pInterface->IP.Ipv6Changed = FALSE;
                    CcspTraceInfo(("%s %d - Successfully updated IPv6 configure Changes for %s Interface \n",
                                    __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
                else
                {
                    CcspTraceError(("%s %d - Failed to configure IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
            }
            else
            {
                CcspTraceError((" %s %d - Failed to configure IPv6 prefix for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        else
        {
            CcspTraceError(("%s %d - Failed to tear down IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
    else if (pInterface->Wan.Refresh == TRUE)
    {
        return wan_state_refreshing_wan(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_UP)
    {
        return wan_transition_ipv4_up(pWanIfaceCtrl);
    }
#ifdef FEATURE_MAPT
    else if (pInterface->Wan.EnableMAPT == TRUE &&
            pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_UP)
    {
        if (checkIpv6AddressAssignedToBridge() == RETURN_OK) // Wait for default gateway before MAP-T configuration
        {
            return wan_transition_mapt_up(pWanIfaceCtrl);
        } //wanmgr_Ipv6Toggle() is called below.
    }
    else if (pInterface->Wan.EnableMAPT == TRUE &&
             pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
             mapt_feature_enable_changed == TRUE &&
             pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_DOWN)
    {
        if (TRUE == wanmanager_mapt_feature())
        {
            mapt_feature_enable_changed = FALSE;
            return wan_transition_mapt_feature_refresh(pWanIfaceCtrl);
        }
    }
#endif //FEATURE_MAPT
#ifdef FEATURE_IPOE_HEALTH_CHECK
    else if (pInterface->IP.Ipv6Renewed == TRUE)
    {
        char IHC_V6_status[BUFLEN_16] = {0};
        sysevent_get(sysevent_fd, sysevent_token, IPOE_HEALTH_CHECK_V6_STATUS, IHC_V6_status, sizeof(IHC_V6_status));
        if((strcmp(IHC_V6_status, IPOE_STATUS_FAILED) == 0) && (pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
        {
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
            Force_IPv6_toggle(pInterface->Wan.Name); //Force Ipv6 toggle to update default route
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
        }
        pInterface->IP.Ipv6Renewed = FALSE;
    }
#endif
    wanmgr_Ipv6Toggle();
#if defined(FEATURE_IPOE_HEALTH_CHECK) && defined(IPOE_HEALTH_CHECK_LAN_SYNC_SUPPORT)
    if((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) && (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
    {
        if(lanState == LAN_STATE_STOPPED)
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_DOWN, pInterface->Wan.Name);
            lanState = LAN_STATE_RESET;
        }
        else if(lanState == LAN_STATE_STARTED)
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
            lanState = LAN_STATE_RESET;
        }
    }
#endif
    return WAN_STATE_IPV6_LEASED;
}

static eWanState_t wan_state_dual_stack_active(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

#ifdef FEATURE_IPOE_HEALTH_CHECK
    if ((pInterface->Wan.EnableIPoE == TRUE) && (pInterface->PPP.Enable == FALSE))
    {
        // IHC is enabled
        if (pWanIfaceCtrl->IhcPid <= 0)
        {
            // IHC enabled but not running, So Starting IHC
            UINT IhcPid = 0;
            IhcPid = WanManager_StartIpoeHealthCheckService(pInterface->Wan.Name);
            if (IhcPid > 0)
            {
                pWanIfaceCtrl->IhcPid = IhcPid;
                CcspTraceInfo(("%s %d - Starting IPoE Health Check pid - %u for interface %s \n", 
                                __FUNCTION__, __LINE__, pWanIfaceCtrl->IhcPid, pInterface->Wan.Name));
            }
            else
            {
                CcspTraceError(("%s %d - Failed to start IPoE Health Check for interface %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        if ((pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STOPPED))
        {
            // sending v4 UP event to IHC, IHC starts to send BFD v4 packts to BNG
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
            pWanIfaceCtrl->IhcV4Status = IHC_STARTED;
        }
        if ((pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STOPPED))
        {
            // sending v6 UP event to IHC, IHC starts to send BFD v6 packts to BNG
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
            pWanIfaceCtrl->IhcV6Status = IHC_STARTED;
        }
    }
    else if (pWanIfaceCtrl->IhcPid > 0)
    {
        // IHC Disbled but running, So Stoping IHC
        WanManager_StopIHC(pWanIfaceCtrl);
    }
#endif

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }
    else if (pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
             (pInterface->Wan.RefreshDHCP == TRUE) && (pInterface->Wan.EnableDHCP == FALSE))
    {
        return wan_transition_dual_stack_down(pWanIfaceCtrl);
    }
    else if (pInterface->Wan.Refresh == TRUE)
    {
        return wan_state_refreshing_wan(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv4Status == WAN_IFACE_IPV4_STATE_DOWN)
    {
        /* TODO: Add IPoE Health Check failed for IPv4 here */
        return wan_transition_ipv4_down(pWanIfaceCtrl);
    }
    else if ((pInterface->SelectionStatus != WAN_IFACE_ACTIVE) || (pWanIfaceCtrl->DeviceNwModeChanged == TRUE))
    {
        return wan_transition_standby_deconfig_ips(pWanIfaceCtrl);
    }

    else if (pInterface->IP.Ipv4Changed == TRUE)
    {
        if (wan_tearDownIPv4(pWanIfaceCtrl) == RETURN_OK)
        {
            if (wan_setUpIPv4(pWanIfaceCtrl) == RETURN_OK)
            {
#ifdef FEATURE_IPOE_HEALTH_CHECK
                if ((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                        (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STARTED))
                {
                    WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
                }
#endif
                pInterface->IP.Ipv4Changed = FALSE;
                CcspTraceInfo(("%s %d - Successfully updated IPv4 configure Changes for %s Interface \n",
                            __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
            else
            {
                CcspTraceError(("%s %d - Failed to configure IPv4 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        else
        {
            CcspTraceError(("%s %d - Failed to tear down IPv4 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
    else if (pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_DOWN)
    {
        /* TODO: Add IPoE Health Check failed for IPv6 here */
        return wan_transition_ipv6_down(pWanIfaceCtrl);
    }
    else if (pInterface->IP.Ipv6Changed == TRUE)
    {
        if (wan_tearDownIPv6(pWanIfaceCtrl) == RETURN_OK)
        {
            if (setUpLanPrefixIPv6(pInterface) == RETURN_OK)
            {
                if (wan_setUpIPv6(pWanIfaceCtrl) == RETURN_OK)
                {
#ifdef FEATURE_IPOE_HEALTH_CHECK
                    if ((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                            (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
                    {
                        WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
                    }
#endif
                    pInterface->IP.Ipv6Changed = FALSE;
                    CcspTraceInfo(("%s %d - Successfully updated IPv6 configure Changes for %s Interface \n",
                                __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
                else
                {
                    CcspTraceError(("%s %d - Failed to configure IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
            }
            else
            {
                CcspTraceError((" %s %d - Failed to configure IPv6 prefix for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        else
        {
            CcspTraceError(("%s %d - Failed to tear down IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
#ifdef FEATURE_MAPT
    else if (pInterface->Wan.EnableMAPT == TRUE &&
            pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_UP)
    {
        if (checkIpv6AddressAssignedToBridge() == RETURN_OK) // Wait for default gateway before MAP-T configuration
        {
            return wan_transition_mapt_up(pWanIfaceCtrl);
        }//wanmgr_Ipv6Toggle() is called below.
    }
    else if (pInterface->Wan.EnableMAPT == TRUE &&
            pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
            mapt_feature_enable_changed == TRUE &&
            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_DOWN)
    {
        if (TRUE == wanmanager_mapt_feature())
        {
            mapt_feature_enable_changed = FALSE;
            return wan_transition_mapt_feature_refresh(pWanIfaceCtrl);
        }
    }
#endif //FEATURE_MAPT
#ifdef FEATURE_IPOE_HEALTH_CHECK
    else if (pInterface->IP.Ipv4Renewed == TRUE)
    {
        char IHC_V4_status[BUFLEN_16] = {0};
        sysevent_get(sysevent_fd, sysevent_token, IPOE_HEALTH_CHECK_V4_STATUS, IHC_V4_status, sizeof(IHC_V4_status));
        if((strcmp(IHC_V4_status, IPOE_STATUS_FAILED) == 0) && (pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV4Status == IHC_STARTED))
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_UP, pInterface->Wan.Name);
        }
        pInterface->IP.Ipv4Renewed = FALSE;
    }
    else if (pInterface->IP.Ipv6Renewed == TRUE)
    {
        char IHC_V6_status[BUFLEN_16] = {0};
        sysevent_get(sysevent_fd, sysevent_token, IPOE_HEALTH_CHECK_V6_STATUS, IHC_V6_status, sizeof(IHC_V6_status));
        if((strcmp(IHC_V6_status, IPOE_STATUS_FAILED) == 0) && (pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
        {
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
            Force_IPv6_toggle(pInterface->Wan.Name); //Force Ipv6 toggle to update default route
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
        }
        pInterface->IP.Ipv6Renewed = FALSE;
    }
#endif
    wanmgr_Ipv6Toggle();
#if defined(FEATURE_IPOE_HEALTH_CHECK) && defined(IPOE_HEALTH_CHECK_LAN_SYNC_SUPPORT)
    if((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) && (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
    {
        if(lanState == LAN_STATE_STOPPED)
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_DOWN, pInterface->Wan.Name);
            lanState = LAN_STATE_RESET;
        }
        else if(lanState == LAN_STATE_STARTED)
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
            lanState = LAN_STATE_RESET;
        }
    }
#endif
    return WAN_STATE_DUAL_STACK_ACTIVE;
}

#ifdef FEATURE_MAPT
static eWanState_t wan_state_mapt_active(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

#ifdef FEATURE_IPOE_HEALTH_CHECK
    if ((pInterface->Wan.EnableIPoE == TRUE) && (pInterface->PPP.Enable == FALSE))
    {
        // IHC is enabled
        if (pWanIfaceCtrl->IhcPid <= 0)
        {
            // IHC enabled but not running, So Starting IHC
            UINT IhcPid = 0;
            IhcPid = WanManager_StartIpoeHealthCheckService(pInterface->Wan.Name);
            if (IhcPid > 0)
            {
                pWanIfaceCtrl->IhcPid = IhcPid;
                CcspTraceInfo(("%s %d - Starting IPoE Health Check pid - %u for interface %s \n", 
                                __FUNCTION__, __LINE__, pWanIfaceCtrl->IhcPid, pInterface->Wan.Name));
            }
            else
            {
                CcspTraceError(("%s %d - Failed to start IPoE Health Check for interface %s \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        if ((pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STOPPED))
        {
            // sending v6 UP event to IHC, IHC starts to send BFD v6 packts to BNG
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
            pWanIfaceCtrl->IhcV6Status = IHC_STARTED;
        }
    }
    else if (pWanIfaceCtrl->IhcPid > 0)
    {
        // IHC Disbled but running, So Stoping IHC
        WanManager_StopIHC(pWanIfaceCtrl);
    }
#endif

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }
    else if ((pInterface->SelectionStatus != WAN_IFACE_ACTIVE) || (pWanIfaceCtrl->DeviceNwModeChanged == TRUE))
    {
        return wan_transition_standby_deconfig_ips(pWanIfaceCtrl);
    }
    else if (pInterface->Wan.EnableMAPT == FALSE ||
            pInterface->IP.Ipv6Status == WAN_IFACE_IPV6_STATE_DOWN ||
            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_DOWN ||
            pInterface->Wan.LinkStatus ==  WAN_IFACE_LINKSTATUS_DOWN ||
            pInterface->Wan.Refresh == TRUE )
    {
        CcspTraceInfo(("%s %d - LinkStatus=[%d] \n", __FUNCTION__, __LINE__, pInterface->Wan.LinkStatus));
        return wan_transition_mapt_down(pWanIfaceCtrl);
    }
    else if (mapt_feature_enable_changed == TRUE)
    {
        if (FALSE == wanmanager_mapt_feature())
        {
            mapt_feature_enable_changed = FALSE;
            wan_transition_mapt_down(pWanIfaceCtrl);
            return wan_transition_mapt_feature_refresh(pWanIfaceCtrl);
        }
    }
    else if (pInterface->IP.Ipv6Changed == TRUE)
    {
        if (wan_tearDownIPv6(pWanIfaceCtrl) == RETURN_OK)
        {
            if (setUpLanPrefixIPv6(pInterface) == RETURN_OK)
            {
                if (wan_setUpIPv6(pWanIfaceCtrl) == RETURN_OK)
                {
#ifdef FEATURE_IPOE_HEALTH_CHECK
                    if ((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                            (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
                    {
                        WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
                    }
#endif
                    pInterface->IP.Ipv6Changed = FALSE;
                    CcspTraceInfo(("%s %d - Successfully updated IPv6 configure Changes for %s Interface \n",
                                __FUNCTION__, __LINE__, pInterface->Wan.Name));

                    if (pInterface->Wan.EnableMAPT == TRUE &&
                            pInterface->SelectionStatus == WAN_IFACE_ACTIVE &&
                            pInterface->MAP.MaptStatus == WAN_IFACE_MAPT_STATE_UP)
                    {
                        pInterface->MAP.MaptChanged = TRUE; // Reconfigure MAPT if V6 Updated
                    }
                }
                else
                {
                    CcspTraceError(("%s %d - Failed to configure IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
            }
            else
            {
                CcspTraceError((" %s %d - Failed to configure IPv6 prefix for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        else
        {
            CcspTraceError(("%s %d - Failed to tear down IPv6 for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
    else if (pInterface->MAP.MaptChanged == TRUE)
    {
        if (wan_tearDownMapt() == RETURN_OK)
        {
            if (WanManager_ResetMAPTConfiguration(pInterface->Name, pInterface->Wan.Name) != RETURN_OK)
            {
                CcspTraceError(("%s %d Error resetting MAP-T configuration", __FUNCTION__, __LINE__));
            }

            if (wan_setUpMapt() == RETURN_OK)
            {
                if (WanManager_ProcessMAPTConfiguration(&(pInterface->MAP.dhcp6cMAPTparameters), pInterface->Name, pInterface->IP.Ipv6Data.ifname) == RETURN_OK)
                {
                    pInterface->MAP.MaptChanged = FALSE;
                    CcspTraceInfo(("%s %d - Successfully updated MAP-T configure Changes for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
                else
                {
                    CcspTraceError(("%s %d - Failed to configure MAP-T for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
                }
            }
            else
            {
                CcspTraceError((" %s %d - Failed to configure  MAP-T for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
            }
        }
        else
        {
            CcspTraceError(("%s %d - Failed to tear down MAP-T for %s Interface \n", __FUNCTION__, __LINE__, pInterface->Wan.Name));
        }
    }
#ifdef FEATURE_IPOE_HEALTH_CHECK
    else if (pInterface->IP.Ipv6Renewed == TRUE)
    {
        char IHC_V6_status[BUFLEN_16] = {0};
        sysevent_get(sysevent_fd, sysevent_token, IPOE_HEALTH_CHECK_V6_STATUS, IHC_V6_status, sizeof(IHC_V6_status));
        if((strcmp(IHC_V6_status, IPOE_STATUS_FAILED) == 0) && (pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) &&
                (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
        {
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_FIREWALL_RESTART, NULL, 0);
            Force_IPv6_toggle(pInterface->Wan.Name); //Force Ipv6 toggle to update default route
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
        }
        pInterface->IP.Ipv6Renewed = FALSE;
    }
#endif
    wanmgr_Ipv6Toggle();
#if defined(FEATURE_IPOE_HEALTH_CHECK) && defined(IPOE_HEALTH_CHECK_LAN_SYNC_SUPPORT)
    if((pInterface->PPP.Enable == FALSE) && (pInterface->Wan.EnableIPoE == TRUE) && (pWanIfaceCtrl->IhcPid > 0) && (pWanIfaceCtrl->IhcV6Status == IHC_STARTED))
    {
        if(lanState == LAN_STATE_STOPPED)
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_DOWN, pInterface->Wan.Name);
            lanState = LAN_STATE_RESET;
        }
        else if(lanState == LAN_STATE_STARTED)
        {
            WanMgr_SendMsgToIHC(IPOE_MSG_WAN_CONNECTION_IPV6_UP, pInterface->Wan.Name);
            lanState = LAN_STATE_RESET;
        }
    }
#endif
    return WAN_STATE_MAPT_ACTIVE;
}
#endif //FEATURE_MAPT

static eWanState_t wan_state_refreshing_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pWanIfaceCtrl->WanEnable == FALSE ||
        pInterface->Wan.Enable == FALSE ||
        pInterface->SelectionStatus == WAN_IFACE_NOT_SELECTED ||
        pInterface->Phy.Status !=  WAN_IFACE_PHY_STATUS_UP)
    {
        return wan_transition_physical_interface_down(pWanIfaceCtrl);
    }
    else if (pInterface->Wan.LinkStatus == WAN_IFACE_LINKSTATUS_UP &&
             pInterface->Wan.Refresh == TRUE)
    {
        return wan_transition_refreshing_wan(pWanIfaceCtrl);
    }
    else if (pInterface->Wan.LinkStatus == WAN_IFACE_LINKSTATUS_UP &&
             pInterface->Wan.Refresh == FALSE)
    {
        return wan_transition_wan_refreshed(pWanIfaceCtrl);
    }

    return WAN_STATE_REFRESHING_WAN;
}

static eWanState_t wan_state_deconfiguring_wan(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pInterface->Wan.LinkStatus == WAN_IFACE_LINKSTATUS_DOWN )
    {
        return wan_transition_exit(pWanIfaceCtrl);
    }

    return WAN_STATE_DECONFIGURING_WAN;
}

static eWanState_t wan_state_exit(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    //Clear WAN Name
    memset(pWanIfaceCtrl->pIfaceData->Wan.Name, 0, sizeof(pWanIfaceCtrl->pIfaceData->Wan.Name));
    /* Clear DHCP data */
    WanManager_ClearDHCPData(pWanIfaceCtrl->pIfaceData);
    WanMgr_Set_ISM_RunningStatus(pWanIfaceCtrl->pIfaceData->uiIfaceIdx, FALSE);

    return WAN_STATE_EXIT;
}

/*********************************************************************************/
/*********************************************************************************/
/*********************************************************************************/
ANSC_STATUS WanMgr_InterfaceSMThread_Init(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    int retStatus = ANSC_STATUS_SUCCESS;
    return retStatus;
}


ANSC_STATUS WanMgr_InterfaceSMThread_Finalise(void)
{
    int retStatus = ANSC_STATUS_SUCCESS;

    retStatus = WanMgr_CloseIpcServer();
    if(retStatus != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("%s %d - IPC Thread failed to start!\n", __FUNCTION__, __LINE__ ));
    }

    return retStatus;
}


static ANSC_STATUS WanMgr_IfaceIpcMsg_handle(WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl)
{
    if((pWanIfaceCtrl == NULL) || (pWanIfaceCtrl->pIfaceData == NULL))
    {
        return ANSC_STATUS_FAILURE;
    }

    DML_WAN_IFACE* pInterface = pWanIfaceCtrl->pIfaceData;

    if (pInterface->IP.pIpcIpv4Data != NULL )
    {
        wanmgr_handle_dhcpv4_event_data(pInterface);
    }

    if (pInterface->IP.pIpcIpv6Data != NULL )
    {
        wanmgr_handle_dhcpv6_event_data(pInterface);
    }

    return ANSC_STATUS_SUCCESS;
}


static void* WanMgr_InterfaceSMThread( void *arg )
{
    CcspTraceInfo(("%s %d \n", __FUNCTION__, __LINE__));

    //Validate buffer
    if ( NULL == arg )
    {
       CcspTraceError(("%s %d Invalid buffer\n", __FUNCTION__,__LINE__));
       //Cleanup current thread when exit
       pthread_exit(NULL);
    }

    //SM variables
    WanMgr_IfaceSM_Controller_t* pWanIfaceCtrl = ( WanMgr_IfaceSM_Controller_t *) arg;
    WanMgr_Iface_Data_t* pWanDmlIfaceData = NULL;
    eWanState_t iface_sm_state = WAN_STATE_EXIT;
    bool bRunning = true;

    // event handler
    int n = 0;
    struct timeval tv;


    //detach thread from caller stack
    pthread_detach(pthread_self());


    CcspTraceInfo(("%s %d - Interface state machine (TID %lu) initialising for iface idx %d\n", __FUNCTION__, __LINE__, syscall(SYS_gettid), pWanIfaceCtrl->interfaceIdx));


    // initialise state machine
    if(WanMgr_InterfaceSMThread_Init(pWanIfaceCtrl) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("%s %d Policy Controller Error \n", __FUNCTION__, __LINE__));
        return (void *)ANSC_STATUS_FAILURE;
    }

    //Transition Start
    pWanDmlIfaceData = WanMgr_GetIfaceData_locked(pWanIfaceCtrl->interfaceIdx);
    if(pWanDmlIfaceData != NULL)
    {
        pWanIfaceCtrl->pIfaceData = &(pWanDmlIfaceData->data);
        iface_sm_state = wan_transition_start(pWanIfaceCtrl); // do this first before anything else to init variables
        WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
    }
    else
    {
        bRunning = false;
    }

    while (bRunning)
    {
        pWanIfaceCtrl->pIfaceData = NULL;

        /* Wait up to 500 milliseconds */
        tv.tv_sec = 0;
        tv.tv_usec = LOOP_TIMEOUT;

        n = select(0, NULL, NULL, NULL, &tv);
        if (n < 0)
        {
            /* interrupted by signal or something, continue */
            continue;
        }

        //Update Wan config
        WanMgr_Config_Data_t*   pWanConfigData = WanMgr_GetConfigData_locked();
        if(pWanConfigData != NULL)
        {
            pWanIfaceCtrl->WanEnable = pWanConfigData->data.Enable;
            pWanIfaceCtrl->DeviceNwMode = pWanConfigData->data.DeviceNwMode;
            pWanIfaceCtrl->DeviceNwModeChanged = pWanConfigData->data.DeviceNwModeChanged;
            pWanConfigData->data.DeviceNwModeChanged = FALSE;   // setting DeviceNwMode to FALSE, pWanIfaceCtrl->DeviceNwModeChanged will handle the current value
            WanMgrDml_GetConfigData_release(pWanConfigData);
        }

        //Get Interface data
        pWanDmlIfaceData = WanMgr_GetIfaceData_locked(pWanIfaceCtrl->interfaceIdx);
        if(pWanDmlIfaceData != NULL)
        {
            pWanIfaceCtrl->pIfaceData = &(pWanDmlIfaceData->data);
        }

        //Handle IPC messages
        WanMgr_IfaceIpcMsg_handle(pWanIfaceCtrl);

        // Store current state
        pWanIfaceCtrl->eCurrentState = iface_sm_state;
        pWanIfaceCtrl->pIfaceData->eCurrentState = iface_sm_state;

        // process state
        switch (iface_sm_state)
        {
            case WAN_STATE_CONFIGURING_WAN:
                {
                    iface_sm_state = wan_state_configuring_wan(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_VALIDATING_WAN:
                {
                    iface_sm_state = wan_state_validating_wan(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_OBTAINING_IP_ADDRESSES:
                {
                    iface_sm_state = wan_state_obtaining_ip_addresses(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_IPV4_LEASED:
                {
                    iface_sm_state = wan_state_ipv4_leased(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_IPV6_LEASED:
                {
                    iface_sm_state = wan_state_ipv6_leased(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_DUAL_STACK_ACTIVE:
                {
                    iface_sm_state = wan_state_dual_stack_active(pWanIfaceCtrl);
                    break;
                }
#ifdef FEATURE_MAPT
            case WAN_STATE_MAPT_ACTIVE:
                {
                    iface_sm_state = wan_state_mapt_active(pWanIfaceCtrl);
                    break;
                }
#endif //FEATURE_MAPT
            case WAN_STATE_REFRESHING_WAN:
                {
                    iface_sm_state = wan_state_refreshing_wan(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_DECONFIGURING_WAN:
                {
                    iface_sm_state = wan_state_deconfiguring_wan(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_STANDBY:
                {
                    iface_sm_state = wan_state_standby(pWanIfaceCtrl);
                    break;
                }
            case WAN_STATE_EXIT :
            default:
                {
                    iface_sm_state = wan_state_exit(pWanIfaceCtrl);
                    bRunning = false;
                    CcspTraceInfo(("%s %d - Exit from state machine\n", __FUNCTION__, __LINE__));
                    break;
                }
        }

        if(pWanDmlIfaceData != NULL)
        {
            WanMgrDml_GetIfaceData_release(pWanDmlIfaceData);
        }

    }

    WanMgr_InterfaceSMThread_Finalise();


    CcspTraceInfo(("%s %d - Interface state machine (TID %lu) exiting for iface idx %d\n", __FUNCTION__, __LINE__, syscall(SYS_gettid), pWanIfaceCtrl->interfaceIdx));

    int  uptime = 0;
    char buffer[64];
    memset(&buffer, 0, sizeof(buffer));
    WanManager_GetDateAndUptime( buffer, &uptime );
    LOG_CONSOLE("%s [tid=%ld] Wan_exit_complete at %d\n",buffer, syscall(SYS_gettid), uptime);

    //Free current private resource before exit
    if(NULL != pWanIfaceCtrl)
    {
        free(pWanIfaceCtrl);
        pWanIfaceCtrl = NULL;
    }
    pthread_exit(NULL);
}


int WanMgr_StartInterfaceStateMachine(WanMgr_IfaceSM_Controller_t *wanIf)
{
    WanMgr_IfaceSM_Controller_t *     wanIfLocal = NULL;
    pthread_t                wanSmThreadId;
    int                      iErrorCode     = 0;
    static int               siKeyCreated   = 0;

    //Allocate memory and pass it to thread
    wanIfLocal = ( WanMgr_IfaceSM_Controller_t * )malloc( sizeof( WanMgr_IfaceSM_Controller_t ) );
    if( NULL == wanIfLocal )
    {
        CcspTraceError(("%s %d Failed to allocate memory\n", __FUNCTION__, __LINE__));
        return -1;
    }

    //Copy buffer
    memcpy( wanIfLocal , wanIf, sizeof(WanMgr_IfaceSM_Controller_t) );

    CcspTraceInfo (("%s %d - WAN interface data received in the state machine (iface idx %d) \n", __FUNCTION__, __LINE__, wanIfLocal->interfaceIdx));

    //Wanmanager state machine thread
    iErrorCode = pthread_create( &wanSmThreadId, NULL, &WanMgr_InterfaceSMThread, (void*)wanIfLocal );

    if( 0 != iErrorCode )
    {
        CcspTraceError(("%s %d - Failed to start WanManager State Machine Thread EC:%d\n", __FUNCTION__, __LINE__, iErrorCode ));
    }
    else
    {
        CcspTraceInfo(("%s %d - WanManager State Machine Thread Started Successfully\n", __FUNCTION__, __LINE__ ));
    }
    return iErrorCode ;
}


void WanMgr_IfaceSM_Init(WanMgr_IfaceSM_Controller_t* pWanIfaceSMCtrl, INT iface_idx)
{
    if(pWanIfaceSMCtrl != NULL)
    {
        pWanIfaceSMCtrl->WanEnable = FALSE;
        pWanIfaceSMCtrl->interfaceIdx = iface_idx;
#ifdef FEATURE_IPOE_HEALTH_CHECK
        WanMgr_IfaceSM_IHC_Init(pWanIfaceSMCtrl);
#endif
        pWanIfaceSMCtrl->pIfaceData = NULL;        
    }
}
