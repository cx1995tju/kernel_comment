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
/* 
 1. 套接口有一个sk_error_queue的错误队列，
         + ICMP收到差错报文会挂载到该队列
         + UDP输出报文出错，会产生描述错误信息的SKB挂到该队列
         + RAW套接口输出报文出错的时候，也会产生错误信息到该队列
 2. 错误信息传递给用户进程的时候，不是直接以数据报正文的形式提供，而是以错误信息块的结构(sock_exterr_skb)保存在skb->cb中的
 */

struct sock_exterr_skb {
	union {
		struct inet_skb_parm	h4; //兼容ip的私有控制块, 因为该错误信息也会在ip层被处理，ip层的控制块也需要保存在skb->cb中
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
