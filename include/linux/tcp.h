/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol.
 *
 * Version:	@(#)tcp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_TCP_H
#define _LINUX_TCP_H


#include <linux/skbuff.h>
#include <linux/win_minmax.h>
#include <net/sock.h>
#include <net/inet_connection_sock.h>
#include <net/inet_timewait_sock.h>
#include <uapi/linux/tcp.h>

static inline struct tcphdr *tcp_hdr(const struct sk_buff *skb)
{
	return (struct tcphdr *)skb_transport_header(skb);
}

static inline unsigned int __tcp_hdrlen(const struct tcphdr *th)
{
	return th->doff * 4;
}

static inline unsigned int tcp_hdrlen(const struct sk_buff *skb)
{
	return __tcp_hdrlen(tcp_hdr(skb));
}

static inline struct tcphdr *inner_tcp_hdr(const struct sk_buff *skb)
{
	return (struct tcphdr *)skb_inner_transport_header(skb);
}

static inline unsigned int inner_tcp_hdrlen(const struct sk_buff *skb)
{
	return inner_tcp_hdr(skb)->doff * 4;
}

static inline unsigned int tcp_optlen(const struct sk_buff *skb)
{
	return (tcp_hdr(skb)->doff - 5) * 4;
}

/* TCP Fast Open */
#define TCP_FASTOPEN_COOKIE_MIN	4	/* Min Fast Open Cookie size in bytes */
#define TCP_FASTOPEN_COOKIE_MAX	16	/* Max Fast Open Cookie size in bytes */
#define TCP_FASTOPEN_COOKIE_SIZE 8	/* the size employed by this impl. */

/* TCP Fast Open Cookie as stored in memory */
struct tcp_fastopen_cookie {
	union {
		u8	val[TCP_FASTOPEN_COOKIE_MAX];
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr addr;
#endif
	};
	s8	len;
	bool	exp;	/* In RFC6994 experimental option format */
};

/* This defines a selective acknowledgement block. */
struct tcp_sack_block_wire {
	__be32	start_seq;
	__be32	end_seq;
};

struct tcp_sack_block {
	u32	start_seq;
	u32	end_seq;
};

/*These are used to set the sack_ok field in struct tcp_options_received */
#define TCP_SACK_SEEN     (1 << 0)   /*1 = peer is SACK capable, */
#define TCP_DSACK_SEEN    (1 << 2)   /*1 = DSACK was received from peer*/

/* 保存接收到的TCP选项结构 */
struct tcp_options_received {
/*	PAWS/RTTM data	*/
	int	ts_recent_stamp;/* Time we stored ts_recent (for aging) */
	u32	ts_recent;	/* Time stamp to echo next		*/
	u32	rcv_tsval;	/* Time stamp value             	*/
	u32	rcv_tsecr;	/* Time stamp echo reply        	*/
	u16 	saw_tstamp : 1,	/* Saw TIMESTAMP on last packet		*/
		tstamp_ok : 1,	/* TIMESTAMP seen on SYN packet		*/
		dsack : 1,	/* D-SACK is scheduled			*/
		wscale_ok : 1,	/* Wscale seen on SYN packet		*/
		sack_ok : 3,	/* SACK seen on SYN packet		*/
		smc_ok : 1,	/* SMC seen on SYN packet		*/
		snd_wscale : 4,	/* Window scaling received from sender	*/
		rcv_wscale : 4;	/* Window scaling to send to receiver	*/
	u8	num_sacks;	/* Number of SACK blocks		*/
	u16	user_mss;	/* mss requested by user in ioctl	*/
	u16	mss_clamp;	/* Maximal mss, negotiated at connection setup, 表示对端的MSS, 初始值是536,接收到对端通告后修正 */
};

static inline void tcp_clear_options(struct tcp_options_received *rx_opt)
{
	rx_opt->tstamp_ok = rx_opt->sack_ok = 0;
	rx_opt->wscale_ok = rx_opt->snd_wscale = 0;
#if IS_ENABLED(CONFIG_SMC)
	rx_opt->smc_ok = 0;
#endif
}

/* This is the max number of SACKS that we'll generate and process. It's safe
 * to increase this, although since:
 *   size = TCPOLEN_SACK_BASE_ALIGNED (4) + n * TCPOLEN_SACK_PERBLOCK (8)
 * only four options will fit in a standard TCP header */
#define TCP_NUM_SACKS 4

struct tcp_request_sock_ops;

/* 保存连接初始相关信息 */
struct tcp_request_sock {
	struct inet_request_sock 	req;
	const struct tcp_request_sock_ops *af_specific;
	u64				snt_synack; /* first SYNACK sent time */
	bool				tfo_listener;
	u32				txhash;
	u32				rcv_isn; //客户端初始序号
	u32				snt_isn; //服务端初始序号
	u32				ts_off; //用于随机化timestamp，避免timestamp被猜出来, timestamp-offset
	u32				last_oow_ack_time; /* last SYNACK */
	u32				rcv_nxt; /* the ack # by SYNACK. For
						  * FastOpen it's the seq#
						  * after data-in-SYN.
						  */
};

static inline struct tcp_request_sock *tcp_rsk(const struct request_sock *req)
{
	return (struct tcp_request_sock *)req;
}

struct tcp_sock {
	/* inet_connection_sock has to be the first member of tcp_sock */
	struct inet_connection_sock	inet_conn;
	u16	tcp_header_len;	/* Bytes of tcp header to send, tcp首部长度，含选项, TCP首部中的长度表示的是4B		*/
	u16	gso_segs;	/* Max number of segs per GSO packet	*/

/*
 *	Header prediction flags
 *	0x5?10 << 16 + snd_wnd in net byte order
 */
	__be32	pred_flags; //首部预测标志

/*
 *	RFC793 variables by their proper names. This means you can
 *	read the code and the spec side by side (and laugh ...)
 *	See RFC793 and RFC1122. The RFC writes these in capitals.
 */
	u64	bytes_received;	/* RFC4898 tcpEStatsAppHCThruOctetsReceived
				 * sum(delta(rcv_nxt)), or how many bytes
				 * were acked.
				 */
	u32	segs_in;	/* RFC4898 tcpEStatsPerfSegsIn
				 * total number of segments in.
				 */
	u32	data_segs_in;	/* RFC4898 tcpEStatsPerfDataSegsIn
				 * total number of data segments in.
				 */
 	u32	rcv_nxt;	/* What we want to receive next, 等待接收的下一个TCP序号，每接收一个TCP报文就更新 	*/
	u32	copied_seq;	/* Head of yet unread data	, 还没有复制到用户空间的数据的序号，但是已经被接收了 */ 
	u32	rcv_wup;	/* rcv_nxt on last window update sent	lastack? */
 	u32	snd_nxt;	/* Next sequence we send, 等待发送的下一个TCP报文段序号		*/
	u32	segs_out;	/* RFC4898 tcpEStatsPerfSegsOut
				 * The total number of segments sent.
				 */
	u32	data_segs_out;	/* RFC4898 tcpEStatsPerfDataSegsOut
				 * total number of data segments sent.
				 */
	u64	bytes_sent;	/* RFC4898 tcpEStatsPerfHCDataOctetsOut
				 * total number of data bytes sent.
				 */
	u64	bytes_acked;	/* RFC4898 tcpEStatsAppHCThruOctetsAcked, 启动了ABC后，在拥塞避免阶段保存已经确认的字节数目
				 * sum(delta(snd_una)), or how many bytes 参考tcp_snd_una_update
				 * were acked.
				 */
	u32	dsack_dups;	/* RFC4898 tcpEStatsStackDSACKDups
				 * total number of DSACK blocks received
				 */
 	u32	snd_una;	/* First byte we want an ack for, 第一个想要被ack的序号	*/
 	u32	snd_sml;	/* Last byte of the most recently transmitted small packet, 最近发送的小包的最后一个字节序号, 成功发送报文后，如果长度小于mss，则更新，主要用于判断是否启动nagle算法  */
	u32	rcv_tstamp;	/* timestamp of last received ACK (for keepalives), 最近一次接收到ack段的时间 */
	u32	lsndtime;	/* timestamp of last sent data packet (for restart window), 最后一次发送数据包的时间 */
	u32	last_oow_ack_time;  /* timestamp of last out-of-window ACK */
	u32	compressed_ack_rcv_nxt;

	u32	tsoffset;	/* timestamp offset */

	struct list_head tsq_node; /* anchor in tsq_tasklet.head list */
	struct list_head tsorted_sent_queue; /* time-sorted sent but un-SACKed skbs */

	u32	snd_wl1;	/* Sequence for window update, 记录更新发送窗口的那个ack端的序号，如果后续接收到的ack端大于snd_wl1，就需要更新窗口见tcp_may_update_window		*/
	u32	snd_wnd;	/* The window we expect to receive, 接收方提供的接收窗口大小，即发送方发送窗口大小	*/
	u32	max_window;	/* Maximal window ever seen from peer, 对端通告过的最大窗口值	tcp_may_update_window*/
	u32	mss_cache;	/* Cached effective mss, not including SACKS, 发送方当前有效的mss  */

	u32	window_clamp;	/* Maximal window to advertise. 滑动窗口大小不会超过这个值		,即开始的时候通告值 */
	u32	rcv_ssthresh;	/* Current window clamp, 当前接收窗口的阈值			*/

	/* Information of the most recently (s)acked skb */
	struct tcp_rack {
		u64 mstamp; /* (Re)sent time of the skb */
		u32 rtt_us;  /* Associated RTT */
		u32 end_seq; /* Ending TCP sequence of the skb */
		u32 last_delivered; /* tp->delivered at last reo_wnd adj */
		u8 reo_wnd_steps;   /* Allowed reordering window */
#define TCP_RACK_RECOVERY_THRESH 16
		u8 reo_wnd_persist:5, /* No. of recovery since last adj */
		   dsack_seen:1, /* Whether DSACK seen after last adj */
		   advanced:1;	 /* mstamp advanced since last lost marking */
	} rack;
	u16	advmss;		/* Advertised MSS, 通告对方的mss,其值来自与路由项中的metrics[RTAX_ADVMSS -1], 而路由项的MSS是通过网络设备接口的MTU减去TCP IP头部计算得到的			*/
	u8	compressed_ack;
	u32	chrono_start;	/* Start time in jiffies of a TCP chrono */
	u32	chrono_stat[3];	/* Time in jiffies for chrono_stat stats */
	u8	chrono_type:2,	/* current chronograph type */
		rate_app_limited:1,  /* rate_{delivered,interval_us} limited? */
		fastopen_connect:1, /* FASTOPEN_CONNECT sockopt */
		fastopen_no_cookie:1, /* Allow send/recv SYN+data without a cookie */
		is_sack_reneg:1,    /* in recovery from loss with SACK reneg? */
		unused:2;
	u8	nonagle     : 4,/* Disable Nagle algorithm?             %TCP_NAGLE_OFF*/
		thin_lto    : 1,/* Use linear timeouts for thin streams */
		recvmsg_inq : 1,/* Indicate # of bytes in queue upon recvmsg */
		repair      : 1, //TCP套接口热迁移相关
		frto        : 1;/* F-RTO (RFC5682) activated in CA_Loss 在Loss状态激活frto算法 */
	u8	repair_queue;
	u8	syn_data:1,	/* SYN includes data */
		syn_fastopen:1,	/* SYN includes Fast Open option */
		syn_fastopen_exp:1,/* SYN includes Fast Open exp. option */
		syn_fastopen_ch:1, /* Active TFO re-enabling probe */
		syn_data_acked:1,/* data in SYN is acked by SYN-ACK */
		save_syn:1,	/* Save headers of SYN packet */
		is_cwnd_limited:1,/* forward progress limited by snd_cwnd? */
		syn_smc:1;	/* SYN includes SMC */
	u32	tlp_high_seq;	/* snd_nxt at the time of TLP retransmit. tlp发送的时候，发送的数据 */

/* RTT measurement */
	u64	tcp_mstamp;	/* most recent packet received/sent */
	u32	srtt_us;	/* smoothed round trip time << 3 in usecs */
	u32	mdev_us;	/* medium deviation, rtt平均偏差			*/
	u32	mdev_max_us;	/* maximal mdev for the last rtt period	*/
	u32	rttvar_us;	/* smoothed mdev_max			*/
	u32	rtt_seq;	/* sequence number to update rttvar, 记录SND.UNA, 计算RTO的时候会用到	*/
	struct  minmax rtt_min;

	u32	packets_out;	/* Packets which are "in flight",	 //i.e. SND.NXT - SND.UNA	*/
	u32	retrans_out;	/* Retransmitted packets out, 重传没有得到确认的段		*/
	u32	max_packets_out;  /* max packets_out in last window */
	u32	max_packets_seq;  /* right edge of max_packets_out flight */

	u16	urg_data;	/* Saved octet of OOB data and control flags, 低8位存放紧急数据，高8位标识紧急数据相关状态 %TCP_URG_VALID */
	u8	ecn_flags;	/* ECN status bits., %TCP_ECN_OK			*/
	u8	keepalive_probes; /* num of allowed keep alive probes, 最大的保活探测次数	*/
	u32	reordering;	/* Packet reordering metric., 不支持sack的时候，是由于连接接收到dupack进入快速回复阶段的dupack阈值, 支持sack的时候，就是tcp流中可以重排序的数据段数目		, 路由缓存项中的reordering或者系统参数tcp_reordering初始化*/

	u32	reord_seen;	/* number of data packet reordering events */
	u32	snd_up;		/* Urgent pointer		*/

/*
 *      Options received (usually on last packet, some only on SYN packets).
 */
	struct tcp_options_received rx_opt; //接收到的tcp选项

/*
 *	Slow start and congestion control (see also Nagle, and Karn & Partridge)
 */
 	u32	snd_ssthresh;	/* Slow start size threshold		*/
 	u32	snd_cwnd;	/* Sending congestion window		*/
	u32	snd_cwnd_cnt;	/* Linear increase counter, 从上次拥塞窗口调整到目前为止接收的总ack数目，如果该值为0，表示已经调整了拥塞窗口，还没有接收到ack		参考 tcp_enter_loss*/
	u32	snd_cwnd_clamp; /* Do not allow snd_cwnd to grow above this, 允许的最大拥塞窗口值 */
	u32	snd_cwnd_used;
	u32	snd_cwnd_stamp; //最近一次检验拥塞窗口的时间
	u32	prior_cwnd;	/* cwnd right before starting loss recovery */
	u32	prr_delivered;	/* Number of newly delivered packets to
				 * receiver in Recovery. */
	u32	prr_out;	/* Total number of pkts sent during Recovery. */
	u32	delivered;	/* Total data packets delivered incl. rexmits */
	u32	delivered_ce;	/* Like the above but only ECE marked packets */
	u32	lost;		/* Total data packets lost incl. rexmits */
	u32	app_limited;	/* limited until "delivered" reaches this val */
	u64	first_tx_mstamp;  /* start of window send phase */
	u64	delivered_mstamp; /* time we reached "delivered" */
	u32	rate_delivered;    /* saved rate sample: packets delivered */
	u32	rate_interval_us;  /* saved rate sample: time elapsed */

 	u32	rcv_wnd;	/* Current receiver window,当前接收窗口大小		*/
	u32	write_seq;	/* Tail(+1) of data held in tcp send buffer, 已经加入发送队列中的最后一个字节序号 */
	u32	notsent_lowat;	/* TCP_NOTSENT_LOWAT */
	u32	pushed_seq;	/* Last pushed seq, required to talk to windows, 通常表示已经真正发送出去的最后一个字节序号， 有时表示期望发送出去的最后一个字节序号*/
	u32	lost_out;	/* Lost packets, 估计在网络中丢失的段			*/
	u32	sacked_out;	/* SACK'd packets, sack场景下表示sack块数目，非sack场景表示接收到的重复确认数目, sack计数器			*/

	struct hrtimer	pacing_timer; //参考tcp pacing功能
	struct hrtimer	compressed_ack_timer;

	/* from STCP, retrans queue hinting */
	struct sk_buff* lost_skb_hint;
	struct sk_buff *retransmit_skb_hint;

	/* OOO segments go in this rbtree. Socket lock must be held. */
	struct rb_root	out_of_order_queue; //乱序缓存队列, 现在是组织成红黑树的
	struct sk_buff	*ooo_last_skb; /* cache rb_last(out_of_order_queue) */

	/* SACKs data, these 2 need to be together (see tcp_options_write) */
	struct tcp_sack_block duplicate_sack[1]; /* D-SACK block */
	struct tcp_sack_block selective_acks[4]; /* The SACKS themselves, 存储sack信息*/

	struct tcp_sack_block recv_sack_cache[4]; //保存之前的sack段????, 参考tcp_sacktag_write_queue

	struct sk_buff *highest_sack;   /* skb just after the highest
					 * skb with SACKed bit set
					 * (validity guaranteed only if
					 * sacked_out > 0)
					 */

	int     lost_cnt_hint; //拥塞状态没有撤销或没有进入loss状态的时候，在重传队列，缓存上一次标记记分牌未丢失的最后一个段

	u32	prior_ssthresh; /* ssthresh saved at recovery start, 记录ssthresh值，用于拥塞撤销	*/
	u32	high_seq;	/* snd_nxt at onset of congestion, 拥塞开始的时候NXT值, 即表示重传队列的末尾, 见	tcp_init_cwnd_reduction*/

	u32	retrans_stamp;	/* Timestamp of the last retransmit, 主动连接的时候，记录第一个syn段发送时间，检测ack序号是否回绕, 数据传输阶段，记录上次重传阶段第一个重传段的时间，用来判断是否可以进行拥塞撤销
				 * also used in SYN-SENT to remember stamp of
				 * the first SYN. */
	u32	undo_marker;	/* snd_una upon a new recovery episode. 一个新的恢复阶段的snd_una 参考tcp_init_undo */
	int	undo_retrans;	/* number of undoable retransmissions. 这个才是undo的计数器，每次重传一个包增加1，每次收到DSACK(或者F-RTO)递减1，如果为0就可以undo了，说明重发的都白发了 */
/* 用于标记是否要执行撤销算法的。F-RTO算法进行超时处理的时候，或者进入Recovery进行重传，或者进入Loss开始慢启动的时候，记录SND.UNA,标记重传起点，后续可以使用其检测是否拥塞撤销 */
	u64	bytes_retrans;	/* RFC4898 tcpEStatsPerfOctetsRetrans
				 * Total data bytes retransmitted
				 */
	u32	total_retrans;	/* Total retransmits for entire connection, 整个连接的重传次数 */

	u32	urg_seq;	/* Seq of received urgent pointer */
	unsigned int		keepalive_time;	  /* time before keep alive takes place , 保活定时器启动阈值 */
	unsigned int		keepalive_intvl;  /* time interval between keep alive probes, 保活探测时间间隔 TCP_KEEPINTVL选项 */

	int			linger2;


/* Sock_ops bpf program related variables */
#ifdef CONFIG_BPF
	u8	bpf_sock_ops_cb_flags;  /* Control calling BPF programs
					 * values defined in uapi/linux/tcp.h
					 */
#define BPF_SOCK_OPS_TEST_FLAG(TP, ARG) (TP->bpf_sock_ops_cb_flags & ARG)
#else
#define BPF_SOCK_OPS_TEST_FLAG(TP, ARG) 0
#endif

/* Receiver side RTT estimation */
	u32 rcv_rtt_last_tsecr;
	struct {
		u32	rtt_us;
		u32	seq; //接收到的段没有时间戳的情况下，更新接收方rtt时的接收窗口右端序号
		u64	xime;
	} rcv_rtt_est; //存储接收方rtt估计值

/* Receiver queue space */
	struct {
		u32	space;
		u32	seq;
		u64	time;
	} rcvq_space; //调整TCP接收缓冲空间和接收窗口大小

/* TCP-specific MTU probe information. */
	struct {
		u32		  probe_seq_start;
		u32		  probe_seq_end;
	} mtu_probe;
	u32	mtu_info; /* We received an ICMP_FRAG_NEEDED / ICMPV6_PKT_TOOBIG
			   * while socket was owned by user.
			   */

#ifdef CONFIG_TCP_MD5SIG
/* TCP AF-Specific parts; only used by MD5 Signature support so far */
	const struct tcp_sock_af_ops	*af_specific;

/* TCP MD5 Signature Option information */
	struct tcp_md5sig_info	__rcu *md5sig_info;
#endif

/* TCP fastopen related information */
	struct tcp_fastopen_request *fastopen_req;
	/* fastopen_rsk points to request_sock that resulted in this big
	 * socket. Used to retransmit SYNACKs etc.
	 */
	struct request_sock *fastopen_rsk;
	u32	*saved_syn;
};

enum tsq_enum {
	TSQ_THROTTLED,
	TSQ_QUEUED,
	TCP_TSQ_DEFERRED,	   /* tcp_tasklet_func() found socket was owned */
	TCP_WRITE_TIMER_DEFERRED,  /* tcp_write_timer() found socket was owned */
	TCP_DELACK_TIMER_DEFERRED, /* tcp_delack_timer() found socket was owned */
	TCP_MTU_REDUCED_DEFERRED,  /* tcp_v{4|6}_err() could not call
				    * tcp_v{4|6}_mtu_reduced()
				    */
};

enum tsq_flags {
	TSQF_THROTTLED			= (1UL << TSQ_THROTTLED),
	TSQF_QUEUED			= (1UL << TSQ_QUEUED),
	TCPF_TSQ_DEFERRED		= (1UL << TCP_TSQ_DEFERRED),
	TCPF_WRITE_TIMER_DEFERRED	= (1UL << TCP_WRITE_TIMER_DEFERRED),
	TCPF_DELACK_TIMER_DEFERRED	= (1UL << TCP_DELACK_TIMER_DEFERRED),
	TCPF_MTU_REDUCED_DEFERRED	= (1UL << TCP_MTU_REDUCED_DEFERRED),
};

static inline struct tcp_sock *tcp_sk(const struct sock *sk)
{
	return (struct tcp_sock *)sk;
}

struct tcp_timewait_sock {
	struct inet_timewait_sock tw_sk;
#define tw_rcv_nxt tw_sk.__tw_common.skc_tw_rcv_nxt
#define tw_snd_nxt tw_sk.__tw_common.skc_tw_snd_nxt
	u32			  tw_rcv_wnd;
	u32			  tw_ts_offset;
	u32			  tw_ts_recent;

	/* The time we sent the last out-of-window ACK: */
	u32			  tw_last_oow_ack_time;

	int			  tw_ts_recent_stamp;
#ifdef CONFIG_TCP_MD5SIG
	struct tcp_md5sig_key	  *tw_md5_key;
#endif
};

static inline struct tcp_timewait_sock *tcp_twsk(const struct sock *sk)
{
	return (struct tcp_timewait_sock *)sk;
}

static inline bool tcp_passive_fastopen(const struct sock *sk)
{
	return (sk->sk_state == TCP_SYN_RECV &&
		tcp_sk(sk)->fastopen_rsk != NULL);
}

static inline void fastopen_queue_tune(struct sock *sk, int backlog)
{
	struct request_sock_queue *queue = &inet_csk(sk)->icsk_accept_queue;
	int somaxconn = READ_ONCE(sock_net(sk)->core.sysctl_somaxconn);

	queue->fastopenq.max_qlen = min_t(unsigned int, backlog, somaxconn);
}

static inline void tcp_move_syn(struct tcp_sock *tp,
				struct request_sock *req)
{
	tp->saved_syn = req->saved_syn;
	req->saved_syn = NULL;
}

static inline void tcp_saved_syn_free(struct tcp_sock *tp)
{
	kfree(tp->saved_syn);
	tp->saved_syn = NULL;
}

struct sk_buff *tcp_get_timestamping_opt_stats(const struct sock *sk);

static inline u16 tcp_mss_clamp(const struct tcp_sock *tp, u16 mss)
{
	/* We use READ_ONCE() here because socket might not be locked.
	 * This happens for listeners.
	 */
	u16 user_mss = READ_ONCE(tp->rx_opt.user_mss);

	return (user_mss && user_mss < mss) ? user_mss : mss;
}
#endif	/* _LINUX_TCP_H */
