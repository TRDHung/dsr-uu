/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#ifdef __KERNEL__
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#include <linux/config.h>
#endif
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <linux/icmp.h>
#include <net/icmp.h>
#endif

#ifdef NS2
#include "ns-agent.h"
#endif

#include "tbl.h"
#include "send-buf.h"
#include "debug.h"
#include "link-cache.h"
#include "dsr-srt.h"
#include "timer.h"

#ifdef __KERNEL__
#define SEND_BUF_PROC_FS_NAME "send_buf"

TBL(send_buf, SEND_BUF_MAX_LEN);
static DSRUUTimer send_buf_timer;
static int send_buf_print(struct tbl *t, char *buffer);
#endif

struct send_buf_entry {
	list_t l;
	struct dsr_pkt *dp;
	struct timeval qtime;
	xmit_fct_t okfn;
};


static inline int crit_addr(void *pos, void *addr)
{
	struct in_addr *a = (struct in_addr *)addr;
	struct send_buf_entry *e = (struct send_buf_entry *)pos;

	if (e->dp->dst.s_addr == a->s_addr)
		return 1;
	return 0;
}

static inline int crit_garbage(void *pos, void *n)
{
	struct timeval *now = (struct timeval *)n;
	struct send_buf_entry *e = (struct send_buf_entry *)pos;

	if (timeval_diff(now, &e->qtime) >=
	    (int)ConfValToUsecs(SendBufferTimeout)) {
		if (e->dp)
			dsr_pkt_free(e->dp);
		return 1;
	}
	return 0;
}

void NSCLASS send_buf_set_max_len(unsigned int max_len)
{
	send_buf.max_len = max_len;
}

void NSCLASS send_buf_timeout(unsigned long data)
{
	struct send_buf_entry *e;
	int pkts;
	struct timeval expires, now;
/* 	char buf[2048]; */
	
	gettime(&now);

/* 	send_buf_print(&send_buf, buf); */
/* 	LOG_DBG("\n%s\n", buf); */

	pkts = tbl_for_each_del(&send_buf, &now, crit_garbage);

	LOG_DBG("%d packets garbage collected\n", pkts);

	read_lock_bh(&send_buf.lock);
	
	/* Get first packet in maintenance buffer */
	e = (struct send_buf_entry *)__tbl_find(&send_buf, NULL, crit_none);

	if (!e) {
		LOG_DBG("No packet to set timeout for\n");
		read_unlock_bh(&send_buf.lock);
		return;
	}
	expires = e->qtime;

	timeval_add_usecs(&expires, ConfValToUsecs(SendBufferTimeout));

	LOG_DBG("now=%s qtime=%s exp=%s\n", 
                print_timeval(&now), 
                print_timeval(&e->qtime), 
                print_timeval(&expires));
        
	read_unlock_bh(&send_buf.lock);

	set_timer(&send_buf_timer, &expires);
}

static struct send_buf_entry *send_buf_entry_create(struct dsr_pkt *dp,
						    xmit_fct_t okfn)
{
	struct send_buf_entry *e;

	e = (struct send_buf_entry *)kmalloc(sizeof(*e), GFP_ATOMIC);

	if (!e)
		return NULL;

	e->dp = dp;
	e->okfn = okfn;
	gettime(&e->qtime);

	return e;
}

int NSCLASS send_buf_enqueue_packet(struct dsr_pkt *dp, xmit_fct_t okfn)
{
	struct send_buf_entry *e;
	struct timeval expires;
	int res, empty = 0;
	
	e = send_buf_entry_create(dp, okfn);

	if (!e)
		return -ENOMEM;

	LOG_DBG("enqueing packet to %s\n", print_ip(dp->dst));

	write_lock_bh(&send_buf.lock);
	
	if (tbl_empty(&send_buf))
		empty = 1;

	res = __tbl_add_tail(&send_buf, &e->l);

	if (res < 0) {
		struct send_buf_entry *f;

		LOG_DBG("buffer full, removing first\n");
		f = (struct send_buf_entry *)__tbl_detach_first(&send_buf);

		if (f) {
			dsr_pkt_free(f->dp);
			kfree(f);
		}

		res = tbl_add_tail(&send_buf, &e->l);

		if (res < 0) {
			LOG_DBG("Could not buffer packet\n");
			kfree(e);
			write_unlock_bh(&send_buf.lock);
			return -ENOSPC;
		}
	}

	write_unlock_bh(&send_buf.lock);

	if (empty) {
		gettime(&expires);
		timeval_add_usecs(&expires, ConfValToUsecs(SendBufferTimeout));
		set_timer(&send_buf_timer, &expires);
	}

	return res;
}

