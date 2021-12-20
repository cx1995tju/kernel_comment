#include <linux/virtio.h>
#include <linux/spinlock.h>
#include <linux/virtio_config.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <uapi/linux/virtio_ids.h>

/* Unique numbering for virtio devices. */
static DEFINE_IDA(virtio_index_ida);

static ssize_t device_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "0x%04x\n", dev->id.device);
}
static DEVICE_ATTR_RO(device);

static ssize_t vendor_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "0x%04x\n", dev->id.vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t status_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "0x%08x\n", dev->config->get_status(dev));
}
static DEVICE_ATTR_RO(status);

static ssize_t modalias_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sprintf(buf, "virtio:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t features_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	unsigned int i;
	ssize_t len = 0;

	/* We actually represent this as a bitstring, as it could be
	 * arbitrary length in future. */
	for (i = 0; i < sizeof(dev->features)*8; i++)
		len += sprintf(buf+len, "%c",
			       __virtio_test_bit(dev, i) ? '1' : '0');
	len += sprintf(buf+len, "\n");
	return len;
}
static DEVICE_ATTR_RO(features);

static struct attribute *virtio_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_status.attr,
	&dev_attr_modalias.attr,
	&dev_attr_features.attr,
	NULL,
};
ATTRIBUTE_GROUPS(virtio_dev);

