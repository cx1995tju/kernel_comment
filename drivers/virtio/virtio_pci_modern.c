/*
 * Virtio PCI driver - modern (virtio 1.0) device support
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

#include <linux/delay.h>
#define VIRTIO_PCI_NO_LEGACY
#include "virtio_pci_common.h"

/*
 * Type-safe wrappers for io accesses.
 * Use these to enforce at compile time the following spec requirement:
 *
 * The driver MUST access each field using the “natural” access
 * method, i.e. 32-bit accesses for 32-bit fields, 16-bit accesses
 * for 16-bit fields and 8-bit accesses for 8-bit fields.
 */
static inline u8 vp_ioread8(u8 __iomem *addr)
{
	return ioread8(addr);
}
static inline u16 vp_ioread16 (__le16 __iomem *addr)
{
	return ioread16(addr);
}

static inline u32 vp_ioread32(__le32 __iomem *addr)
{
	return ioread32(addr);
}

static inline void vp_iowrite8(u8 value, u8 __iomem *addr)
{
	iowrite8(value, addr);
}

static inline void vp_iowrite16(u16 value, __le16 __iomem *addr)
{
	iowrite16(value, addr);
}

static inline void vp_iowrite32(u32 value, __le32 __iomem *addr)
{
	iowrite32(value, addr);
}

static void vp_iowrite64_twopart(u64 val,
				 __le32 __iomem *lo, __le32 __iomem *hi)
{
	vp_iowrite32((u32)val, lo);
	vp_iowrite32(val >> 32, hi);
}

static void __iomem *map_capability(struct pci_dev *dev, int off,
				    size_t minlen,
				    u32 align,
				    u32 start, u32 size,
				    size_t *len)
{
	u8 bar;
	u32 offset, length;
	void __iomem *p;

	pci_read_config_byte(dev, off + offsetof(struct virtio_pci_cap,
						 bar),
			     &bar);
	pci_read_config_dword(dev, off + offsetof(struct virtio_pci_cap, offset),
			     &offset);
	pci_read_config_dword(dev, off + offsetof(struct virtio_pci_cap, length),
			      &length);

	if (length <= start) {
		dev_err(&dev->dev,
			"virtio_pci: bad capability len %u (>%u expected)\n",
			length, start);
		return NULL;
	}

	if (length - start < minlen) {
		dev_err(&dev->dev,
			"virtio_pci: bad capability len %u (>=%zu expected)\n",
			length, minlen);
		return NULL;
	}

	length -= start;

	if (start + offset < offset) {
		dev_err(&dev->dev,
			"virtio_pci: map wrap-around %u+%u\n",
			start, offset);
		return NULL;
	}

	offset += start;

	if (offset & (align - 1)) {
		dev_err(&dev->dev,
			"virtio_pci: offset %u not aligned to %u\n",
			offset, align);
		return NULL;
	}

	if (length > size)
		length = size;

	if (len)
		*len = length;

	if (minlen + offset < minlen ||
	    minlen + offset > pci_resource_len(dev, bar)) {
		dev_err(&dev->dev,
			"virtio_pci: map virtio %zu@%u "
			"out of range on bar %i length %lu\n",
			minlen, offset,
			bar, (unsigned long)pci_resource_len(dev, bar));
		return NULL;
	}

	p = pci_iomap_range(dev, bar, offset, length);
	if (!p)
		dev_err(&dev->dev,
			"virtio_pci: unable to map virtio %u@%u on bar %i\n",
			length, offset, bar);
	return p;
}

/* virtio config->get_features() implementation */
static u64 vp_get_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u64 features;

	vp_iowrite32(0, &vp_dev->common->device_feature_select);
	features = vp_ioread32(&vp_dev->common->device_feature);
	vp_iowrite32(1, &vp_dev->common->device_feature_select);
	features |= ((u64)vp_ioread32(&vp_dev->common->device_feature) << 32);

	return features;
}

static void vp_transport_features(struct virtio_device *vdev, u64 features)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct pci_dev *pci_dev = vp_dev->pci_dev;

	if ((features & BIT_ULL(VIRTIO_F_SR_IOV)) &&
			pci_find_ext_capability(pci_dev, PCI_EXT_CAP_ID_SRIOV))
		__virtio_set_bit(vdev, VIRTIO_F_SR_IOV);
}

