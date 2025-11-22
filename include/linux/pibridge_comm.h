#ifndef _PIBRIDGE_COMM_H
#define _PIBRIDGE_COMM_H

#include <linux/types.h>
#include <linux/u64_stats_sync.h>
#include <linux/kfifo.h>
#include <linux/wait.h>


struct pibridge_stats;

struct pibridge_pkthdr_gate {
	u8	dst;
	u8	src;
	u16	cmd;
	u16	seq;
	u8	len;
} __attribute__((packed));

#define PIBRIDGE_MAX_GATE_DATA		255
#define PIBRIDGE_CRC_LEN		1

struct pibridge_stats {
	u64 tx_bytes;
	u64 tx_err;
	u64 tx_io_err;
	u64 tx_gate_err;
	u64 rx_bytes;
	u64 rx_err;
	u64 rx_gate_hdr_err;
	u64 rx_gate_data_err;
	u64 rx_gate_crc_err;
	u64 rx_gate_crc_inval;
	u64 rx_gate_format_inval;
	u64 rx_gate_discarded;
	u64 rx_gate_remote_err;
	u64 rx_io_hdr_err;
	u64 rx_io_data_err;
	u64 rx_io_crc_err;
	u64 rx_io_crc_inval;
	u64 rx_io_format_inval;
	u64 rx_io_discarded;

	struct u64_stats_sync syncp;
};

struct pibridge {
	struct serdev_device *serdev;
	struct mutex lock;
	struct kfifo read_fifo;
	wait_queue_head_t read_queue;
	struct pibridge_stats stats;
};

struct pibridge_gate_datagram {
	struct pibridge_pkthdr_gate hdr;
	u8 data[PIBRIDGE_MAX_GATE_DATA + PIBRIDGE_CRC_LEN];
} __attribute__((packed));


struct pibridge *pibridge_get(void);
int pibridge_req_io(struct pibridge *pi, u8 addr, u8 cmd, void *snd_buf,
		    u8 snd_len, void *rcv_buf, u8 rcv_len);
int pibridge_req_send_io(struct pibridge *pi, u8 addr, u8 cmd, void *snd_buf,
			 u8 buf_len);
int pibridge_req_gate_datagram(struct pibridge *pi,
			       struct pibridge_gate_datagram *req,
			       struct pibridge_gate_datagram *resp);
int pibridge_req_gate_tmt(struct pibridge *pi, u8 dst, u16 cmd, void *snd_buf,
			  u8 snd_len, void *rcv_buf, u8 rcv_len, u16 tmt);
int pibridge_req_gate(struct pibridge *pi, u8 dst, u16 cmd, void *snd_buf,
		      u8 snd_len, void *rcv_buf, u8 rcv_len);
int pibridge_req_send_gate(struct pibridge *pi, u8 dst, u16 cmd, void *snd_buf,
			   u8 buf_len);
int pibridge_recv(struct pibridge *pi, void *buf, u8 len);
int pibridge_recv_timeout(struct pibridge *pi, void *buf, u8 len, u16 timeout);
int pibridge_send(struct pibridge *pi, void *buf, u32 len);
void pibridge_clear_fifo(struct pibridge *pi);
#endif	/* _PIBRIDGE_COMM_H */
