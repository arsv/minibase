#define RTM_NEWUSEROPT   68

struct nduseroptmsg {
	struct nlmsg nlm;
	uint8_t family;
	uint8_t _1;
	uint16_t optslen;
	uint32_t ifindex;
	uint8_t type;
	uint8_t code;
	uint16_t _2;
	uint32_t _3;
	char payload[];
} __attribute__((packed));
