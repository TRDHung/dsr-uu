/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/protocol.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/proc_fs.h>
#include <linux/socket.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/neighbour.h>
#include <asm/uaccess.h>
#include <linux/netfilter_ipv4.h>
#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif
#include <net/icmp.h>
#include <linux/ctype.h>

#include "dsr.h"
#include "dsr-dev.h"
#include "dsr-io.h"
#include "dsr-pkt.h"
#include "debug.h"
#include "neigh.h"
#include "dsr-rreq.h"
#include "dsr-rrep.h"
#include "maint-buf.h"
#include "send-buf.h"
#include "link-cache.h"

static char *ifname = NULL;
static char *mackill = NULL;

MODULE_AUTHOR("erik.nordstrom@it.uu.se");
MODULE_DESCRIPTION("Dynamic Source Routing (DSR) protocol stack");
MODULE_LICENSE("GPL");

#ifdef KERNEL26
module_param(ifname, charp, 0);
module_param(mackill, charp, 0);
#else
MODULE_PARM(ifname, "s");
MODULE_PARM(mackill, "s");
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
#define NF_INET_PRE_ROUTING NF_IP_PRE_ROUTING
#define NF_INET_LOCAL_IN NF_IP_LOCAL_IN
#define NF_INET_FORWARD NF_IP_FORWARD
#define NF_INET_LOCAL_OUT NF_IP_LOCAL_OUT
#define NF_INET_POST_ROUTING NF_IP_POST_ROUTING
#define NF_INET_NUMHOOKS NF_IP_NUMHOOKS
#endif

#define CONFIG_PROC_NAME "dsr_config"

#define MAX_MACKILL 10

static unsigned char mackill_list[MAX_MACKILL][ETH_ALEN];
static int mackill_len = 0;

static char *confval_names[CONFVAL_TYPE_MAX] = { 
	"Seconds (s)",
	"Milliseconds (ms)",
	"Microseconds (us)",
	"Nanoseconds (ns)",
	"Quanta",
	"Binary",
	"Command"
};

/* Shamelessly stolen from LUNAR <christian.tschudin@unibas.ch> */
static int parse_mackill(void)
{
	char *pa[MAX_MACKILL], *cp;
	int i, j;		// , ia[ETH_ALEN];

	cp = mackill;
	while (cp && mackill_len < MAX_MACKILL) {
		pa[mackill_len] = strsep(&cp, ",");
		if (!pa[mackill_len])
			break;
		mackill_len++;
	}
	for (i = 0; i < mackill_len; i++) {
		// lnx kernel bug in 2.4.X: sscanf format "%x" does not work ....
		cp = pa[i];
		for (j = 0; j < ETH_ALEN; j++, cp++) {
			mackill_list[i][j] = 0;
			for (; isxdigit(*cp); cp++) {
				mackill_list[i][j] =
				    (mackill_list[i][j] << 4) |
				    (*cp <= '9' ? (*cp - '0') :
				     ((*cp & 0x07) + 9));
			}
			if (*cp && *cp != ':')
				break;
		}
		if (j != ETH_ALEN) {
			LOG_DBG("mackill: error in MAC addr %s\n", pa[i]);
			mackill_len--;
			return -1;
		}

		LOG_DBG("mackill +%s\n", print_eth(mackill_list[i]));
	}
	return 0;
}

int do_mackill(char *mac)
{
	int i;

	for (i = 0; i < mackill_len; i++) {
		if (memcmp(mac, mackill_list[i], ETH_ALEN) == 0)
			return 1;
	}
	return 0;
}

int dsr_ip_recv(struct sk_buff *skb)
{
	struct dsr_pkt *dp;
#ifdef ENABLE_DEBUG
	atomic_inc(&num_pkts);
#endif
	LOG_DBG("Received DSR packet\n");

	dp = dsr_pkt_alloc(skb);

	if (!dp) {
		LOG_DBG("Could not allocate DSR packet\n");
		dev_kfree_skb_any(skb);
		return 0;
	}

	if (skb->pkt_type == PACKET_OTHERHOST) {
		dp->flags |= PKT_PROMISC_RECV;
	}
	if ((skb->len + (dp->nh.iph->ihl << 2)) < ntohs(dp->nh.iph->tot_len)) {
		LOG_DBG("Data to short! IP header len=%d tot_len=%d!\n", 
		      skb->len + (dp->nh.iph->ihl << 2), 
		      ntohs(dp->nh.iph->tot_len));
		dsr_pkt_free(dp);
		return 0;
	}

/* 	LOG_DBG("iph_len=%d iph_totlen=%d dsr_opts_len=%d data_len=%d\n", */
/* 	      (dp->nh.iph->ihl << 2), ntohs(dp->nh.iph->tot_len), */
/* 	      dsr_pkt_opts_len(dp), dp->payload_len); */

	/* Add mac address of previous hop to the arp table */
	dsr_recv(dp);

	return 0;
};

