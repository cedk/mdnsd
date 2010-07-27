/*
 * Copyright (c) 2010 Christiano F. Haesbaert <haesbaert@haesbaert.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MDNSD_H_
#define	_MDNSD_H_

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>

#include <event.h>
#include <imsg.h>

#include "mdns.h"
#include "control.h"

#define MDNSD_USER		"_mdnsd"
#define ALL_MDNS_DEVICES	"224.0.0.251"
#define MDNS_ADDRT		0xFB0000E0 /* the in_addr for 224.0.0.251 */
#define MDNS_TTL		255
#define MDNS_PORT		5353
#define TTL_HNAME		120
#define MDNS_QUERY		0
#define MDNS_RESPONSE		1

#define ANSWERS(q, rr)							\
	((((q)->qtype == T_ANY) || ((q)->qtype == (rr)->type))    &&	\
	    (q)->qclass == (rr)->class                            &&	\
	    strcmp((q)->dname, (rr)->dname) == 0)

#define RR_UNIQ(rr) (rr->cacheflush)

struct rrt_node {
	RB_ENTRY(rrt_node)      entry;
	LIST_HEAD(rr_head, rr) 	hrr; /* head rr */
};
RB_HEAD(rrt_tree, rrt_node);
RB_PROTOTYPE(rrt_tree, rrt_node, entry, rrt_compare);

struct rr {
	LIST_ENTRY(rr)		centry; /* cache entry */
	LIST_ENTRY(rr)		pentry; /* packet entry */
	char			dname[MAXHOSTNAMELEN];
	u_int16_t		type;
	int			cacheflush;
	u_int16_t		class;
	u_int32_t		ttl;
	union {
		struct in_addr	A;
		char		CNAME[MAXHOSTNAMELEN];
		char		PTR[MAXHOSTNAMELEN];
		char		NS[MAXHOSTNAMELEN];
		char		TXT[MAX_CHARSTR];
		struct srv	SRV;
		struct hinfo	HINFO;

	} rdata;
	int		revision;	/* at 80% of ttl, then 90% and 95% */
	struct event	rev_timer;	/* cache revision timer */
};

struct pkt {
	HEADER	h;
	LIST_HEAD(, question) qlist;  /* Question section */
	LIST_HEAD(, rr)       anlist; /* Answer section */
	LIST_HEAD(, rr)       nslist; /* Authority section */
	LIST_HEAD(, rr)       arlist; /* Additional section */
};

struct question {
	LIST_ENTRY(question)	entry;
	char			dname[MAXHOSTNAMELEN];
	u_int16_t		qtype;
	u_int16_t		qclass;
	int			uniresp;
};

enum query_style {
	QUERY_LKUP,
	QUERY_BROWSE,
};

struct query {
	int			active;
	enum query_style	style;
	int			sleep;
	struct question		mq;
	struct event		timer;
};

enum publish_state {
	PUB_INITIAL,
	PUB_PROBE,
	PUB_ANNOUNCE,
	PUB_DONE
};

struct publish {
	LIST_ENTRY(publish)	entry;
	struct pkt		pkt;
	struct event		timer;	/* probe/announce timer */
	struct iface	       *iface;
	enum publish_state	state;
	int			sent;	/* how many send packets */
	unsigned long		id;	/* unique id */
};

struct kif {
	char		ifname[IF_NAMESIZE];
	u_int64_t	baudrate;
	int		flags;
	int		mtu;
	u_short		ifindex;
	u_int8_t	media_type;
	u_int8_t	link_state;
};

/* interface states */
#define IF_STA_DOWN		0x01
#define IF_STA_ACTIVE		(~IF_STA_DOWN)
#define IF_STA_ANY		0x7f

/* interface events */
enum iface_event {
	IF_EVT_NOTHING,
	IF_EVT_UP,
	IF_EVT_DOWN
};

/* interface actions */
enum iface_action {
	IF_ACT_NOTHING,
	IF_ACT_STRT,
	IF_ACT_RST
};

/* interface types */
enum iface_type {
	IF_TYPE_POINTOPOINT,
	IF_TYPE_BROADCAST,
	IF_TYPE_NBMA,
	IF_TYPE_POINTOMULTIPOINT
};

