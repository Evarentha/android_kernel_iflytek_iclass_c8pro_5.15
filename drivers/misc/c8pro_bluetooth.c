// SPDX-License-Identifier: GPL-2.0-only
/* C8Pro Broadcom UART board control for the factory Android vendor HAL. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/proc_fs.h>
#include <linux/rfkill.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#define C8_BT_IDLE_MS 3000

struct c8_bt {
	struct device *dev;
	struct gpio_desc *reset, *wake, *host;
	struct rfkill *rfkill;
	struct wakeup_source *ws;
	struct proc_dir_entry *proc;
	struct mutex lock;
	struct delayed_work idle;
	int irq;
	bool powered, lpm, awake, stopping;
};

/* All state changes and sleepable GPIO accesses are serialized by lock. */
static void c8_bt_awake(struct c8_bt *bt, bool awake)
{
	gpiod_set_value_cansleep(bt->wake, awake);
	if (awake)
		__pm_stay_awake(bt->ws);
	else
		__pm_relax(bt->ws);
	bt->awake = awake;
}

static void c8_bt_activity(struct c8_bt *bt)
{
	c8_bt_awake(bt, true);
	if (bt->lpm)
		mod_delayed_work(system_wq, &bt->idle,
				 msecs_to_jiffies(C8_BT_IDLE_MS));
}

static void c8_bt_idle(struct work_struct *work)
{
	struct c8_bt *bt = container_of(to_delayed_work(work), struct c8_bt, idle);
	int host;

	mutex_lock(&bt->lock);
	if (!bt->stopping && bt->powered && bt->lpm) {
		host = gpiod_get_value_cansleep(bt->host);
		if (!host)
			c8_bt_awake(bt, false);
		else /* An input error must not let the host sleep during traffic. */
			c8_bt_activity(bt);
	}
	mutex_unlock(&bt->lock);
}

static irqreturn_t c8_bt_irq(int irq, void *data)
{
	struct c8_bt *bt = data;

	/* Hold off suspend until the threaded handler can inspect the GPIO. */
	pm_wakeup_event(bt->dev, C8_BT_IDLE_MS);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t c8_bt_irq_thread(int irq, void *data)
{
	struct c8_bt *bt = data;

	mutex_lock(&bt->lock);
	if (!bt->stopping && bt->powered)
		c8_bt_activity(bt);
	mutex_unlock(&bt->lock);
	return IRQ_HANDLED;
}

static int c8_bt_set_block(void *data, bool blocked)
{
	struct c8_bt *bt = data;

	mutex_lock(&bt->lock);
	if (bt->stopping) {
		mutex_unlock(&bt->lock);
		return -ENODEV;
	}
	if (bt->powered == !blocked)
		goto out;
	if (blocked) {
		bt->powered = false;
		bt->lpm = false;
		cancel_delayed_work(&bt->idle);
		gpiod_set_value_cansleep(bt->reset, 0);
		msleep(100);
		c8_bt_awake(bt, false);
	} else {
		/* Factory sequence: reset low 50 ms, high 50 ms. */
		bt->lpm = false;
		c8_bt_awake(bt, true);
		gpiod_set_value_cansleep(bt->reset, 0);
		msleep(50);
		gpiod_set_value_cansleep(bt->reset, 1);
		msleep(50);
		bt->powered = true;
	}
	dev_info(bt->dev, "C8BT power=%u\n", bt->powered);
out:
	mutex_unlock(&bt->lock);
	return 0;
}

static const struct rfkill_ops c8_bt_rfkill_ops = {
	.set_block = c8_bt_set_block,
};

static ssize_t c8_bt_read(struct file *file, char __user *buf,
			 size_t count, loff_t *pos)
{
	struct c8_bt *bt = PDE_DATA(file_inode(file));
	char value[2];

	mutex_lock(&bt->lock);
	value[0] = bt->lpm ? '1' : '0';
	value[1] = '\n';
	mutex_unlock(&bt->lock);
	return simple_read_from_buffer(buf, count, pos, value, sizeof(value));
}

static ssize_t c8_bt_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *pos, bool lpm)
{
	struct c8_bt *bt = PDE_DATA(file_inode(file));
	char value[2];
	int ret = 0;

	if (!count || count > sizeof(value))
		return -EINVAL;
	if (copy_from_user(value, buf, count))
		return -EFAULT;
	if ((value[0] != '0' && value[0] != '1') ||
	    (count == 2 && value[1] != '\n'))
		return -EINVAL;
	mutex_lock(&bt->lock);
	if (bt->stopping) {
		ret = -ENODEV;
		goto out;
	}
	if (lpm) {
		bt->lpm = value[0] == '1';
		if (!bt->lpm)
			cancel_delayed_work(&bt->idle);
		if (bt->powered)
			c8_bt_activity(bt);
		dev_info(bt->dev, "C8BT lpm=%u\n", bt->lpm);
	} else if (bt->powered) {
		/* '1' announces TX; '0' ends TX with a drain grace period. */
		c8_bt_activity(bt);
	}
out:
	mutex_unlock(&bt->lock);
	if (ret)
		return ret;
	return count;
}

static ssize_t c8_bt_lpm_write(struct file *f, const char __user *b,
			      size_t n, loff_t *p)
{
	return c8_bt_write(f, b, n, p, true);
}

static ssize_t c8_bt_tx_write(struct file *f, const char __user *b,
			     size_t n, loff_t *p)
{
	return c8_bt_write(f, b, n, p, false);
}

static const struct proc_ops c8_bt_lpm_ops = {
	.proc_read = c8_bt_read,
	.proc_write = c8_bt_lpm_write,
	.proc_lseek = default_llseek,
};

