#ifndef _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
#define _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
/*
 * Virtio PCI driver - APIs for common functionality for all device versions
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>

//表示一个virito queue的信息
//是vq的最顶层结构
struct virtio_pci_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the list node for the virtqueues list */
	struct list_head node;

	/* MSI-X vector (or none) */
	unsigned msix_vector;
};

/* Our device structure */
//这是纯粹的virito机制使用的的设备，是在virito_device基础上扩充, 表示一个virtio pci代理设备
struct virtio_pci_device {
	struct virtio_device vdev; //表示该代理设备对应的virtio设备
	struct pci_dev *pci_dev;

	/* In legacy mode, these two point to within ->legacy. */
	/* Where to read and clear interrupt */
	u8 __iomem *isr;

	/* Modern only fields */
	/* The IO mapping for the PCI config space (non-legacy mode) */
	struct virtio_pci_common_cfg __iomem *common; //会直接映射到设备的bar空间的，那么访问设备就简单了, 直接用一般的内存读写操作, 下述两个成员也都是直接指向bar空间的, 后续读写这些地址的话，会陷入到qemu的
	/* Device-specific data (non-legacy mode)  */
	void __iomem *device;
	/* Base of vq notifications (non-legacy mode). */
	void __iomem *notify_base; //通知设备的时候，在这个位置去写

	/* So we can sanity-check accesses. */
	size_t notify_len;
	size_t device_len;

	/* Capability for when we need to map notifications per-vq. */
	int notify_map_cap;

	/* Multiply queue_notify_off by this value. (non-legacy mode). */
	u32 notify_offset_multiplier;

	int modern_bars;

	/* Legacy only field */
	/* the IO mapping for the PCI config space */
	void __iomem *ioaddr;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* array of all queues for house-keeping */
	struct virtio_pci_vq_info **vqs;

	/* MSI-X support */
	int msix_enabled;
	int intx_enabled;
	cpumask_var_t *msix_affinity_masks;
	/* Name strings for interrupts. This size should be enough,
	 * and I'm too lazy to allocate each name separately. */
	char (*msix_names)[256];
	/* Number of available vectors */
	unsigned msix_vectors;
	/* Vectors allocated, excluding per-vq vectors if any */
	unsigned msix_used_vectors;

	/* Whether we have vector per vq */
	bool per_vq_vectors;

	struct virtqueue *(*setup_vq)(struct virtio_pci_device *vp_dev, //设置virtqeueue
				      struct virtio_pci_vq_info *info,
				      unsigned idx,
				      void (*callback)(struct virtqueue *vq),
				      const char *name,
				      bool ctx,
				      u16 msix_vec);
	void (*del_vq)(struct virtio_pci_vq_info *info); //删除virt queue

	u16 (*config_vector)(struct virtio_pci_device *vp_dev, u16 vector); //与MSI中断相关
};

/* Constants for MSI-X */
/* Use first vector for configuration changes, second and the rest for
 * virtqueues Thus, we need at least 2 vectors for MSI. */
enum {
	VP_MSIX_CONFIG_VECTOR = 0,
	VP_MSIX_VQ_VECTOR = 1,
};

/* Convert a generic virtio device to our structure */
static struct virtio_pci_device *to_vp_device(struct virtio_device *vdev)
{
	return container_of(vdev, struct virtio_pci_device, vdev);
}

/* wait for pending irq handlers */
void vp_synchronize_vectors(struct virtio_device *vdev);
/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq);
/* the config->del_vqs() implementation */
void vp_del_vqs(struct virtio_device *vdev);
/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], const bool *ctx,
		struct irq_affinity *desc);
const char *vp_bus_name(struct virtio_device *vdev);

/* Setup the affinity for a virtqueue:
 * - force the affinity for per vq vector
 * - OR over all affinities for shared MSI
 * - ignore the affinity request if we're using INTX
 */
int vp_set_vq_affinity(struct virtqueue *vq, const struct cpumask *cpu_mask);

const struct cpumask *vp_get_vq_affinity(struct virtio_device *vdev, int index);

#if IS_ENABLED(CONFIG_VIRTIO_PCI_LEGACY)
int virtio_pci_legacy_probe(struct virtio_pci_device *);
void virtio_pci_legacy_remove(struct virtio_pci_device *);
#else//内核看到的virito pci代理设备的，走正常的那套pci机制到这里，需要virito自己去实现virito 设备的probe机制，这里就是这样
static inline int virtio_pci_legacy_probe(struct virtio_pci_device *vp_dev)
{
	return -ENODEV;
}
static inline void virtio_pci_legacy_remove(struct virtio_pci_device *vp_dev)
{
}
#endif
int virtio_pci_modern_probe(struct virtio_pci_device *);
void virtio_pci_modern_remove(struct virtio_pci_device *);

#endif
