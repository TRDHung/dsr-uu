#ifndef _DSR_RREQ
#define _DSR_RREQ

#include <asm/byteorder.h>
#include <linux/types.h>

#include "dsr.h"

struct dsr_rreq_opt {
	u_int8_t type;
	u_int8_t length;
	u_int16_t id;
	u_int32_t target;
	u_int32_t addrs[0];
};

#define DSR_RREQ_HDR_LEN sizeof(struct dsr_rreq_opt)
#define DSR_RREQ_OPT_LEN (DSR_RREQ_HDR_LEN - 2)
#define DSR_RREQ_TOT_LEN IP_HDR_LEN + sizeof(struct dsr_opt_hdr) + sizeof(struct dsr_rreq_opt)
#define DSR_RREQ_ADDRS_LEN(rreq_opt) (rreq_opt->length - 6)

void rreq_tbl_set_max_len(unsigned int max_len);
int rreq_tbl_disable_route_discovery(struct in_addr dst);
int dsr_rreq_opt_recv(struct dsr_pkt *dp, struct dsr_rreq_opt *rreq_opt);
int dsr_rreq_route_discovery(struct in_addr target);
int rreq_tbl_init(void);
void rreq_tbl_cleanup(void);

#endif  /* _DSR_RREQ */
