/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NETLINK_H
#define __LINUX_NETLINK_H


#include <linux/capability.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <net/scm.h>
#include <uapi/linux/netlink.h>

struct net;

static inline struct nlmsghdr *nlmsg_hdr(const struct sk_buff *skb)
{
	return (struct nlmsghdr *)skb->data;
}

enum netlink_skb_flags {
	NETLINK_SKB_MMAPED	= 0x1,	/* Packet data is mmaped */
	NETLINK_SKB_TX		= 0x2,	/* Packet was sent by userspace */
	NETLINK_SKB_DELIVERED	= 0x4,	/* Packet was delivered */
	NETLINK_SKB_DST		= 0x8,	/* Dst set in sendto or sendmsg */
};

struct netlink_skb_parms {
	struct scm_creds	creds;		/* Skb credentials	*/
	__u32			portid;
	__u32			dst_group;
	__u32			flags;
	struct sock		*sk;
	bool			nsid_is_set;
	int			nsid;
};

#define NETLINK_CB(skb)		(*(struct netlink_skb_parms*)&((skb)->cb))
#define NETLINK_CREDS(skb)	(&NETLINK_CB((skb)).creds)


extern void netlink_table_grab(void);
extern void netlink_table_ungrab(void);

#define NL_CFG_F_NONROOT_RECV	(1 << 0)
#define NL_CFG_F_NONROOT_SEND	(1 << 1)

/* optional Netlink kernel configuration parameters */
struct netlink_kernel_cfg {
	unsigned int	groups;
	unsigned int	flags;
	void		(*input)(struct sk_buff *skb);
	struct mutex	*cb_mutex;
	int		(*bind)(struct net *net, int group);
	void		(*unbind)(struct net *net, int group);
	bool		(*compare)(struct net *net, struct sock *sk);
};

extern struct sock *__netlink_kernel_create(struct net *net, int unit,
					    struct module *module,
					    struct netlink_kernel_cfg *cfg);
static inline struct sock *
netlink_kernel_create(struct net *net, int unit, struct netlink_kernel_cfg *cfg)
{
	return __netlink_kernel_create(net, unit, THIS_MODULE, cfg);
}

/* this can be increased when necessary - don't expose to userland */
#define NETLINK_MAX_COOKIE_LEN	20

/**
 * struct netlink_ext_ack - netlink extended ACK report struct
 * @_msg: message string to report - don't access directly, use
 *	%NL_SET_ERR_MSG
 * @bad_attr: attribute with error
 * @cookie: cookie data to return to userspace (for success)
 * @cookie_len: actual cookie data length
 */
struct netlink_ext_ack {
	const char *_msg;
	const struct nlattr *bad_attr;
	u8 cookie[NETLINK_MAX_COOKIE_LEN];
	u8 cookie_len;
};

/* Always use this macro, this allows later putting the
 * message into a separate section or such for things
 * like translation or listing all possible messages.
 * Currently string formatting is not supported (due
 * to the lack of an output buffer.)
 */
#define NL_SET_ERR_MSG(extack, msg) do {		\
	static const char __msg[] = (msg);		\
	struct netlink_ext_ack *__extack = (extack);	\
							\
	if (__extack)					\
		__extack->_msg = __msg;			\
} while (0)

#define NL_SET_ERR_MSG_MOD(extack, msg)			\
	NL_SET_ERR_MSG((extack), KBUILD_MODNAME ": " msg)

#define NL_SET_BAD_ATTR(extack, attr) do {		\
	if ((extack))					\
		(extack)->bad_attr = (attr);		\
} while (0)

#define NL_SET_ERR_MSG_ATTR(extack, attr, msg) do {	\
	static const char __msg[] = (msg);		\
	struct netlink_ext_ack *__extack = (extack);	\
							\
	if (__extack) {					\
		__extack->_msg = __msg;			\
		__extack->bad_attr = (attr);		\
	}						\
} while (0)

extern void netlink_kernel_release(struct sock *sk);
extern int __netlink_change_ngroups(struct sock *sk, unsigned int groups);
extern int netlink_change_ngroups(struct sock *sk, unsigned int groups);
extern void __netlink_clear_multicast_users(struct sock *sk, unsigned int group);
extern void netlink_ack(struct sk_buff *in_skb, struct nlmsghdr *nlh, int err,
			const struct netlink_ext_ack *extack);
