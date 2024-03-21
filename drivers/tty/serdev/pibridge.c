#include <linux/errno.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/pibridge_comm.h>
#include <linux/wait.h>

#include "pibridge.h"

#define CREATE_TRACE_POINTS
#include "pibridge_trace.h"

#define PIBRIDGE_BAUDRATE		115200
#define PIBRIDGE_IO_TIMEOUT		10         // msec
#define PIBRIDGE_BC_ADDR		0xff

#define PIBRIDGE_RESP_CMD		0x3fff
#define PIBRIDGE_RESP_OK		0x4000
#define PIBRIDGE_RESP_ERR		0x8000

struct pibridge {
	struct serdev_device *serdev;
	struct mutex lock;
	struct kfifo read_fifo;
	wait_queue_head_t read_queue;
};

static struct pibridge *pibridge_s; /* unique instance of the pibridge */

static u8 pibridge_crc8(u8 base, void *data, u16 len)
{
	u8 ret = base;

	while (len--)
		ret = ret ^ ((u8 *)data)[len];
	return ret;
}

static int pibridge_receive_buf(struct serdev_device *serdev,
				const unsigned char *buf, size_t count)
{
	struct pibridge *pi = serdev_device_get_drvdata(serdev);
	int ret;

	mutex_lock(&pi->lock);
	ret = kfifo_in(&pi->read_fifo, buf, count);
	mutex_unlock(&pi->lock);

	trace_pibridge_wakeup_receive_buffer(buf, count);
	wake_up(&pi->read_queue);

	if (ret < count)
		dev_warn_ratelimited(&serdev->dev,
			"failed to fill receive fifo (received: %zd, filled: %d)\n",
			count, ret);
	return ret;
}

static void pibridge_write_wakeup(struct serdev_device *serdev)
{
	trace_pibridge_wakeup_write(serdev);
	serdev_device_write_wakeup(serdev);
}

static const struct serdev_device_ops pibridge_serdev_ops = {
	.receive_buf	= pibridge_receive_buf,
	.write_wakeup	= pibridge_write_wakeup,
};

static int pibridge_set_serial(struct serdev_device *serdev)
{
	serdev_device_set_baudrate(serdev, PIBRIDGE_BAUDRATE);
	/* RTS is used to drive Transmit Enable pin, hence no flow control */
	serdev_device_set_flow_control(serdev, false);
	return serdev_device_set_parity(serdev, SERDEV_PARITY_EVEN);
}

static int pibridge_discard_timeout(u16 len, u16 timeout)
{
	struct pibridge *pi = pibridge_s;
	unsigned int discarded;
	int ret = 0;

	wait_event_hrtimeout(pi->read_queue,
			     kfifo_len(&pi->read_fifo) >= len,
			     ms_to_ktime(timeout));

	mutex_lock(&pi->lock);
	discarded = kfifo_len(&pi->read_fifo);
	trace_pibridge_discard_data(&pi->read_fifo);
	kfifo_reset(&pi->read_fifo);
	mutex_unlock(&pi->lock);

	if (discarded < len) {
		trace_pibridge_discard_timeout(discarded, len, timeout);
		ret = -1;
	}

	return ret;
}

static int pibridge_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct pibridge *pi;
	int ret;

	pi = devm_kzalloc(dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pibridge_s = pi;
	pi->serdev = serdev;

	serdev_device_set_drvdata(serdev, pi);
	serdev_device_set_client_ops(serdev, &pibridge_serdev_ops);

	mutex_init(&pi->lock);
	init_waitqueue_head(&pi->read_queue);

	ret = kfifo_alloc(&pi->read_fifo, PIBRIDGE_RECV_FIFO_SIZE, GFP_KERNEL);
	if (ret)
		return ret;

	ret = serdev_device_open(serdev);
	if (ret) {
		dev_err(&serdev->dev, "failed to open serdev: %i\n", ret);
		goto err_kfifo_free;
	}

	ret = pibridge_set_serial(serdev);
	if (ret) {
		dev_err(&serdev->dev,
			"failed to set serial parameters: %i\n", ret);
		goto err_serdev_close;
	}

	dev_info(&serdev->dev, "pibridge initialized\n");

	return 0;

err_serdev_close:
	serdev_device_close(serdev);
err_kfifo_free:
	kfifo_free(&pi->read_fifo);

	dev_err(&serdev->dev, "failed to initialize pibridge\n");

	return ret;
}

