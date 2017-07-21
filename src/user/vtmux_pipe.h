#define PIPE_CMD_OPEN       0

#define PIPE_REP_OK         0
#define PIPE_REP_ACTIVATE   1
#define PIPE_REP_DEACTIVATE 2

struct pmsg {
	int code;
	char payload[];
};

struct pmsg_open {
	int code;
	int mode;
	char path[];
};
