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

/*
 * pibridge_gate_header_class
 *
 * Print the elements of a gateway header.
 *
 * dst: destination address. Specifies to which RevPi device the packet is sent.
 * src: source address of the sending device.
 * cmd: command issued by this header.
 * seq: sequence number of this packet.
 * len: packet length.
 */
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
		"dst=%u src=%u cmd=0x%04x seq=%u len=%u",
		__entry->dst,
		__entry->src,
		__entry->cmd,
		__entry->seq,
		__entry->len
	)
);

/*
 * pibridge_send_gate_header
 *
 * Info: the header of a gateway packet to be sent.
 * Time: before the gate packet header is sent on the wire.
 */
DEFINE_EVENT(pibridge_gate_header_class, pibridge_send_gate_header,
	TP_PROTO(struct pibridge_pkthdr_gate *hdr),
	TP_ARGS(hdr)
);

/*
 * pibridge_receive_gate_header
 *
 * Info: the header of a received gateway packet.
 * Time: after the gate packet header was received from the wire.
 */
DEFINE_EVENT(pibridge_gate_header_class, pibridge_receive_gate_header,
	TP_PROTO(struct pibridge_pkthdr_gate *hdr),
	TP_ARGS(hdr)
);

/*
 * pibridge_io_header_class
 *
 * Print the elements of an IO packet header.
 *
 * addr: destination address. Specifies to which RevPi device the packet is
 *       sent.
 * type: packet type (unicast or broadcast).
 * rsp: specifies if this message is a request or a response.
 * len: packet length.
 * cmd: command issued by this header.
 */
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

/*
 * pibridge_send_io_header
 *
 * Info: the header of an IO packet to be sent.
 * Time: before the IO packet header is sent on the wire.
 */
DEFINE_EVENT(pibridge_io_header_class, pibridge_send_io_header,
	TP_PROTO(struct pibridge_pkthdr_io *hdr),
	TP_ARGS(hdr)
);

/*
 * pibridge_receive_io_header
 *
 * Info: the header of a received IO packet.
 * Time: after the header of an IO response packet has been received.
 */
DEFINE_EVENT(pibridge_io_header_class, pibridge_receive_io_header,
	TP_PROTO(struct pibridge_pkthdr_io *hdr),
	TP_ARGS(hdr)
);

/*
 * pibridge_send_crc_class
 *
 * Print the CRC.
 *
 * crc: the CRC that is part of the sent packet.
 */
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

/*
 * pibridge_send_io_crc
 *
 * Info: the CRC of an IO packet to be sent.
 * Time: before the CRC of an IO packet is sent on the wire.
 */
DEFINE_EVENT(pibridge_send_crc_class, pibridge_send_io_crc,
	TP_PROTO(u8 crc),
	TP_ARGS(crc)
);

/*
 * pibridge_send_gate_crc
 *
 * Info: the CRC to send for the gateway packet.
 * Time: before the CRC of a gateway packet is sent on the wire.
 */
DEFINE_EVENT(pibridge_send_crc_class, pibridge_send_gate_crc,
	TP_PROTO(u8 crc),
	TP_ARGS(crc)
);

/*
 * pibridge_receive_crc_class
 *
 * Print the CRC that is part of a received packet as well as the expected CRC
 * that has been calculated for this packet.
 *
 * crc: CRC received with the packet
 * exp_crc: expected CRC as calculated for the received packet.
 */
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

/*
 * pibridge_receive_io_crc
 *
 * Info: the CRC that is part of the received IO packet.
 * Time: after the CRC of an IO packet has been received.
 */
DEFINE_EVENT(pibridge_receive_crc_class, pibridge_receive_io_crc,
	TP_PROTO(u8 crc, u8 exp_crc),
	TP_ARGS(crc, exp_crc)
);

/*
 * pibridge_receive_gate_crc
 *
 * Info: the CRC that is part of the received gateway packet.
 * Time: after the CRC of a gateway packet has been received.
 */
DEFINE_EVENT(pibridge_receive_crc_class, pibridge_receive_gate_crc,
	TP_PROTO(u8 crc, u8 exp_crc),
	TP_ARGS(crc, exp_crc)
);

/*
 * pibridge_buffer_data_class
 *
 * Print a data buffer and the buffer length.
 *
 * buffer: data bytes.
 * len: number of data bytes.
 */
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
		__entry->len = len;
	),
	TP_printk(
		"datalen=%d data:%s",
		__entry->len,
		__print_array(__entry->buffer, __entry->len,
			      sizeof(unsigned char))
	)
);