static void pibridge_remove(struct serdev_device *serdev)
{
	struct pibridge *pi = serdev_device_get_drvdata(serdev);

	serdev_device_close(serdev);
	kfifo_free(&pi->read_fifo);
};

/*****************/

int pibridge_send(void *buf, u32 len)
{
	struct pibridge *pi = pibridge_s;
	struct serdev_device *serdev = pi->serdev;
	int ret;

	trace_pibridge_send_begin(buf, len);

	ret = serdev_device_write(serdev, buf, len, MAX_SCHEDULE_TIMEOUT);
	serdev_device_wait_until_sent(serdev, 0);

	trace_pibridge_send_end(ret);

	return ret;
}
EXPORT_SYMBOL(pibridge_send);

void pibridge_clear_fifo(void)
{
	mutex_lock(&pibridge_s->lock);
	kfifo_reset(&pibridge_s->read_fifo);
	mutex_unlock(&pibridge_s->lock);
}
EXPORT_SYMBOL(pibridge_clear_fifo);

int pibridge_recv_timeout(void *buf, u8 len, u16 timeout)
{
	struct pibridge *pi = pibridge_s;
	unsigned int received;

	trace_pibridge_receive_begin(len);

	wait_event_hrtimeout(pi->read_queue, kfifo_len(&pi->read_fifo) >= len,
			     ms_to_ktime(timeout));

	mutex_lock(&pi->lock);
	received = kfifo_out(&pi->read_fifo, buf, len);
	mutex_unlock(&pi->lock);

	trace_pibridge_receive_end(buf, received);

	if (received != len)
		trace_pibridge_receive_timeout(received, len, timeout);

	return received;
}
EXPORT_SYMBOL(pibridge_recv_timeout);

int pibridge_recv(void *buf, u8 len)
{
	/* using default timeout PIBRIDGE_IO_TIMEOUT */
	return pibridge_recv_timeout(buf, len, PIBRIDGE_IO_TIMEOUT);
}
EXPORT_SYMBOL(pibridge_recv);

int pibridge_req_send_gate(u8 dst, u16 cmd, void *snd_buf, u8 snd_len)
{
	struct pibridge_pkthdr_gate pkthdr;
	u8 crc;

	memset(&pkthdr, 0, sizeof(pkthdr));

	pkthdr.dst = dst;
	pkthdr.cmd = cmd;
	pkthdr.len = snd_len;

	trace_pibridge_send_gate_header(&pkthdr);

	if (pibridge_send(&pkthdr, sizeof(pkthdr)) < 0) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"send head error in gate-send\n");
		return -EIO;
	}
	if (snd_len != 0) {
		trace_pibridge_send_gate_data(snd_buf, snd_len);

		if (pibridge_send(snd_buf, snd_len) < 0) {
			dev_warn_ratelimited(&pibridge_s->serdev->dev,
				"send data error in gate-send(len: %d)\n", snd_len);
			return -EIO;
		}

	}

	crc = pibridge_crc8(0, &pkthdr, sizeof(pkthdr));
	if (snd_len != 0)
		crc = pibridge_crc8(crc, snd_buf, snd_len);

	trace_pibridge_send_gate_crc(crc);

	if (pibridge_send(&crc, sizeof(u8)) < 0) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"send crc error in gate-send\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(pibridge_req_send_gate);

