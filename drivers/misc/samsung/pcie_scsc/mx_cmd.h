#define PACKET_SIZE MXMGR_MESSAGE_PAYLOAD_SIZE
#define NUM_PACKET 10
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

struct cmd_msg_packet {
	char msg[PACKET_SIZE];
};


int scsc_mx_cmd_driver_create(void);
void scsc_mx_cmd_driver_destory(void);
