#undef TRACE_SYSTEM
#define TRACE_SYSTEM pibridge

#if !defined(_PIBRIDGE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _PIBRIDGE_TRACE_H

#include <linux/tracepoint.h>
#include <linux/serdev.h>
#include <linux/kfifo.h>

#include "pibridge.h"

#define PIBRIDGE_TRACE_MAX_FIFO_DUMP		128
#define PIBRIDGE_TRACE_MAX_PACKET_DATA_LEN	256

/*  Header */
DECLARE_EVENT_CLASS(pibridge_gate_header_class,
	TP_PROTO(struct pibridge_pkthdr_gate *hdr),
	TP_ARGS(hdr),
	TP_STRUCT__entry(
		__field(u8, dst)
		__field(u8, src)
		__field(u16, cmd)
		__field(u16, seq)
		__field(u8, len)
	),
	TP_fast_assign(
		__entry->dst = hdr->dst;
		__entry->src = hdr->src;
		__entry->cmd = hdr->cmd;
		__entry->seq = hdr->seq;
		__entry->len = hdr->len;
	),
	TP_printk(
		"dst=%u src=%u cmd=%u seq=%u len=%u",
		__entry->dst,
		__entry->src,
		__entry->cmd,
		__entry->seq,
		__entry->len
	)
);

DEFINE_EVENT(pibridge_gate_header_class, pibridge_send_gate_header,
	TP_PROTO(struct pibridge_pkthdr_gate *hdr),
	TP_ARGS(hdr)
);

DEFINE_EVENT(pibridge_gate_header_class, pibridge_receive_gate_header,
	TP_PROTO(struct pibridge_pkthdr_gate *hdr),
	TP_ARGS(hdr)
);

DECLARE_EVENT_CLASS(pibridge_io_header_class,
	TP_PROTO(struct pibridge_pkthdr_io *hdr),
	TP_ARGS(hdr),
	TP_STRUCT__entry(
		__field(u8, addr)
		__field(u8, type)
		__field(u8, rsp)
		__field(u8, len)
		__field(u8, cmd)
	),
	TP_fast_assign(
		__entry->addr = hdr->addr;
		__entry->type = hdr->type;
		__entry->rsp = hdr->rsp;
		__entry->len = hdr->len;
		__entry->cmd = hdr->cmd;
	),
	TP_printk(
		"addr=%u type=%u rsp=%u len=%u cmd=%u",
		__entry->addr,
		__entry->type,
		__entry->rsp,
		__entry->len,
		__entry->cmd)
);

DEFINE_EVENT(pibridge_io_header_class, pibridge_send_io_header,
	TP_PROTO(struct pibridge_pkthdr_io *hdr),
	TP_ARGS(hdr)
);

DEFINE_EVENT(pibridge_io_header_class, pibridge_receive_io_header,
	TP_PROTO(struct pibridge_pkthdr_io *hdr),
	TP_ARGS(hdr)
);

/*  CRC */
DECLARE_EVENT_CLASS(pibridge_send_crc_class,
	TP_PROTO(u8 crc),
	TP_ARGS(crc),
	TP_STRUCT__entry(
		__field(u8, crc)
	),
	TP_fast_assign(
		__entry->crc = crc;
	),
	TP_printk(
		"crc=0x%02x",
		__entry->crc
	)
);

DEFINE_EVENT(pibridge_send_crc_class, pibridge_send_io_crc,
	TP_PROTO(u8 crc),
	TP_ARGS(crc)
);

DEFINE_EVENT(pibridge_send_crc_class, pibridge_send_gate_crc,
	TP_PROTO(u8 crc),
	TP_ARGS(crc)
);

DECLARE_EVENT_CLASS(pibridge_receive_crc_class,
	TP_PROTO(u8 crc, u8 exp_crc),
	TP_ARGS(crc, exp_crc),
	TP_STRUCT__entry(
		__field(u8, crc)
		__field(u8, exp_crc)
	),
	TP_fast_assign(
		__entry->crc = crc;
		__entry->exp_crc = exp_crc;
	),
	TP_printk(
		"crc=0x%02x (exp=0x%02x)",
		__entry->crc,
		__entry->exp_crc
	)
);

DEFINE_EVENT(pibridge_receive_crc_class, pibridge_receive_io_crc,
	TP_PROTO(u8 crc, u8 exp_crc),
	TP_ARGS(crc, exp_crc)
);

DEFINE_EVENT(pibridge_receive_crc_class, pibridge_receive_gate_crc,
	TP_PROTO(u8 crc, u8 exp_crc),
	TP_ARGS(crc, exp_crc)
);