static const struct proc_ops c8_bt_tx_ops = {
	.proc_write = c8_bt_tx_write,
};

static void c8_bt_stop(struct c8_bt *bt)
{
	mutex_lock(&bt->lock);
	bt->stopping = true;
	bt->powered = false;
	gpiod_set_value_cansleep(bt->reset, 0);
	c8_bt_awake(bt, false);
	mutex_unlock(&bt->lock);
	disable_irq(bt->irq);
	cancel_delayed_work_sync(&bt->idle);
}

static int c8_bt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct proc_dir_entry *sleep, *entry;
	struct c8_bt *bt;
	int ret;

	bt = devm_kzalloc(dev, sizeof(*bt), GFP_KERNEL);
	if (!bt)
		return -ENOMEM;
	bt->dev = dev;
	mutex_init(&bt->lock);
	INIT_DELAYED_WORK(&bt->idle, c8_bt_idle);
	bt->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(bt->reset))
		return dev_err_probe(dev, PTR_ERR(bt->reset), "reset GPIO\n");
	bt->wake = devm_gpiod_get(dev, "device-wake", GPIOD_OUT_LOW);
	if (IS_ERR(bt->wake))
		return dev_err_probe(dev, PTR_ERR(bt->wake), "device-wake GPIO\n");
	bt->host = devm_gpiod_get(dev, "host-wake", GPIOD_IN);
	if (IS_ERR(bt->host))
		return dev_err_probe(dev, PTR_ERR(bt->host), "host-wake GPIO\n");
	bt->irq = gpiod_to_irq(bt->host);
	if (bt->irq < 0)
		return bt->irq;
	bt->ws = wakeup_source_register(dev, "c8pro-bluetooth");
	if (!bt->ws)
		return -ENOMEM;
	ret = device_init_wakeup(dev, true);
	if (ret)
		goto free_ws;
	ret = devm_request_threaded_irq(dev, bt->irq, c8_bt_irq, c8_bt_irq_thread,
		IRQF_ONESHOT | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"c8pro-bt-hostwake", bt);
	if (ret)
		goto free_wakeup;
	ret = enable_irq_wake(bt->irq);
	if (ret)
		goto free_irq;
	bt->proc = proc_mkdir("bluetooth", NULL);
	if (!bt->proc) {
		ret = -ENOMEM;
		goto disable_wake;
	}
	sleep = proc_mkdir("sleep", bt->proc);
	if (!sleep) {
		ret = -ENOMEM;
		goto free_proc;
	}
	entry = proc_create_data("lpm", 0660, sleep, &c8_bt_lpm_ops, bt);
	if (!entry) {
		ret = -ENOMEM;
		goto free_proc;
	}
	proc_set_user(entry, KUIDT_INIT(1002), KGIDT_INIT(3001));
	entry = proc_create_data("btwrite", 0660, sleep, &c8_bt_tx_ops, bt);
	if (!entry) {
		ret = -ENOMEM;
		goto free_proc;
	}
	proc_set_user(entry, KUIDT_INIT(1002), KGIDT_INIT(3001));
	bt->rfkill = rfkill_alloc("bcm43xx", dev, RFKILL_TYPE_BLUETOOTH,
				 &c8_bt_rfkill_ops, bt);
	if (!bt->rfkill) {
		ret = -ENOMEM;
		goto free_proc;
	}
	rfkill_init_sw_state(bt->rfkill, true);
	ret = rfkill_register(bt->rfkill);
	if (ret) {
		rfkill_destroy(bt->rfkill);
		goto free_proc;
	}
	platform_set_drvdata(pdev, bt);
	dev_info(dev, "C8BT rfkill and factory LPM interfaces ready\n");
	return 0;

free_proc:
	proc_remove(bt->proc);
	mutex_lock(&bt->lock);
	bt->stopping = true;
	gpiod_set_value_cansleep(bt->reset, 0);
	c8_bt_awake(bt, false);
	mutex_unlock(&bt->lock);
	cancel_delayed_work_sync(&bt->idle);
disable_wake:
	disable_irq_wake(bt->irq);
free_irq:
	devm_free_irq(dev, bt->irq, bt);
free_wakeup:
	device_init_wakeup(dev, false);
free_ws:
	wakeup_source_unregister(bt->ws);
	return ret;
}

static int c8_bt_remove(struct platform_device *pdev)
{
	struct c8_bt *bt = platform_get_drvdata(pdev);

	rfkill_unregister(bt->rfkill);
	proc_remove(bt->proc);
	c8_bt_stop(bt);
	disable_irq_wake(bt->irq);
	devm_free_irq(&pdev->dev, bt->irq, bt);
	device_init_wakeup(&pdev->dev, false);
	rfkill_destroy(bt->rfkill);
	wakeup_source_unregister(bt->ws);
	return 0;
}

static void c8_bt_shutdown(struct platform_device *pdev)
{
	c8_bt_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id c8_bt_match[] = {
	{ .compatible = "iflytek,c8pro-bt-control" },
	{ }
};
MODULE_DEVICE_TABLE(of, c8_bt_match);

static struct platform_driver c8_bt_driver = {
	.probe = c8_bt_probe,
	.remove = c8_bt_remove,
	.shutdown = c8_bt_shutdown,
	.driver = {
		.name = "c8pro-bt-control",
		.of_match_table = c8_bt_match,
	},
};
module_platform_driver(c8_bt_driver);

MODULE_DESCRIPTION("C8Pro Broadcom UART rfkill and vendor LPM control");
MODULE_LICENSE("GPL");
