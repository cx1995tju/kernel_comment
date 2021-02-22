/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_NEIGHBOUR_H
#define __LINUX_NEIGHBOUR_H

#include <linux/types.h>
#include <linux/netlink.h>

struct ndmsg {
	__u8		ndm_family;
	__u8		ndm_pad1;
	__u16		ndm_pad2;
	__s32		ndm_ifindex;
	__u16		ndm_state;
	__u8		ndm_flags;
	__u8		ndm_type;
};

enum {
	NDA_UNSPEC,
	NDA_DST,
	NDA_LLADDR,
	NDA_CACHEINFO,
	NDA_PROBES,
	NDA_VLAN,
	NDA_PORT,
	NDA_VNI,
	NDA_IFINDEX,
	NDA_MASTER,
	NDA_LINK_NETNSID,
	NDA_SRC_VNI,
	__NDA_MAX
};

#define NDA_MAX (__NDA_MAX - 1)

/*
 *	Neighbor Cache Entry Flags
 */

#define NTF_USE		0x01
#define NTF_SELF	0x02
#define NTF_MASTER	0x04
#define NTF_PROXY	0x08	/* == ATF_PUBL */
#define NTF_EXT_LEARNED	0x10
#define NTF_OFFLOADED   0x20
#define NTF_ROUTER	0x80

/*
 *	Neighbor Cache Entry States.
 */
//INCOMPLETE 和 PROBE这两种状态下，尝试发送请求报文的次数大于上限，转变为NUD_FAILED状态，然后删除释放缓存队列上的保温，发送错误报告给报文的发送方, 在ARP就是目的不可达ICMP消息


//请求报文已经发送，但是还没有收到应答的状态, 此状态还没有硬件地址，如果有报文要发送，就会缓存起来。进入该状态的时候会启动给一个定时器，在定时器到期没有接收到邻居回应的话，会重复尝试多起，如果在达到上限前成功，进入NUD_RECHABLE状态，否则进入到NUD_STALE状态
#define NUD_INCOMPLETE	0x01
//可达状态，进入该状态时，首先设置邻居项的output函数指针,查看是否有要发送给该邻居的报文，如果有发送出去(快速路径)。在该状态如果限制时间到达上限，且引用计数为1的话，可以通过垃圾回收机制将其删除
#define NUD_REACHABLE	0x02
//过期状态，如果有报文要输出到该邻居，就会进入到NUD_DELAY状态。在该状态闲置时间过久，且引用计数为1，会被垃圾回收机制回收
#define NUD_STALE	0x04
//报文已经发出，需要得到邻居可达性的确认，便会进入到NUD_PROBE状态，否则进入到NUD_REACHABLE状态。该状态下报文发送不受限制，用慢速发送
#define NUD_DELAY	0x08
//过去状态，类似于NUD_INCOMPLETE状态。该状态下，输出不受限制，也是慢速发送
#define NUD_PROBE	0x10
//由于没有接收到应答而无法访问的状态
#define NUD_FAILED	0x20

/* Dummy states */
//不需要将三层地址映射到二层地址协议的支持
#define NUD_NOARP	0x40
//静态配置的
#define NUD_PERMANENT	0x80
//邻居项刚建立时的初始状态, 此状态下还没有硬件地址使用，不能发送请求报文，一旦有报文要输出到该邻居，就会触发对该邻居硬件地址的请求，进入到NUD_INCOMPLETE状态, 并缓存要发送的报文
#define NUD_NONE	0x00

/* NUD_NOARP & NUD_PERMANENT are pseudostates, they never change
   and make no address resolution or NUD.
   NUD_PERMANENT also cannot be deleted by garbage collectors.
 */

struct nda_cacheinfo {
	__u32		ndm_confirmed;
	__u32		ndm_used;
	__u32		ndm_updated;
	__u32		ndm_refcnt;
};

