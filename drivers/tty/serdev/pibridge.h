#ifndef DRIVERS_PIBRIDGE_H
#define DRIVERS_PIBRIDGE_H

#include <linux/types.h>

#define PIBRIDGE_RECV_FIFO_SIZE		128

struct pibridge_pkthdr_gate {
	u8	dst;
	u8	src;
	u16	cmd;
	u16	seq;
	u8	len;
} __attribute__((packed));

struct pibridge_pkthdr_io {
	u8 addr	:6;
	u8 type	:1;	/* 0 for unicast, 1 for broadcast */
	u8 rsp	:1;	/* 0 for request, 1 for response */
	u8 len	:5;
	u8 cmd	:3;	/* 0 for broadcast*/
} __attribute__((packed));

#endif /* DRIVERS_PIBRIDGE_H */
