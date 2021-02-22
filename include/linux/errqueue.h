/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ERRQUEUE_H
#define _LINUX_ERRQUEUE_H 1


#include <net/ip.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <linux/ipv6.h>
#endif
#include <uapi/linux/errqueue.h>

#define SKB_EXT_ERR(skb) ((struct sock_exterr_skb *) ((skb)->cb))

//这是错误信息块的结构
struct sock_exterr_skb {
	union {
		struct inet_skb_parm	h4; //兼容ip的私有控制块
#if IS_ENABLED(CONFIG_IPV6)
		struct inet6_skb_parm	h6;
#endif
	} header;
	struct sock_extended_err	ee; //记录出错信息, 见sock_extended_err
	u16				addr_offset; //导致出错的原始数据报在负载ICMP报文的IP数据报的偏移量
	__be16				port; //UDP出错报文的目的端口
	u8				opt_stats:1,
					unused:7;
};

#endif