static inline int virtio_id_match(const struct virtio_device *dev,
				  const struct virtio_device_id *id)
{
	if (id->device != dev->id.device && id->device != VIRTIO_DEV_ANY_ID)
		return 0;

	return id->vendor == VIRTIO_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/* This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call virtio_dev_probe(). */
static int virtio_dev_match(struct device *_dv, struct device_driver *_dr)
{
	unsigned int i;
	struct virtio_device *dev = dev_to_virtio(_dv);
	const struct virtio_device_id *ids;

	ids = drv_to_virtio(_dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (virtio_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int virtio_uevent(struct device *_dv, struct kobj_uevent_env *env)
{
	struct virtio_device *dev = dev_to_virtio(_dv);

	return add_uevent_var(env, "MODALIAS=virtio:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

void virtio_check_driver_offered_feature(const struct virtio_device *vdev,
					 unsigned int fbit)
{
	unsigned int i;
	struct virtio_driver *drv = drv_to_virtio(vdev->dev.driver);

	for (i = 0; i < drv->feature_table_size; i++)
		if (drv->feature_table[i] == fbit)
			return;

	if (drv->feature_table_legacy) {
		for (i = 0; i < drv->feature_table_size_legacy; i++)
			if (drv->feature_table_legacy[i] == fbit)
				return;
	}

	BUG();
}
EXPORT_SYMBOL_GPL(virtio_check_driver_offered_feature);

static void __virtio_config_changed(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	if (!dev->config_enabled)
		dev->config_change_pending = true;
	else if (drv && drv->config_changed)
		drv->config_changed(dev);
}

void virtio_config_changed(struct virtio_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->config_lock, flags);
	__virtio_config_changed(dev);
	spin_unlock_irqrestore(&dev->config_lock, flags);
}
EXPORT_SYMBOL_GPL(virtio_config_changed);

void virtio_config_disable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_enabled = false;
	spin_unlock_irq(&dev->config_lock);
}
EXPORT_SYMBOL_GPL(virtio_config_disable);

void virtio_config_enable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_enabled = true;
	if (dev->config_change_pending)
		__virtio_config_changed(dev);
	dev->config_change_pending = false;
	spin_unlock_irq(&dev->config_lock);
}
EXPORT_SYMBOL_GPL(virtio_config_enable);

void virtio_add_status(struct virtio_device *dev, unsigned int status)
{
	dev->config->set_status(dev, dev->config->get_status(dev) | status);
}
EXPORT_SYMBOL_GPL(virtio_add_status);

int virtio_finalize_features(struct virtio_device *dev)
{
	int ret = dev->config->finalize_features(dev);
	unsigned status;

	if (ret)
		return ret;

	if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1))
		return 0;

	virtio_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
	status = dev->config->get_status(dev);
	if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
		dev_err(&dev->dev, "virtio: device refuses features: %x\n",
			status);
		return -ENODEV;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_finalize_features);

/* virtio 驱动初始化设备的一般过程, 具体设备specific的还是要参考具体设备的probe函数， %virtio_balloon_probe */
/* 1. 重置设备， register_virtio_device 中会调用dev->config->reset */
/* 2. 设置acknowledge状态位，表示driver知道了设备存在, register_virtio_device -> add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE) */
/* 3. 设置DRIVER状态位, 表示driver知道如何驱动设备, 在virtio总线的probe函数，virtio_dev_probe -> add_status(dev, VIRTIO_CONFIG_S_DRIVER) */
/* 4. 读取virtio 设备的feature位, virtio_dev_probe 中计算driver_features device_features，然后调用virtio_finalize_features */
/* 5. 设置FEATURES_OK特性位，这之后，virtio驱动就不接受新的feature了，这是在virtio_finalize_features -> add_status(VIRTIO_VIRTIO_CONFIG_S_FEATURES_OK) */
/* 6. 重新读取设备的feature状态位，确保设置了FEATURES_OK，这也是 virtio_finalize_features 中完成的 */
/* 7. 执行设备相关的初始化操作 */
/* 8. 设置DRIVER_OK标志位， 在具体的设备probe函数中，调用virtio_device_ready完成的, %virtballoon_probe */
static int virtio_dev_probe(struct device *_d) //virtio spec中规定的通用的部分了
{
	int err, i;
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	u64 device_features;
	u64 driver_features;
	u64 driver_features_legacy;

	/* We have a driver! */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	/* Figure out what features the device supports. */
	device_features = dev->config->get_features(dev);

	/* Figure out what features the driver supports. */
	driver_features = 0;
	for (i = 0; i < drv->feature_table_size; i++) {
		unsigned int f = drv->feature_table[i];
		BUG_ON(f >= 64);
		driver_features |= (1ULL << f);
	}

	/* Some drivers have a separate feature table for virtio v1.0 */
	if (drv->feature_table_legacy) {
		driver_features_legacy = 0;
		for (i = 0; i < drv->feature_table_size_legacy; i++) {
			unsigned int f = drv->feature_table_legacy[i];
			BUG_ON(f >= 64);
			driver_features_legacy |= (1ULL << f);
		}
	} else {
		driver_features_legacy = driver_features;
	}

	if (device_features & (1ULL << VIRTIO_F_VERSION_1))
		dev->features = driver_features & device_features;
	else
		dev->features = driver_features_legacy & device_features;

	/* Transport features always preserved to pass to finalize_features. */
	for (i = VIRTIO_TRANSPORT_F_START; i < VIRTIO_TRANSPORT_F_END; i++)
		if (device_features & (1ULL << i))
			__virtio_set_bit(dev, i);

	if (drv->validate) {
		err = drv->validate(dev);
		if (err)
			goto err;
	}

	err = virtio_finalize_features(dev);
	if (err)
		goto err;

	err = drv->probe(dev);
	if (err)
		goto err;

	/* If probe didn't do it, mark device DRIVER_OK ourselves. */
	if (!(dev->config->get_status(dev) & VIRTIO_CONFIG_S_DRIVER_OK))
		virtio_device_ready(dev);

	if (drv->scan) //有些设备米有scan函数
		drv->scan(dev);

	virtio_config_enable(dev);

	return 0;
err:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;

}

static int virtio_dev_remove(struct device *_d)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	virtio_config_disable(dev);

	drv->remove(dev);

	/* Driver should have reset device. */
	WARN_ON_ONCE(dev->config->get_status(dev));

	/* Acknowledge the device's existence again. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	return 0;
}

static struct bus_type virtio_bus = {
	.name  = "virtio",
	.match = virtio_dev_match,
	.dev_groups = virtio_dev_groups,
	.uevent = virtio_uevent,
	.probe = virtio_dev_probe,
	.remove = virtio_dev_remove,
};

int register_virtio_driver(struct virtio_driver *driver)
{
	/* Catch this early. */
	BUG_ON(driver->feature_table_size && !driver->feature_table);
	driver->driver.bus = &virtio_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(register_virtio_driver);

void unregister_virtio_driver(struct virtio_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(unregister_virtio_driver);

/**
 * register_virtio_device - register virtio device
 * @dev        : virtio device to be registered
 *
 * On error, the caller must call put_device on &@dev->dev (and not kfree),
 * as another code path may have obtained a reference to @dev.
 *
 * Returns: 0 on suceess, -error on failure
 */
//将设备信息注册到virtio机制中
//这部分与pci已经完全无关了，需要的信息已经通过virtio_pci_modern_probe, 从pci机制流动到了virtio机制了
//这里是纯粹的virtio bus，virtio设备的操作了
//1. 设置bus
//2. 做bus probe，driver probe
//	1. bus probe中做设备match，设置好设备驱动
//	2. 然后进入driver probe做具体设备的probe
//3. 一些virtio spec要求的操作
//注：注意virtio pci代理设备，virtio bus，virtio设备的1:1:1关系
int register_virtio_device(struct virtio_device *dev)
{
	int err;

	dev->dev.bus = &virtio_bus; //设置设备的bus，即该virtio设备是依赖于这个类型的bus的, 所有该类型的bus都共享一个结构的
	device_initialize(&dev->dev);

	/* Assign a unique device index and hence name. */
	err = ida_simple_get(&virtio_index_ida, 0, 0, GFP_KERNEL);
	if (err < 0)
		goto out;

	dev->index = err;
	dev_set_name(&dev->dev, "virtio%u", dev->index); //设置设备名字，virito1这种模式

	spin_lock_init(&dev->config_lock);
	dev->config_enabled = false;
	dev->config_change_pending = false;

	/* We always start by resetting the device, in case a previous
	 * driver messed it up.  This also tests that code path a little. */
	dev->config->reset(dev); //调用重置设备的回调函数, refer to virtio_pci_modern_probe 

	/* Acknowledge that we've seen the device. */
	//参考virtio spec
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE); // virtio_pci_config_ops

	INIT_LIST_HEAD(&dev->vqs);

	/*
	 * device_add() causes the bus infrastructure to look for a matching
	 * driver.
	 */
	//极其重要, virtio bus probe, virtio device probe
	err = device_add(&dev->dev); //这里通过bus_probe_device 进入到具体的virtio bus 和设备的probe函数: bus_probe: %virtio_dev_probe, drv probe: %virtballoon_probe, 注意：virtiobus和设备是1:1的
	//  device_add -> bus_probe_device -> device_initial_probe -> __device_attach -> __device_attach_driver -> driver_probe_device -> really_probe -> bus_probe / driver_probe
	if (err)
		ida_simple_remove(&virtio_index_ida, dev->index);
out:
	if (err)
		virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;
}
EXPORT_SYMBOL_GPL(register_virtio_device);

void unregister_virtio_device(struct virtio_device *dev)
{
	int index = dev->index; /* save for after device release */

	device_unregister(&dev->dev);
	ida_simple_remove(&virtio_index_ida, index);
}
EXPORT_SYMBOL_GPL(unregister_virtio_device);

#ifdef CONFIG_PM_SLEEP
int virtio_device_freeze(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	virtio_config_disable(dev);

	dev->failed = dev->config->get_status(dev) & VIRTIO_CONFIG_S_FAILED;

	if (drv && drv->freeze)
		return drv->freeze(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_device_freeze);

int virtio_device_restore(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	int ret;

	/* We always start by resetting the device, in case a previous
	 * driver messed it up. */
	dev->config->reset(dev);

	/* Acknowledge that we've seen the device. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	/* Maybe driver failed before freeze.
	 * Restore the failed status, for debugging. */
	if (dev->failed)
		virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);

	if (!drv)
		return 0;

	/* We have a driver! */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	ret = virtio_finalize_features(dev);
	if (ret)
		goto err;

	if (drv->restore) {
		ret = drv->restore(dev);
		if (ret)
			goto err;
	}

	/* Finally, tell the device we're all set */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER_OK);

	virtio_config_enable(dev);

	return 0;

err:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_device_restore);
#endif

static int virtio_init(void)
{
	if (bus_register(&virtio_bus) != 0)
		panic("virtio bus registration failed");
	return 0;
}

static void __exit virtio_exit(void)
{
	bus_unregister(&virtio_bus);
	ida_destroy(&virtio_index_ida);
}
core_initcall(virtio_init);
module_exit(virtio_exit);

MODULE_LICENSE("GPL");
