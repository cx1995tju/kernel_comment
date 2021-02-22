/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Forwarding Information Base.
 *
 * Authors:	A.N.Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IP_FIB_H
#define _NET_IP_FIB_H

#include <net/flow.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <net/fib_notifier.h>
#include <net/fib_rules.h>
#include <net/inetpeer.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/refcount.h>

//fib_table_insert的参数, 用来查找匹配路由表项
struct fib_config {
	u8			fc_dst_len; //目的掩码长度
	u8			fc_tos; //路由的TOS字段
	u8			fc_protocol; //该路由的特性
	u8			fc_scope; //该路由的范围
	u8			fc_type; //该路由表项的类型
	/* 3 bytes unused */
	u32			fc_table; //对应的路由表id
	__be32			fc_dst; //路由项的目的地址
	__be32			fc_gw; //路由项的网关地址
	int			fc_oif; //路由项的输出网络设备索引
	u32			fc_flags; //一些标志
	u32			fc_priority; //路由项的优先级，越小越优先
	__be32			fc_prefsrc; //首选源地址
	struct nlattr		*fc_mx;
	struct rtnexthop	*fc_mp;
	int			fc_mx_len;
	int			fc_mp_len;
	u32			fc_flow; //基于策略路由的分类标签
	u32			fc_nlflags; //操作模式 %NLM_F_REPLACE
	struct nl_info		fc_nlinfo; //配置路由的netlink数据包信息
	struct nlattr		*fc_encap;
	u16			fc_encap_type;
};

struct fib_info;
struct rtable;

struct fib_nh_exception {
	struct fib_nh_exception __rcu	*fnhe_next;
	int				fnhe_genid;
	__be32				fnhe_daddr;
	u32				fnhe_pmtu;
	bool				fnhe_mtu_locked;
	__be32				fnhe_gw;
	unsigned long			fnhe_expires;
	struct rtable __rcu		*fnhe_rth_input;
	struct rtable __rcu		*fnhe_rth_output;
	unsigned long			fnhe_stamp;
	struct rcu_head			rcu;
};

struct fnhe_hash_bucket {
	struct fib_nh_exception __rcu	*chain;
};

#define FNHE_HASH_SHIFT		11
#define FNHE_HASH_SIZE		(1 << FNHE_HASH_SHIFT)
#define FNHE_RECLAIM_DEPTH	5

/* 下一跳路由的信息， next hop */
struct fib_nh {
	struct net_device	*nh_dev; //该路由表项的输出设备
	struct hlist_node	nh_hash;
	struct fib_info		*nh_parent; //指向所属路由表项的fib_info结构
	unsigned int		nh_flags;
	unsigned char		nh_scope; //路由范围
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	int			nh_weight;
	atomic_t		nh_upper_bound;
#endif
#ifdef CONFIG_IP_ROUTE_CLASSID
	__u32			nh_tclassid; //基于策略路由的分类标签
#endif
	int			nh_oif; //该路由表项的输出网络设备索引
	__be32			nh_gw; //路由项的网关地址
	__be32			nh_saddr;
	int			nh_saddr_genid;
	struct rtable __rcu * __percpu *nh_pcpu_rth_output;
	struct rtable __rcu	*nh_rth_input;
	struct fnhe_hash_bucket	__rcu *nh_exceptions;
	struct lwtunnel_state	*nh_lwtstate;
};

/*
 * This structure contains data shared by many of routes.
 */
//记录如何处理与该路由匹配的数据报的信息
struct fib_info {
	struct hlist_node	fib_hash; //插入到fib_info_hash散列表中的, 所有的fib_info实例都插入到这个散列表中
	struct hlist_node	fib_lhash; //插入到fib_info_laddrhash散列表中，当路由表项有一个首选源地址的时候，插入到该散列表
	struct net		*fib_net;
	int			fib_treeref;
	refcount_t		fib_clntref;
	unsigned int		fib_flags;
	unsigned char		fib_dead; //路由表项正在被删除
	unsigned char		fib_protocol; //设置路由的协议
	unsigned char		fib_scope; //路由表项的作用范围
	unsigned char		fib_type;
	__be32			fib_prefsrc; //首选源地址
	u32			fib_tb_id;
	u32			fib_priority; //路由优先级，值越小，优先级越高
	struct dst_metrics	*fib_metrics; //与路由相关的一组度量值
#define fib_mtu fib_metrics->metrics[RTAX_MTU-1]
#define fib_window fib_metrics->metrics[RTAX_WINDOW-1]
#define fib_rtt fib_metrics->metrics[RTAX_RTT-1]
#define fib_advmss fib_metrics->metrics[RTAX_ADVMSS-1]
	int			fib_nhs; //下一跳数量，通常为1，只有当内核支持多路径路由的时候，才可能大于1
	struct rcu_head		rcu;
	struct fib_nh		fib_nh[0]; //支持多路径路由的时候，保存下一跳的散列表
#define fib_dev		fib_nh[0].nh_dev
};


