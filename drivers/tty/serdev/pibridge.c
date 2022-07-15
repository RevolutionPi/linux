#include <linux/errno.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/wait.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/module.h>

#define REV_PI_IO_TIMEOUT           10         // msec
#define REV_PI_RECV_BUFFER_SIZE     100
#define REV_PI_BC_ADDR		    0xff

#define pb_err(n, fmt, ...) \
	do {\
		static int cc = 0;\
		if (cc < n && pibridge_s ){\
			dev_err(&pibridge_s->serdev->dev, fmt, ##__VA_ARGS__);\
			cc ++;\
		}\
	} while (0)

struct pibridge {
	struct serdev_device *serdev;

	struct mutex lock;
	struct kfifo read_fifo;
	wait_queue_head_t read_queue;
};

static struct pibridge *pibridge_s; /* unique instance of the pibridge */

static int pibridge_receive_buf(struct serdev_device *serdev,
		const unsigned char *buf, size_t count)
{
	struct pibridge *pi = serdev_device_get_drvdata(serdev);
	int ret;

	mutex_lock(&pi->lock);
	ret = kfifo_in(&pi->read_fifo, buf, count);
	mutex_unlock(&pi->lock);

	wake_up_interruptible(&pi->read_queue);

	if (ret < count)
		pb_err(25, "put received buffer to fifo error(count:%d, ret:%d)\n", count, ret);
	return ret;
}

static const struct serdev_device_ops pibridge_serdev_ops = {
	.receive_buf	= pibridge_receive_buf,
	.write_wakeup	= serdev_device_write_wakeup,
};

static int pibridge_parse_dt(struct serdev_device *serdev)
{
	struct device_node *node = serdev->dev.of_node;
	u32 speed = 115200;

	of_property_read_u32(node, "current-speed", &speed);
	serdev_device_set_baudrate(serdev, speed);

	/* RTS is used to drive Transmit Enable pin, hence no flow control */
	serdev_device_set_flow_control(serdev, false);

	return serdev_device_set_parity(serdev, SERDEV_PARITY_EVEN);
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

	ret = kfifo_alloc(&pi->read_fifo, REV_PI_RECV_BUFFER_SIZE, GFP_KERNEL);
	if (ret)
		return ret;

	ret = serdev_device_open(serdev);
	if (ret)
		goto err_kfifo_free;

	ret = pibridge_parse_dt(serdev);
	if (ret)
		goto err_serdev_close;

	return 0;

err_serdev_close:
	serdev_device_close(serdev);
err_kfifo_free:
	kfifo_free(&pi->read_fifo);
	return ret;
}

static void pibridge_remove(struct serdev_device *serdev)
{
	struct pibridge *pi = serdev_device_get_drvdata(serdev);

	serdev_device_close(serdev);

	kfifo_free(&pi->read_fifo);
};

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


/*****************/

int pibridge_send(u8 *buf, u16 len)
{
	struct pibridge *pi = pibridge_s;
	struct serdev_device *serdev = pi->serdev;
	int ret;

	ret = serdev_device_write(serdev, buf, len, MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		pb_err(25, "write to serdev error(len:%d)\n", len);

	/* read fifo may contain stale bytes */
	mutex_lock(&pi->lock);
	kfifo_reset(&pi->read_fifo);
	mutex_unlock(&pi->lock);

	serdev_device_wait_until_sent(serdev, 0);

	return ret;
}
EXPORT_SYMBOL(pibridge_send);

int pibridge_recv_timeout(u8 *buf, u16 len, u16 timeout)
{
	struct pibridge *pi = pibridge_s;
	int jiffies;
	int ret;

	jiffies = wait_event_timeout(pi->read_queue, kfifo_len(&pi->read_fifo) >= len,
			msecs_to_jiffies(timeout));
	mutex_lock(&pi->lock);
	if (jiffies){
		ret = kfifo_out(&pi->read_fifo, buf, len);
	} else {
		ret = 0;
	}
	mutex_unlock(&pi->lock);
	if (ret < len)
		pb_err(25, "receive message error(len:%d, ret:%d, jiffies:%d, fifo:%d)\n",
				len, ret, jiffies, kfifo_len(&pi->read_fifo));
	return ret;
}
EXPORT_SYMBOL(pibridge_recv_timeout);

int pibridge_recv(u8 *buf, u16 len)
{
	/* using default timeout REV_PI_IO_TIMEOUT */
	return pibridge_recv_timeout(buf, len, REV_PI_IO_TIMEOUT);
}
EXPORT_SYMBOL(pibridge_recv);

#pragma pack(1)
struct pibridge_pkthdr_gate{
	u8	dst;
	u8	src;
	u16	cmd;
	u16	seq;
	u8	len;
};

u8 pibridge_crc8(u8 base, u8 *data, u16 len)
{
	u8 ret = base;

	while (len--) {
		ret = ret ^ data[len];
	}
	return ret;
}

int pibridge_req_send_gate(u8 dst, u16 cmd, u8 *snd_buf, u16 snd_len)
{
	struct pibridge_pkthdr_gate pkthdr;
	u8 crc;

	pkthdr.dst = dst;
	pkthdr.src = 0;
	pkthdr.cmd = cmd;
	pkthdr.seq = 0;
	pkthdr.len = snd_len;

	if (pibridge_send((u8 *)&pkthdr, sizeof(pkthdr))){
		pb_err(25, "send head error in gate-send\n");
		return -EIO;
	}
	if (snd_len != 0){
		if (pibridge_send(snd_buf, snd_len)){
			pb_err(25, "send data error in gate-send(len:%d)\n",
					snd_len);
			return -EIO;
		}

	}

	crc = pibridge_crc8(0, (u8 *)&pkthdr, sizeof(pkthdr));
	if (snd_len != 0){
		crc = pibridge_crc8(crc, snd_buf, snd_len);
	}

	if (pibridge_send(&crc, sizeof(u8))){
		pb_err(25, "send crc error in gate-send\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(pibridge_req_send_gate);

#define PIBRIDGE_RESP_CMD	0x3fff
#define PIBRIDGE_RESP_OK	0x4000
#define PIBRIDGE_RESP_ERR	0x8000

int pibridge_req_gate_tmt(u8 dst, u16 cmd, u8 *snd_buf, u16 snd_len,
		u8 *rcv_buf, u16 rcv_len, u16 tmt)
{
	struct pibridge_pkthdr_gate pkthdr;
	u8 crc_rcv;
	u8 crc;


	if (pibridge_req_send_gate(dst, cmd, snd_buf, snd_len)){
		pb_err(25, "send message error in gate-req(dst:%d, cmd:%d, len:%d)\n",
				dst, cmd, snd_len);
		return -EIO;
	}
	/* Do not wait for a response in case of a broadcast address */
	if (dst == REV_PI_BC_ADDR)
		return 0;

	if (sizeof(pkthdr) != pibridge_recv_timeout((u8 *)&pkthdr, sizeof(pkthdr), tmt)){
		pb_err(25, "receive head error in gate-req(hdr_len:%d, timeout:%d, data0:%c)\n",
				sizeof(pkthdr), tmt, snd_buf?snd_buf[0]:0);
		return -EIO;
	}

	if (rcv_len != 0) {
		if (rcv_len != pibridge_recv(rcv_buf, rcv_len)){
			pb_err(25, "receive data error in gate-req(len:%d)\n", rcv_len);
			return -EIO;
		}
	}

	if (sizeof(u8) != pibridge_recv(&crc_rcv, sizeof(u8))){
		pb_err(25, "receive crc error in gate-req\n");
		return -EIO;
	}

	crc = pibridge_crc8(0, (u8 *)&pkthdr, sizeof(pkthdr));

	if (rcv_len != 0){
		crc = pibridge_crc8(crc, rcv_buf, rcv_len);
	}
	if (crc != crc_rcv){
		return -EBADMSG;
	}

	if (!(pkthdr.cmd & PIBRIDGE_RESP_OK)){
		pb_err(25, "bad responsed OK code in gate-req(cmd:%d)\n",
				pkthdr.cmd);
		return -EBADMSG;
	}

	if (pkthdr.cmd & PIBRIDGE_RESP_ERR){
		pb_err(25, "bad responsed ERR code in gate-req(cmd:%d)\n",
				pkthdr.cmd);
		return -EBADMSG;
	}

	if ((pkthdr.cmd & PIBRIDGE_RESP_CMD) != cmd ){
		pb_err(25, "bad responsed CMD code in gate-req(cmd:%d)\n",
				pkthdr.cmd);
		return -EBADMSG;
	}

	if (rcv_len != pkthdr.len)
		pb_err(25, "received len is not as expected in gate-req(received:%d, expected:%d)\n",
				pkthdr.len, rcv_len);

	return 0;
}
EXPORT_SYMBOL(pibridge_req_gate_tmt);

int pibridge_req_gate(u8 dst, u16 cmd, u8 *snd_buf, u16 snd_len,
		u8 *rcv_buf, u16 rcv_len)
{
	return pibridge_req_gate_tmt(dst, cmd, snd_buf, snd_len,
			rcv_buf, rcv_len, REV_PI_IO_TIMEOUT);
}
EXPORT_SYMBOL(pibridge_req_gate);
struct pibridge_pkthdr_io
{
	u8 addr	:6;
	u8 typ	:1;	/* 0 for unicast, 1 for broadcast */
	u8 rsp	:1;	/*always be 0 for sending, might be 1 for receiving*/

	u8 len	:5;
	u8 cmd	:3;	/* 0 for broadcast*/
};

int pibridge_req_send_io(u8 addr, u8 cmd, u8 *snd_buf, u16 snd_len)
{
	struct pibridge_pkthdr_io pkthdr;
	u8 crc;

	pkthdr.addr	= addr;
	pkthdr.typ	= (addr == 0x3f) ? 1 : 0; /* 0 for unicast, 1 for broadcast */
	pkthdr.cmd	= cmd;
	pkthdr.len	= snd_len;

	if (pibridge_send((u8 *)&pkthdr, sizeof(pkthdr))){
		pb_err(25, "send head error in io-send(len:%d)\n",
				sizeof(pkthdr));
		return -EIO;
	}

	if (snd_len != 0){
		if (pibridge_send(snd_buf, snd_len)){
			pb_err(25, "send data error in io-send(len:%d)\n",
					snd_len);
			return -EIO;
		}
	}
	crc = pibridge_crc8(0, (u8 *)&pkthdr, sizeof(pkthdr));
	crc = pibridge_crc8(crc, snd_buf, snd_len);
	if (pibridge_send(&crc, sizeof(u8))){
		pb_err(25, "send crc error in io-send\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(pibridge_req_send_io);

int pibridge_req_io(u8 addr, u8 cmd, u8 *snd_buf, u16 snd_len, u8 *rcv_buf, u16 rcv_len)
{
	struct pibridge_pkthdr_io pkthdr;
	u8 crc_rcv;
	u8 crc;

	if (pibridge_req_send_io(addr, cmd, snd_buf, snd_len)) {
		pb_err(25, "send message error in io-req(addr:%d, cmd:%d, len:%d)\n",
				addr, cmd, snd_len);
		return -EIO;
	}

	if (sizeof(pkthdr) != pibridge_recv((u8 *)&pkthdr, sizeof(pkthdr))) {
		pb_err(25, "receive head error in io-req\n");
		return -EIO;
	}

	if(rcv_len != 0){
		if (rcv_len != pibridge_recv(rcv_buf, rcv_len)) {
			pb_err(25, "receive data error in io-req\n");
			return -EBADMSG;
		}
	}
	if (sizeof(u8) != pibridge_recv(&crc_rcv, sizeof(u8))) {
		pb_err(25, "receive crc error in io-req\n");
		return -EIO;
	}

	crc = pibridge_crc8(0, (u8 *)&pkthdr, sizeof(pkthdr));
	crc = pibridge_crc8(crc, rcv_buf, rcv_len);
	if (crc != crc_rcv) {
		pb_err(25, "check crc error in io-req\n");
		return -EBADMSG;
	}

	/*received header check is not performed in io mode*/

	if (rcv_len != pkthdr.len)
		pb_err(25, "received len is not as expected in io-req(received:%d, expected:%d)\n",
				pkthdr.len, rcv_len);

	return 0;
}
EXPORT_SYMBOL(pibridge_req_io);

MODULE_LICENSE("GPL");
