/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H
/* Everything a virtio driver needs to work with any particular virtio
 * implementation. */
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/gfp.h>
#include <linux/vringh.h>

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 * @index: the zero-based ordinal number for this queue.
 * @num_free: number of elements we expect to be able to fit.
 *
 * A note on @num_free: with indirect buffers, each buffer needs one
 * element in the queue, otherwise a buffer will need one element per
 * sg element.
 */

//仅仅是driver使用的一个更高level的vring管理结构（这个名字与spec中的virtqueue不是一个概念）
//在virtio_vring.c中还有一个更高level的结构，继承了virtqueue和vring结构，同时将这两个结构一一对应起来了。
//通过container_of可以互相索引了
//在driver侧管理都是直接使用vring_virtqueue结构的，virtqueue和vring结构都是其成员之一
struct virtqueue {
	struct list_head list;
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
	unsigned int index;
	unsigned int num_free;
	void *priv; //会保存vq notify 位置的偏移, 需要通知设备的时候，直接写这个成员就可以了, refer to %setup_vq vp_notify
};

int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 gfp_t gfp);

int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			gfp_t gfp);

int virtqueue_add_inbuf_ctx(struct virtqueue *vq,
			    struct scatterlist sg[], unsigned int num,
			    void *data,
			    void *ctx,
			    gfp_t gfp);

int virtqueue_add_sgs(struct virtqueue *vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      gfp_t gfp);

bool virtqueue_kick(struct virtqueue *vq);

bool virtqueue_kick_prepare(struct virtqueue *vq);

bool virtqueue_notify(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void *virtqueue_get_buf_ctx(struct virtqueue *vq, unsigned int *len,
			    void **ctx);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

unsigned virtqueue_enable_cb_prepare(struct virtqueue *vq);

bool virtqueue_poll(struct virtqueue *vq, unsigned);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(struct virtqueue *vq);

bool virtqueue_is_broken(struct virtqueue *vq);

const struct vring *virtqueue_get_vring(struct virtqueue *vq);
dma_addr_t virtqueue_get_desc_addr(struct virtqueue *vq);
dma_addr_t virtqueue_get_avail_addr(struct virtqueue *vq);
dma_addr_t virtqueue_get_used_addr(struct virtqueue *vq);

/*
 * Legacy accessors -- in almost all cases, these are the wrong functions
 * to use.
 */
static inline void *virtqueue_get_desc(struct virtqueue *vq)
{
	return virtqueue_get_vring(vq)->desc;
}
static inline void *virtqueue_get_avail(struct virtqueue *vq)
{
	return virtqueue_get_vring(vq)->avail;
}
static inline void *virtqueue_get_used(struct virtqueue *vq)
{
	return virtqueue_get_vring(vq)->used;
}

/**
 * virtio_device - representation of a device using virtio
 * @index: unique position on the virtio bus
 * @failed: saved value for VIRTIO_CONFIG_S_FAILED bit (for restore)
 * @config_enabled: configuration change reporting enabled
 * @config_change_pending: configuration change reported while disabled
 * @config_lock: protects configuration change reporting
 * @dev: underlying device.
 * @id: the device type identification (used to match it with a driver).
 * @config: the configuration ops for this device.
 * @vringh_config: configuration ops for host vrings.
 * @vqs: the list of virtqueues for this device.
 * @features: the features supported by both driver and device.
 * @priv: private pointer for the driver's use.
 */
//这些结构是对接Linux内部设备驱动模型的结构了，都是从device结构继承下来的
//后文的driver结构也是类似了。
//当然要实现driver需要的probe等函数了
struct virtio_device {
	int index;
	bool failed;
	bool config_enabled;
	bool config_change_pending;
	spinlock_t config_lock;
	struct device dev;
	struct virtio_device_id id;
	const struct virtio_config_ops *config; //这个设备配置空间的操作函数
	const struct vringh_config_ops *vringh_config;
	struct list_head vqs;
	u64 features;
	void *priv;
};

static inline struct virtio_device *dev_to_virtio(struct device *_dev)
{
	return container_of(_dev, struct virtio_device, dev);
}

void virtio_add_status(struct virtio_device *dev, unsigned int status);
int register_virtio_device(struct virtio_device *dev);
void unregister_virtio_device(struct virtio_device *dev);

void virtio_break_device(struct virtio_device *dev);

void virtio_config_changed(struct virtio_device *dev);
void virtio_config_disable(struct virtio_device *dev);
void virtio_config_enable(struct virtio_device *dev);
int virtio_finalize_features(struct virtio_device *dev);
#ifdef CONFIG_PM_SLEEP
int virtio_device_freeze(struct virtio_device *dev);
int virtio_device_restore(struct virtio_device *dev);
#endif

#define virtio_device_for_each_vq(vdev, vq) \
	list_for_each_entry(vq, &vdev->vqs, list)

/**
 * virtio_driver - operations for a virtio I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @feature_table: an array of feature numbers supported by this driver.
 * @feature_table_size: number of entries in the feature table array.
 * @feature_table_legacy: same as feature_table but when working in legacy mode.
 * @feature_table_size_legacy: number of entries in feature table legacy array.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @scan: optional function to call after successful probe; intended
 *    for virtio-scsi to invoke a scan.
 * @remove: the function to call when a device is removed.
 * @config_changed: optional function to call when the device configuration
 *    changes; may be called in interrupt context.
 * @freeze: optional function to call during suspend/hibernation.
 * @restore: optional function to call on resume.
 */
struct virtio_driver {
	struct device_driver driver;
	const struct virtio_device_id *id_table;
	const unsigned int *feature_table;
	unsigned int feature_table_size;
	const unsigned int *feature_table_legacy;
	unsigned int feature_table_size_legacy;
	int (*validate)(struct virtio_device *dev);
	int (*probe)(struct virtio_device *dev); //实现多态的。在CPP中是，多态是什么？当需要一个父类的指针或者引用的时候，可以直接传递子类的指针或引用。通过这个指针或引用调用虚函数的时候，会自动匹配其真实类型的虚函数。
							//在cpp中是通过虚函数表实现的。在这里，如果我们有一个virtio_driver的结构，传指针或引用后。我们可以将这个指针看成device_driver类型，那么调用probe函数就会调用到父类的probe函数
							//将其看作virtio_driver类型，那么就会调用到子类的probe函数。（与CPP还是不一样？？？？？？）
							//上面描述有问题，这里不能理解为多态。理解成多层次的构造反而更恰当了，driver与device匹配后，先调用父类的probe，然后调用子类的probe。
							//类似的remove函数，则类似于多层次的析构函数。
							//果然如此，我们对比device_driver结构就知道，只有这两个函数是重复的。
							//多态还是通过函数指针实现的
	void (*scan)(struct virtio_device *dev);
	void (*remove)(struct virtio_device *dev);
	void (*config_changed)(struct virtio_device *dev);
#ifdef CONFIG_PM
	int (*freeze)(struct virtio_device *dev);
	int (*restore)(struct virtio_device *dev);
#endif
};

static inline struct virtio_driver *drv_to_virtio(struct device_driver *drv)
{
	return container_of(drv, struct virtio_driver, driver);
}

int register_virtio_driver(struct virtio_driver *drv);
void unregister_virtio_driver(struct virtio_driver *drv);

/* module_virtio_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_virtio_driver(__virtio_driver) \
	module_driver(__virtio_driver, register_virtio_driver, \
			unregister_virtio_driver)
#endif /* _LINUX_VIRTIO_H */
