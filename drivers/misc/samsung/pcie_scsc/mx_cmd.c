#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <asm/uaccess.h>

#include <pcie_scsc/scsc_mx.h>
#include <pcie_scsc/scsc_logring.h>
#include "mxmgmt_transport_format.h"
#include "mxmgmt_transport.h"
#include "mx_cmd.h"
#include "mxman.h"

static dev_t cmd_dev_t;
static struct class *cmd_class;
static struct cdev  *cmd_cdev;

struct kfifo read_buffer, write_buffer;

struct scsc_mx_cmd {
	struct scsc_mx *mx;
};

static struct scsc_mx_cmd *cmd;

wait_queue_head_t read_wait;

static void channel_message_handler(const void *message, void *data)
{
	const struct cmd_msg_packet *msg = message;

	SCSC_TAG_INFO(MXMAN_TEST, "Receive buffer available : %d\n", kfifo_avail(&read_buffer));

	if (kfifo_avail(&read_buffer) >= PACKET_SIZE) {
		kfifo_in(&read_buffer, msg->msg, PACKET_SIZE);
		SCSC_TAG_INFO(MXMAN_TEST, "Received message moved to read buffer(%d)\n", kfifo_len(&read_buffer));
		wake_up_interruptible(&read_wait);
	} else {
		SCSC_TAG_ERR(MXMAN_TEST, "Not enough read buffer to handle received message\n");
	}
}

void cmd_module_probe(struct scsc_mx_module_client *module_client, struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);

	cmd->mx = mx;

	init_waitqueue_head(&read_wait);

	SCSC_TAG_INFO(MXMAN_TEST, "OK\n");
}

void cmd_module_remove(struct scsc_mx_module_client *module_client, struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	if (!cmd)
		return;

	if (cmd->mx != mx) {
		SCSC_TAG_ERR(MXMAN_TEST, "cmd->mx != mx\n");
		return;
	}

	kfree(cmd);

	SCSC_TAG_INFO(MXMAN_TEST, "OK\n");
}

static int cmd_dev_open(struct inode *inode, struct file *file)
{
	struct mxman *mxman = scsc_mx_get_mxman(cmd->mx);

	SCSC_TAG_INFO(MXMAN_TEST, "open mx cmd\n");

	if (cmd == NULL || cmd->mx == NULL) {
		SCSC_TAG_ERR(MXMAN_TEST, "cmd is NULL\n");
		return -EFAULT;
	}

#if defined(CONFIG_SCSC_INDEPENDENT_SUBSYSTEM)
	if (!mxman_if_subsys_active(mxman, SCSC_SUBSYSTEM_WLAN) && !mxman_if_subsys_active(mxman, SCSC_SUBSYSTEM_WLAN_WPAN))
#else
	if (mxman->mxman_state != MXMAN_STATE_STARTED)
#endif
	{
		SCSC_TAG_ERR(MXMAN_TEST, "WLAN is not active\n");
		return -EFAULT;
	}

	scsc_mx_service_claim(DEFAULT_CLAIM_TYPE);

	mxmgmt_transport_register_channel_handler(scsc_mx_get_mxmgmt_transport(cmd->mx),
		MMTRANS_CHAN_ID_MAXWELL_COMMAND, &channel_message_handler, NULL);
	SCSC_TAG_INFO(MXMAN_TEST, "handler registered\n");

	if (kfifo_alloc(&write_buffer, PACKET_SIZE * NUM_PACKET, GFP_KERNEL)){
		SCSC_TAG_ERR(MXMAN_TEST, "error kfifo_alloc\n");
		return -EFAULT;
	}

	if (kfifo_alloc(&read_buffer, PACKET_SIZE * NUM_PACKET, GFP_KERNEL)){
		SCSC_TAG_ERR(MXMAN_TEST, "error kfifo_alloc\n");
		return -EFAULT;
	}

	return 0;
}

static int cmd_dev_release(struct inode *inode, struct file *file)
{
	SCSC_TAG_INFO(MXMAN_TEST, "close mx cmd\n");

	kfifo_free(&write_buffer);
	kfifo_free(&read_buffer);

	scsc_mx_service_release(DEFAULT_CLAIM_TYPE);

	if (cmd->mx != NULL) {
		mxmgmt_transport_register_channel_handler(scsc_mx_get_mxmgmt_transport(cmd->mx), 
			MMTRANS_CHAN_ID_MAXWELL_COMMAND, NULL, NULL);
		SCSC_TAG_INFO(MXMAN_TEST, "handler unregistered\n");
	} else {
		SCSC_TAG_ERR(MXMAN_TEST, "scsc_mx NULL \n");
		return -EFAULT;
	}

	return 0;
}

static ssize_t cmd_dev_write(struct file *file, const char *data, size_t len, loff_t *offset)
{
	int ret, transfered = 0;
	char packet_to_fw[PACKET_SIZE];
	unsigned int copied = 0;

	if (kfifo_avail(&write_buffer) >= len + PACKET_SIZE) {
		ret = kfifo_from_user(&write_buffer, data, len, &copied);
		if (!ret) {
			SCSC_TAG_INFO(MX_MMAP, "Copied %d bytes from user.\n", copied);
			ret = copied;
		} else {
			SCSC_TAG_ERR(MX_MMAP, "returned : %d\n", ret);
			return ret;
		}
	}

	while (kfifo_len(&write_buffer) >= PACKET_SIZE) {
		ret = kfifo_out(&write_buffer, packet_to_fw, PACKET_SIZE);
		if (cmd->mx != NULL) {
			mxmgmt_transport_send(scsc_mx_get_mxmgmt_transport(cmd->mx),
			MMTRANS_CHAN_ID_MAXWELL_COMMAND, packet_to_fw, PACKET_SIZE);

			SCSC_TAG_INFO(MX_MMAP, "1 packet %d bytes sent to fw\n", PACKET_SIZE);
			transfered += PACKET_SIZE;
		}
		SCSC_TAG_INFO(MX_MMAP, "%d bytes ramains after a packet sent to fw\n", kfifo_len(&write_buffer));
	}

	SCSC_TAG_INFO(MX_MMAP, "Finally, %d transfered, %d bytes ramains after write operation\n", transfered, kfifo_len(&write_buffer));

	return copied;
}

