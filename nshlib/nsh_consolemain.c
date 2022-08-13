/****************************************************************************
 * apps/nshlib/nsh_consolemain.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <assert.h>

#include <sys/boardctl.h>

#include "nsh.h"
#include "nsh_console.h"

#include "netutils/netinit.h"

#if !defined(CONFIG_NSH_ALTCONDEV) && !defined(HAVE_USB_CONSOLE) && \
    !defined(HAVE_USB_KEYBOARD)


#include "netutils/netlib.h"
#include <sys/mount.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

static int sd;

void reproduce_issue()
{
	/* Wait for the PHY to detect the link. */

	sleep(5);


	/* Mount procfs. */

	mount(NULL, "/proc", "procfs", 0, NULL);


	/* Configure the Ethernet interface. */

	/*
	 * Note! The following may need to be adjusted
	 * for your network configuration.
	 */

	netlib_ifdown("eth0");

	uint8_t mac[6] = { 0x80, 0x1F, 0x12, 0x38, 0xDD, 0x93 };
	netlib_setmacaddr("eth0", mac);

	struct in_addr addr;

	addr.s_addr = htonl(0xc0a801c8);
	netlib_set_ipv4addr("eth0", &addr);

	addr.s_addr = htonl(0xffffff00);
	netlib_set_ipv4netmask("eth0", &addr);

	addr.s_addr = htonl(0xc0a80101);
	netlib_set_dripv4addr("eth0", &addr);

	addr.s_addr = htonl(0x08080808);
	netlib_set_ipv4dnsaddr(&addr);

	netlib_ifup("eth0");


	/* Connect to a server. */

	struct sockaddr_in server;

	char serv[16];
	itoa(1883, serv, 10);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo * info;

	if (getaddrinfo("test.mosquitto.org", serv, &hints, &info) != 0)
		return;

	memcpy(&server, info->ai_addr, info->ai_addrlen);

	freeaddrinfo(info);

	if (server.sin_family != AF_INET)
		return;

	sd = socket(AF_INET, SOCK_STREAM, 0);

	struct timeval tv;
	tv.tv_sec  = 5;
	tv.tv_usec = 0;
	setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval));
	setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));

	connect(sd, (struct sockaddr*)&server, sizeof(struct sockaddr_in));

	send(sd, "some data", strlen("some data"), 0);


	/*
	 * Now everything is set-up correctly to reproduce the issue.
	 * A TCP connection is active, we only have to "properly"
	 * close it to trigger the crash.
	 */

	/*
	 * Note that the scheduler is locked here.
	 * This is important to reproduce the issue.
	 * It simulates the interface going down, before the
	 * server has the chance to send a FIN ACK.
	 */

	sched_lock();

	close(sd);
	netlib_ifdown("eth0");

	sched_unlock();


	/* The system should have crashed now. */

	while (1)
		sleep(1);
}



/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nsh_consolemain (Normal character device version)
 *
 * Description:
 *   This interface may be to called or started with task_start to start a
 *   single an NSH instance that operates on stdin and stdout.  This
 *   function does not normally return (see below).
 *
 *   This version of nsh_consolemain handles generic /dev/console character
 *   devices (see nsh_usbconsole.c and usb_usbkeyboard for other versions
 *   for special USB console devices).
 *
 * Input Parameters:
 *   Standard task start-up arguments.  These are not used.  argc may be
 *   zero and argv may be NULL.
 *
 * Returned Values:
 *   This function does not normally return.  exit() is usually called to
 *   terminate the NSH session.  This function will return in the event of
 *   an error.  In that case, a non-zero value is returned (EXIT_FAILURE=1).
 *
 ****************************************************************************/

int nsh_consolemain(int argc, FAR char *argv[])
{
  FAR struct console_stdio_s *pstate = nsh_newconsole(true);
  int ret;

  DEBUGASSERT(pstate != NULL);

#ifdef CONFIG_NSH_USBDEV_TRACE
  /* Initialize any USB tracing options that were requested */

  usbtrace_enable(TRACE_BITSET);
#endif

#if defined(CONFIG_NSH_ROMFSETC) && !defined(CONFIG_NSH_DISABLESCRIPT)
  /* Execute the system init script */

  nsh_sysinitscript(&pstate->cn_vtbl);
#endif

#ifdef CONFIG_NSH_NETINIT
  /* Bring up the network */

  netinit_bringup();
#endif

#if defined(CONFIG_NSH_ARCHINIT) && defined(CONFIG_BOARDCTL_FINALINIT)
  /* Perform architecture-specific final-initialization (if configured) */

  boardctl(BOARDIOC_FINALINIT, 0);
#endif

#if defined(CONFIG_NSH_ROMFSETC) && !defined(CONFIG_NSH_DISABLESCRIPT)
  /* Execute the start-up script */

  nsh_initscript(&pstate->cn_vtbl);
#endif

  /* Reproduce the TCP issue. */

  reproduce_issue();

  /* Execute the session */

  ret = nsh_session(pstate, true, argc, argv);

  /* Exit upon return */

  nsh_exit(&pstate->cn_vtbl, ret);
  return ret;
}

#endif /* !HAVE_USB_CONSOLE && !HAVE_USB_KEYBOARD !HAVE_SLCD_CONSOLE */
