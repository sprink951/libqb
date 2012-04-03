/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of libqb.
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "os_base.h"
#include "loop_poll_int.h"

#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#endif /* HAVE_SYS_EVENT_H */

#define MAX_EVENTS 12

static int32_t
_poll_to_filter_(int32_t event)
{
	int32_t out = 0;
	if (event & POLLIN)
		out |= EVFILT_READ;
	if (event & POLLOUT)
		out |= EVFILT_WRITE;
	return out;
}

static int32_t
_filter_to_poll_event_(int32_t event)
{
	int32_t out = 0;
	if (event & EPOLLIN)
		out |= POLLIN;
	if (event & EPOLLOUT)
		out |= POLLOUT;
	if (event & EPOLLPRI)
		out |= POLLPRI;
	if (event & EPOLLERR)
		out |= POLLERR;
	if (event & EPOLLHUP)
		out |= POLLHUP;
	return out;
}

static void
_fini(struct qb_poll_source *s)
{
	if (s->epollfd != -1) {
		close(s->epollfd);
		s->epollfd = -1;
	}
}

static int32_t
_add(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events)
{
	int32_t res = 0;
	struct kevent ke;
	int kents = _poll_to_filter_(events);

	/* fill out the kevent struct */
	EV_SET(&ke, pe->check, kents, EV_ADD, 0, NULL, pe);

	/* set the event */
	res = kevent(kq, &ke, 1, NULL, 0, NULL);
	if (res == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "kevent(add)");
	}

	return res;
}


static int32_t
_mod(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t events)
{
}

static int32_t
_del(struct qb_poll_source *s, struct qb_poll_entry *pe, int32_t fd, int32_t arr_index)
{
	int32_t res = 0;
	struct kevent ke;
	int kents = _poll_to_filter_(events);

	/* fill out the kevent struct */
	EV_SET(&ke, pe->check, kents, EV_DELETE, 0, NULL, pe);

	/* set the event */
	res = kevent(kq, &ke, 1, NULL, 0, NULL);
	if (res == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "kevent(del)");
	}
	return res;
}

static int32_t
_poll_and_add_to_jobs_(struct qb_loop_source *src, int32_t ms_timeout)
{
	int32_t i;
	int32_t res;
	int32_t event_count;
	int32_t new_jobs = 0;
	int32_t revents;
	struct qb_poll_entry *pe = NULL;
	struct qb_poll_source *s = (struct qb_poll_source *)src;
	struct kevent events[MAX_EVENTS];
	struct timespec timeout = { 0, 0 };

	qb_timespec_add_ms(&timeout, ms_timeout);

	qb_poll_fds_usage_check_(s);

retry_poll:

	event_count = kevent(s->epollfd, NULL, 0, events, MAX_EVENTS, NULL);
	if (errno == EINTR && event_count == -1) {
		goto retry_poll;
	} else if (event_count == -1) {
		return -errno;
	}

	for (i = 0; i < event_count; i++) {
		if (evi.flags & EV_ERROR) {
			revents = POLLHUP;
		}
		if (evi.filter == EVFILT_READ) {
			revents |= POLLIN;
		}
		if (evi.filter == EVFILT_WRITE) {
			revents |= POLLOUT;
		}
		pe = evi.udata;
		if (pe->check != evi.ident) {
			qb_util_log(LOG_WARNING,
				    "can't find poll entry for new event.");
			continue;
		}
		if (pe->ufd.fd == -1 || pe->state == QB_POLL_ENTRY_DELETED) {
			qb_util_log(LOG_WARNING,
				    "can't post new event to a deleted entry.");
			/*
			 * empty/deleted
			 */
			continue;
		}
		if (revents == pe->ufd.revents ||
		    pe->state == QB_POLL_ENTRY_JOBLIST) {
			/*
			 * entry already in the job queue.
			 */
			continue;
		}
		pe->ufd.revents = revents;

		new_jobs += pe->add_to_jobs(src->l, pe);
	}

	return new_jobs;
}

int32_t
qb_epoll_init(struct qb_poll_source *s)
{
	s->epollfd = kqueue();

	if (s->epollfd < 0) {
		return -errno;
	}
	s->driver.fini = _fini;
	s->driver.add = _add;
	s->driver.mod = _mod;
	s->driver.del = _del;
	s->s.poll = _poll_and_add_to_jobs_;
	return 0;
}
