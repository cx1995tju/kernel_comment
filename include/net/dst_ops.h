/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_DST_OPS_H
#define _NET_DST_OPS_H
#include <linux/types.h>
#include <linux/percpu_counter.h>
#include <linux/cache.h>

struct dst_entry;
struct kmem_cachep;
struct net_device;
struct sk_buff;
struct sock;
struct net;

struct dst_ops {
	unsigned short		family;
	unsigned int		gc_thresh; //路由缓存的容量，垃圾回收会使用的，在ip_rt_init中被初始化

	int			(*gc)(struct dst_ops *ops); //垃圾回收函数，当分配的dst_entry实例超过了gc_thresh的时候，dst_alloc()会调用的
	struct dst_entry *	(*check)(struct dst_entry *, __u32 cookie);
	unsigned int		(*default_advmss)(const struct dst_entry *);
	unsigned int		(*mtu)(const struct dst_entry *); /* 从路由项中取出该套接口相关的PMTU */
	u32 *			(*cow_metrics)(struct dst_entry *, unsigned long);
	void			(*destroy)(struct dst_entry *); //析构函数，删除路由缓存项时的清理工作
	void			(*ifdown)(struct dst_entry *,
					  struct net_device *dev, int how); //设备down的时候调用，譬如：ipv4_dst_ifdown函数
	struct dst_entry *	(*negative_advice)(struct dst_entry *); //检测路由缓存项，tcp超时的时候会激活这个接口。譬如：ipv4_negative_advice, 在tcp_write_timeout中被调用
	void			(*link_failure)(struct sk_buff *); //处理目的不可大错误，譬如在ipv4的邻居子系统中，arp没有得到应答的时候会调用的
	void			(*update_pmtu)(struct dst_entry *dst, struct sock *sk,
					       struct sk_buff *skb, u32 mtu); //更新缓存路由的pmtu
	void			(*redirect)(struct dst_entry *dst, struct sock *sk,
					    struct sk_buff *skb);
	int			(*local_out)(struct net *net, struct sock *sk, struct sk_buff *skb);
	struct neighbour *	(*neigh_lookup)(const struct dst_entry *dst,
						struct sk_buff *skb,
						const void *daddr);
	void			(*confirm_neigh)(const struct dst_entry *dst,
						 const void *daddr);

	struct kmem_cache	*kmem_cachep; //对应的分配池

	struct percpu_counter	pcpuc_entries ____cacheline_aligned_in_smp; //这个是entries
};

static inline int dst_entries_get_fast(struct dst_ops *dst)
{
	return percpu_counter_read_positive(&dst->pcpuc_entries);
}

static inline int dst_entries_get_slow(struct dst_ops *dst)
{
	return percpu_counter_sum_positive(&dst->pcpuc_entries);
}

static inline void dst_entries_add(struct dst_ops *dst, int val)
{
	percpu_counter_add(&dst->pcpuc_entries, val);
}

static inline int dst_entries_init(struct dst_ops *dst)
{
	return percpu_counter_init(&dst->pcpuc_entries, 0, GFP_KERNEL);
}

static inline void dst_entries_destroy(struct dst_ops *dst)
{
	percpu_counter_destroy(&dst->pcpuc_entries);
}

#endif