#ifdef CONFIG_IP_MULTIPLE_TABLES
struct fib_rule;
#endif

struct fib_table;
struct fib_result {
	__be32		prefix;
	unsigned char	prefixlen;
	unsigned char	nh_sel;
	unsigned char	type;
	unsigned char	scope;
	u32		tclassid;
	struct fib_info *fi;
	struct fib_table *table;
	struct hlist_head *fa_head;
};

struct fib_result_nl {
	__be32		fl_addr;   /* To be looked up*/
	u32		fl_mark;
	unsigned char	fl_tos;
	unsigned char   fl_scope;
	unsigned char   tb_id_in;

	unsigned char   tb_id;      /* Results */
	unsigned char	prefixlen;
	unsigned char	nh_sel;
	unsigned char	type;
	unsigned char	scope;
	int             err;
};

#ifdef CONFIG_IP_ROUTE_MULTIPATH
#define FIB_RES_NH(res)		((res).fi->fib_nh[(res).nh_sel])
#else /* CONFIG_IP_ROUTE_MULTIPATH */
#define FIB_RES_NH(res)		((res).fi->fib_nh[0])
#endif /* CONFIG_IP_ROUTE_MULTIPATH */

#ifdef CONFIG_IP_MULTIPLE_TABLES
#define FIB_TABLE_HASHSZ 256
#else
#define FIB_TABLE_HASHSZ 2
#endif

__be32 fib_info_update_nh_saddr(struct net *net, struct fib_nh *nh);

#define FIB_RES_SADDR(net, res)				\
	((FIB_RES_NH(res).nh_saddr_genid ==		\
	  atomic_read(&(net)->ipv4.dev_addr_genid)) ?	\
	 FIB_RES_NH(res).nh_saddr :			\
	 fib_info_update_nh_saddr((net), &FIB_RES_NH(res)))
#define FIB_RES_GW(res)			(FIB_RES_NH(res).nh_gw)
#define FIB_RES_DEV(res)		(FIB_RES_NH(res).nh_dev)
#define FIB_RES_OIF(res)		(FIB_RES_NH(res).nh_oif)

#define FIB_RES_PREFSRC(net, res)	((res).fi->fib_prefsrc ? : \
					 FIB_RES_SADDR(net, res))

struct fib_entry_notifier_info {
	struct fib_notifier_info info; /* must be first */
	u32 dst;
	int dst_len;
	struct fib_info *fi;
	u8 tos;
	u8 type;
	u32 tb_id;
};

struct fib_nh_notifier_info {
	struct fib_notifier_info info; /* must be first */
	struct fib_nh *fib_nh;
};

int call_fib4_notifier(struct notifier_block *nb, struct net *net,
		       enum fib_event_type event_type,
		       struct fib_notifier_info *info);
int call_fib4_notifiers(struct net *net, enum fib_event_type event_type,
			struct fib_notifier_info *info);

int __net_init fib4_notifier_init(struct net *net);
void __net_exit fib4_notifier_exit(struct net *net);

void fib_notify(struct net *net, struct notifier_block *nb);

struct fib_table {
	struct hlist_node	tb_hlist; //所有的路由表组织在一个hash表中
	u32			tb_id; //路由表id，在支持策略路由的场景下，主机最多可以有256个路由表，即fib_rule中的table成员
	int			tb_num_default;
	struct rcu_head		rcu;
	unsigned long 		*tb_data; //一颗字典树，保存路由表项
	unsigned long		__data[0]; //零长数组
};

