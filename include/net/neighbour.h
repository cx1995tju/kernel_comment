/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_NEIGHBOUR_H
#define _NET_NEIGHBOUR_H

#include <linux/neighbour.h>

/*
 *	Generic neighbour manipulation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 * 	Changes:
 *
 *	Harald Welte:		<laforge@gnumonks.org>
 *		- Add neighbour cache statistics like rtstat
 */

#include <linux/atomic.h>
#include <linux/refcount.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/bitmap.h>

#include <linux/err.h>
#include <linux/sysctl.h>
#include <linux/workqueue.h>
#include <net/rtnetlink.h>

/*
 * NUD stands for "neighbor unreachability detection"
 */

#define NUD_IN_TIMER	(NUD_INCOMPLETE|NUD_REACHABLE|NUD_DELAY|NUD_PROBE)
#define NUD_VALID	(NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE|NUD_PROBE|NUD_STALE|NUD_DELAY)
#define NUD_CONNECTED	(NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE)

struct neighbour;

//参考neigh_parms->data
enum {
	NEIGH_VAR_MCAST_PROBES, //多播或广播标识一个邻居项不可达之前的最大尝试次数, 默认为3
	NEIGH_VAR_UCAST_PROBES, //请求ARP守护进程进行单播探测的最大尝试次数，默认为3
	NEIGH_VAR_APP_PROBES, //退回到多播探测之前，通过netlink向用户空间的ARP守护进程最多发送的探测的次数
	NEIGH_VAR_MCAST_REPROBES, 
	NEIGH_VAR_RETRANS_TIME, // 重传一个请求前延迟的jiffies值，默认为1s
	NEIGH_VAR_BASE_REACHABLE_TIME, //邻居项有效期初始值, 每隔300s会随机更新的
	NEIGH_VAR_DELAY_PROBE_TIME,
	NEIGH_VAR_GC_STALETIME, //多久检测一次邻居项过期，如果一个邻居项被确定为过期，则在向它发数据之前会再次确认，默认值是60s。
	NEIGH_VAR_QUEUE_LEN_BYTES,
	NEIGH_VAR_PROXY_QLEN,
	NEIGH_VAR_ANYCAST_DELAY,
	NEIGH_VAR_PROXY_DELAY,
	NEIGH_VAR_LOCKTIME,
#define NEIGH_VAR_DATA_MAX (NEIGH_VAR_LOCKTIME + 1)
	/* Following are used as a second way to access one of the above */
	NEIGH_VAR_QUEUE_LEN, /* same data as NEIGH_VAR_QUEUE_LEN_BYTES */
	NEIGH_VAR_RETRANS_TIME_MS, /* same data as NEIGH_VAR_RETRANS_TIME */
	NEIGH_VAR_BASE_REACHABLE_TIME_MS, /* same data as NEIGH_VAR_BASE_REACHABLE_TIME */
	/* Following are used by "default" only */
	NEIGH_VAR_GC_INTERVAL,
	NEIGH_VAR_GC_THRESH1,
	NEIGH_VAR_GC_THRESH2,
	NEIGH_VAR_GC_THRESH3,
	NEIGH_VAR_MAX
};

//一个邻居协议对应一个参数配置块
struct neigh_parms {
	possible_net_t net;
	struct net_device *dev; //对应的网络设备, 一个邻居协议只对应一个网络设备？？？
	struct list_head list;
	int	(*neigh_setup)(struct neighbour *);
	void	(*neigh_cleanup)(struct neighbour *);
	struct neigh_table *tbl; //对应的邻居表

	void	*sysctl_table; //邻居表的sysctl表，通过proc来读写邻居表的参数

	int dead; //为1，表示正在被删除
	refcount_t refcnt;
	struct rcu_head rcu_head;

	int	reachable_time; //NUD_REACHABLE状态的超时时间, 一般每300s更新一次，会更新位一个介于base_reachable_time, 1.5base_reachable_time之间的一个随机值
	int	data[NEIGH_VAR_DATA_MAX]; //保存了各种参数，譬如：base_reachable_time, %NEIGH_VAR_BASE_REACHABLE_TIME
	DECLARE_BITMAP(data_state, NEIGH_VAR_DATA_MAX);
};

