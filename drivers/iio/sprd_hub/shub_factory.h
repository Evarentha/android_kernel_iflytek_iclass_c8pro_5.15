/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Legacy SensorHub Firmware Compatibility
 *
 * Implements factory opcode downloads and calibration initialization.
 * Included by shub_core.c.
 *
 * Authors:
 * worryzu <worryzu@gmail.com> @LinearTeam
 *
 * Copyright (C) 2026 Evarentha
 */
/* The Android 9 extractor uses /sys/module/shub_core/parameters. */
#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "shub_core."
#define SHUB_FACTORY_CANDIDATES 6
static char *acc_firms[SHUB_FACTORY_CANDIDATES];
static char *gryo_firms[SHUB_FACTORY_CANDIDATES];
static char *mag_firms[SHUB_FACTORY_CANDIDATES];
static char *light_firms[SHUB_FACTORY_CANDIDATES];
static char *prox_firms[SHUB_FACTORY_CANDIDATES];
static char *pressure_firms[SHUB_FACTORY_CANDIDATES];
static unsigned int sensor_fusion_mode;
module_param_array(acc_firms, charp, NULL, 0644);
module_param_array(gryo_firms, charp, NULL, 0644);
module_param_array(mag_firms, charp, NULL, 0644);
module_param_array(light_firms, charp, NULL, 0644);
module_param_array(prox_firms, charp, NULL, 0644);
module_param_array(pressure_firms, charp, NULL, 0644);
module_param(sensor_fusion_mode, uint, 0644);

struct shub_factory_blob {
	u8 *data;
	size_t size;
};

static void shub_factory_response(struct shub_data *sensor, u8 *data, u32 len)
{
	/* Factory CM4 appends four bytes to the command/status pair. */
	if (len < 2 || data[0] != SHUB_DOWNLOAD_OPCODE_SUBTYPE)
		return;
	sensor->sent_cmddata.status = data[1];
	WRITE_ONCE(sensor->sent_cmddata.condition, true);
	wake_up(&sensor->rw_wait_queue);
}

static int shub_factory_read(const char *name, bool calibration,
			     struct shub_factory_blob *blob)
{
	char path[160];
	struct file *file;
	loff_t size, pos = 0;
	ssize_t ret;

	if (!name || !*name || strchr(name, '/') || strlen(name) > 100)
		return -EINVAL;
	snprintf(path, sizeof(path), "/mnt/vendor/sensorhub/shub_fw_%s%s",
		 name, calibration ? "_cali" : "");
	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);
	size = i_size_read(file_inode(file));
	if (size <= 0 || size > (calibration ? 255 :
		MAX_MSG_BUFF_SIZE - SHUB_MAX_HEAD_LEN - SHUB_MAX_DATA_CRC)) {
		ret = -EFBIG;
		goto close;
	}
	blob->data = kmalloc(size, GFP_KERNEL);
	if (!blob->data) {
		ret = -ENOMEM;
		goto close;
	}
	ret = kernel_read(file, blob->data, size, &pos);
	if (ret != size) {
		kfree(blob->data);
		blob->data = NULL;
		ret = ret < 0 ? ret : -EIO;
	} else {
		blob->size = size;
		ret = 0;
	}
close:
	filp_close(file, NULL);
	return ret;
}

