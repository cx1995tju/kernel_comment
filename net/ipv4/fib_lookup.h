/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FIB_LOOKUP_H
#define _FIB_LOOKUP_H

#include <linux/types.h>
#include <linux/list.h>
#include <net/ip_fib.h>

//代表一条路由表项
//其实fib_info结构才是存储了更多的路由表项信息，但是为了优化，有时候几条表项仅仅是fa_tos不同，我们就将其指向同一个fib_info中
struct fib_alias {
	struct hlist_node	fa_list; //这是啥list？？？？？？ 链到字典树上的node, 参考fib_insert_alias
	struct fib_info		*fa_info; //记录如何处理与该路由匹配的数据报的信息
	u8			fa_tos; //路由的服务类型比特位字段。当该值位0的时候，表示还没有配置TOS，所以在路由查找时任何值都可以匹配。
	u8			fa_type; //路由表项的类型 %RTN_UNSPEC
	u8			fa_state; //一些标志的位图，%FA_S_ACCESSED, 目前只有这一个标志
	u8			fa_slen;
	u32			tb_id; //route table_id
	s16			fa_default;
	struct rcu_head		rcu;
};

#define FA_S_ACCESSED	0x01 //表示该表项已经被访问过了

/* Dont write on fa_state unless needed, to keep it shared on all cpus */
static inline void fib_alias_accessed(struct fib_alias *fa)
{
	if (!(fa->fa_state & FA_S_ACCESSED))
		fa->fa_state |= FA_S_ACCESSED;
}

/* Exported by fib_semantics.c */
void fib_release_info(struct fib_info *);
struct fib_info *fib_create_info(struct fib_config *cfg,
				 struct netlink_ext_ack *extack);
int fib_nh_match(struct fib_config *cfg, struct fib_info *fi,
		 struct netlink_ext_ack *extack);
bool fib_metrics_match(struct fib_config *cfg, struct fib_info *fi);
int fib_dump_info(struct sk_buff *skb, u32 pid, u32 seq, int event, u32 tb_id,
		  u8 type, __be32 dst, int dst_len, u8 tos, struct fib_info *fi,
		  unsigned int);
void rtmsg_fib(int event, __be32 key, struct fib_alias *fa, int dst_len,
	       u32 tb_id, const struct nl_info *info, unsigned int nlm_flags);

static inline void fib_result_assign(struct fib_result *res,
				     struct fib_info *fi)
{
	/* we used to play games with refcounts, but we now use RCU */
	res->fi = fi;
}

//如果fib_info中的fib_type类型是RTN_PROHIBIT， 那么就需要发送ICMP报文（ICMP——PKT_FILTERED）
//就需要这个结构数组fib_props保存信息
struct fib_prop {
	int	error;
	u8	scope;
};

extern const struct fib_prop fib_props[RTN_MAX + 1];

#endif /* _FIB_LOOKUP_H */
