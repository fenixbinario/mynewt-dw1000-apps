/**
 * Copyright (C) 2017-2018, Decawave Limited, All Rights Reserved
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_bsp.h"
#ifdef ARCH_sim
#include "mcu/mcu_sim.h"
#endif

#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_hal.h>
#include <dw1000/dw1000_phy.h>
#include <dw1000/dw1000_mac.h>
#include <dw1000/dw1000_rng.h>
#include <dw1000/dw1000_lwip.h>
#include <dw1000/dw1000_ftypes.h>

#include <lwip/init.h>
#include <lwip/ethip6.h>
#include <lwip/raw.h>
#include "lwip/inet_chksum.h"
#include "lwip/icmp.h"
#include <netif/lowpan6.h>

#define PING_ID 0xDECA
#define PING_DATA_SIZE 2
#define RX_STATUS true

static
dw1000_lwip_config_t lwip_config = {
	.poll_resp_delay = 0x4800,
	.resp_timeout = 0xF000,
	.uwbtime_to_systime = 0
};


ip_addr_t my_ip_addr = {
	.addr[0] = MYNEWT_VAL(DEV_IP6_ADDR_1),
	.addr[1] = MYNEWT_VAL(DEV_IP6_ADDR_2),
	.addr[2] = MYNEWT_VAL(DEV_IP6_ADDR_3),
	.addr[3] = MYNEWT_VAL(DEV_IP6_ADDR_4)
};

ip_addr_t tgt_ip_addr = {
	.addr[0] = MYNEWT_VAL(TGT_IP6_ADDR_1),
	.addr[1] = MYNEWT_VAL(TGT_IP6_ADDR_2),
	.addr[2] = MYNEWT_VAL(TGT_IP6_ADDR_3),
	.addr[3] = MYNEWT_VAL(TGT_IP6_ADDR_4)
};


struct netif dw1000_netif;
static struct raw_pcb *ping_pcb;
ip_addr_t ip6_tgt_addr[LWIP_IPV6_NUM_ADDRESSES];

struct pbuf *pb;

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);


int
main(int argc, char **argv){

	int rc;

	dw1000_dev_instance_t * inst = hal_dw1000_inst(0);
	dw1000_lwip_context_t *cntxt;

	sysinit();
	dw1000_softreset(inst);

	inst->PANID = MYNEWT_VAL(DEVICE_PAN_ID);
	inst->my_short_address = MYNEWT_VAL(SHORT_ADDRESS);

	dw1000_set_panid(inst,inst->PANID);
	dw1000_low_level_init(inst, NULL, NULL);
	dw1000_lwip_init(inst, &lwip_config, MYNEWT_VAL(NUM_FRAMES), MYNEWT_VAL(BUFFER_SIZE));

	dw1000_netif_config(inst, &dw1000_netif, &my_ip_addr, RX_STATUS);

	lwip_init();

	dw1000_netif.flags |= NETIF_FLAG_UP | NETIF_FLAG_BROADCAST | NETIF_FLAG_LINK_UP;
	assert(dw1000_netif.state);
	cntxt = (dw1000_lwip_context_t*)dw1000_netif.state;
	IP_ADDR6(ip6_tgt_addr, tgt_ip_addr.addr[0], tgt_ip_addr.addr[1], tgt_ip_addr.addr[2], tgt_ip_addr.addr[3]);

	ping_pcb = raw_new(IP_PROTO_ICMP);
	raw_bind(ping_pcb, dw1000_netif.ip6_addr);
	raw_connect(ping_pcb, ip6_tgt_addr);
	raw_recv(ping_pcb, ping_recv, NULL);

	while (1) {
		if (inst->lwip->status.start_rx_error)
			printf("timer_ev_cb:[start_rx_error]\n");
		if (inst->lwip->status.rx_error)
			printf("timer_ev_cb:[rx_error]\n");
		if (inst->lwip->status.request_timeout){
			printf("timer_ev_cb:[request_timeout]\n");
		}
		if (inst->lwip->status.rx_timeout_error){
			printf("timer_ev_cb:[rx_timeout_error]\n");
		}

		if (inst->lwip->status.rx_error || inst->lwip->status.request_timeout ||  inst->lwip->status.rx_timeout_error){
			inst->lwip->status.start_tx_error = inst->lwip->status.rx_error = inst->lwip->status.request_timeout = inst->lwip->status.rx_timeout_error = 0;
		}

		cntxt->rx_cb.recv(inst, 0xffff);
	}

	assert(0);
	return rc;
}


static u8_t
ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr){

	LWIP_UNUSED_ARG(arg);
	LWIP_UNUSED_ARG(pcb);
	LWIP_UNUSED_ARG(addr);
	LWIP_ASSERT("p != NULL", p != NULL);
	assert(dw1000_netif.state);
	/*TODO: Read data from received packet/frame.
		This is a user defined function.
	 */
	struct icmp_echo_hdr *iecho;

	if (pbuf_header( p, -PBUF_IP_HLEN)==0) {
		iecho = (struct icmp_echo_hdr *)p->payload;

		if ((iecho->id == PING_ID)) {
			printf("\n\t[Ping Received]\n\tSeq # - %d\n\n", iecho->seqno);
			memp_free(MEMP_PBUF_POOL,p);
			return 1; /* eat the packet */
		}
	}
	memp_free(MEMP_PBUF_POOL,p);
	return 0; /* don't eat the packet */
}