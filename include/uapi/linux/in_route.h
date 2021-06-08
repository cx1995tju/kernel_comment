/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_IN_ROUTE_H
#define _LINUX_IN_ROUTE_H

/* IPv4 routing cache flags */

#define RTCF_DEAD	RTNH_F_DEAD
#define RTCF_ONLINK	RTNH_F_ONLINK

/* Obsolete flag. About to be deleted */
#define RTCF_NOPMTUDISC RTM_F_NOPMTUDISC

#define RTCF_NOTIFY	0x00010000 //路由表项的所有变化通过netlink通知给用户空间
#define RTCF_DIRECTDST	0x00020000 /* unused */
#define RTCF_REDIRECTED	0x00040000 //对于接收到的ICMP重定向消息做相应，添加一条路由缓存项
#define RTCF_TPROXY	0x00080000 /* unused */

#define RTCF_FAST	0x00200000 /* unused */
#define RTCF_MASQ	0x00400000 /* unused */
#define RTCF_SNAT	0x00800000 /* unused */
#define RTCF_DOREDIRECT 0x01000000 //如果设置了，那么就表示应该发送ICMP 重定向报文, 作为进入的报文的回复 __mkroute_output()
#define RTCF_DIRECTSRC	0x04000000 //不正确的源地址
#define RTCF_DNAT	0x08000000
#define RTCF_BROADCAST	0x10000000 //路由目的是广播地址, 在__mkroute_output() ip_route_input_slow()中可能被设置
#define RTCF_MULTICAST	0x20000000 //路由目的是多播地址 __mkroute_output() ip_route_input_mc()
#define RTCF_REJECT	0x40000000 /* unused */
#define RTCF_LOCAL	0x80000000 //路由目的是本地接口的地址, ip_route_input_slow() __mkroute_output() ip_route_input_mc() __ip_route_output_key()

#define RTCF_NAT	(RTCF_DNAT|RTCF_SNAT)

#define RT_TOS(tos)	((tos)&IPTOS_TOS_MASK)

#endif /* _LINUX_IN_ROUTE_H */