int pibridge_req_gate_tmt(u8 dst, u16 cmd, void *snd_buf, u8 snd_len,
			  void *rcv_buf, u8 rcv_len, u16 tmt)
{
	struct pibridge_pkthdr_gate pkthdr;
	u8 to_receive;
	u8 to_discard;
	u8 crc_rcv;
	u8 crc;

	/* Read fifo may contain stale data, so clear it first */
	pibridge_clear_fifo();

	if (pibridge_req_send_gate(dst, cmd, snd_buf, snd_len)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"send message error in gate-req(dst: %d, cmd: %d, len: %d)\n",
			dst, cmd, snd_len);
		return -EIO;
	}
	/* Do not wait for a response in case of a broadcast address */
	if (dst == PIBRIDGE_BC_ADDR)
		return 0;

	if (pibridge_recv_timeout(&pkthdr, sizeof(pkthdr), tmt) !=
	    sizeof(pkthdr)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"receive head error in gate-req(hdr_len: %zd, timeout: %d, data0: %c)\n",
			sizeof(pkthdr), tmt, snd_buf ? ((u8 *)snd_buf)[0] : 0);
		return -EIO;
	}

	trace_pibridge_receive_gate_header(&pkthdr);

	crc = pibridge_crc8(0, &pkthdr, sizeof(pkthdr));

	to_receive = min(pkthdr.len, rcv_len);
	to_discard = pkthdr.len - to_receive;

	if (to_receive) {
		if (pibridge_recv(rcv_buf, to_receive) != to_receive) {
			dev_warn_ratelimited(&pibridge_s->serdev->dev,
				"receive data error in gate-req(len: %d)\n",
				to_receive);
			return -EIO;
		}
		trace_pibridge_receive_gate_data(rcv_buf, to_receive);
		crc = pibridge_crc8(crc, rcv_buf, to_receive);
	}

	if (to_discard) {
		/*
		 * The provided buffer was too small. Discard the rest of the
		 * received data as well as the following CRC checksum byte.
		 */
		if (pibridge_discard_timeout(to_discard + 1, tmt))
			dev_warn_ratelimited(&pibridge_s->serdev->dev,
				"failed to discard %u bytes within timeout\n",
				to_discard);
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"received packet truncated (%u bytes missing)\n",
			to_discard);
		return -EBADMSG;
	}
	/* We got the whole data, now get the CRC */
	if (pibridge_recv(&crc_rcv, sizeof(u8)) != sizeof(u8)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"failed to receive CRC in gate-req\n");
		return -EIO;
	}

	trace_pibridge_receive_gate_crc(crc_rcv, crc);

	if (crc != crc_rcv) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"invalid checksum (expected: 0x%02x, got 0x%02x)\n",
			crc_rcv, crc);
			return -EBADMSG;
	}

	if ((pkthdr.cmd & PIBRIDGE_RESP_CMD) != cmd) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"bad responded CMD code in gate-req(cmd: %d)\n",
			pkthdr.cmd);
		return -EBADMSG;
	}

	if (!(pkthdr.cmd & PIBRIDGE_RESP_OK)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"bad responded OK code in gate-req(cmd: %d)\n",
			pkthdr.cmd);
		return -EBADMSG;
	}

	if (pkthdr.cmd & PIBRIDGE_RESP_ERR) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"bad responded ERR code in gate-req(cmd: %d)\n",
			pkthdr.cmd);
		return -EBADMSG;
	}

	return to_receive;
}
EXPORT_SYMBOL(pibridge_req_gate_tmt);

int pibridge_req_gate(u8 dst, u16 cmd, void *snd_buf, u8 snd_len,
		      void *rcv_buf, u8 rcv_len)
{
	return pibridge_req_gate_tmt(dst, cmd, snd_buf, snd_len, rcv_buf,
				     rcv_len, PIBRIDGE_IO_TIMEOUT);
}
EXPORT_SYMBOL(pibridge_req_gate);

int pibridge_req_send_io(u8 addr, u8 cmd, void *snd_buf, u8 snd_len)
{
	struct pibridge_pkthdr_io pkthdr;
	u8 crc;

	memset(&pkthdr, 0, sizeof(pkthdr));

	pkthdr.addr	= addr;
	pkthdr.type	= (addr == 0x3f) ? 1 : 0; /* 0 for unicast, 1 for broadcast */
	pkthdr.cmd	= cmd;
	pkthdr.len	= snd_len;

	trace_pibridge_send_io_header(&pkthdr);

	if (pibridge_send(&pkthdr, sizeof(pkthdr)) < 0) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"send head error in io-send(len: %zd)\n", sizeof(pkthdr));
		return -EIO;
	}

	if (snd_len != 0) {
		trace_pibridge_send_io_data(snd_buf, snd_len);

		if (pibridge_send(snd_buf, snd_len) < 0) {
			dev_warn_ratelimited(&pibridge_s->serdev->dev,
				"send data error in io-send(len: %d)\n",
				snd_len);
			return -EIO;
		}
	}
	crc = pibridge_crc8(0, &pkthdr, sizeof(pkthdr));
	crc = pibridge_crc8(crc, snd_buf, snd_len);

	trace_pibridge_send_io_crc(crc);

	if (pibridge_send(&crc, sizeof(u8)) < 0) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"send crc error in io-send\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(pibridge_req_send_io);