/* virtio config->finalize_features() implementation */
static int vp_finalize_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u64 features = vdev->features;

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* Give virtio_pci a chance to accept features. */
	vp_transport_features(vdev, features);

	if (!__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "virtio: device uses modern interface "
			"but does not have VIRTIO_F_VERSION_1\n");
		return -EINVAL;
	}

	vp_iowrite32(0, &vp_dev->common->guest_feature_select);
	vp_iowrite32((u32)vdev->features, &vp_dev->common->guest_feature);
	vp_iowrite32(1, &vp_dev->common->guest_feature_select);
	vp_iowrite32(vdev->features >> 32, &vp_dev->common->guest_feature);

	return 0;
}

/* virtio config->get() implementation */
static void vp_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u8 b;
	__le16 w;
	__le32 l;

	BUG_ON(offset + len > vp_dev->device_len);

	switch (len) {
	case 1:
		b = ioread8(vp_dev->device + offset);
		memcpy(buf, &b, sizeof b);
		break;
	case 2:
		w = cpu_to_le16(ioread16(vp_dev->device + offset));
		memcpy(buf, &w, sizeof w);
		break;
	case 4:
		l = cpu_to_le32(ioread32(vp_dev->device + offset));
		memcpy(buf, &l, sizeof l);
		break;
	case 8:
		l = cpu_to_le32(ioread32(vp_dev->device + offset));
		memcpy(buf, &l, sizeof l);
		l = cpu_to_le32(ioread32(vp_dev->device + offset + sizeof l));
		memcpy(buf + sizeof l, &l, sizeof l);
		break;
	default:
		BUG();
	}
}

/* the config->set() implementation.  it's symmetric to the config->get()
 * implementation */
static void vp_set(struct virtio_device *vdev, unsigned offset,
		   const void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u8 b;
	__le16 w;
	__le32 l;

	BUG_ON(offset + len > vp_dev->device_len);

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof b);
		iowrite8(b, vp_dev->device + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof w);
		iowrite16(le16_to_cpu(w), vp_dev->device + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof l);
		iowrite32(le32_to_cpu(l), vp_dev->device + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof l);
		iowrite32(le32_to_cpu(l), vp_dev->device + offset);
		memcpy(&l, buf + sizeof l, sizeof l);
		iowrite32(le32_to_cpu(l), vp_dev->device + offset + sizeof l);
		break;
	default:
		BUG();
	}
}

static u32 vp_generation(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	return vp_ioread8(&vp_dev->common->config_generation);
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	return vp_ioread8(&vp_dev->common->device_status); //直接读取virtio spec规定的device status位置
}

static void vp_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* We should never be setting status to 0. */
	BUG_ON(status == 0);
	vp_iowrite8(status, &vp_dev->common->device_status);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* 0 status means a reset. */
	vp_iowrite8(0, &vp_dev->common->device_status);
	/* After writing 0 to device_status, the driver MUST wait for a read of
	 * device_status to return 0 before reinitializing the device.
	 * This will flush out the status write, and flush in device writes,
	 * including MSI-X interrupts, if any.
	 */
	while (vp_ioread8(&vp_dev->common->device_status))
		msleep(1);
	/* Flush pending VQ/configuration callbacks. */
	vp_synchronize_vectors(vdev);
}

static u16 vp_config_vector(struct virtio_pci_device *vp_dev, u16 vector)
{
	/* Setup the vector used for configuration events */
	vp_iowrite16(vector, &vp_dev->common->msix_config);
	/* Verify we had enough resources to assign the vector */
	/* Will also flush the write out to device */
	return vp_ioread16(&vp_dev->common->msix_config);
}