static void dsr_ip_recv_err(struct sk_buff *skb, u32 info)
{
	LOG_DBG("received error, info=%u\n", info);

	dev_kfree_skb_any(skb);
}

static int dsr_config_proc_read(char *buffer, char **start, 
				off_t offset, int length,
				int *eof, void *data)
{
	int len = 0;
	int i;

	len += sprintf(buffer + len, 
		       "# %-23s %-6s %s\n", "Name", "Value", "Type");

	for (i = 0; i < CONFVAL_MAX; i++)
		len += sprintf(buffer + len, "%-25s %-6u %s\n",
			       confvals_def[i].name,
			       get_confval(i),
			       confval_names[confvals_def[i].type]);

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}
static int dsr_config_proc_write(struct file *file, const char *buffer,
				 unsigned long count, void *data)
{
#define CMD_MAX_LEN 256
	char cmd[CMD_MAX_LEN];
	int i;

	memset(cmd, '\0', CMD_MAX_LEN);

	if (count > CMD_MAX_LEN)
		return -EINVAL;

	/* Don't read the '\n' or '\0' character... */
	if (copy_from_user(cmd, buffer, count))
		return -EFAULT;

	for (i = 0; i < CONFVAL_MAX; i++) {
		int n = strlen(confvals_def[i].name);

		if (strncmp(cmd, confvals_def[i].name, n) == 0) {
			char *from, *to;
			unsigned int val, val_prev;

			if (confvals_def[i].type == COMMAND) {
				if (i == FlushLinkCache)
					lc_flush();
				break;
			}

			if (strlen(cmd) - 2 <= n)
				continue;

			from = strstr(cmd, "=");
			from++;	/* Exclude '=' */

			val_prev = ConfVal(i);
			val = simple_strtol(from, &to, 10);

			if (confvals_def[i].type == BINARY)
				val = (val ? 1 : 0);

			set_confval(i, val);

			if (i == PromiscOperation && val_prev != val
			    && dsr_node) {
				if (val) {
					LOG_DBG("Setting promiscuous operation\n");
				} else {
					LOG_DBG("Disabling promiscuous operation\n");
                                }

				dsr_node_lock(dsr_node);
				dev_set_promiscuity(dsr_node->slave_dev,
						    val ? 1 : -1);
				dsr_node_unlock(dsr_node);
			}
			if (i == RequestTableSize)
				rreq_tbl_set_max_len(val);

			if (i == RexmtBufferSize)
				maint_buf_set_max_len(val);

			if (i == SendBufferSize)
				send_buf_set_max_len(val);

			LOG_DBG("Setting %s to %d\n", confvals_def[i].name, val);
		}
	}
	return count;
}

/* This hook is used to do mac filtering or to receive promiscuously snooped
 * packets */
static unsigned int dsr_pre_routing_recv(unsigned int hooknum,
					 struct sk_buff *skb,
					 const struct net_device *in,
					 const struct net_device *out,
					 int (*okfn) (struct sk_buff *))
{
	if (in && in->ifindex == get_slave_dev_ifindex() &&
	    (skb)->protocol == htons(ETH_P_IP)) {

		if (do_mackill(SKB_MAC_HDR_RAW(skb) + ETH_ALEN))
			return NF_DROP;
	}
	return NF_ACCEPT;
}

/* This hook is the architecturally correct place to look at DSR packets that
 * are to be forwarded. This enables you to, for example, disable forwarding by
 * setting "/proc/sys/net/ipv4/conf/<eth*>/forwarding" to 0. */
static unsigned int dsr_ip_forward_recv(unsigned int hooknum,
					struct sk_buff *skb,
					const struct net_device *in,
					const struct net_device *out,
					int (*okfn) (struct sk_buff *))
{
	struct iphdr *iph = SKB_NETWORK_HDR_IPH(skb);
	struct in_addr myaddr = my_addr();

	if (in && in->ifindex == get_slave_dev_ifindex() &&
	    skb->protocol == htons(ETH_P_IP)) {
		
		if (iph && iph->protocol == IPPROTO_DSR &&
		    iph->daddr != myaddr.s_addr &&
		    iph->daddr != DSR_BROADCAST) {
			
			skb->data = SKB_NETWORK_HDR_RAW(skb) + (iph->ihl << 2);
			skb->len = SKB_TAIL(skb) - skb->data;
			dsr_ip_recv(skb);

			return NF_STOLEN;
		}
	}
	return NF_ACCEPT;
}

static struct nf_hook_ops dsr_pre_routing_hook = {