struct iface {
	LIST_ENTRY(iface)	 entry;
	struct rrt_tree		 rrt;
	char			 name[IF_NAMESIZE];
	struct in_addr		 addr;
	struct in_addr		 dst;
	struct in_addr		 mask;
	u_int64_t		 baudrate;
	time_t			 uptime;
	u_int			 mtu;
	int			 fd; /* XXX */
	int			 state;
	u_short			 ifindex;
	u_int16_t		 cost;
	u_int16_t		 flags;
	enum iface_type		 type;
	u_int8_t		 linktype;
	u_int8_t		 media_type;
	u_int8_t		 linkstate;
};

/* interface.c */
const char	*if_action_name(int);
const char	*if_event_name(int);
int		 if_act_reset(struct iface *);
int		 if_act_start(struct iface *);
int		 if_fsm(struct iface *, enum iface_event);
int		 if_join_group(struct iface *, struct in_addr *);
int		 if_leave_group(struct iface *, struct in_addr *);
int		 if_set_mcast(struct iface *);
int		 if_set_mcast_loop(int);
int		 if_set_mcast_ttl(int, u_int8_t);
int		 if_set_opt(int);
int		 if_set_tos(int, int);
struct iface	*if_find_index(u_short);
struct iface	*if_find_iface(unsigned int, struct in_addr);
struct iface	*if_new(struct kif *);
void		 if_set_recvbuf(int);

struct mdnsd_conf {
	LIST_HEAD(, iface)	iface_list;
	int			mdns_sock;
	struct event		ev_mdns;
	struct hinfo		hi;
	char			myname[MAXHOSTNAMELEN];
};

/* kiface.c */
int		 kif_init(void);
void		 kif_cleanup(void);
struct kif	*kif_findname(char *);
void		 kev_init(void);
void		 kev_cleanup(void);

/* mdnsd.c */
int	peersuser(int);
void	reversstr(char [MAXHOSTNAMELEN], struct in_addr *);
int	mdnsd_imsg_compose_ctl(struct ctl_conn *, u_int16_t, void *, u_int16_t);
void	imsg_event_add(struct imsgev *);
int	imsg_compose_event(struct imsgev *, u_int16_t, u_int32_t, pid_t,
    int, void *, u_int16_t);

/* packet.c */
void	packet_init(void);
void	recv_packet(int, short, void *);   
int	send_packet(struct iface *, void *, size_t, struct sockaddr_in *);
int	pkt_send_if(struct pkt *, struct iface *);
int	pkt_send_allif(struct pkt *);
void	pkt_init(struct pkt *);
void	pkt_cleanup(struct pkt *);
int	pkt_add_question(struct pkt *, struct question *);
int	pkt_add_anrr(struct pkt *, struct rr *);
int	pkt_add_nsrr(struct pkt *, struct rr *);
int	pkt_add_arrr(struct pkt *, struct rr *);
int	rr_rdata_cmp(struct rr *, struct rr *);
void	pktcomp_reset(int, u_int8_t *, u_int16_t);
int	question_set(struct question *, char [MAXHOSTNAMELEN], u_int16_t,
    u_int16_t, int);
int	rr_set(struct rr *, char [MAXHOSTNAMELEN], u_int16_t, u_int16_t,
    u_int32_t, int, void *, size_t);

/* mdns.c */
void		 publish_init(void);
void		 publish_allrr(struct iface *);
int		 publish_insert(struct iface *, struct rr *);
int		 publish_delete(struct iface *, struct rr *);
struct rr *	 publish_lookupall(char [MAXHOSTNAMELEN], u_int16_t, u_int16_t);
void		 publish_fsm(int, short, void *_pub);
void		 query_init(void);
struct query *	 query_place(enum query_style, char *, u_int16_t, u_int16_t);
struct query *	 query_lookup(char [MAXHOSTNAMELEN], u_int16_t, u_int16_t);
int		 query_answerctl(struct ctl_conn *, struct rr *, int);
int		 query_notify(struct rr *, int);
void		 query_remove(struct query *);
void		 cache_init(void);
int		 cache_process(struct rr *);
struct rr	*cache_lookup(char [MAXHOSTNAMELEN], u_int16_t, u_int16_t);

/* control.c */
TAILQ_HEAD(ctl_conns, ctl_conn) ctl_conns;

#endif /* _MDNSD_H_ */
