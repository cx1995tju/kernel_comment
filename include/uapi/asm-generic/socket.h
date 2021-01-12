/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_SOCKET_H
#define __ASM_GENERIC_SOCKET_H

#include <asm/sockios.h>

/* For setsockopt(2) */
#define SOL_SOCKET	1

#define SO_DEBUG	1 //开启后，内核调用SOCK_DEBUG宏向屏幕或者日志输出调试信息
#define SO_REUSEADDR	2 //设置是否复用端口， <1 不能复用， =1, 能够复用，与之复用的socket也必须设置为1， 》1, 肯定能复用
#define SO_TYPE		3 //获取套接口类型
#define SO_ERROR	4 //获取套接字错误代码
#define SO_DONTROUTE	5 //使能后，不需要查询路由表，直接通过套接口绑定的接口将数据传送出去
#define SO_BROADCAST	6 //套接口处于广播模式
#define SO_SNDBUF	7 //设置缓冲上限, 不超过tcp_wmem
#define SO_RCVBUF	8
#define SO_SNDBUFFORCE	32 //设置缓冲上限
#define SO_RCVBUFFORCE	33
#define SO_KEEPALIVE	9 //开启TCP 保活
#define SO_OOBINLINE	10 //可以在带外数据中加入正常数据流，或普通数据流中接收带外数据
#define SO_NO_CHECK	11 //决定对RAW UDP是否校验和
#define SO_PRIORITY	12 //设置QoS
#define SO_LINGER	13 // 设置LINGER值
#define SO_BSDCOMPAT	14
#define SO_REUSEPORT	15
#ifndef SO_PASSCRED /* powerpc only differs in these */
#define SO_PASSCRED	16
#define SO_PEERCRED	17
#define SO_RCVLOWAT	18 //接收下限设置
#define SO_SNDLOWAT	19
#define SO_RCVTIMEO	20 //设置接收超时值
#define SO_SNDTIMEO	21
#endif

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		22
#define SO_SECURITY_ENCRYPTION_TRANSPORT	23
#define SO_SECURITY_ENCRYPTION_NETWORK		24

#define SO_BINDTODEVICE	25 //将套接字绑定到指定的网络设备

/* Socket filtering */
#define SO_ATTACH_FILTER	26
#define SO_DETACH_FILTER	27
#define SO_GET_FILTER		SO_ATTACH_FILTER

#define SO_PEERNAME		28 //获取peer的地址和端口信息
#define SO_TIMESTAMP		29 //将数据包接收时间作为时间戳
#define SCM_TIMESTAMP		SO_TIMESTAMP

#define SO_ACCEPTCONN		30 //是否处于listen状态

#define SO_PEERSEC		31
#define SO_PASSSEC		34
#define SO_TIMESTAMPNS		35
#define SCM_TIMESTAMPNS		SO_TIMESTAMPNS

#define SO_MARK			36

#define SO_TIMESTAMPING		37
#define SCM_TIMESTAMPING	SO_TIMESTAMPING

#define SO_PROTOCOL		38
#define SO_DOMAIN		39

#define SO_RXQ_OVFL             40

#define SO_WIFI_STATUS		41
#define SCM_WIFI_STATUS	SO_WIFI_STATUS
#define SO_PEEK_OFF		42

/* Instruct lower device to use last 4-bytes of skb data as FCS */
#define SO_NOFCS		43

#define SO_LOCK_FILTER		44

#define SO_SELECT_ERR_QUEUE	45

#define SO_BUSY_POLL		46

#define SO_MAX_PACING_RATE	47

#define SO_BPF_EXTENSIONS	48

#define SO_INCOMING_CPU		49

#define SO_ATTACH_BPF		50
#define SO_DETACH_BPF		SO_DETACH_FILTER

#define SO_ATTACH_REUSEPORT_CBPF	51
#define SO_ATTACH_REUSEPORT_EBPF	52

#define SO_CNX_ADVICE		53

#define SCM_TIMESTAMPING_OPT_STATS	54

#define SO_MEMINFO		55

#define SO_INCOMING_NAPI_ID	56

#define SO_COOKIE		57

#define SCM_TIMESTAMPING_PKTINFO	58

#define SO_PEERGROUPS		59

#define SO_ZEROCOPY		60

#define SO_TXTIME		61
#define SCM_TXTIME		SO_TXTIME

#endif /* __ASM_GENERIC_SOCKET_H */
