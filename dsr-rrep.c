#include <linux/string.h>
#include <net/ip.h>
#
#include "dsr.h"
#include "debug.h"
#include "dsr-rrep.h"
#include "dsr-srt.h"
#include "dsr-rtc.h"
#include "p-queue.h"

static inline int dsr_rrep_add_srt(dsr_rrep_opt_t *rrep, dsr_srt_t *srt)
{
	int n;

	if (!rrep | !srt)
		return -1;

	n = srt->laddrs / sizeof(struct in_addr);

	memcpy(rrep->addrs, srt->addrs, srt->laddrs);
	memcpy(&rrep->addrs[n], &srt->dst, sizeof(struct in_addr));
	
	return 0;
}

static dsr_rrep_opt_t *dsr_rrep_opt_add(char *buf, int len, dsr_srt_t *srt)
{
	dsr_rrep_opt_t *rrep;
	
	if (!buf || !srt || len < DSR_RREP_OPT_LEN(srt))
		return NULL;

	rrep = (dsr_rrep_opt_t *)buf;
	
	rrep->type = DSR_OPT_RREP;
	rrep->length = srt->laddrs + sizeof(struct in_addr) + 1;
	rrep->l = 0;
	rrep->res = 0;

	/* Add source route to RREP */
	dsr_rrep_add_srt(rrep, srt);
	
	return rrep;	
}

int dsr_rrep_create(dsr_pkt_t *dp)
{
	dsr_rrep_opt_t *rrep;
	char *off;
	int l;
	
	l = IP_HDR_LEN + DSR_OPT_HDR_LEN + 
		DSR_SRT_OPT_LEN(srt) + DSR_RREP_OPT_LEN(srt);
	
	if (!dp || !dp->srt || dp->len < l)
		return -1;
	
	dp->dst.s_addr = srt->src.s_addr;
	off = dp->data;
	
	dsr_build_ip(off, l, ldev_info.ifaddr, dp->dst, 1);
	
	off += IP_HDR_LEN;
	l -= IP_HDR_LEN;
	
	dsr_hdr_add(off, l, 0);
	
	off += DSR_OPT_HDR_LEN;
	l -= DSR_OPT_HDR_LEN;
	
	/* Add the source route option to the packet */
	dsr_srt_opt_add(off, l, dp->srt);
	
	off += DSR_SRT_OPT_LEN(dp->srt);
	l -= DSR_SRT_OPT_LEN(dp->srt);

	rrep = dsr_rrep_opt_add(off, l, srt);

	if (!rrep) {
		DEBUG("Could not create RREQ\n");
		return -1;
	}
	return 0;
}

int dsr_rrep_recv(dsr_pkt_t *dp)
{
	dsr_rrep_opt_t *rrep;
	
	if (!dp || !dp->rrep)
		return DSR_PKT_DROP;

	rrep = dp->rrep;
	
	dp->srt = dsr_srt_new(dp->dst, dp->src, DSR_RREP_ADDRS_LEN(rrep), 
			  rrep->addrs);
	
	if (dp->srt) {
		DEBUG("Adding srt to cache\n");
		dsr_rtc_add(dp->srt, 60000, 0);
	}
		
	if (dp->dst.s_addr == ldev_info.ifaddr.s_addr) {
		/*RREP for this node */
		
		DEBUG("RREP for me!\n");
				
		/* Send buffered packets */
		p_queue_set_verdict(P_QUEUE_SEND, dp->dst.s_addr);
				
	} else {
		DEBUG("I am not RREP destination\n");
		/* Forward */
		return DSR_PKT_FORWARD;
	}
	
	return DSR_PKT_DROP;
}