static inline void neigh_var_set(struct neigh_parms *p, int index, int val)
{
	set_bit(index, p->data_state);
	p->data[index] = val;
}

#define NEIGH_VAR(p, attr) ((p)->data[NEIGH_VAR_ ## attr])

/* In ndo_neigh_setup, NEIGH_VAR_INIT should be used.
 * In other cases, NEIGH_VAR_SET should be used.
 */
#define NEIGH_VAR_INIT(p, attr, val) (NEIGH_VAR(p, attr) = val)
#define NEIGH_VAR_SET(p, attr, val) neigh_var_set(p, NEIGH_VAR_ ## attr, val)

static inline void neigh_parms_data_state_setall(struct neigh_parms *p)
{
	bitmap_fill(p->data_state, NEIGH_VAR_DATA_MAX);
}

static inline void neigh_parms_data_state_cleanall(struct neigh_parms *p)
{
	bitmap_zero(p->data_state, NEIGH_VAR_DATA_MAX);
}

struct neigh_statistics {
	unsigned long allocs;		/* number of allocated neighs */
	unsigned long destroys;		/* number of destroyed neighs */
	unsigned long hash_grows;	/* number of hash resizes */

	unsigned long res_failed;	/* number of failed resolutions */

	unsigned long lookups;		/* number of lookups */
	unsigned long hits;		/* number of hits (among lookups) */

	unsigned long rcv_probes_mcast;	/* number of received mcast ipv6 */
	unsigned long rcv_probes_ucast; /* number of received ucast ipv6 */

	unsigned long periodic_gc_runs;	/* number of periodic GC runs */
	unsigned long forced_gc_runs;	/* number of forced GC runs */

	unsigned long unres_discards;	/* number of unresolved drops */
	unsigned long table_fulls;      /* times even gc couldn't help */
};

#define NEIGH_CACHE_STAT_INC(tbl, field) this_cpu_inc((tbl)->stats->field)

/* 邻居表中的邻居项, 以散列表的形式存在 */
struct neighbour {
	struct neighbour __rcu	*next; //插入到散列表中的链
	struct neigh_table	*tbl; //指向其所在散列表
	struct neigh_parms	*parms; //指向该邻居项相关的参数块
	unsigned long		confirmed; //最近一次被证实可达邻居的时间
	unsigned long		updated; //最近一次被neigh_update的时间
	rwlock_t		lock;
	refcount_t		refcnt;
	struct sk_buff_head	arp_queue; /* 邻居项无效的时候缓存报文, 该邻居可达后，从其中取出报文输出 */
	unsigned int		arp_queue_len_bytes;
	struct timer_list	timer; //邻居项状态处理定时器，处理函数是neigh_timer_handler,参见：neigh_alloc
	unsigned long		used; //最近一次被使用, 该值也有可能在neigh_event_send中更新，也有可能在异步垃圾回收work中更新，不一定与数据传输同步。
	atomic_t		probes; //尝试发送请求报文没有得到应答的次数，当该值达到上限后，邻居项进入NUD_FAILED状态
	__u8			flags; //一些标志位
	__u8			nud_state; /* 邻居项状态，邻居项是有一个状态机的, %NUD_NONE */
	__u8			type; //邻居项的地址类型，%RTN_UNICAST
	__u8			dead; //设置为1的话，表示正在被删除
	seqlock_t		ha_lock;
	unsigned char		ha[ALIGN(MAX_ADDR_LEN, sizeof(unsigned long))]; /* 二层地址 */
	struct hh_cache		hh; /* 二层头部缓存 , 缓存后就可以直接复制了，而不是一个域一个域的设置头部，加速报文传输 */ 
	int			(*output)(struct neighbour *, struct sk_buff *); /* 输出报文到该邻居的函数 */
	const struct neigh_ops	*ops; //操作集合
	struct rcu_head		rcu;
	struct net_device	*dev; /* 通过此网络设备可以访问到邻居 */
	u8			primary_key[0]; /* 三层地址 */ //0字节是ip地址最左边的, 即类似于大端方式，低字节保存高位数字，即左边的数字
} __randomize_layout;