/* virtio_pci_modern.c: setup_vq 初始化一个virt queue的步骤 */
/* 	1. 将传入的queue的index，与从设备中读取的queue的数据比较 */ 
/* 	2. 选择要配置的queue，即queue的index写入到config空间的queue_select, 写该位置的时候，会exit到qemu，qemu会在后端做对应的操作 */
/* 	3. 读取队列大小, 队列大小不能是0，必须是power of 2，而且队列不能是enable状态 */
/* 	4. 读取queue_notify_off寄存器的值，这个值表示virito驱动在通知virtio设备后端的时候应该写的地址在notify_base的偏移, QEMU只是简单的以队列的索引返回，素以进行通知的时候，需要队列index * notify_offset_multiplier */
/* 	5. 调用alloc_virtqueue_pages分配virtqueue需要的page，用于保存vring的desc table，available ring，used ring, 这3部分是保存在连续的物理地址空间的，info->queue保存了分配空间的虚拟地址 */
/* 	6. call __vring_new_virtqueue 创建一个vring_virtqueue结构 */
/* 	7. 激活队列，将desc table，avail ring，used ring等信息写入到对应的寄存器 */
/* 	8. 设置virtqueu 的priv成员为notify地址 */
// 这个函数会将前端分配的物理内存告诉后端的
static struct virtqueue *setup_vq(struct virtio_pci_device *vp_dev,
				  struct virtio_pci_vq_info *info,
				  unsigned index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name,
				  bool ctx,
				  u16 msix_vec)
{
	struct virtio_pci_common_cfg __iomem *cfg = vp_dev->common;
	struct virtqueue *vq;
	u16 num, off;
	int err;

	if (index >= vp_ioread16(&cfg->num_queues))
		return ERR_PTR(-ENOENT);

	/* Select the queue we're interested in */
	vp_iowrite16(index, &cfg->queue_select);

	/* Check if queue is either not available or already active. */
	num = vp_ioread16(&cfg->queue_size); //设备提供的queue size
	if (!num || vp_ioread16(&cfg->queue_enable))
		return ERR_PTR(-ENOENT);

	if (num & (num - 1)) {
		dev_warn(&vp_dev->pci_dev->dev, "bad queue size %u", num);
		return ERR_PTR(-EINVAL);
	}

	/* get offset of notification word for this vq */
	off = vp_ioread16(&cfg->queue_notify_off);

	info->msix_vector = msix_vec;

	/* create the vring */
	vq = vring_create_virtqueue(index, num,
				    SMP_CACHE_BYTES, &vp_dev->vdev,
				    true, true, ctx,
				    vp_notify, callback, name);
	if (!vq)
		return ERR_PTR(-ENOMEM);

	/* activate the queue 激活队列，将相关信息写入到寄存器中, 这样设备就能访问driver分配的队列了 */
	vp_iowrite16(virtqueue_get_vring_size(vq), &cfg->queue_size);
	vp_iowrite64_twopart(virtqueue_get_desc_addr(vq),
			     &cfg->queue_desc_lo, &cfg->queue_desc_hi);
	vp_iowrite64_twopart(virtqueue_get_avail_addr(vq),
			     &cfg->queue_avail_lo, &cfg->queue_avail_hi);
	vp_iowrite64_twopart(virtqueue_get_used_addr(vq),
			     &cfg->queue_used_lo, &cfg->queue_used_hi);

	if (vp_dev->notify_base) {
		/* offset should not wrap */
		if ((u64)off * vp_dev->notify_offset_multiplier + 2
		    > vp_dev->notify_len) {
			dev_warn(&vp_dev->pci_dev->dev,
				 "bad notification offset %u (x %u) "
				 "for queue %u > %zd",
				 off, vp_dev->notify_offset_multiplier,
				 index, vp_dev->notify_len);
			err = -EINVAL;
			goto err_map_notify;
		}
		vq->priv = (void __force *)vp_dev->notify_base +
			off * vp_dev->notify_offset_multiplier;
	} else {
		vq->priv = (void __force *)map_capability(vp_dev->pci_dev,
					  vp_dev->notify_map_cap, 2, 2,
					  off * vp_dev->notify_offset_multiplier, 2,
					  NULL);
	} //保存notify的偏移信息，这样子在通知notify的时候，直接谢vq->priv成员就可以了 refer to %vp_notify

	if (!vq->priv) {
		err = -ENOMEM;
		goto err_map_notify;
	}

	if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
		vp_iowrite16(msix_vec, &cfg->queue_msix_vector);
		msix_vec = vp_ioread16(&cfg->queue_msix_vector);
		if (msix_vec == VIRTIO_MSI_NO_VECTOR) {
			err = -EBUSY;
			goto err_assign_vector;
		}
	}

	return vq;