int fib_table_lookup(struct fib_table *tb, const struct flowi4 *flp,
		     struct fib_result *res, int fib_flags);
int fib_table_insert(struct net *, struct fib_table *, struct fib_config *,
		     struct netlink_ext_ack *extack);
int fib_table_delete(struct net *, struct fib_table *, struct fib_config *,
		     struct netlink_ext_ack *extack);
int fib_table_dump(struct fib_table *table, struct sk_buff *skb,
		   struct netlink_callback *cb);
int fib_table_flush(struct net *net, struct fib_table *table, bool flush_all);
struct fib_table *fib_trie_unmerge(struct fib_table *main_tb);
void fib_table_flush_external(struct fib_table *table);
void fib_free_table(struct fib_table *tb);

#ifndef CONFIG_IP_MULTIPLE_TABLES

#define TABLE_LOCAL_INDEX	(RT_TABLE_LOCAL & (FIB_TABLE_HASHSZ - 1)) //这个表用于本地地址，存储了所有的本地地址，如果在该表中查找到匹配的表项，表示数据报是发送给本机的
#define TABLE_MAIN_INDEX	(RT_TABLE_MAIN  & (FIB_TABLE_HASHSZ - 1)) //这个表是用于查找所有其他路由的, 路由表项是手工配置或者路由发现协议动态配置的

static inline struct fib_table *fib_get_table(struct net *net, u32 id)
{
	struct hlist_node *tb_hlist;
	struct hlist_head *ptr;

	ptr = id == RT_TABLE_LOCAL ?
		&net->ipv4.fib_table_hash[TABLE_LOCAL_INDEX] :
		&net->ipv4.fib_table_hash[TABLE_MAIN_INDEX];

	tb_hlist = rcu_dereference_rtnl(hlist_first_rcu(ptr));

	return hlist_entry(tb_hlist, struct fib_table, tb_hlist);
}

static inline struct fib_table *fib_new_table(struct net *net, u32 id)
{
	return fib_get_table(net, id);
}

//这个函数有两个版本，支不支持策略路由是不同的, CONFIG_IP_MULTIPLE_TABLES
static inline int fib_lookup(struct net *net, const struct flowi4 *flp,
			     struct fib_result *res, unsigned int flags)
{
	//res用于保存结果
	struct fib_table *tb;
	int err = -ENETUNREACH;

	rcu_read_lock();

	tb = fib_get_table(net, RT_TABLE_MAIN);
	if (tb)
		err = fib_table_lookup(tb, flp, res, flags | FIB_LOOKUP_NOREF);

	if (err == -EAGAIN)
		err = -ENETUNREACH;

	rcu_read_unlock();

	return err;
}

static inline bool fib4_rule_default(const struct fib_rule *rule)
{
	return true;
}

static inline int fib4_rules_dump(struct net *net, struct notifier_block *nb)
{
	return 0;
}

static inline unsigned int fib4_rules_seq_read(struct net *net)
{
	return 0;
}

static inline bool fib4_rules_early_flow_dissect(struct net *net,
						 struct sk_buff *skb,
						 struct flowi4 *fl4,
						 struct flow_keys *flkeys)
{
	return false;
}
#else /* CONFIG_IP_MULTIPLE_TABLES, 策略路由 */
int __net_init fib4_rules_init(struct net *net);
void __net_exit fib4_rules_exit(struct net *net);

struct fib_table *fib_new_table(struct net *net, u32 id);
struct fib_table *fib_get_table(struct net *net, u32 id);

int __fib_lookup(struct net *net, struct flowi4 *flp,
		 struct fib_result *res, unsigned int flags);

static inline int fib_lookup(struct net *net, struct flowi4 *flp,
			     struct fib_result *res, unsigned int flags)
{
	struct fib_table *tb;
	int err = -ENETUNREACH;

	flags |= FIB_LOOKUP_NOREF;
	if (net->ipv4.fib_has_custom_rules)
		return __fib_lookup(net, flp, res, flags);

	rcu_read_lock();

	res->tclassid = 0;

	tb = rcu_dereference_rtnl(net->ipv4.fib_main);
	if (tb)
		err = fib_table_lookup(tb, flp, res, flags);