/* packet data */
DECLARE_EVENT_CLASS(pibridge_buffer_data_class,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len),
	TP_STRUCT__entry(
		__array(const unsigned char *, buffer,
			PIBRIDGE_TRACE_MAX_PACKET_DATA_LEN)
		__field(unsigned int, len)
	),
	TP_fast_assign(
		memcpy(__entry->buffer, buffer, len);
		__entry->len= len;
	),
	TP_printk(
		"datalen=%d data:%s",
		__entry->len,
		__print_array(__entry->buffer, __entry->len,
			      sizeof(unsigned char))
	)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_receive_io_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_receive_gate_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_send_io_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_send_gate_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_send_begin,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_receive_end,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

DEFINE_EVENT(pibridge_buffer_data_class, pibridge_wakeup_receive_buffer,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/* Timeout */
DECLARE_EVENT_CLASS(pibridge_timeout_class,
	TP_PROTO(unsigned int received, unsigned int expected,
		 unsigned int timeout),
	TP_ARGS(received, expected, timeout),
	TP_STRUCT__entry(
		__field(unsigned int, received)
		__field(unsigned int, expected)
		__field(unsigned int, timeout)
	),
	TP_fast_assign(
		__entry->received = received;
		__entry->expected = expected;
		__entry->timeout = timeout;
	),
	TP_printk(
		"received: %u, expected: %u, timeout: %u",
		__entry->received,
		__entry->expected,
		__entry->timeout
	)
);

DEFINE_EVENT(pibridge_timeout_class, pibridge_receive_timeout,
	TP_PROTO(unsigned int received, unsigned int expected,
		 unsigned int timeout),
	TP_ARGS(received, expected, timeout)
);

DEFINE_EVENT(pibridge_timeout_class, pibridge_discard_timeout,
	TP_PROTO(unsigned int received, unsigned int expected,
		 unsigned int timeout),
	TP_ARGS(received, expected, timeout)
);

/* Serdev name */
DECLARE_EVENT_CLASS(pibridge_serdev_name_class,
	TP_PROTO(struct serdev_device *serdev),
	TP_ARGS(serdev),
	TP_STRUCT__entry(
		__string(name, dev_name(&serdev->dev))
	),
	TP_fast_assign(
		__assign_str(name, dev_name(&serdev->dev));
	),
	TP_printk(
		"Serdev %s",
		__get_str(name)
	)
);

DEFINE_EVENT(pibridge_serdev_name_class, pibridge_wakeup_write,
	TP_PROTO(struct serdev_device *serdev),
	TP_ARGS(serdev)
);

/* packet data len */
DECLARE_EVENT_CLASS(pibridge_data_len_class,
	TP_PROTO(unsigned int len),
	TP_ARGS(len),
	TP_STRUCT__entry(
		__field(unsigned int, len)
	),
	TP_fast_assign(
		__entry->len = len;
	),
	TP_printk(
		"datalen=%d",
		__entry->len
	)
);

DEFINE_EVENT(pibridge_data_len_class, pibridge_send_end,
	TP_PROTO(unsigned int len),
	TP_ARGS(len)
);

DEFINE_EVENT(pibridge_data_len_class, pibridge_receive_begin,
	TP_PROTO(unsigned int len),
	TP_ARGS(len)
);


/* kfifo data */
DECLARE_EVENT_CLASS(pibridge_kfifo_data_class,
	TP_PROTO(struct kfifo *fifo),
	TP_ARGS(fifo),
	TP_STRUCT__entry(
		__field(unsigned int, len)
		__array(unsigned char *, buffer, PIBRIDGE_RECV_FIFO_SIZE)
	),
	TP_fast_assign(
		__entry->len = kfifo_out_peek(fifo, __entry->buffer,
					      PIBRIDGE_RECV_FIFO_SIZE)
	),
	TP_printk(
		"fifo (len=%u) data=%s",
		__entry->len,
		__print_array(__entry->buffer,
			      __entry->len > PIBRIDGE_RECV_FIFO_SIZE ?
					PIBRIDGE_RECV_FIFO_SIZE : __entry->len,
			      sizeof(unsigned char))
	)
);

DEFINE_EVENT(pibridge_kfifo_data_class, pibridge_discard_data,
	TP_PROTO(struct kfifo *fifo),
	TP_ARGS(fifo)
);

#endif /* _PIBRIDGE_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pibridge_trace
#include <trace/define_trace.h>