err_assign_vector:
	if (!vp_dev->notify_base)
		pci_iounmap(vp_dev->pci_dev, (void __iomem __force *)vq->priv);
err_map_notify:
	vring_del_virtqueue(vq);
	return ERR_PTR(err);
}

static int vp_modern_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			      struct virtqueue *vqs[],
			      vq_callback_t *callbacks[],
			      const char * const names[], const bool *ctx,
			      struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq;
	int rc = vp_find_vqs(vdev, nvqs, vqs, callbacks, names, ctx, desc); //使用同样的参数调用该函数

	if (rc)
		return rc;

	/* Select and activate all queues. Has to be done last: once we do
	 * this, there's no way to go back except reset.
	 */
	list_for_each_entry(vq, &vdev->vqs, list) {
		vp_iowrite16(vq->index, &vp_dev->common->queue_select);
		vp_iowrite16(1, &vp_dev->common->queue_enable);
	}

	return 0;
}

static void del_vq(struct virtio_pci_vq_info *info)
{
	struct virtqueue *vq = info->vq;
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);

	vp_iowrite16(vq->index, &vp_dev->common->queue_select);

	if (vp_dev->msix_enabled) {
		vp_iowrite16(VIRTIO_MSI_NO_VECTOR,
			     &vp_dev->common->queue_msix_vector);
		/* Flush the write out to device */
		vp_ioread16(&vp_dev->common->queue_msix_vector);
	}

	if (!vp_dev->notify_base)
		pci_iounmap(vp_dev->pci_dev, (void __force __iomem *)vq->priv);

	vring_del_virtqueue(vq);
}

static const struct virtio_config_ops virtio_pci_config_nodev_ops = {
	.get		= NULL,
	.set		= NULL,
	.generation	= vp_generation,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_modern_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
	.get_vq_affinity = vp_get_vq_affinity,
};

static const struct virtio_config_ops virtio_pci_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.generation	= vp_generation,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_modern_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
	.get_vq_affinity = vp_get_vq_affinity,
};

/**
 * virtio_pci_find_capability - walk capabilities to find device info.
 * @dev: the pci device
 * @cfg_type: the VIRTIO_PCI_CAP_* value we seek
 * @ioresource_types: IORESOURCE_MEM and/or IORESOURCE_IO.
 *
 * Returns offset of the capability, or 0.
 */
//遍历pci配置空间，看看pci设备有哪些capabilities呀, 主要是用来找几种配置空间
static inline int virtio_pci_find_capability(struct pci_dev *dev, u8 cfg_type,
					     u32 ioresource_types, int *bars)
{
	int pos;

	for (pos = pci_find_capability(dev, PCI_CAP_ID_VNDR);
	     pos > 0;
	     pos = pci_find_next_capability(dev, pos, PCI_CAP_ID_VNDR)) {
		u8 type, bar;
		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 cfg_type),
				     &type);
		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 bar),
				     &bar);

		/* Ignore structures with reserved BAR values */
		if (bar > 0x5)
			continue;

		if (type == cfg_type) {
			if (pci_resource_len(dev, bar) &&
			    pci_resource_flags(dev, bar) & ioresource_types) {
				*bars |= (1 << bar);
				return pos;
			}
		}
	}
	return 0;
}