struct neigh_ops {
	int			family;
	void			(*solicit)(struct neighbour *, struct sk_buff *); /* 发送请求报文函数 */
	void			(*error_report)(struct neighbour *, struct sk_buff *);
	int			(*output)(struct neighbour *, struct sk_buff *); //最通用的输出函数，会慢一些
	int			(*connected_output)(struct neighbour *, struct sk_buff *); //确定邻居可达的时候的输出函数，会快些
};

struct pneigh_entry {
	struct pneigh_entry	*next;
	possible_net_t		net;
	struct net_device	*dev;
	u8			flags;
	u8			key[0];
};

/*
 *	neighbour table manipulation
 */

#define NEIGH_NUM_HASH_RND	4

struct neigh_hash_table {
	struct neighbour __rcu	**hash_buckets;
	unsigned int		hash_shift;
	__u32			hash_rnd[NEIGH_NUM_HASH_RND];
	struct rcu_head		rcu;
};


struct neigh_table { //系统中的所有邻居表是按照family组织成数组保存的，譬如：ipv4的arp的arp_tbl, 参见neigh_find_table, linux支持三种
	int			family; //所属协议族
	unsigned int		entry_size; //每个邻居项的大小
	unsigned int		key_len; //hash函数使用的key的长度, 实际上就是三层地址，在ipv4中就是ip地址，长度为4B
	__be16			protocol;
	__u32			(*hash)(const void *pkey, //计算key的hash函数
					const struct net_device *dev,
					__u32 *hash_rnd);
	bool			(*key_eq)(const struct neighbour *, const void *pkey);
	int			(*constructor)(struct neighbour *); //邻居项初始化函数
	int			(*pconstructor)(struct pneigh_entry *);
	void			(*pdestructor)(struct pneigh_entry *); //创建和释放一个代理项的时候被调用, ipv4中没有使用
	void			(*proxy_redo)(struct sk_buff *skb); //处理neigh_table_proxy_queue缓存队列中的代理arp报文
	char			*id; //分配neighbour结构实例的缓冲池字符串
	struct neigh_parms	parms; //存储一些与协议相关的可调节参数
	struct list_head	parms_list;
	int			gc_interval; //垃圾回收时钟，gc_timer的到期间隔时间
	int			gc_thresh1; //缓存中的邻居项小于thresh1的话，不会执行垃圾回收
	int			gc_thresh2; //如果邻居项数目超过thresh2的话，新建邻居项的时候超过5s没有属性，立即强制刷新并强制垃圾回收
	int			gc_thresh3; //如果超过thresh3的话，新建邻居项的时候，必须立即刷新并强制垃圾回收
	unsigned long		last_flush; //最近一次调用neigh_forced_gc强制刷新邻居表的时间
	struct delayed_work	gc_work; //垃圾回收work
	struct timer_list 	proxy_timer;
	struct sk_buff_head	proxy_queue;
	atomic_t		entries; //整个表中邻居项的数目
	rwlock_t		lock; //邻居表读写锁
	unsigned long		last_rand; //neigh_parms结构中reachable_time成员最近一次被更新的时间
	struct neigh_statistics	__percpu *stats;
	struct neigh_hash_table __rcu *nht; //保存邻居项的散列表
	struct pneigh_entry	**phash_buckets;
};

//linux支持的三种邻居发现协议
enum {
	NEIGH_ARP_TABLE = 0, //ipv4的arp
	NEIGH_ND_TABLE = 1,//ipv6的ND(邻居发现)
	NEIGH_DN_TABLE = 2, //DECNet
	NEIGH_NR_TABLES,
	NEIGH_LINK_TABLE = NEIGH_NR_TABLES /* Pseudo table for neigh_xmit */
};

static inline int neigh_parms_family(struct neigh_parms *p)
{
	return p->tbl->family;
}

#define NEIGH_PRIV_ALIGN	sizeof(long long)
#define NEIGH_ENTRY_SIZE(size)	ALIGN((size), NEIGH_PRIV_ALIGN)