int pibridge_req_io(u8 addr, u8 cmd, void *snd_buf, u8 snd_len, void *rcv_buf,
		    u8 rcv_len)
{
	struct pibridge_pkthdr_io pkthdr;
	u8 to_receive;
	u8 to_discard;
	u8 crc_rcv;
	u8 crc;

	/* Read fifo may contain stale data, so clear it first */
	pibridge_clear_fifo();

	if (pibridge_req_send_io(addr, cmd, snd_buf, snd_len)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"send message error in io-req(addr: %d, cmd: %d, len: %d)\n",
			addr, cmd, snd_len);
		return -EIO;
	}

	if (pibridge_recv(&pkthdr, sizeof(pkthdr)) != sizeof(pkthdr)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"receive head error in io-req\n");
		return -EIO;
	}

	trace_pibridge_receive_io_header(&pkthdr);

	crc = pibridge_crc8(0, &pkthdr, sizeof(pkthdr));

	to_receive = min((u8) pkthdr.len, rcv_len);
	to_discard = pkthdr.len - to_receive;

	if (to_receive) {
		if (pibridge_recv(rcv_buf, to_receive) != to_receive) {
			dev_warn_ratelimited(&pibridge_s->serdev->dev,
				"receive data error in io-req(len: %d)\n",
				to_receive);
			return -EIO;
		}
		trace_pibridge_receive_io_data(rcv_buf, to_receive);
		crc = pibridge_crc8(crc, rcv_buf, to_receive);
	}

	if (to_discard) {
		/*
		 * The provided buffer was too small. Discard the rest of the
		 * received data as well as the following CRC checksum byte.
		 */
		if (pibridge_discard_timeout(to_discard + 1,
					     PIBRIDGE_IO_TIMEOUT))
			dev_warn_ratelimited(&pibridge_s->serdev->dev,
				"failed to discard %u bytes within timeout\n",
				to_discard);
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"received packet truncated (%u bytes missing)\n",
			to_discard);
		return -EBADMSG;
	}
	/* We got the whole data, now get the CRC */
	if (pibridge_recv(&crc_rcv, sizeof(u8)) != sizeof(u8)) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"receive crc error in io-req\n");
		return -EIO;
	}

	trace_pibridge_receive_io_crc(crc_rcv, crc);

	if (crc != crc_rcv) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"invalid checksum (expected: 0x%02x, got 0x%02x\n",
			crc_rcv, crc);
		return -EBADMSG;
	}

	if (pkthdr.addr != addr) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"unexpected response addr 0x%02x\n", pkthdr.addr);
		return -EBADMSG;
	}

	if (!pkthdr.rsp) {
		dev_warn_ratelimited(&pibridge_s->serdev->dev,
			"response flag not set in received packet\n");
		return -EBADMSG;
	}

	return to_receive;
}
EXPORT_SYMBOL(pibridge_req_io);

#ifdef CONFIG_OF
static const struct of_device_id pibridge_of_match[] = {
	{ .compatible = "kunbus,pi-bridge" },
	{},
};
MODULE_DEVICE_TABLE(of, pibridge_of_match);
#endif

static struct serdev_device_driver pibridge_driver = {
	.driver	= {
		.name		= "pi-bridge",
		.of_match_table	= of_match_ptr(pibridge_of_match),
	},
	.probe	= pibridge_probe,
	.remove	= pibridge_remove,
};
module_serdev_device_driver(pibridge_driver);

MODULE_LICENSE("GPL");