/* This is part of the ABI.  Don't screw with it. */
static inline void check_offsets(void)
{
	/* Note: disk space was harmed in compilation of this function. */
	BUILD_BUG_ON(VIRTIO_PCI_CAP_VNDR !=
		     offsetof(struct virtio_pci_cap, cap_vndr));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_NEXT !=
		     offsetof(struct virtio_pci_cap, cap_next));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_LEN !=
		     offsetof(struct virtio_pci_cap, cap_len));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_CFG_TYPE !=
		     offsetof(struct virtio_pci_cap, cfg_type));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_BAR !=
		     offsetof(struct virtio_pci_cap, bar));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_OFFSET !=
		     offsetof(struct virtio_pci_cap, offset));
	BUILD_BUG_ON(VIRTIO_PCI_CAP_LENGTH !=
		     offsetof(struct virtio_pci_cap, length));
	BUILD_BUG_ON(VIRTIO_PCI_NOTIFY_CAP_MULT !=
		     offsetof(struct virtio_pci_notify_cap,
			      notify_off_multiplier));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_DFSELECT !=
		     offsetof(struct virtio_pci_common_cfg,
			      device_feature_select));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_DF !=
		     offsetof(struct virtio_pci_common_cfg, device_feature));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_GFSELECT !=
		     offsetof(struct virtio_pci_common_cfg,
			      guest_feature_select));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_GF !=
		     offsetof(struct virtio_pci_common_cfg, guest_feature));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_MSIX !=
		     offsetof(struct virtio_pci_common_cfg, msix_config));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_NUMQ !=
		     offsetof(struct virtio_pci_common_cfg, num_queues));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_STATUS !=
		     offsetof(struct virtio_pci_common_cfg, device_status));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_CFGGENERATION !=
		     offsetof(struct virtio_pci_common_cfg, config_generation));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_SELECT !=
		     offsetof(struct virtio_pci_common_cfg, queue_select));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_SIZE !=
		     offsetof(struct virtio_pci_common_cfg, queue_size));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_MSIX !=
		     offsetof(struct virtio_pci_common_cfg, queue_msix_vector));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_ENABLE !=
		     offsetof(struct virtio_pci_common_cfg, queue_enable));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_NOFF !=
		     offsetof(struct virtio_pci_common_cfg, queue_notify_off));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_DESCLO !=
		     offsetof(struct virtio_pci_common_cfg, queue_desc_lo));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_DESCHI !=
		     offsetof(struct virtio_pci_common_cfg, queue_desc_hi));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_AVAILLO !=
		     offsetof(struct virtio_pci_common_cfg, queue_avail_lo));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_AVAILHI !=
		     offsetof(struct virtio_pci_common_cfg, queue_avail_hi));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_USEDLO !=
		     offsetof(struct virtio_pci_common_cfg, queue_used_lo));
	BUILD_BUG_ON(VIRTIO_PCI_COMMON_Q_USEDHI !=
		     offsetof(struct virtio_pci_common_cfg, queue_used_hi));
}