static int shub_factory_download(struct shub_data *sensor)
{
	/* Calibration wire order differs from sensor discovery order. */
	char **names[] = { mag_firms, prox_firms, light_firms, acc_firms,
			 gryo_firms, pressure_firms };
	const int types[] = { SENSOR_GEOMAGNETIC_FIELD, SENSOR_PROXIMITY,
		SENSOR_LIGHT, SENSOR_ACCELEROMETER, SENSOR_GYROSCOPE, SENSOR_PRESSURE };
	const int order[] = { 3, 4, 0, 2, 1, 5 };
	struct shub_factory_blob cal[6] = { }, fw;
	u8 *packed, *cursor;
	size_t size = 7 + sizeof(sensor_fusion_mode);
	int n, index, i, retry, ret = -ENODEV;

	if (!acc_firms[0])
		return -ENOENT;
	for (n = 0; n < ARRAY_SIZE(order); n++) {
		index = order[n];
		for (i = 0; i < SHUB_FACTORY_CANDIDATES && names[index][i]; i++) {
			fw = (struct shub_factory_blob) { };
			ret = shub_factory_read(names[index][i], true, &cal[index]);
			if (ret)
				continue;
			ret = shub_factory_read(names[index][i], false, &fw);
			if (!ret) {
				for (retry = 0; retry < 10; retry++) {
					ret = shub_send_command(sensor, types[index],
						SHUB_DOWNLOAD_OPCODE_SUBTYPE, fw.data, fw.size);
					if (ret != RESPONSE_TIMEOUT)
						break;
					msleep(200);
				}
			}
			kfree(fw.data);
			if (!ret) {
				dev_info(&sensor->sensor_pdev->dev,
					 "factory sensor %s identified\n", names[index][i]);
				size += cal[index].size;
				break;
			}
			kfree(cal[index].data);
			cal[index] = (struct shub_factory_blob) { };
		}
		msleep(200);
	}
	if (!cal[3].size) {
		ret = -ENODEV;
		goto free;
	}
	packed = kzalloc(size, GFP_KERNEL);
	if (!packed) {
		ret = -ENOMEM;
		goto free;
	}
	cursor = packed + 7;
	for (i = 0; i < ARRAY_SIZE(cal); i++) {
		packed[i] = cal[i].size;
		if (cal[i].size) {
			memcpy(cursor, cal[i].data, cal[i].size);
			cursor += cal[i].size;
		}
	}
	packed[6] = sizeof(sensor_fusion_mode);
	memcpy(cursor, &sensor_fusion_mode, sizeof(sensor_fusion_mode));
	ret = shub_send_command(sensor, SENSOR_TYPE_CALIBRATION_CFG,
		SHUB_DOWNLOAD_CALIBRATION_SUBTYPE, packed, size);
	kfree(packed);
	if (ret > 0)
		ret = 0;
free:
	for (i = 0; i < ARRAY_SIZE(cal); i++)
		kfree(cal[i].data);
	return ret;
}

static void shub_factory_restore_calibration(struct shub_data *sensor)
{
	static const char * const names[] = { "none", "acc", "mag", "orientation",
		"gyro", "light", "pressure", "tempreature", "proximity" };
	char path[128];
	u8 data[30];
	int i;

	for (i = 1; i < ARRAY_SIZE(names); i++) {
		struct file *file;
		loff_t pos = 0;

		snprintf(path, sizeof(path),
			 "/mnt/vendor/productinfo/sensor_calibration_data/%s", names[i]);
		file = filp_open(path, O_RDONLY, 0);
		if (IS_ERR(file))
			continue;
		if (i_size_read(file_inode(file)) == sizeof(data) &&
		    kernel_read(file, data, sizeof(data), &pos) == sizeof(data))
			shub_send_command(sensor, i, SHUB_SET_CALIBRATION_DATA_SUBTYPE,
					  data, sizeof(data));
		filp_close(file, NULL);
	}
}

static ssize_t op_download_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct shub_data *sensor = dev_get_drvdata(dev);
	u8 version[4];
	int ret = 0, retry;

	mutex_lock(&sensor->factory_init_lock);
	if (sensor->mcu_mode == SHUB_BOOT) {
		for (retry = 0; retry < 15; retry++) {
			ret = shub_sipc_read(sensor, SHUB_GET_FWVERSION_SUBTYPE, version, 4);
			if (!ret)
				break;
			msleep(1000);
		}
		if (ret)
			goto out;
		ret = shub_factory_download(sensor);
		if (ret)
			goto out;
		sensor->factory_version = version[0] | version[1] << 8;
		sensor->factory_subversion = version[0];
		sensor->mcu_mode = SHUB_NORMAL;
		shub_factory_restore_calibration(sensor);
		queue_delayed_work(sensor->driver_wq, &sensor->time_sync_work, 0);
	}
out:
	mutex_unlock(&sensor->factory_init_lock);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d %u\n", sensor->factory_version,
			  sensor->factory_subversion);
}
static DEVICE_ATTR_RO(op_download);
