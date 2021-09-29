/*
 * NET		Generic infrastructure for INET connection oriented protocols.
 *
 *		Definitions for inet_connection_sock
 *
 * Authors:	Many people, see the TCP sources
 *
 * 		From code originally in TCP
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _INET_CONNECTION_SOCK_H
#define _INET_CONNECTION_SOCK_H

#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/kernel.h>

#include <net/inet_sock.h>
#include <net/request_sock.h>

/* Cancel timers, when they are not required. */
#undef INET_CSK_CLEAR_TIMERS

struct inet_bind_bucket;
struct tcp_congestion_ops;

/*
 * Pointers to address related TCP functions
 * (i.e. things that depend on the address family)
 */
//tcp层的实例是ipv4_specific，是tx方向传输层到网络层的关键
struct inet_connection_sock_af_ops {
	int	    (*queue_xmit)(struct sock *sk, struct sk_buff *skb, struct flowi *fl); //传输层到网络层接口，TCP设置为ip_queue_xmit
	void	    (*send_check)(struct sock *sk, struct sk_buff *skb); //计算传输层首部校验和, tcp初始化为tcp_v4_send_check
	int	    (*rebuild_header)(struct sock *sk); //如果对应的sock没有路由缓存向，为其选择，tcp设置为inet_sk_rebuild_header
	void	    (*sk_rx_dst_set)(struct sock *sk, const struct sk_buff *skb);
	int	    (*conn_request)(struct sock *sk, struct sk_buff *skb); //连接请求处理， tcp:tcp_v4_conn_request
	struct sock *(*syn_recv_sock)(const struct sock *sk, struct sk_buff *skb, //完成三次握手后调用该接口创建套接口,tcp_v4_syn_recv_sock
				      struct request_sock *req,
				      struct dst_entry *dst,
				      struct request_sock *req_unhash,
				      bool *own_req);
	u16	    net_header_len; //网络层首部长度
	u16	    net_frag_header_len;
	u16	    sockaddr_len; //网络层socket地址长度
	int	    (*setsockopt)(struct sock *sk, int level, int optname,
				  char __user *optval, unsigned int optlen);
	int	    (*getsockopt)(struct sock *sk, int level, int optname,
				  char __user *optval, int __user *optlen);
#ifdef CONFIG_COMPAT
	int	    (*compat_setsockopt)(struct sock *sk,
				int level, int optname,
				char __user *optval, unsigned int optlen);
	int	    (*compat_getsockopt)(struct sock *sk,
				int level, int optname,
				char __user *optval, int __user *optlen);
#endif
	void	    (*addr2sockaddr)(struct sock *sk, struct sockaddr *); 
	void	    (*mtu_reduced)(struct sock *sk);
};

/** inet_connection_sock - INET connection oriented sock
 *
 * @icsk_accept_queue:	   FIFO of established children , 完成3次握手的accept队列？？？ 如果是FAST OPEN，那么没有完成也会挂上来
 * @icsk_bind_hash:	   Bind node
 * @icsk_timeout:	   Timeout
 * @icsk_retransmit_timer: Resend (no ack)
 * @icsk_rto:		   Retransmit timeout
 * @icsk_pmtu_cookie	   Last pmtu seen by socket
 * @icsk_ca_ops		   Pluggable congestion control hook
 * @icsk_af_ops		   Operations which are AF_INET{4,6} specific
 * @icsk_ulp_ops	   Pluggable ULP control hook
 * @icsk_ulp_data	   ULP private data
 * @icsk_clean_acked	   Clean acked data hook
 * @icsk_listen_portaddr_node	hash to the portaddr listener hashtable
 * @icsk_ca_state:	   Congestion control state
 * @icsk_retransmits:	   Number of unrecovered [RTO] timeouts
 * @icsk_pending:	   Scheduled timer event
 * @icsk_backoff:	   Backoff
 * @icsk_syn_retries:      Number of allowed SYN (or equivalent) retries
 * @icsk_probes_out:	   unanswered 0 window probes
 * @icsk_ext_hdr_len:	   Network protocol overhead (IP/IPv6 options)
 * @icsk_ack:		   Delayed ACK control data
 * @icsk_mtup;		   MTU probing control data
 */