	.hook = dsr_pre_routing_recv,
#ifdef KERNEL26
	.owner = THIS_MODULE,
#endif
	.pf = PF_INET,
	.hooknum = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static struct nf_hook_ops dsr_ip_forward_hook = {

	.hook = dsr_ip_forward_recv,
#ifdef KERNEL26
	.owner = THIS_MODULE,
#endif
	.pf = PF_INET,
	.hooknum = NF_INET_FORWARD,
	.priority = NF_IP_PRI_FIRST,
};

/* This is kind of a mess */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
static struct inet_protocol dsr_inet_prot = {
#else
static struct net_protocol dsr_inet_prot = {
#endif
	.handler = dsr_ip_recv,
	.err_handler = dsr_ip_recv_err,
#ifdef KERNEL26
	.no_policy = 1,
	.netns_ok = 1,
#else
	.protocol = IPPROTO_DSR,
	.name = "DSR"
#endif
};

static int __init dsr_module_init(void)
{
	int res = -EAGAIN;
	struct proc_dir_entry *proc;

#ifdef DEBUG
	dbg_init();
#endif
	parse_mackill();
	
	res = dsr_dev_init(ifname);

	if (res < 0) {
		LOG_DBG("dsr-dev init failed\n");
		return -EAGAIN;
	}

	res = send_buf_init();

	if (res < 0)
		goto cleanup_dsr_dev;

	res = rreq_tbl_init();

	if (res < 0)
		goto cleanup_send_buf;

	res = grat_rrep_tbl_init();

	if (res < 0)
		goto cleanup_grat_rrep_tbl;

	res = neigh_tbl_init();

	if (res < 0)
		goto cleanup_rreq_tbl;

	res = nf_register_hook(&dsr_pre_routing_hook);

	if (res < 0)
		goto cleanup_neigh_tbl;

	res = nf_register_hook(&dsr_ip_forward_hook);

	if (res < 0)
		goto cleanup_nf_hook2;

	res = maint_buf_init();

	if (res < 0)
		goto cleanup_nf_hook1;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23))
#define proc_net init_net.proc_net
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
	proc = create_proc_entry(CONFIG_PROC_NAME, S_IRUGO | S_IWUSR, proc_net);

	if (!proc)
		goto cleanup_maint_buf;

	proc->owner = THIS_MODULE;
	proc->read_proc = dsr_config_proc_read;
	proc->write_proc = dsr_config_proc_write;
#else
	/* create_proc_entry is removed from 3.10, use proc_create instead */
	static const struct file_operations dsr_config_proc_fops = {
		.owner = THIS_MODULE,
		.open  = dsr_config_proc_read,
		.read  = dsr_config_proc_write,
	};

	proc = proc_create(CONFIG_PROC_NAME, S_IRUGO | S_IWUSR, proc_net,
												&dsr_config_proc_fops);
	if (!proc)
		goto cleanup_maint_buf;

#endif

#ifndef KERNEL26
	inet_add_protocol(&dsr_inet_prot);
	LOG_DBG("Setup finished\n");
	return 0;
#else
	res = inet_add_protocol(&dsr_inet_prot, IPPROTO_DSR);

	if (res < 0) {
		LOG_DBG("Could not register inet protocol\n");
		goto cleanup_proc;
	}

	LOG_DBG("Setup finished res=%d\n", res);

	return 0;
cleanup_proc:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_remove(CONFIG_PROC_NAME);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
	proc_net_remove(&init_net, CONFIG_PROC_NAME);
#else
	remove_proc_entry (CONFIG_PROC_NAME, proc_net);
#endif

#endif /* KERNEL26 */

cleanup_maint_buf:
	maint_buf_cleanup();
cleanup_nf_hook1:
	nf_unregister_hook(&dsr_ip_forward_hook);
cleanup_nf_hook2:
	nf_unregister_hook(&dsr_pre_routing_hook);
cleanup_neigh_tbl:
	neigh_tbl_cleanup();
cleanup_grat_rrep_tbl:
	grat_rrep_tbl_cleanup();
cleanup_rreq_tbl:
	rreq_tbl_cleanup();
cleanup_send_buf:
	send_buf_cleanup();
cleanup_dsr_dev:
	dsr_dev_cleanup();
#ifdef DEBUG
	dbg_cleanup();
#endif
	return res;
}

static void __exit dsr_module_cleanup(void)
{
#ifdef KERNEL26
	inet_del_protocol(&dsr_inet_prot, IPPROTO_DSR);
#else
	inet_del_protocol(&dsr_inet_prot);
#endif
	nf_unregister_hook(&dsr_pre_routing_hook);
	nf_unregister_hook(&dsr_ip_forward_hook);
	dsr_dev_cleanup();
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_remove(CONFIG_PROC_NAME);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
	proc_net_remove(&init_net, CONFIG_PROC_NAME);
#else
	/* proc_net_remove is removed from 3.10, use remove_proc_entry */
	remove_proc_entry (CONFIG_PROC_NAME, proc_net);
#endif
	rreq_tbl_cleanup();
	grat_rrep_tbl_cleanup();
	neigh_tbl_cleanup();
	maint_buf_cleanup();
	send_buf_cleanup();
#ifdef DEBUG
	dbg_cleanup();
#endif
}

module_init(dsr_module_init);
module_exit(dsr_module_cleanup);