extern int netlink_has_listeners(struct sock *sk, unsigned int group);

extern int netlink_unicast(struct sock *ssk, struct sk_buff *skb, __u32 portid, int nonblock);
extern int netlink_broadcast(struct sock *ssk, struct sk_buff *skb, __u32 portid,
			     __u32 group, gfp_t allocation);
extern int netlink_broadcast_filtered(struct sock *ssk, struct sk_buff *skb,
	__u32 portid, __u32 group, gfp_t allocation,
	int (*filter)(struct sock *dsk, struct sk_buff *skb, void *data),
	void *filter_data);
extern int netlink_set_err(struct sock *ssk, __u32 portid, __u32 group, int code);
extern int netlink_register_notifier(struct notifier_block *nb);
extern int netlink_unregister_notifier(struct notifier_block *nb);

/* finegrained unicast helpers: */
struct sock *netlink_getsockbyfilp(struct file *filp);
int netlink_attachskb(struct sock *sk, struct sk_buff *skb,
		      long *timeo, struct sock *ssk);
void netlink_detachskb(struct sock *sk, struct sk_buff *skb);
int netlink_sendskb(struct sock *sk, struct sk_buff *skb);

static inline struct sk_buff *
netlink_skb_clone(struct sk_buff *skb, gfp_t gfp_mask)
{
	struct sk_buff *nskb;

	nskb = skb_clone(skb, gfp_mask);
	if (!nskb)
		return NULL;

	/* This is a large skb, set destructor callback to release head */
	if (is_vmalloc_addr(skb->head))
		nskb->destructor = skb->destructor;

	return nskb;
}

/*
 *	skb should fit one page. This choice is good for headerless malloc.
 *	But we should limit to 8K so that userspace does not have to
 *	use enormous buffer sizes on recvmsg() calls just to avoid
 *	MSG_TRUNC when PAGE_SIZE is very large.
 */
#if PAGE_SIZE < 8192UL
#define NLMSG_GOODSIZE	SKB_WITH_OVERHEAD(PAGE_SIZE)
#else
#define NLMSG_GOODSIZE	SKB_WITH_OVERHEAD(8192UL)
#endif

#define NLMSG_DEFAULT_SIZE (NLMSG_GOODSIZE - NLMSG_HDRLEN)


struct netlink_callback {
	struct sk_buff		*skb;
	const struct nlmsghdr	*nlh;
	int			(*start)(struct netlink_callback *);
	int			(*dump)(struct sk_buff * skb,
					struct netlink_callback *cb);
	int			(*done)(struct netlink_callback *cb);
	void			*data;
	/* the module that dump function belong to */
	struct module		*module;
	u16			family;
	u16			min_dump_alloc;
	bool			strict_check;
	unsigned int		prev_seq, seq;
	long			args[6];
};

struct netlink_notify {
	struct net *net;
	u32 portid;
	int protocol;
};

struct nlmsghdr *
__nlmsg_put(struct sk_buff *skb, u32 portid, u32 seq, int type, int len, int flags);

struct netlink_dump_control {
	int (*start)(struct netlink_callback *);
	int (*dump)(struct sk_buff *skb, struct netlink_callback *);
	int (*done)(struct netlink_callback *);
	void *data;
	struct module *module;
	u16 min_dump_alloc;
};

extern int __netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
				const struct nlmsghdr *nlh,
				struct netlink_dump_control *control);
static inline int netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
				     const struct nlmsghdr *nlh,
				     struct netlink_dump_control *control)
{
	if (!control->module)
		control->module = THIS_MODULE;

	return __netlink_dump_start(ssk, skb, nlh, control);
}

struct netlink_tap {
	struct net_device *dev;
	struct module *module;
	struct list_head list;
};

extern int netlink_add_tap(struct netlink_tap *nt);
extern int netlink_remove_tap(struct netlink_tap *nt);

bool __netlink_ns_capable(const struct netlink_skb_parms *nsp,
			  struct user_namespace *ns, int cap);
bool netlink_ns_capable(const struct sk_buff *skb,
			struct user_namespace *ns, int cap);
bool netlink_capable(const struct sk_buff *skb, int cap);
bool netlink_net_capable(const struct sk_buff *skb, int cap);

#endif	/* __LINUX_NETLINK_H */