struct inet_connection_sock {
	/* inet_sock has to be the first member! */
	struct inet_sock	  icsk_inet;
	struct request_sock_queue icsk_accept_queue; //TCP层完成了三次握手后，会创建一个sock结构存档到这里, 等待accept来获取
	struct inet_bind_bucket	  *icsk_bind_hash; //绑定的端口信息
	unsigned long		  icsk_timeout; //即超时重传时刻, 一般等于jiffies + icsk_rto
 	struct timer_list	  icsk_retransmit_timer; //重传定时器或持续定时器，使用icsk_pending来区分%ICSK_TIME_RETRANS, 参考%tcp_init_xmit_timers
 	struct timer_list	  icsk_delack_timer; //延迟ack定时器
	__u32			  icsk_rto; //RTO
	__u32			  icsk_pmtu_cookie; //最近一次更新的PMTU
	const struct tcp_congestion_ops *icsk_ca_ops; //指向实现某个拥塞控制算法的指针，LINUX支持多种，用户也可以自己编写后加载到内核中
	const struct inet_connection_sock_af_ops *icsk_af_ops;  //向网络层发送的接口，tcp的该成员是ipv4_specific
	const struct tcp_ulp_ops  *icsk_ulp_ops; //ulp相关内容
	void			  *icsk_ulp_data;
	void (*icsk_clean_acked)(struct sock *sk, u32 acked_seq);
	struct hlist_node         icsk_listen_portaddr_node;
	unsigned int		  (*icsk_sync_mss)(struct sock *sk, u32 pmtu); //根据pmtu同步本地MSS的函数指针, 在加载tcp协议模块的时候，在tcp_v4_init_sock中被初始化为tcp_sync_mss
	__u8			  icsk_ca_state:6, //拥塞控制状态机的状态
				  icsk_ca_setsockopt:1,
				  icsk_ca_dst_locked:1;
	__u8			  icsk_retransmits; //超时重传的次数
	__u8			  icsk_pending; //区分重传定时器和持续定时器，
	__u8			  icsk_backoff; //用来计算下一个持续定时器设定值的指数退避算法指数
	__u8			  icsk_syn_retries; //最大的重发syn或syn_ack的次数
	__u8			  icsk_probes_out; //持续定时器或保活定时器周期性发出去但是没有被确认的TCP段，接收到ack后清零
	__u16			  icsk_ext_hdr_len; //ip首部选项长度
	struct {
		__u8		  pending;	 /* ACK is pending,标识当前ack发送的紧急程度，在调用send的时候会检测该状态，如果需要就立即发送ack，%ICSK_ACK_SCHED			   */
		__u8		  quick;	 /* Scheduled number of quick acks,快速ack模式，可以发送的ack数目	   */
		__u8		  pingpong;	 /* The session is interactive,标识是否开启了快速ack模式, pingpong为0表示快速确认，见tcp_enter_quickack_mode		   */
		__u8		  blocked;	 /* Delayed ACK was blocked by socket lock, socket被进程占据了，现在不能立即发送ack，有机会立即发送 */
		__u32		  ato;		 /* Predicted tick of soft clock, 延时确认定时器的估值	   */
		unsigned long	  timeout;	 /* Currently scheduled timeout,当前的延时确认时间		   */
		__u32		  lrcvtime;	 /* timestamp of last received data packet,最近一次接受到数据包的时间 */
		__u16		  last_seg_size; /* Size of last incoming segment, 最后一个接收到的端长度，可以计算rcv_mss	   */
		__u16		  rcv_mss;	 /* MSS used for delayed ACK decisions	   */
	} icsk_ack;
	struct {
		int		  enabled;

		/* Range of MTUs to search */
		int		  search_high;
		int		  search_low;

