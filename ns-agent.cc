
// #include <mac.h>
// #include <ll.h>

#include "ns-agent.h"

/* Link to OTcl name space */
static class DSRUUClass : public TclClass {
public:
	DSRUUClass() : TclClass("Agent/DSRUU") {}
	TclObject* create(int, const char*const*) {
		return (new DSRUU);
	}
} class_DSRUU;


int DSRUU::params[PARAMS_MAX];

DSRUU::DSRUU() : Agent(PT_DSR), ack_timer(this), grat_rrep_tbl_timer(this), 
		 send_buf_timer(this), garbage_timer(this), lc_timer(this)
{
	int i;

	printf("Initializing DSR Agent\n");
	/* Initialize Link Cache */
	
	for (i = 0; i < PARAMS_MAX; i++)
		params[i] = params_def[i].val;

	lc_init();
		
	/* Initilize other tables */
// 	rreq_tbl_init();
// 	grat_rrep_tbl_init();
// 	send_buf_init();
// 	maint_buf_init();
	
	return;
}

DSRUU::~DSRUU()
{
// 	lc_cleanup();
// 	rreq_tbl_cleanup();
// 	grat_rrep_tbl_cleanup();
// 	send_buf_cleanup();
// 	maint_buf_cleanup();

	fprintf(stderr,"DFU: Don't do this! I haven't figured out ~DSRAgent\n");
	exit(-1);
}

Packet *
DSRUU::ns_packet_create(struct dsr_pkt *dp)
{
	Packet *p;
	struct hdr_cmn *ch;
	struct hdr_ip *iph;
	
	p = allocpkt();

	ch = HDR_CMN(p);
	iph = HDR_IP(p);

	return p;
}

void 
DSRUU::recv(Packet* p, Handler*)
{
	struct dsr_pkt *dp;
	int action, res = -1;

	dp = dsr_pkt_alloc(p);

	printf("%s Recv: src=%s dst=%s\n",  print_ip(ip_addr), print_ip(dp->src), print_ip(dp->dst));

	if (dp->src.s_addr == ip_addr.s_addr) {
		
		dp->srt = dsr_rtc_find(dp->src, dp->dst);
		
		if (dp->srt) {
			
			if (dsr_srt_add(dp) < 0) {
				DEBUG("Could not add source route\n");
				return;
			}
			/* Send packet */

			XMIT(dp);
			
			return;

		} else {			
		// 	res = send_buf_enqueue_packet(dp, XMIT);
			
			if (res < 0) {
				DEBUG("Queueing failed!\n");
				dsr_pkt_free(dp);
				return;
			}
			res = dsr_rreq_route_discovery(dp->dst);
			
			if (res < 0)
				DEBUG("RREQ Transmission failed...");
			
			return;
		}
	}
		
	action = dsr_opt_recv(dp);

	
	dsr_pkt_free(dp);
	return;
}

enum {
	SET_ADDR,
	SET_MAC_ADDR,
	SET_NODE,
	SET_LL,
	SET_TAP,
	SET_DMUX,
	MAX_CMD
};

char *cmd[MAX_CMD] = {
	"addr",
	"mac-addr",
	"node",
	"add-ll",
	"install-tap",
	"port-dmux"
};

static int name2cmd(const char *name)
{
	int i;

	for (i = 0; i < MAX_CMD; i++) {
		if (strcasecmp(cmd[i], name) == 0) 
			return i;
	}
	return -1;
}

int 
DSRUU::command(int argc, const char* const* argv)
{
	switch (name2cmd(argv[1])) {
	case SET_ADDR:
		ip_addr.s_addr = Address::instance().str2addr(argv[2]);
		break;
	case SET_MAC_ADDR:
		break;
	case SET_NODE:
		node_ = (MobileNode *)TclObject::lookup(argv[2]);
		break;
	case SET_LL:
		ll_ = (LL *)TclObject::lookup(argv[2]);
		ifq_ = (CMUPriQueue *)TclObject::lookup(argv[3]);
		break;
	case SET_DMUX:
		break;
	case SET_TAP:
		mac_ = (Mac *)TclObject::lookup(argv[2]);
		printf("MAC: %d\n", mac_->addr());
		break;
	default:
		return TCL_OK;
	}
	return TCL_OK;
}

void 
DSRUUTimer::expire (Event *e)
{
	(a_->*function)(data);
	return;
}
