#include <linux/slab.h>

#include "dsr.h"
#include "dsr-srt.h"
#include "debug.h"

char *print_srt(dsr_srt_t *srt)
{
#define BUFLEN 256
	static char buf[BUFLEN];
	int i, len;

	if (!srt)
		return NULL;
	
	len = sprintf(buf, "%s-", print_ip(srt->src.s_addr));
	
	for (i = 0; i < (srt->laddrs / sizeof(u_int32_t)) && 
		     (len + 16) < BUFLEN; i++)
		len += sprintf(buf+len, "%s-", print_ip(srt->addrs[i].s_addr));
	
	if ((len + 16) < BUFLEN)
		len = sprintf(buf+len, "%s", print_ip(srt->dst.s_addr));
	return buf;
}

dsr_srt_t *dsr_srt_new(struct in_addr src, struct in_addr dst,
		       unsigned int length, u_int32_t *addrs)
{
	dsr_srt_t *sr;

	sr = kmalloc(sizeof(dsr_srt_t) + length, GFP_ATOMIC);

	sr->src.s_addr = src.s_addr;
	sr->dst.s_addr = dst.s_addr;
	sr->laddrs = length;
	memcpy(sr->addrs, addrs, length);

	return sr;
}
dsr_srt_t *dsr_srt_new_rev(dsr_srt_t *srt)
{
	dsr_srt_t *srt_rev;
	int i, n;

	if (!srt)
		return NULL;
	
	srt_rev = kmalloc(sizeof(dsr_srt_t) + srt->laddrs, GFP_ATOMIC);
	
	srt_rev->src.s_addr = srt->dst.s_addr;
	srt_rev->dst.s_addr = srt->src.s_addr;
	srt_rev->laddrs = srt->laddrs;

	n = srt->laddrs / sizeof(struct in_addr);

	for (i = 0; i < n; i++)
		srt_rev->addrs[i].s_addr = srt->addrs[n-1-i].s_addr;

	return srt_rev;
}

dsr_srt_opt_t *dsr_srt_opt_add(char *buf, int len, dsr_srt_t *srt)
{
	dsr_srt_opt_t *sopt;
	
	if (len < DSR_SRT_OPT_LEN(srt))
		return NULL;

	sopt = (dsr_srt_opt_t *)buf;

	sopt->type = DSR_OPT_SRT;
	sopt->length = srt->laddrs + 2;
	sopt->f = 0;
	sopt->l = 0;
	sopt->res = 0;
	SET_SALVAGE(sopt, 0);
	sopt->sleft = (srt->laddrs / sizeof(struct in_addr));
	
	memcpy(sopt->addrs, srt->addrs, srt->laddrs);
	
	return sopt;
}

void dsr_parse_srt(struct in_addr initiator, dsr_srt_t *srt)
{
	DEBUG("Parse source route\n");
	return;
}

int dsr_srt_recv(dsr_srt_opt_t *sopt, struct in_addr src, struct in_addr dst)
{
	dsr_srt_t *srt;
	int n;
	
	
	if (!sopt)
		return DSR_SRT_ERROR;
	
	srt = dsr_srt_new(src, dst, sopt->length, sopt->addrs);
	
	n = (sopt->length - 2) / sizeof(struct in_addr);
	
	if (sopt->sleft == 0) {
	/* 	if (dst.s_addr == ldev_info.ifaddr.s_addr) */
/* 			return DSR_SRT_DELIVER; */
/* 		else */
			return DSR_SRT_REMOVE;
	}
	if (sopt->sleft > n) {
		// Send ICMP parameter error
		return DSR_SRT_SEND_ICMP;
	} else {
		int i;

		sopt->sleft--;
		
		i = n - sopt->sleft;
	
		/* TODO: check for multicast address in next hop or dst */
		/* TODO: check MTU and compare to pkt size */
	
		return DSR_SRT_FORWARD;
	}
	return DSR_SRT_ERROR;
}

struct in_addr dsr_srt_next_hop(dsr_srt_opt_t *sopt)
{
	int n, i;
	struct in_addr a;

	n = (sopt->length - 2) / sizeof(struct in_addr);
	i = n - sopt->sleft;
	
	a.s_addr = sopt->addrs[i];

	return a;
}