static inline void *neighbour_priv(const struct neighbour *n)
{
	return (char *)n + n->tbl->entry_size;
}

/* flags for neigh_update() */
#define NEIGH_UPDATE_F_OVERRIDE			0x00000001
#define NEIGH_UPDATE_F_WEAK_OVERRIDE		0x00000002
#define NEIGH_UPDATE_F_OVERRIDE_ISROUTER	0x00000004
#define NEIGH_UPDATE_F_EXT_LEARNED		0x20000000
#define NEIGH_UPDATE_F_ISROUTER			0x40000000
#define NEIGH_UPDATE_F_ADMIN			0x80000000


static inline bool neigh_key_eq16(const struct neighbour *n, const void *pkey)
{
	return *(const u16 *)n->primary_key == *(const u16 *)pkey;
}

static inline bool neigh_key_eq32(const struct neighbour *n, const void *pkey)
{
	return *(const u32 *)n->primary_key == *(const u32 *)pkey;
}

static inline bool neigh_key_eq128(const struct neighbour *n, const void *pkey)
{
	const u32 *n32 = (const u32 *)n->primary_key;
	const u32 *p32 = pkey;

	return ((n32[0] ^ p32[0]) | (n32[1] ^ p32[1]) |
		(n32[2] ^ p32[2]) | (n32[3] ^ p32[3])) == 0;
}

static inline struct neighbour *___neigh_lookup_noref(
	struct neigh_table *tbl,
	bool (*key_eq)(const struct neighbour *n, const void *pkey),
	__u32 (*hash)(const void *pkey,
		      const struct net_device *dev,
		      __u32 *hash_rnd),
	const void *pkey,
	struct net_device *dev)
{
	struct neigh_hash_table *nht = rcu_dereference_bh(tbl->nht);
	struct neighbour *n;
	u32 hash_val;

	hash_val = hash(pkey, dev, nht->hash_rnd) >> (32 - nht->hash_shift);
	for (n = rcu_dereference_bh(nht->hash_buckets[hash_val]);
	     n != NULL;
	     n = rcu_dereference_bh(n->next)) {
		if (n->dev == dev && key_eq(n, pkey))
			return n;
	}

	return NULL;
}

static inline struct neighbour *__neigh_lookup_noref(struct neigh_table *tbl,
						     const void *pkey,
						     struct net_device *dev)
{
	return ___neigh_lookup_noref(tbl, tbl->key_eq, tbl->hash, pkey, dev);
}

void neigh_table_init(int index, struct neigh_table *tbl);
int neigh_table_clear(int index, struct neigh_table *tbl);
struct neighbour *neigh_lookup(struct neigh_table *tbl, const void *pkey,
			       struct net_device *dev);
struct neighbour *neigh_lookup_nodev(struct neigh_table *tbl, struct net *net,
				     const void *pkey);
struct neighbour *__neigh_create(struct neigh_table *tbl, const void *pkey,
				 struct net_device *dev, bool want_ref);
static inline struct neighbour *neigh_create(struct neigh_table *tbl,
					     const void *pkey,
					     struct net_device *dev)
{
	return __neigh_create(tbl, pkey, dev, true);
}
void neigh_destroy(struct neighbour *neigh);
int __neigh_event_send(struct neighbour *neigh, struct sk_buff *skb);
int neigh_update(struct neighbour *neigh, const u8 *lladdr, u8 new, u32 flags,
		 u32 nlmsg_pid);
void __neigh_set_probe_once(struct neighbour *neigh);
bool neigh_remove_one(struct neighbour *ndel, struct neigh_table *tbl);
void neigh_changeaddr(struct neigh_table *tbl, struct net_device *dev);
int neigh_ifdown(struct neigh_table *tbl, struct net_device *dev);
int neigh_resolve_output(struct neighbour *neigh, struct sk_buff *skb);
int neigh_connected_output(struct neighbour *neigh, struct sk_buff *skb);
int neigh_direct_output(struct neighbour *neigh, struct sk_buff *skb);
struct neighbour *neigh_event_ns(struct neigh_table *tbl,
						u8 *lladdr, void *saddr,
						struct net_device *dev);