		/* Information on the current probe. */
		int		  probe_size; //当前探测值，探测结束后置0

		u32		  probe_timestamp;
	} icsk_mtup; //pmtu相关
	u32			  icsk_user_timeout;

	u64			  icsk_ca_priv[88 / sizeof(u64)]; //关于拥塞控制算法的各种私有数据
#define ICSK_CA_PRIV_SIZE      (11 * sizeof(u64))
};

#define ICSK_TIME_RETRANS	1	/* Retransmit timer */
#define ICSK_TIME_DACK		2	/* Delayed ack timer */
#define ICSK_TIME_PROBE0	3	/* Zero window probe timer */
#define ICSK_TIME_EARLY_RETRANS 4	/* Early retransmit timer */
#define ICSK_TIME_LOSS_PROBE	5	/* Tail loss probe timer , 与retransmit公用定时器 */
#define ICSK_TIME_REO_TIMEOUT	6	/* Reordering timer */

static inline struct inet_connection_sock *inet_csk(const struct sock *sk)
{
	return (struct inet_connection_sock *)sk;
}

static inline void *inet_csk_ca(const struct sock *sk)
{
	return (void *)inet_csk(sk)->icsk_ca_priv;
}

struct sock *inet_csk_clone_lock(const struct sock *sk,
				 const struct request_sock *req,
				 const gfp_t priority);

enum inet_csk_ack_state_t {
	ICSK_ACK_SCHED	= 1,  //有ack需要发送，是立即发送还是delay需要看其他标志，是发送ack的前提，在接收到有负荷的TCP包后，会设置
	ICSK_ACK_TIMER  = 2, //延时发送ack定时器已经启动
	ICSK_ACK_PUSHED = 4, //如果pingpong是0，立即发送ack, pingpong为0表示是快速ack阶段
	ICSK_ACK_PUSHED2 = 8, //无条件立即发送ack
	ICSK_ACK_NOW = 16	/* Send the next ACK immediately (once) */
};

void inet_csk_init_xmit_timers(struct sock *sk,
			       void (*retransmit_handler)(struct timer_list *),
			       void (*delack_handler)(struct timer_list *),
			       void (*keepalive_handler)(struct timer_list *));
void inet_csk_clear_xmit_timers(struct sock *sk);

static inline void inet_csk_schedule_ack(struct sock *sk)
{
	inet_csk(sk)->icsk_ack.pending |= ICSK_ACK_SCHED;
}

static inline int inet_csk_ack_scheduled(const struct sock *sk)
{
	return inet_csk(sk)->icsk_ack.pending & ICSK_ACK_SCHED;
}

static inline void inet_csk_delack_init(struct sock *sk)
{
	memset(&inet_csk(sk)->icsk_ack, 0, sizeof(inet_csk(sk)->icsk_ack));
}

void inet_csk_delete_keepalive_timer(struct sock *sk);
void inet_csk_reset_keepalive_timer(struct sock *sk, unsigned long timeout);

static inline void inet_csk_clear_xmit_timer(struct sock *sk, const int what)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	if (what == ICSK_TIME_RETRANS || what == ICSK_TIME_PROBE0) {
		icsk->icsk_pending = 0;
#ifdef INET_CSK_CLEAR_TIMERS
		sk_stop_timer(sk, &icsk->icsk_retransmit_timer);
#endif
	} else if (what == ICSK_TIME_DACK) {
		icsk->icsk_ack.blocked = icsk->icsk_ack.pending = 0;
#ifdef INET_CSK_CLEAR_TIMERS
		sk_stop_timer(sk, &icsk->icsk_delack_timer);
#endif
	} else {
		pr_debug("inet_csk BUG: unknown timer value\n");
	}
}

/*
 *	Reset the retransmission timer
 */