int NSCLASS send_buf_set_verdict(int verdict, struct in_addr dst)
{
	struct send_buf_entry *e;
	int pkts = 0;

	write_lock_bh(&send_buf.lock);

	switch (verdict) {
	case SEND_BUF_DROP:

		while ((e =
			(struct send_buf_entry *)__tbl_find_detach(&send_buf,
								   &dst,
								   crit_addr))) {
			/* Only send one ICMP message */
#ifdef __KERNEL__
			if (pkts == 0)
				icmp_send(e->dp->skb, ICMP_DEST_UNREACH,
					  ICMP_HOST_UNREACH, 0);
#endif
			dsr_pkt_free(e->dp);
			kfree(e);
			pkts++;
		}
		LOG_DBG("Dropped %d queued pkts for %s\n", pkts, print_ip(dst));
		break;
	case SEND_BUF_SEND:

	  while ((e =
		  (struct send_buf_entry *)__tbl_find_detach(&send_buf,
							     &dst,
							     crit_addr))) {
	    LOG_DBG("Send packet\n");
	    /* Get source route */
			e->dp->srt = dsr_rtc_find(e->dp->src, e->dp->dst);

			if (e->dp->srt) {

				if (dsr_srt_add(e->dp) < 0) {
					LOG_DBG("Could not add source route\n");
					dsr_pkt_free(e->dp);
				} else
					/* Send packet */
#ifdef NS2
					(this->*e->okfn) (e->dp);
#else
					e->okfn(e->dp);
#endif
			} else {
				LOG_DBG("No source route found for %s!\n",
                                        print_ip(dst));

				dsr_pkt_free(e->dp);
			}
			pkts++;
			kfree(e);
		}
		LOG_DBG("Sent %d queued packets to %s\n", pkts, print_ip(dst));

		/*      if (pkts == 0) */
/* 			LOG_DBG("No packets for dest %s\n", print_ip(dst)); */
		break;
	}

	write_unlock_bh(&send_buf.lock);

	return pkts;
}

static inline int send_buf_flush(struct tbl *t)
{
	struct send_buf_entry *e;
	int pkts = 0;
	/* Flush send buffer */
	write_lock_bh(&t->lock);
	while ((e = (struct send_buf_entry *)
		__tbl_find_detach(t, NULL, crit_none))) {
		dsr_pkt_free(e->dp);
		kfree(e);
		pkts++;
	}
	write_unlock_bh(&t->lock);
	return pkts;
}

#ifdef __KERNEL__
static int send_buf_print(struct tbl *t, char *buffer)
{
	list_t *p;
	int len;
	struct timeval now;

	gettime(&now);

	len = sprintf(buffer, "# %-15s %-8s\n", "IPAddr", "Age (s)");

	read_lock_bh(&t->lock);

	list_for_each(p, &t->head) {
		struct send_buf_entry *e = (struct send_buf_entry *)p;

		if (e && e->dp)
			len += sprintf(buffer + len, "  %-15s %-8lu\n",
				       print_ip(e->dp->dst),
				       timeval_diff(&now, &e->qtime) / 1000000);
	}

	len += sprintf(buffer + len,
		       "\nQueue length      : %u\n"
		       "Queue max. length : %u\n", t->len, t->max_len);

	read_unlock_bh(&t->lock);

	return len;
}

static int
send_buf_get_info(char *buffer, char **start, off_t offset, 
		  int length, int *eof, void *data)
{
	int len;

	len = send_buf_print(&send_buf, buffer);

	*start = buffer + offset;
	len -= offset;

	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

/* Similar to above function, for using with proc_create() */
static int send_buf_proc_show(struct seq_file *m, void *v)
{
	list_t *p;
	int len;
	struct timeval now;
	struct tbl *t = &send_buf;

	gettime(&now);

	seq_printf(m, "# %-15s %-8s\n", "IPAddr", "Age (s)");

	read_lock_bh(&t->lock);

	list_for_each(p, &t->head) {
		struct send_buf_entry *e = (struct send_buf_entry *)p;

		if (e && e->dp)
			seq_printf(m, "  %-15s %-8lu\n",
				       print_ip(e->dp->dst),
				       timeval_diff(&now, &e->qtime) / 1000000);
	}

	seq_printf(m, "\nQueue length      : %u\n"
		             "Queue max. length : %u\n", t->len, t->max_len);

	read_unlock_bh(&t->lock);

	return 0;
}

/* For using with proc_create() */
static int send_buf_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, send_buf_proc_show, NULL);
}
static const struct file_operations send_buf_proc_fops = {
       .open           = send_buf_proc_open,
       .read           = seq_read,
       .llseek         = seq_lseek,
       .release        = seq_release,
};
#endif				/* __KERNEL__ */

int __init NSCLASS send_buf_init(void)
{
#ifdef __KERNEL__
	struct proc_dir_entry *proc;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23))
#define proc_net init_net.proc_net
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
	proc = create_proc_read_entry(SEND_BUF_PROC_FS_NAME, 0, 
				      proc_net, send_buf_get_info, NULL);
#else
	/* create_proc_read_entry() function is deprecated, use proc_create
	 * instead */
	proc = proc_create(SEND_BUF_PROC_FS_NAME, 0444, proc_net, &send_buf_proc_fops);
#endif

	if (!proc) {
		printk(KERN_ERR "send_buf: failed to create proc entry\n");
		return -1;
	}
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
	proc->owner = THIS_MODULE;
#endif

#endif
	INIT_TBL(&send_buf, SEND_BUF_MAX_LEN);

	init_timer(&send_buf_timer);

	send_buf_timer.function = &NSCLASS send_buf_timeout;

	return 1;
}

void __exit NSCLASS send_buf_cleanup(void)
{
	int pkts;
#ifdef KERNEL26
	synchronize_net();
#endif
	if (timer_pending(&send_buf_timer))
		del_timer_sync(&send_buf_timer);

	pkts = send_buf_flush(&send_buf);

	LOG_DBG("Flushed %d packets\n", pkts);

#ifdef __KERNEL__
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_remove(SEND_BUF_PROC_FS_NAME);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
	proc_net_remove(&init_net, SEND_BUF_PROC_FS_NAME);
#else
	remove_proc_entry (SEND_BUF_PROC_FS_NAME, proc_net);
#endif

#endif
}