static ssize_t cmd_dev_read(struct file *file, char *data, size_t len, loff_t *offset)
{
	int ret = 0;
	unsigned int copied;

	SCSC_TAG_INFO(MX_MMAP, "%d requested, %d in read buffer\n", len, kfifo_len(&read_buffer));

	while (len) {
		if (!kfifo_is_empty(&read_buffer)) {
			ret = kfifo_to_user(&read_buffer, data, MIN(len, kfifo_len(&read_buffer)), &copied);
			if (!ret) {
				SCSC_TAG_INFO(MX_MMAP, "%d bytes copied to user, %d bytes left in read buffer\n", copied, kfifo_len(&read_buffer));
				ret = copied;
			}
			break;
		}

		SCSC_TAG_INFO(MX_MMAP, "no more message available to send\n");

		if (file->f_flags & O_NONBLOCK) {
			SCSC_TAG_INFO(MX_MMAP, "O_NONBLOCK\n");
			ret = -EAGAIN;
			break;
		}

		ret = wait_event_interruptible(read_wait,
				!kfifo_is_empty(&read_buffer));

		if (ret < 0) {
			break;
		}
	}

	SCSC_TAG_INFO(MX_MMAP, "retuned : %d\n", ret);

	return ret;
}

static unsigned int cmd_dev_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(filp, &read_wait, wait);

	if (!kfifo_is_empty(&read_buffer)) {
		SCSC_TAG_INFO(MX_MMAP, "readable & ");
		mask |= POLLIN | POLLRDNORM;  /* readeable */
	}
	SCSC_TAG_INFO(MX_MMAP, "writable\n");
	mask |= POLLOUT | POLLWRNORM;         /* writable */

	return mask;
}


static const struct file_operations mx_cmd_dev_fops = {
	.owner = THIS_MODULE,
	.open = cmd_dev_open,
	.read = cmd_dev_read,
	.write = cmd_dev_write,
	.poll = cmd_dev_poll,
	.release = cmd_dev_release,
};


static struct scsc_mx_module_client mx_cmd_driver = {
	.name = "MX cmd driver",
	.probe = cmd_module_probe,
	.remove = cmd_module_remove,
};


int scsc_mx_cmd_driver_create(void)
{
	int r;

	SCSC_TAG_INFO(MXMAN_TEST, "\n");

	r = scsc_mx_module_register_client_module(&mx_cmd_driver);
	if (r) {
		SCSC_TAG_ERR(MXMAN_TEST, "scsc_mx_module_register_client_module failed: r=%d\n", r);
		return r;
	}

	r = alloc_chrdev_region(&cmd_dev_t, 0, 1, "wlbt-mx-cmd");
	if (r < 0) {
		SCSC_TAG_ERR(MXMAN_TEST, "failed to alloc chrdev region\n");
		goto fail_alloc_chrdev_region;
	}

	cmd_cdev = cdev_alloc();
	if (!cmd_cdev) {
		r = -ENOMEM;
		SCSC_TAG_ERR(MXMAN_TEST, "failed to alloc cdev\n");
		goto fail_alloc_cdev;
	}

	cdev_init(cmd_cdev, &mx_cmd_dev_fops);
	r = cdev_add(cmd_cdev, cmd_dev_t, 1);
	if (r < 0) {
		SCSC_TAG_ERR(MXMAN_TEST, "failed to add cdev\n");
		goto fail_add_cdev;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	cmd_class = class_create("mx_cmd_class");
#else
	cmd_class = class_create(THIS_MODULE, "mx_cmd_class");
#endif
	if (!cmd_class) {
		r = -EEXIST;
		SCSC_TAG_ERR(MXMAN_TEST, "failed to create class\n");
		goto fail_create_class;
	}

	if (!device_create(cmd_class, NULL, cmd_dev_t, NULL, "mx_cmd_%d", MINOR(cmd_dev_t))) {
		r = -EINVAL;
		SCSC_TAG_ERR(MXMAN_TEST, "failed to create device\n");
		goto fail_create_device;
	}

	return 0;
fail_create_device:
	class_destroy(cmd_class);
fail_create_class:
	cdev_del(cmd_cdev);
fail_add_cdev:
fail_alloc_cdev:
	unregister_chrdev_region(cmd_dev_t, 1);
fail_alloc_chrdev_region:
	return r;
}

void scsc_mx_cmd_driver_destory(void)
{
	SCSC_TAG_INFO(MXMAN_TEST, "\n");
	scsc_mx_module_unregister_client_module(&mx_cmd_driver);

	device_destroy(cmd_class, cmd_dev_t);
	class_destroy(cmd_class);
	cdev_del(cmd_cdev);
	unregister_chrdev_region(cmd_dev_t, 1);
}