struct neigh_parms *neigh_parms_alloc(struct net_device *dev,
				      struct neigh_table *tbl);
void neigh_parms_release(struct neigh_table *tbl, struct neigh_parms *parms);

static inline
struct net *neigh_parms_net(const struct neigh_parms *parms)
{
	return read_pnet(&parms->net);
}

unsigned long neigh_rand_reach_time(unsigned long base);

void pneigh_enqueue(struct neigh_table *tbl, struct neigh_parms *p,
		    struct sk_buff *skb);
struct pneigh_entry *pneigh_lookup(struct neigh_table *tbl, struct net *net,
				   const void *key, struct net_device *dev,
				   int creat);
struct pneigh_entry *__pneigh_lookup(struct neigh_table *tbl, struct net *net,
				     const void *key, struct net_device *dev);
int pneigh_delete(struct neigh_table *tbl, struct net *net, const void *key,
		  struct net_device *dev);

static inline struct net *pneigh_net(const struct pneigh_entry *pneigh)
{
	return read_pnet(&pneigh->net);
}

void neigh_app_ns(struct neighbour *n);
void neigh_for_each(struct neigh_table *tbl,
		    void (*cb)(struct neighbour *, void *), void *cookie);
void __neigh_for_each_release(struct neigh_table *tbl,
			      int (*cb)(struct neighbour *));
int neigh_xmit(int fam, struct net_device *, const void *, struct sk_buff *);
void pneigh_for_each(struct neigh_table *tbl,
		     void (*cb)(struct pneigh_entry *));

struct neigh_seq_state {
	struct seq_net_private p;
	struct neigh_table *tbl;
	struct neigh_hash_table *nht;
	void *(*neigh_sub_iter)(struct neigh_seq_state *state,
				struct neighbour *n, loff_t *pos);
	unsigned int bucket;
	unsigned int flags;
#define NEIGH_SEQ_NEIGH_ONLY	0x00000001
#define NEIGH_SEQ_IS_PNEIGH	0x00000002
#define NEIGH_SEQ_SKIP_NOARP	0x00000004
};
void *neigh_seq_start(struct seq_file *, loff_t *, struct neigh_table *,
		      unsigned int);
void *neigh_seq_next(struct seq_file *, void *, loff_t *);
void neigh_seq_stop(struct seq_file *, void *);