/*****************************************************************
 *		Neighbour tables specific messages.
 *
 * To retrieve the neighbour tables send RTM_GETNEIGHTBL with the
 * NLM_F_DUMP flag set. Every neighbour table configuration is
 * spread over multiple messages to avoid running into message
 * size limits on systems with many interfaces. The first message
 * in the sequence transports all not device specific data such as
 * statistics, configuration, and the default parameter set.
 * This message is followed by 0..n messages carrying device
 * specific parameter sets.
 * Although the ordering should be sufficient, NDTA_NAME can be
 * used to identify sequences. The initial message can be identified
 * by checking for NDTA_CONFIG. The device specific messages do
 * not contain this TLV but have NDTPA_IFINDEX set to the
 * corresponding interface index.
 *
 * To change neighbour table attributes, send RTM_SETNEIGHTBL
 * with NDTA_NAME set. Changeable attribute include NDTA_THRESH[1-3],
 * NDTA_GC_INTERVAL, and all TLVs in NDTA_PARMS unless marked
 * otherwise. Device specific parameter sets can be changed by
 * setting NDTPA_IFINDEX to the interface index of the corresponding
 * device.
 ****/

struct ndt_stats {
	__u64		ndts_allocs;
	__u64		ndts_destroys;
	__u64		ndts_hash_grows;
	__u64		ndts_res_failed;
	__u64		ndts_lookups;
	__u64		ndts_hits;
	__u64		ndts_rcv_probes_mcast;
	__u64		ndts_rcv_probes_ucast;
	__u64		ndts_periodic_gc_runs;
	__u64		ndts_forced_gc_runs;
	__u64		ndts_table_fulls;
};

enum {
	NDTPA_UNSPEC,
	NDTPA_IFINDEX,			/* u32, unchangeable */
	NDTPA_REFCNT,			/* u32, read-only */
	NDTPA_REACHABLE_TIME,		/* u64, read-only, msecs */
	NDTPA_BASE_REACHABLE_TIME,	/* u64, msecs */
	NDTPA_RETRANS_TIME,		/* u64, msecs */
	NDTPA_GC_STALETIME,		/* u64, msecs */
	NDTPA_DELAY_PROBE_TIME,		/* u64, msecs */
	NDTPA_QUEUE_LEN,		/* u32 */
	NDTPA_APP_PROBES,		/* u32 */
	NDTPA_UCAST_PROBES,		/* u32 */
	NDTPA_MCAST_PROBES,		/* u32 */
	NDTPA_ANYCAST_DELAY,		/* u64, msecs */
	NDTPA_PROXY_DELAY,		/* u64, msecs */
	NDTPA_PROXY_QLEN,		/* u32 */
	NDTPA_LOCKTIME,			/* u64, msecs */
	NDTPA_QUEUE_LENBYTES,		/* u32 */
	NDTPA_MCAST_REPROBES,		/* u32 */
	NDTPA_PAD,
	__NDTPA_MAX
};
#define NDTPA_MAX (__NDTPA_MAX - 1)

struct ndtmsg {
	__u8		ndtm_family;
	__u8		ndtm_pad1;
	__u16		ndtm_pad2;
};

struct ndt_config {
	__u16		ndtc_key_len;
	__u16		ndtc_entry_size;
	__u32		ndtc_entries;
	__u32		ndtc_last_flush;	/* delta to now in msecs */
	__u32		ndtc_last_rand;		/* delta to now in msecs */
	__u32		ndtc_hash_rnd;
	__u32		ndtc_hash_mask;
	__u32		ndtc_hash_chain_gc;
	__u32		ndtc_proxy_qlen;
};

enum {
	NDTA_UNSPEC,
	NDTA_NAME,			/* char *, unchangeable */
	NDTA_THRESH1,			/* u32 */
	NDTA_THRESH2,			/* u32 */
	NDTA_THRESH3,			/* u32 */
	NDTA_CONFIG,			/* struct ndt_config, read-only */
	NDTA_PARMS,			/* nested TLV NDTPA_* */
	NDTA_STATS,			/* struct ndt_stats, read-only */
	NDTA_GC_INTERVAL,		/* u64, msecs */
	NDTA_PAD,
	__NDTA_MAX
};
#define NDTA_MAX (__NDTA_MAX - 1)

#endif