/* the PCI probing function */
//从pci机制中获取各种信息保存到virtio机制中，是信息从pci机制流动到virtio机制的关键函数
//核心结构：pci代理设备pci_dev, virtio设备vp_dev
/* 1. 基于pci机制，从对应的pci代理设备读取一些信息，并映射好 %map_capability, 映射好的信息当然要保存到vp_dev中 */
/* 2. 设置vp_dev中的一些回调函数 */
int virtio_pci_modern_probe(struct virtio_pci_device *vp_dev) //virtio 设备
{
	struct pci_dev *pci_dev = vp_dev->pci_dev; //对应的virtio代理设备
	int err, common, isr, notify, device;
	u32 notify_length;
	u32 notify_offset;

	check_offsets();

	/* We only own devices >= 0x1000 and <= 0x107f: leave the rest. */
	if (pci_dev->device < 0x1000 || pci_dev->device > 0x107f)
		return -ENODEV;

	if (pci_dev->device < 0x1040) {
		/* Transitional devices: use the PCI subsystem device id as
		 * virtio device id, same as legacy driver always did.
		 */
		vp_dev->vdev.id.device = pci_dev->subsystem_device;
	} else {
		/* Modern devices: simply use PCI device id, but start from 0x1040. */
		vp_dev->vdev.id.device = pci_dev->device - 0x1040;
	}
	vp_dev->vdev.id.vendor = pci_dev->subsystem_vendor; //保存pci设备的vendor id等信息到virtio机制内部的device结构,这些信息从pci机制流向到virtio机制的

	/* check for a common config: if not, use legacy mode (bar 0). */
	common = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_COMMON_CFG, //通过pci capability，来寻找common cfg空间
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &vp_dev->modern_bars);
	if (!common) {
		dev_info(&pci_dev->dev,
			 "virtio_pci: leaving for legacy driver\n");
		return -ENODEV;
	}

	/* If common is there, these should be too... */
	isr = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_ISR_CFG,
					 IORESOURCE_IO | IORESOURCE_MEM,
					 &vp_dev->modern_bars);
	notify = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_NOTIFY_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &vp_dev->modern_bars);
	if (!isr || !notify) {
		dev_err(&pci_dev->dev,
			"virtio_pci: missing capabilities %i/%i/%i\n",
			common, isr, notify);
		return -EINVAL;
	}

	err = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pci_dev->dev,
						DMA_BIT_MASK(32));
	if (err)
		dev_warn(&pci_dev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");

	/* Device capability is only mandatory for devices that have
	 * device-specific configuration.
	 */
	device = virtio_pci_find_capability(pci_dev, VIRTIO_PCI_CAP_DEVICE_CFG,
					    IORESOURCE_IO | IORESOURCE_MEM,
					    &vp_dev->modern_bars);

	err = pci_request_selected_regions(pci_dev, vp_dev->modern_bars,
					   "virtio-pci-modern");
	if (err)
		return err;

	err = -EINVAL;
	//重要, 映射各个bar空间，并保存到vp_dev中
	vp_dev->common = map_capability(pci_dev, common, //将bar空间映射到内核虚拟地址空间, 这里就将设备的virtio_pci_common_cfg这个bar空间映射过来了，那么后续直接通过内存读写操作访问.而物理内存的分配则是与bios相关，或者pci host bridge。
					sizeof(struct virtio_pci_common_cfg), 4,
					0, sizeof(struct virtio_pci_common_cfg),
					NULL);
	if (!vp_dev->common)
		goto err_map_common;
	vp_dev->isr = map_capability(pci_dev, isr, sizeof(u8), 1,
				     0, 1,
				     NULL);
	if (!vp_dev->isr)
		goto err_map_isr;

	//同上，读取一些配置信息并保存
	/* Read notify_off_multiplier from config space. */
	pci_read_config_dword(pci_dev,
			      notify + offsetof(struct virtio_pci_notify_cap,
						notify_off_multiplier),
			      &vp_dev->notify_offset_multiplier);
	/* Read notify length and offset from config space. */
	pci_read_config_dword(pci_dev,
			      notify + offsetof(struct virtio_pci_notify_cap,
						cap.length),
			      &notify_length);

	pci_read_config_dword(pci_dev,
			      notify + offsetof(struct virtio_pci_notify_cap,
						cap.offset),
			      &notify_offset);

	/* We don't know how many VQs we'll map, ahead of the time.
	 * If notify length is small, map it all now.
	 * Otherwise, map each VQ individually later.
	 */
	if ((u64)notify_length + (notify_offset % PAGE_SIZE) <= PAGE_SIZE) {
		vp_dev->notify_base = map_capability(pci_dev, notify, 2, 2,
						     0, notify_length,
						     &vp_dev->notify_len);
		if (!vp_dev->notify_base)
			goto err_map_notify;
	} else {
		vp_dev->notify_map_cap = notify;
	}

	/* Again, we don't know how much we should map, but PAGE_SIZE
	 * is more than enough for all existing devices.
	 */
	if (device) {
		vp_dev->device = map_capability(pci_dev, device, 0, 4,
						0, PAGE_SIZE,
						&vp_dev->device_len);
		if (!vp_dev->device)
			goto err_map_device;

		vp_dev->vdev.config = &virtio_pci_config_ops; //设置vdev的config成员
	} else {
		vp_dev->vdev.config = &virtio_pci_config_nodev_ops;
	}

	//同样是设置呀，设置好一些重要的回调函数
	vp_dev->config_vector = vp_config_vector; //继续设置几个回调函数
	vp_dev->setup_vq = setup_vq;
	vp_dev->del_vq = del_vq;

	return 0;

err_map_device:
	if (vp_dev->notify_base)
		pci_iounmap(pci_dev, vp_dev->notify_base);
err_map_notify:
	pci_iounmap(pci_dev, vp_dev->isr);
err_map_isr:
	pci_iounmap(pci_dev, vp_dev->common);
err_map_common:
	return err;
}

void virtio_pci_modern_remove(struct virtio_pci_device *vp_dev)
{
	struct pci_dev *pci_dev = vp_dev->pci_dev;

	if (vp_dev->device)
		pci_iounmap(pci_dev, vp_dev->device);
	if (vp_dev->notify_base)
		pci_iounmap(pci_dev, vp_dev->notify_base);
	pci_iounmap(pci_dev, vp_dev->isr);
	pci_iounmap(pci_dev, vp_dev->common);
	pci_release_selected_regions(pci_dev, vp_dev->modern_bars);
}
