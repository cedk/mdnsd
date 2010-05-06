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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mdns.h"

struct mdns_state {
	struct imsgbuf ibuf;
};

static void	reversstr(char [MAXHOSTNAMELEN], struct in_addr *);
static int	mdns_connect(struct mdns_state *);
static void	mdns_finish(struct mdns_state *);
static int	mdns_lkup_do(const char *, u_int16_t, void *, size_t);
static int	ibuf_read_imsg(struct imsgbuf *, struct imsg *);
static int	ibuf_send_imsg(struct imsgbuf *, u_int32_t,
    void *, u_int16_t);

int
mdns_lkup(const char *hostname, struct in_addr *addr)
{
	return (mdns_lkup_do(hostname, T_A, addr, sizeof(*addr)));
}

int
mdns_lkup_hinfo(const char *hostname, struct hinfo *h)
{
	return (mdns_lkup_do(hostname, T_HINFO, h, sizeof(*h)));
}

int
mdns_lkup_addr(struct in_addr *addr, char *hostname, size_t len)
{
	char	name[MAXHOSTNAMELEN];
	char	res[MAXHOSTNAMELEN];
	int	r;
	
	reversstr(name, addr);
	name[sizeof(name) - 1] = '\0';
	r = mdns_lkup_do(name, T_PTR, res, sizeof(res));
	if (r == 1)
		strlcpy(hostname, res, len);
	
	return (r);
}

int
mdns_lkup_srv(const char *hostname, struct srv *srv)
{
	return (mdns_lkup_do(hostname, T_SRV, srv, sizeof(*srv)));
}

int
mdns_lkup_txt(const char *hostname, char *txt, size_t len)
{
	char	res[MAXHOSTNAMELEN];
	int	r;
	
	r = mdns_lkup_do(hostname, T_TXT, res, sizeof(res));
	if (r == 1)
		strlcpy(txt, res, len);
	
	return (r);
}

static int
mdns_connect(struct mdns_state *mst)
{
	struct sockaddr_un	sun;
	int			sockfd;
	
	bzero(mst, sizeof(struct mdns_state));
	bzero(&sun, sizeof(sun));
	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return (-1);
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, MDNSD_SOCKET, sizeof(sun.sun_path));
	if (connect(sockfd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		if (errno == ENOENT)
			errno = ECONNREFUSED;
		return (-1);
	}
	
	imsg_init(&mst->ibuf, sockfd);
	
	return (0);
}

static void
mdns_finish(struct mdns_state *mst)
{
	imsg_clear(&mst->ibuf);
}

static int
ibuf_send_imsg(struct imsgbuf *ibuf, u_int32_t type,
    void *data, u_int16_t datalen)
{
	struct buf	*wbuf;

	if ((wbuf = imsg_create(ibuf, type, 0,
	    0, datalen)) == NULL)
		return (-1);

	if (imsg_add(wbuf, data, datalen) == -1)
		return (-1);

	wbuf->fd = -1;

	imsg_close(ibuf, wbuf);
	
	if (msgbuf_write(&ibuf->w))
		return (-1);

	return (0);
}

static int
ibuf_read_imsg(struct imsgbuf *ibuf, struct imsg *imsg)
{
	ssize_t		 n;
	struct timeval	 tv;
	int		 r;
	fd_set		 rset;

	if ((n = imsg_get(ibuf, imsg)) == -1)
		return (-1);
	if (n == 0) {
		FD_ZERO(&rset);
		FD_SET(ibuf->fd, &rset);
		timerclear(&tv);
		tv.tv_sec = MDNS_TIMEOUT;
		
		r = select(ibuf->fd + 1, &rset, NULL, NULL, &tv);

		if (r == -1)
			return (-1);
		else if (r == 0) {
			errno = ETIMEDOUT;
			return (-1);
		}
		if ((n = imsg_read(ibuf)) == -1)
			return (-1);
	}
	
	if ((n = imsg_get(ibuf, imsg)) <= 0)
		return (-1);
	
	return (0);
}

static void
reversstr(char str[MAXHOSTNAMELEN], struct in_addr *addr)
{
	const u_char *uaddr = (const u_char *)addr;

	(void) snprintf(str, MAXHOSTNAMELEN, "%u.%u.%u.%u.in-addr.arpa",
	    (uaddr[3] & 0xff), (uaddr[2] & 0xff),
	    (uaddr[1] & 0xff), (uaddr[0] & 0xff));
}

static int
mdns_lkup_do(const char *name, u_int16_t type, void *data, size_t len)
{
	struct imsg imsg;
	struct mdns_msg_lkup mlkup;
	struct mdns_state mst;
	int err;
	
	switch (type) {
	case T_A:		/* FALLTHROUGH */
	case T_HINFO:		/* FALLTHROUGH */
	case T_PTR:		/* FALLTHROUGH */
	case T_SRV:		/* FALLTHROUGH */
	case T_TXT:		/* FALLTHROUGH */
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	if (strlen(name) > MAXHOSTNAMELEN) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	
	if (mdns_connect(&mst) == -1)
		return (-1);

	bzero(&mlkup, sizeof(mlkup));
	strlcpy(mlkup.dname, name, sizeof(mlkup.dname));
	mlkup.type  = type;
	mlkup.class = C_IN;
	if (ibuf_send_imsg(&mst.ibuf, IMSG_CTL_LOOKUP,
	    &mlkup, sizeof(mlkup)) == -1)
		return (-1); /* XXX: set errno */
	if (ibuf_read_imsg(&mst.ibuf, &imsg) == -1) {
		err = errno;
		mdns_finish(&mst);
		if (err == ETIMEDOUT) 
			return (0);
		return (-1);
	}
	if (imsg.hdr.type != IMSG_CTL_LOOKUP) {
		errno = EMSGSIZE; /* think of a better errno */
		mdns_finish(&mst);
		imsg_free(&imsg);
		return (-1);
	}
	if (imsg.hdr.len - IMSG_HEADER_SIZE != len) {
		errno = EMSGSIZE;
		mdns_finish(&mst);
		imsg_free(&imsg);
		return (-1);
	}
	
	memcpy(data, imsg.data, len);
	imsg_free(&imsg);
	mdns_finish(&mst);
	return (1);
}
