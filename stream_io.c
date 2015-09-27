#include <stream_io.h>

#include <sys/types.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/eventfd.h>


#include <stdio.h>
#include <string.h>
#include <errno.h>

static inline void __stream_init_io_queue(struct stream *s)
{
	int i;
	for (i = IO_WAIT;i < IO_MAX;++i)
		INIT_LIST_HEAD(s->read_queue + i);
	for (i = IO_WAIT;i < IO_MAX;++i)
		INIT_LIST_HEAD(s->write_queue + i);
}

static void bell_notifier(EV_P_ ev_io *w, int revents)
{
	eventfd_t count = 0;
	eventfd_read(w->fd, &count);
}

static void prepare_notifier(EV_P_ ev_prepare *w, int revents)
{
	struct stream *s;
	struct stream_loop *l = container_of(w, struct stream_loop, prepare_watcher);
	while (!event_list_empty(&l->close_queue)) {
		s = event_list_first_entry(&l->close_queue, struct stream, node);
		event_list_del(&s->node);
		if (s->events & EV_CLOSE) {
			if (s->close_callback) {
				s->events |= EV_IN_CB;
				s->close_callback(s);
			}
			return;
		}
	}
}

static inline int __stream_loop_init_bell(struct stream_loop *loop)
{
	loop->bell = eventfd(0, EFD_NONBLOCK);
	if (loop->bell < 0)
		goto ouch;
	ev_io_init(&loop->bell_watcher, bell_notifier, loop->bell, EV_READ);
	ev_io_start(loop->stream_loop, &loop->bell_watcher);
	return 0;
ouch:
	return -errno;
}

int stream_loop_init(struct stream_loop *loop, int connections)
{
	void *epev;
	struct ev_loop *el = ev_loop_new(EVFLAG_AUTO);
	loop->stream_loop = el;
	if (el == NULL) {
		errno = ENOMEM;
		goto oops;
	}

	loop->connections = connections;
	loop->breaking = 0;
	INIT_LIST_HEAD(&loop->close_queue);

	if (__stream_loop_init_bell(loop) < 0) {
		ev_loop_destroy(el);
		goto oops;
	}

	ev_prepare_init(&loop->prepare_watcher, prepare_notifier);
	ev_prepare_start(el, &loop->prepare_watcher);

	return 0;
oops:
	return -errno;
}

void stream_loop_destroy(struct stream_loop *loop)
{
	struct ev_loop *el = loop->stream_loop;
	ev_io_stop(el, &loop->bell_watcher);
	ev_loop_destroy(el);
	close(loop->bell);
}

int stream_activate(struct stream_loop *loop, struct stream *s)
{
	s->loop = loop;
	return 0;
}

static void stream_try_io_stop_read(struct stream_loop *loop, struct stream *s)
{
	struct ev_loop *el = loop->stream_loop;
	ev_io *ei = s->stream_watcher;

	if (s->watched_events | EV_READ) {
		s->watched_events &= (~EV_READ);
		ev_io_stop(el, &ei[READ_WATCHER]);
	}
}

static void stream_try_io_stop_write(struct stream_loop *loop, struct stream *s)
{
	struct ev_loop *el = loop->stream_loop;
	ev_io *ei = s->stream_watcher;

	if (s->watched_events | EV_WRITE) {
		s->watched_events &= (~EV_WRITE);
		ev_io_stop(el, &ei[WRITE_WATCHER]);
	}
}

int stream_deactivate(struct stream_loop *loop, struct stream *s)
{
	struct ev_loop *el = loop->stream_loop;
	ev_io *ei = s->stream_watcher;

	stream_try_io_stop_read(loop, s);
	stream_try_io_stop_write(loop, s);
	s->events |= EV_CLOSE;
	ev_clear_pending(el, &ei[READ_WATCHER]);
	ev_clear_pending(el, &ei[WRITE_WATCHER]);
	if (event_list_empty(&s->node))
		event_list_add(&s->node, &loop->close_queue);
	return 0;
}

static void __process_read_request(struct stream_loop *loop, struct stream *s);
static void __process_write_request(struct stream_loop *loop, struct stream *s);

static void __process_read_callback(struct stream_loop *loop, struct stream *s);
static void __process_write_callback(struct stream_loop *loop, struct stream *s);

static void libev_read_cb(EV_P_ ev_io *w, int revents)
{
	struct stream *s = container_of(w, struct stream, stream_watcher[READ_WATCHER]);
	/*
	 * XXX: actually setting the EV_READ bit is non-necessary.
	 */
	s->events |= (EV_READ | EV_IN_CB);
	__process_read_request(s->loop, s);
	__process_read_callback(s->loop, s);
	s->events &= ~(EV_READ | EV_IN_CB);
}

