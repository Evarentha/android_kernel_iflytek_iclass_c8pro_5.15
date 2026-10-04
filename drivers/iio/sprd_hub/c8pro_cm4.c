// SPDX-License-Identifier: GPL-2.0
/* Android 9 modem_control PM firmware loader ABI for the C8Pro CM4. */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct cm4_segment {
	char name[32];
	u32 base;
	u32 size;
};

struct c8_cm4 {
	struct cm4_segment segment[2];
	struct regmap *aon;
	u32 reset_reg, reset_mask;
	void __iomem *ram;
	struct proc_dir_entry *dir;
	struct mutex lock;
};

static ssize_t cm4_info_read(struct file *file, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct c8_cm4 *cm4 = PDE_DATA(file_inode(file));

	return simple_read_from_buffer(buf, count, pos, cm4->segment,
				       sizeof(cm4->segment));
}

static ssize_t cm4_control_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct c8_cm4 *cm4 = PDE_DATA(file_inode(file));
	bool start = !strcmp(file->f_path.dentry->d_name.name, "start");
	int ret;

	if (!count)
		return 0;
	mutex_lock(&cm4->lock);
	ret = regmap_update_bits(cm4->aon, cm4->reset_reg, cm4->reset_mask,
				 start ? 0 : cm4->reset_mask);
	mutex_unlock(&cm4->lock);
	return ret ? ret : count;
}

static ssize_t cm4_image_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct c8_cm4 *cm4 = PDE_DATA(file_inode(file));
	unsigned int index = !strcmp(file->f_path.dentry->d_name.name, "cali_lib");
	struct cm4_segment *seg = &cm4->segment[index];
	u8 *data;
	size_t i;
	void __iomem *dst;

	if (*pos < 0 || *pos > seg->size)
		return -EINVAL;
	count = min_t(size_t, count, seg->size - *pos);
	count = min_t(size_t, count, PAGE_SIZE);
	if (!count)
		return 0;
	data = memdup_user(buf, count);
	if (IS_ERR(data))
		return PTR_ERR(data);
	/* modem_control performs the factory TEE unlock before writing here.
	 * Do not load or start firmware autonomously from probe.
	 */
	mutex_lock(&cm4->lock);
	dst = cm4->ram + seg->base - cm4->segment[0].base + *pos;
	for (i = 0; i < count; i++)
		writeb(data[i], dst + i);
	/* Finish firmware writes before userspace requests verification/start. */
	wmb();
	mutex_unlock(&cm4->lock);
	kfree(data);
	*pos += count;
	return count;
}

static const struct proc_ops cm4_info_ops = {
	.proc_read = cm4_info_read,
	.proc_lseek = default_llseek,
};
static const struct proc_ops cm4_control_ops = {
	.proc_write = cm4_control_write,
	.proc_lseek = noop_llseek,
};
static const struct proc_ops cm4_image_ops = {
	.proc_write = cm4_image_write,
	.proc_lseek = default_llseek,
};

static int c8_cm4_probe(struct platform_device *pdev)
{
	struct c8_cm4 *cm4;
	struct resource *res;
	u32 control[2], split;
	int i, ret;

	cm4 = devm_kzalloc(&pdev->dev, sizeof(*cm4), GFP_KERNEL);
	if (!cm4)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res || res->start != 0x800000 || resource_size(res) != 0x40000)
		return -EINVAL;
	ret = of_property_read_u32(pdev->dev.of_node, "sprd,calibration-offset", &split);
	if (ret || !split || split >= resource_size(res))
		return -EINVAL;
	cm4->aon = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "sprd,reset-syscon");
	if (IS_ERR(cm4->aon))
		return PTR_ERR(cm4->aon);
	ret = of_property_read_u32_array(pdev->dev.of_node, "sprd,reset-control", control, 2);
	if (ret)
		return ret;
	cm4->reset_reg = control[0]; cm4->reset_mask = control[1];
	cm4->ram = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cm4->ram))
		return PTR_ERR(cm4->ram);
	strscpy(cm4->segment[0].name, "pm_sys", 32);
	strscpy(cm4->segment[1].name, "cali_lib", 32);
	cm4->segment[0].base = res->start; cm4->segment[0].size = split;
	cm4->segment[1].base = res->start + split;
	cm4->segment[1].size = resource_size(res) - split;
	mutex_init(&cm4->lock);
	cm4->dir = proc_mkdir("c8pro_cm4", NULL);
	if (!cm4->dir)
		return -ENOMEM;
	if (!proc_create_data("ldinfo", 0440, cm4->dir, &cm4_info_ops, cm4) ||
	    !proc_create_data("start", 0200, cm4->dir, &cm4_control_ops, cm4) ||
	    !proc_create_data("stop", 0200, cm4->dir, &cm4_control_ops, cm4))
		goto fail;
	for (i = 0; i < 2; i++)
		if (!proc_create_data(cm4->segment[i].name, 0200, cm4->dir, &cm4_image_ops, cm4))
			goto fail;
	platform_set_drvdata(pdev, cm4);
	dev_info(&pdev->dev, "C8Pro loader ready; awaiting userspace TEE load\n");
	return 0;
fail:
	proc_remove(cm4->dir);
	return -ENOMEM;
}

static int c8_cm4_remove(struct platform_device *pdev)
{
	struct c8_cm4 *cm4 = platform_get_drvdata(pdev);

	proc_remove(cm4->dir);
	return 0;
}
static const struct of_device_id c8_cm4_match[] = {
	{ .compatible = "iflytek,c8pro-cm4-loader" }, { }
};
MODULE_DEVICE_TABLE(of, c8_cm4_match);
static struct platform_driver c8_cm4_driver = {
	.probe = c8_cm4_probe, .remove = c8_cm4_remove,
	.driver = { .name = "c8pro-cm4-loader", .of_match_table = c8_cm4_match },
};
module_platform_driver(c8_cm4_driver);
MODULE_LICENSE("GPL");