int neigh_proc_dointvec(struct ctl_table *ctl, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
int neigh_proc_dointvec_jiffies(struct ctl_table *ctl, int write,
				void __user *buffer,
				size_t *lenp, loff_t *ppos);
int neigh_proc_dointvec_ms_jiffies(struct ctl_table *ctl, int write,
				   void __user *buffer,
				   size_t *lenp, loff_t *ppos);

int neigh_sysctl_register(struct net_device *dev, struct neigh_parms *p,
			  proc_handler *proc_handler);
void neigh_sysctl_unregister(struct neigh_parms *p);

static inline void __neigh_parms_put(struct neigh_parms *parms)
{
	refcount_dec(&parms->refcnt);
}

static inline struct neigh_parms *neigh_parms_clone(struct neigh_parms *parms)
{
	refcount_inc(&parms->refcnt);
	return parms;
}

/*
 *	Neighbour references
 */

static inline void neigh_release(struct neighbour *neigh)
{
	if (refcount_dec_and_test(&neigh->refcnt))
		neigh_destroy(neigh);
}

static inline struct neighbour * neigh_clone(struct neighbour *neigh)
{
	if (neigh)
		refcount_inc(&neigh->refcnt);
	return neigh;
}

#define neigh_hold(n)	refcount_inc(&(n)->refcnt)

//检查邻居项状态是否有效
static inline int neigh_event_send(struct neighbour *neigh, struct sk_buff *skb)
{
	unsigned long now = jiffies;
	
	if (neigh->used != now)
		neigh->used = now;
	if (!(neigh->nud_state&(NUD_CONNECTED|NUD_DELAY|NUD_PROBE))) //如果不是这三个状态就需要进一步检测，否则可以直接发送
		return __neigh_event_send(neigh, skb);
	return 0;
}

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
static inline int neigh_hh_bridge(struct hh_cache *hh, struct sk_buff *skb)
{
	unsigned int seq, hh_alen;

	do {
		seq = read_seqbegin(&hh->hh_lock);
		hh_alen = HH_DATA_ALIGN(ETH_HLEN);
		memcpy(skb->data - hh_alen, hh->hh_data, ETH_ALEN + hh_alen - ETH_HLEN);
	} while (read_seqretry(&hh->hh_lock, seq));
	return 0;
}
#endif

static inline int neigh_hh_output(const struct hh_cache *hh, struct sk_buff *skb)
{
	unsigned int hh_alen = 0;
	unsigned int seq;
	unsigned int hh_len;

	do {
		seq = read_seqbegin(&hh->hh_lock);
		hh_len = hh->hh_len;
		if (likely(hh_len <= HH_DATA_MOD)) {
			hh_alen = HH_DATA_MOD;

			/* skb_push() would proceed silently if we have room for
			 * the unaligned size but not for the aligned size:
			 * check headroom explicitly.
			 */
			if (likely(skb_headroom(skb) >= HH_DATA_MOD)) {
				/* this is inlined by gcc */
				memcpy(skb->data - HH_DATA_MOD, hh->hh_data,
				       HH_DATA_MOD);
			}
		} else {
			hh_alen = HH_DATA_ALIGN(hh_len);

			if (likely(skb_headroom(skb) >= hh_alen)) {
				memcpy(skb->data - hh_alen, hh->hh_data,
				       hh_alen);
			}
		}
	} while (read_seqretry(&hh->hh_lock, seq));

	if (WARN_ON_ONCE(skb_headroom(skb) < hh_alen)) {
		kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	__skb_push(skb, hh_len);
	return dev_queue_xmit(skb);
}

static inline int neigh_output(struct neighbour *n, struct sk_buff *skb)
{
	const struct hh_cache *hh = &n->hh;

	if ((n->nud_state & NUD_CONNECTED) && hh->hh_len)
		return neigh_hh_output(hh, skb);
	else
		return n->output(n, skb);
}

static inline struct neighbour *
__neigh_lookup(struct neigh_table *tbl, const void *pkey, struct net_device *dev, int creat)
{
	struct neighbour *n = neigh_lookup(tbl, pkey, dev);

	if (n || !creat)
		return n;

	n = neigh_create(tbl, pkey, dev);
	return IS_ERR(n) ? NULL : n;
}

static inline struct neighbour *
__neigh_lookup_errno(struct neigh_table *tbl, const void *pkey,
  struct net_device *dev)
{
	struct neighbour *n = neigh_lookup(tbl, pkey, dev);

	if (n)
		return n;

	return neigh_create(tbl, pkey, dev);
}

struct neighbour_cb {
	unsigned long sched_next;
	unsigned int flags;
};

#define LOCALLY_ENQUEUED 0x1

#define NEIGH_CB(skb)	((struct neighbour_cb *)(skb)->cb)

static inline void neigh_ha_snapshot(char *dst, const struct neighbour *n,
				     const struct net_device *dev)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&n->ha_lock);
		memcpy(dst, n->ha, dev->addr_len);
	} while (read_seqretry(&n->ha_lock, seq));
}

static inline void neigh_update_ext_learned(struct neighbour *neigh, u32 flags,
					    int *notify)
{
	u8 ndm_flags = 0;

	if (!(flags & NEIGH_UPDATE_F_ADMIN))
		return;

	ndm_flags |= (flags & NEIGH_UPDATE_F_EXT_LEARNED) ? NTF_EXT_LEARNED : 0;
	if ((neigh->flags ^ ndm_flags) & NTF_EXT_LEARNED) {
		if (ndm_flags & NTF_EXT_LEARNED)
			neigh->flags |= NTF_EXT_LEARNED;
		else
			neigh->flags &= ~NTF_EXT_LEARNED;
		*notify = 1;
	}
}
#endif