static void libev_write_cb(EV_P_ ev_io *w, int revents)
{
	struct stream *s = container_of(w, struct stream, stream_watcher[WRITE_WATCHER]);
	/*
	 * XXX: actually setting the EV_WRITE bit is non-necessary.
	 */
	s->events |= (EV_WRITE | EV_IN_CB);
	__process_write_request(s->loop, s);
	__process_write_callback(s->loop, s);
	s->events &= ~(EV_WRITE | EV_IN_CB);
}

int stream_init(struct stream *s, int fd)
{
	s->fd = fd;
	s->events = 0;
	__stream_init_io_queue(s);
	INIT_LIST_HEAD(&s->node);

	ev_io_init(&s->stream_watcher[READ_WATCHER], &libev_read_cb, fd, EV_READ);
	ev_io_init(&s->stream_watcher[WRITE_WATCHER], &libev_write_cb, fd, EV_WRITE);

	return 0;
}

int stream_feed(struct stream *s, int events)
{
	struct ev_loop *el;
	if (s->loop == NULL) {
		s->errcode = EINVAL;
		return -EINVAL;
	}
	el = s->loop->stream_loop;

	if (events & EV_READ) {
		ev_feed_event(el, &s->stream_watcher[READ_WATCHER], EV_READ);
	}
	if (events & EV_WRITE) {
		ev_feed_event(el, &s->stream_watcher[WRITE_WATCHER], EV_WRITE);
	}
	/*
	 * TODO: possible for multi-loop.
	 */
#if 0
	eventfd_write(s->loop->bell, 1);
#endif

	return 0;
}

static int __move_to_queue(struct stream_request *req, struct event_list *dest)
{
	event_list_del(&req->node);
	event_list_add(&req->node, dest);
}

static inline int __do_write(struct stream_loop *loop, struct stream *s, struct stream_request *req)
{
	int nbyte;
	char *buf = req->private.buf;
	while (req->private.len) {
		nbyte = write(s->fd, buf, req->private.len);
		if (nbyte < 0)
			goto out;

		buf += nbyte;
		req->private.len -= nbyte;
		req->private.buf = buf;
	}
	req->result.len = req->buffer.len - req->private.len;
	req->result.stats = STAT_NONE;
	req->result.errcode = 0;
	req->result.newsocket = 0;
	s->errcode = 0;
done:
	return 0;
out:
	if (errno && errno != EAGAIN && errno != EINTR) {
		req->result.len = 0;
		req->result.stats = STAT_ERROR;
		req->result.errcode = errno;
		req->result.newsocket = 0;
		s->errcode = errno;
	}
	return -errno;
}

static inline int __do_read(struct stream_loop *loop, struct stream *s, struct stream_request *req)
{
	int nbyte;
	char *buf = req->buffer.buf;
	nbyte = read(s->fd, buf, req->buffer.len);
	if (nbyte < 0)
		goto out;
	req->result.len = nbyte;
	if (nbyte) {
		req->result.stats = STAT_NONE;
		req->result.errcode = 0;
	} else {
		req->result.stats = STAT_CONN_CLOSED;
		req->result.errcode = 0;
	}
	req->result.newsocket = 0;
	s->errcode = 0;
done:
	return 0;
out:
	if (errno && errno != EAGAIN && errno != EINTR) {
		req->result.len = 0;
		req->result.stats = STAT_ERROR;
		req->result.errcode = errno;
		req->result.newsocket = 0;
		s->errcode = errno;
	}
	return -errno;
}

static inline int __do_accept(struct stream_loop *loop, struct stream *s, struct stream_request *req)
{
	int sock;
	char *buf = req->buffer.buf;
	sock = accept(s->fd, (struct sockaddr *)buf, &req->buffer.len);

	if (sock < 0)
		goto out;
	req->result.len = req->buffer.len;
	if (sock) {
		req->result.stats = STAT_NONE;
		req->result.errcode = 0;
	}
	req->result.newsocket = sock;
	s->errcode = 0;
done:
	return 0;
out:
	if (errno && errno != EAGAIN && errno != EINTR) {
		req->result.len = 0;
		req->result.stats = STAT_ERROR;
		req->result.errcode = errno;
		req->result.newsocket = 0;
		s->errcode = errno;
	}
	return -errno;
}

static void __process_read_callback(struct stream_loop *loop, struct stream *s)
{
	struct stream_request *req;
	while (!event_list_empty(&s->read_queue[IO_FIN])) {
		req = event_list_first_entry(&s->read_queue[IO_FIN], struct stream_request, node);
		event_list_del(&req->node);
		req->callback(req);
		if (s->errcode && s->errcode != EAGAIN && s->errcode != EINTR)
			break;
		if (s->events & EV_CLOSE)
			break;
	}

	if (event_list_empty(&s->read_queue[IO_WAIT])) {
		stream_try_io_stop_read(loop, s);
	}
}