static inline void inet_csk_reset_xmit_timer(struct sock *sk, const int what,
					     unsigned long when,
					     const unsigned long max_when)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	if (when > max_when) {
		pr_debug("reset_xmit_timer: sk=%p %d when=0x%lx, caller=%p\n",
			 sk, what, when, (void *)_THIS_IP_);
		when = max_when;
	}

	if (what == ICSK_TIME_RETRANS || what == ICSK_TIME_PROBE0 ||
	    what == ICSK_TIME_EARLY_RETRANS || what == ICSK_TIME_LOSS_PROBE ||
	    what == ICSK_TIME_REO_TIMEOUT) {
		icsk->icsk_pending = what;
		icsk->icsk_timeout = jiffies + when;
		sk_reset_timer(sk, &icsk->icsk_retransmit_timer, icsk->icsk_timeout);
	} else if (what == ICSK_TIME_DACK) {
		icsk->icsk_ack.pending |= ICSK_ACK_TIMER;
		icsk->icsk_ack.timeout = jiffies + when;
		sk_reset_timer(sk, &icsk->icsk_delack_timer, icsk->icsk_ack.timeout);
	} else {
		pr_debug("inet_csk BUG: unknown timer value\n");
	}
}

static inline unsigned long
inet_csk_rto_backoff(const struct inet_connection_sock *icsk,
		     unsigned long max_when)
{
        u64 when = (u64)icsk->icsk_rto << icsk->icsk_backoff;

        return (unsigned long)min_t(u64, when, max_when);
}

struct sock *inet_csk_accept(struct sock *sk, int flags, int *err, bool kern);

int inet_csk_get_port(struct sock *sk, unsigned short snum);

struct dst_entry *inet_csk_route_req(const struct sock *sk, struct flowi4 *fl4,
				     const struct request_sock *req);
struct dst_entry *inet_csk_route_child_sock(const struct sock *sk,
					    struct sock *newsk,
					    const struct request_sock *req);

struct sock *inet_csk_reqsk_queue_add(struct sock *sk,
				      struct request_sock *req,
				      struct sock *child);
void inet_csk_reqsk_queue_hash_add(struct sock *sk, struct request_sock *req,
				   unsigned long timeout);
struct sock *inet_csk_complete_hashdance(struct sock *sk, struct sock *child,
					 struct request_sock *req,
					 bool own_req);

static inline void inet_csk_reqsk_queue_added(struct sock *sk)
{
	reqsk_queue_added(&inet_csk(sk)->icsk_accept_queue);
}

static inline int inet_csk_reqsk_queue_len(const struct sock *sk)
{
	return reqsk_queue_len(&inet_csk(sk)->icsk_accept_queue);
}

//已经建立的连接超过backlog了
static inline int inet_csk_reqsk_queue_is_full(const struct sock *sk)
{
	return inet_csk_reqsk_queue_len(sk) >= sk->sk_max_ack_backlog;
}

void inet_csk_reqsk_queue_drop(struct sock *sk, struct request_sock *req);
void inet_csk_reqsk_queue_drop_and_put(struct sock *sk, struct request_sock *req);

void inet_csk_destroy_sock(struct sock *sk);
void inet_csk_prepare_forced_close(struct sock *sk);

/*
 * LISTEN is a special case for poll..
 */
static inline __poll_t inet_csk_listen_poll(const struct sock *sk)
{
	return !reqsk_queue_empty(&inet_csk(sk)->icsk_accept_queue) ?
			(EPOLLIN | EPOLLRDNORM) : 0;
}

int inet_csk_listen_start(struct sock *sk, int backlog);
void inet_csk_listen_stop(struct sock *sk);

void inet_csk_addr2sockaddr(struct sock *sk, struct sockaddr *uaddr);

int inet_csk_compat_getsockopt(struct sock *sk, int level, int optname,
			       char __user *optval, int __user *optlen);
int inet_csk_compat_setsockopt(struct sock *sk, int level, int optname,
			       char __user *optval, unsigned int optlen);

struct dst_entry *inet_csk_update_pmtu(struct sock *sk, u32 mtu);
#endif /* _INET_CONNECTION_SOCK_H */