	if (!err)
		goto out;

	tb = rcu_dereference_rtnl(net->ipv4.fib_default);
	if (tb)
		err = fib_table_lookup(tb, flp, res, flags);

out:
	if (err == -EAGAIN)
		err = -ENETUNREACH;

	rcu_read_unlock();

	return err;
}

bool fib4_rule_default(const struct fib_rule *rule);
int fib4_rules_dump(struct net *net, struct notifier_block *nb);
unsigned int fib4_rules_seq_read(struct net *net);

static inline bool fib4_rules_early_flow_dissect(struct net *net,
						 struct sk_buff *skb,
						 struct flowi4 *fl4,
						 struct flow_keys *flkeys)
{
	unsigned int flag = FLOW_DISSECTOR_F_STOP_AT_ENCAP;

	if (!net->ipv4.fib_rules_require_fldissect)
		return false;

	skb_flow_dissect_flow_keys(skb, flkeys, flag);
	fl4->fl4_sport = flkeys->ports.src;
	fl4->fl4_dport = flkeys->ports.dst;
	fl4->flowi4_proto = flkeys->basic.ip_proto;

	return true;
}

#endif /* CONFIG_IP_MULTIPLE_TABLES */

/* Exported by fib_frontend.c */
extern const struct nla_policy rtm_ipv4_policy[];
void ip_fib_init(void);
__be32 fib_compute_spec_dst(struct sk_buff *skb);
int fib_validate_source(struct sk_buff *skb, __be32 src, __be32 dst,
			u8 tos, int oif, struct net_device *dev,
			struct in_device *idev, u32 *itag);
#ifdef CONFIG_IP_ROUTE_CLASSID
static inline int fib_num_tclassid_users(struct net *net)
{
	return net->ipv4.fib_num_tclassid_users;
}
#else
static inline int fib_num_tclassid_users(struct net *net)
{
	return 0;
}
#endif
int fib_unmerge(struct net *net);

/* Exported by fib_semantics.c */
int ip_fib_check_default(__be32 gw, struct net_device *dev);
int fib_sync_down_dev(struct net_device *dev, unsigned long event, bool force);
int fib_sync_down_addr(struct net_device *dev, __be32 local);
int fib_sync_up(struct net_device *dev, unsigned int nh_flags);
void fib_sync_mtu(struct net_device *dev, u32 orig_mtu);

#ifdef CONFIG_IP_ROUTE_MULTIPATH
int fib_multipath_hash(const struct net *net, const struct flowi4 *fl4,
		       const struct sk_buff *skb, struct flow_keys *flkeys);
#endif
void fib_select_multipath(struct fib_result *res, int hash);
void fib_select_path(struct net *net, struct fib_result *res,
		     struct flowi4 *fl4, const struct sk_buff *skb);

/* Exported by fib_trie.c */
void fib_trie_init(void);
struct fib_table *fib_trie_table(u32 id, struct fib_table *alias);

static inline void fib_combine_itag(u32 *itag, const struct fib_result *res)
{
#ifdef CONFIG_IP_ROUTE_CLASSID
#ifdef CONFIG_IP_MULTIPLE_TABLES
	u32 rtag;
#endif
	*itag = FIB_RES_NH(*res).nh_tclassid<<16;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	rtag = res->tclassid;
	if (*itag == 0)
		*itag = (rtag<<16);
	*itag |= (rtag>>16);
#endif
#endif
}

void free_fib_info(struct fib_info *fi);

static inline void fib_info_hold(struct fib_info *fi)
{
	refcount_inc(&fi->fib_clntref);
}

static inline void fib_info_put(struct fib_info *fi)
{
	if (refcount_dec_and_test(&fi->fib_clntref))
		free_fib_info(fi);
}

#ifdef CONFIG_PROC_FS
int __net_init fib_proc_init(struct net *net);
void __net_exit fib_proc_exit(struct net *net);
#else
static inline int fib_proc_init(struct net *net)
{
	return 0;
}
static inline void fib_proc_exit(struct net *net)
{
}
#endif

u32 ip_mtu_from_fib_result(struct fib_result *res, __be32 daddr);

#endif  /* _NET_FIB_H */
