/*******************************************************************************
 * Copyright (c) 2023, Peter Christen
 * All rights reserved.
 *
 ******************************************************************************/
#include "generic_networkhandler.h"
#include "opener_api.h"
#include "cipethernetlink.h"
#include "ciptcpipinterface.h"
#include "trace.h"
#include "networkconfig.h"
#include "doublylinkedlist.h"
#include "cipconnectionobject.h"
#include <arpa/inet.h>
#include <netdev.h>

#define OPENER_THREAD_PRIO			8
#define OPENER_STACK_SIZE			  10240

static void opener_thread(void *argument);
rt_thread_t opener_ThreadId;
volatile int g_end_stack = 0;

/**
 * @brief   Initializes the OpENer Ethernet/IP stack
 *          The network interface has to be configured and the link established
 * @param   name      the network interface name
 * @retval  None
 */
void opener_init(const char *name) {

  EipStatus eip_status = 0;
  struct netdev *netdev = netdev_get_by_name(name);
  if (netdev == NULL) {
    OPENER_TRACE_ERR("Interface %s not found\n", name);
    return;
  }

  if (netdev_is_link_up(netdev)) {
    DoublyLinkedListInitialize(&connection_list,
                               CipConnectionObjectListArrayAllocator,
                               CipConnectionObjectListArrayFree);

    /* Fetch MAC address from the platform */
    uint8_t iface_mac[6];
    IfaceGetMacAddress(name, iface_mac);

    /* for a real device the serial number should be unique per device */
    SetDeviceSerialNumber(123456789);

    /* unique_connection_id should be sufficiently random or incremented and stored
     *  in non-volatile memory each time the device boots.
     */
    EipUint16 unique_connection_id = rand();

    /* Setup the CIP Layer. All objects are initialized with the default
     * values for the attribute contents. */
    eip_status = CipStackInit(unique_connection_id);

    CipEthernetLinkSetMac(iface_mac);

    /* The current host name is used as a default. */
    GetHostName(&g_tcpip.hostname);

    /* register for closing signals so that we can trigger the stack to end */
    g_end_stack = 0;


    eip_status = IfaceGetConfiguration(name, &g_tcpip.interface_configuration);
    if (eip_status < 0) {
      OPENER_TRACE_WARN("Problems getting interface configuration\n");
    }

    eip_status = NetworkHandlerInitialize();
  }
  else {
    OPENER_TRACE_WARN("Network link is down, OpENer not started\n");
    g_end_stack = 1;  // end in case of network link is down
  }
  if ((g_end_stack == 0) && (eip_status == kEipStatusOk)) {
    opener_ThreadId = rt_thread_create("OpENer", opener_thread, netdev,
                                       OPENER_STACK_SIZE, OPENER_THREAD_PRIO, 10);
    rt_thread_startup(opener_ThreadId);
    OPENER_TRACE_INFO("OpENer: opener_thread started\n");
  } else {
    OPENER_TRACE_ERR("NetworkHandlerInitialize error %d\n", eip_status);
  }
}

static void opener_thread(void *argument) {
  struct netdev *netdev = (struct netdev*) argument;
  /* The event loop. Put other processing you need done continually in here */
  while (!g_end_stack) {
    if (kEipStatusOk != NetworkHandlerProcessCyclic()) {
      OPENER_TRACE_ERR("Error in NetworkHandler loop! Exiting OpENer!\n");
      g_end_stack = 1;	// end loop in case of error
    }
    if (!netdev_is_link_up(netdev)) {
      OPENER_TRACE_INFO("Network link is down, exiting OpENer\n");
      g_end_stack = 1;	// end loop in case of network link is down
    }
  }		// loop ended
  /* clean up network state */
  NetworkHandlerFinish();
  /* close remaining sessions and connections, clean up used data */
  ShutdownCipStack();
}

static int opener_sample(int argc, char *argv[]) {
  if (argc != 2) {
    rt_kprintf("Usage: %s <interface name>\r\n\n", argv[0]);
    return -1;
  }

  opener_init(argv[1]);
  return 0;
}
MSH_CMD_EXPORT(opener_sample, OpENer sample application);