/*
 * pibridge_receive_io_data
 *
 * Info: the payload data received with an IO response packet. If the packet is
 *       larger than expected, only the expected number of bytes is emitted.
 *       Excess bytes are discarded, see pibridge_discard_data and
 *       pibridge_discard_timeout events.
 * Time: after the data bytes of an IO response packet have been received.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_receive_io_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_receive_gate_data
 *
 * Info: the payload data received with a gateway response packet. If the packet
 *       is larger than expected, only the expected number of bytes is emitted.
 *       Excess bytes are discarded, see pibridge_discard_data and
 *       pibridge_discard_timeout events.
 * Time: after the data bytes of a gateway response packet have been received.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_receive_gate_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_send_io_data
 *
 * Info: the data sent with an IO request packet.
 * Time: before the data portion of an IO packet is sent on the wire.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_send_io_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_send_gate_data
 *
 * Info: the data sent with a gateway request packet.
 * Time: before the data portion of a gateway packet is sent on the wire.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_send_gate_data,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_send_begin
 *
 * Info: the bytes to send in raw format.
 * Time: before the bytes are sent on the wire.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_send_begin,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_receive_end
 *
 * Info: received bytes in raw format.
 * Time: after the expected number of bytes has been received or a timeout
 *       occurred.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_receive_end,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_wakeup_receive_buffer
 *
 * Info: received bytes in raw format.
 * Time: after a chunk of bytes has been received from the serial line following
 *       a notification by the serdev layer.
 */
DEFINE_EVENT(pibridge_buffer_data_class, pibridge_wakeup_receive_buffer,
	TP_PROTO(const unsigned char *buffer, unsigned int len),
	TP_ARGS(buffer, len)
);

/*
 * pibridge_timeout_class
 *
 * Print the number of bytes received within a timeout. Also print the timeout
 * itself and the number of bytes that were expected to be received.
 *
 * received: number of received bytes when the timeout occurred.
 * expected: number of bytes expected before timeout expiration.
 * timeout: timespan until the timeout.
 */
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

/*
 * pibridge_receive_timeout
 *
 * Info: timeout occurred while waiting for data reception.
 * Time: after the timeout occurred.
 */
DEFINE_EVENT(pibridge_timeout_class, pibridge_receive_timeout,
	TP_PROTO(unsigned int received, unsigned int expected,
		 unsigned int timeout),
	TP_ARGS(received, expected, timeout)
);

/*
 * pibridge_discard_timeout

 * Info: a timeout occurred while waiting for excess data (of a larger than
 *       expected packet) that was to be discarded.
 * Time: after the timeout occurred.
 */
DEFINE_EVENT(pibridge_timeout_class, pibridge_discard_timeout,
	TP_PROTO(unsigned int received, unsigned int expected,
		 unsigned int timeout),
	TP_ARGS(received, expected, timeout)
);

/*
 * pibridge_serdev_name_class
 *
 * Print the serdev device name.
 *
 * serdev: serdev device to print the name for.
 */
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

/*
 * pibridge_wakeup_write
 *
 * Info: the serdev device that is notified by the serdev subsystem that it is
 *       ready to write more data.
 * Time: at notification by the serdev subsystem.
 */
DEFINE_EVENT(pibridge_serdev_name_class, pibridge_wakeup_write,
	TP_PROTO(struct serdev_device *serdev),
	TP_ARGS(serdev)
);

/*
 * pibridge_data_len_class
 *
 * len: the number of bytes.
 */
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

/*
 * pibridge_send_end
 *
 * Info: the number of bytes sent.
 * Time: after the bytes have been sent.
 */
DEFINE_EVENT(pibridge_data_len_class, pibridge_send_end,
	TP_PROTO(unsigned int len),
	TP_ARGS(len)
);

/*
 * pibridge_receive_begin
 *
 * Info: the number of bytes expected to be received.
 * Time: before waiting for data reception.
 */
DEFINE_EVENT(pibridge_data_len_class, pibridge_receive_begin,
	TP_PROTO(unsigned int len),
	TP_ARGS(len)
);

/*
 * pibridge_kfifo_data_class
 *
 * Print the data contained in the receive fifo and its size in bytes.
 *
 * len: number of bytes stored in the receive fifo.
 * data: data stored in the receive fifo.
 */
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

/*
 * pibridge_discard_data
 *
 * Info: the bytes contained in the receive fifo being discarded.
 * Time: after the excess bytes of a larger than expected packet have been
 *       discarded or a timeout occurred while waiting for the data which
 *       was going to be discarded.
 */
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