static void __process_write_callback(struct stream_loop *loop, struct stream *s)
{
	struct stream_request *req;
	while (!event_list_empty(&s->write_queue[IO_FIN])) {
		req = event_list_first_entry(&s->write_queue[IO_FIN], struct stream_request, node);
		event_list_del(&req->node);
		req->callback(req);
		if (s->errcode && s->errcode != EAGAIN && s->errcode != EINTR)
			break;
		if (s->events & EV_CLOSE)
			break;
	}

	if (event_list_empty(&s->write_queue[IO_WAIT])) {
		stream_try_io_stop_write(loop, s);
	}
}

static void __process_read_request(struct stream_loop *loop, struct stream *s)
{
	int np = 0;
	struct stream_request *req;
	while (!event_list_empty(&s->read_queue[IO_WAIT])) {
		int ret;
		req = event_list_first_entry(&s->read_queue[IO_WAIT], struct stream_request, node);
		if (req->request == REQ_READ)
			ret = __do_read(loop, s, req);
		else
			ret = __do_accept(loop, s, req);
		if (ret && ret != -EAGAIN && ret != -EINTR)
			goto out;
		if (!ret) {
			__move_to_queue(req, &s->read_queue[IO_FIN]);
		} else
			break;
		np++;
	}
	return;
out:
	__move_to_queue(req, &s->read_queue[IO_FIN]);
}

static void __process_write_request(struct stream_loop *loop, struct stream *s)
{
	int np = 0;
	struct stream_request *req;
	while (!event_list_empty(&s->write_queue[IO_WAIT])) {
		int ret;
		req = event_list_first_entry(&s->write_queue[IO_WAIT], struct stream_request, node);
		ret = __do_write(loop, s, req);
		if (ret && ret != -EAGAIN && ret != -EINTR)
			goto out;
		if (!ret)
			__move_to_queue(req, &s->write_queue[IO_FIN]);
		else
			break;
		np++;
	}
	return;
out:
	__move_to_queue(req, &s->write_queue[IO_FIN]);
}

static void stream_try_io_start_read(struct stream_loop *loop, struct stream *s)
{
	struct ev_loop *el = loop->stream_loop;
	ev_io *ei = s->stream_watcher;

	if (!(s->watched_events & EV_READ)) {
		s->watched_events |= EV_READ;
		ev_io_start(el, &ei[READ_WATCHER]);
	}
}

static void stream_try_io_start_write(struct stream_loop *loop, struct stream *s)
{
	struct ev_loop *el = loop->stream_loop;
	ev_io *ei = s->stream_watcher;

	if (!(s->watched_events & EV_WRITE)) {
		s->watched_events |= EV_WRITE;
		ev_io_start(el, &ei[WRITE_WATCHER]);
	}
}

void stream_io_submit(struct stream_request *req)
{
	struct stream *s = req->stream;
	struct stream_loop *loop = s->loop;
	struct event_list *list;
	int events;
	req->private.buf = req->buffer.buf;
	req->private.len = req->buffer.len;

	if (req->request == REQ_READ) {
		list = &s->read_queue[IO_WAIT];
		events = EV_READ;
	} else if (req->request == REQ_WRITE) {
		list = &s->write_queue[IO_WAIT];
		events = EV_WRITE;
	} else if (req->request == REQ_ACCEPT) {
		list = &s->read_queue[IO_WAIT];
		events = EV_READ;
	}

	if (event_list_empty(&req->node)) {
		event_list_add(&req->node, list);
		if (events & EV_READ) {
			__process_read_request(loop, s);
			if (!event_list_empty(&s->read_queue[IO_FIN])) {
				stream_feed(s, EV_READ);
				goto out;
			}
		}
		if (events & EV_WRITE) {
			__process_write_request(loop, s);
			if (!event_list_empty(&s->write_queue[IO_FIN])) {
				stream_feed(s, EV_WRITE);
				goto out;
			}
		}
	} else 
		event_list_add(&req->node, list);

	switch (events) {
	case EV_READ:
		stream_try_io_start_read(loop, s);
		break;
	case EV_WRITE:
		stream_try_io_start_write(loop, s);
	}

	/* For future use of ET mode. */
#if 0
	stream_feed(s, events);
#endif
out:
	return;
}

int stream_init_request(struct stream_request *req)
{
	INIT_LIST_HEAD(&req->node);
}

int stream_loop_start(struct stream_loop *loop, int once)
{
	return ev_run(loop->stream_loop, once);
}
