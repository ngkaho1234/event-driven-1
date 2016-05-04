#ifndef _STREAM_IO_H
#define _STREAM_IO_H

#include <errno.h>
#include <ev.h>
#include "list.h"

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#define EV_CLOSE 4096
#define EV_IN_CB 8192

#define REQ_READ 1
#define REQ_WRITE 2
#define REQ_ACCEPT 3

struct stream_request;
typedef void (*request_cb_t)(struct stream_request *);

struct stream_result {
	unsigned int len;
#define STAT_NONE 1
#define STAT_CONN_CLOSED 2
#define STAT_ERROR -1
	int stats;
	int errcode;
	int newsocket;
};

struct stream_buffer {
	void *buf;
	unsigned int len;
};

struct stream_request {
	int request;
	struct stream *stream;

	int waterlevel;
	struct stream_buffer buffer;
	struct stream_result result;

	request_cb_t callback;

	struct event_list node;
	struct stream_buffer __private;
};

struct stream_loop;

struct stream {
	int fd;
	struct stream_loop *loop;
#define IO_WAIT 0
#define IO_FIN 1
#define IO_MAX 2

	int events;
	int errcode;
	int watched_events;
	struct event_list read_queue[IO_MAX];
	struct event_list write_queue[IO_MAX];

	void (*close_callback)(struct stream *);
	struct event_list node;

#define READ_WATCHER 0
#define WRITE_WATCHER 1
	ev_io stream_watcher[2];
};

struct stream_locker {
	int holder_id;
	int lock;
};

struct stream_loop {
	int breaking;
	int connections;
	ev_async bell_watcher;
	struct event_list close_queue;

	struct ev_loop *stream_loop;
};

int stream_loop_init(struct stream_loop *loop, int connections);
void stream_loop_destroy(struct stream_loop *loop);
int stream_activate(struct stream_loop *loop, struct stream *s);
int stream_deactivate(struct stream_loop *loop, struct stream *s);
int stream_init(struct stream *s, int fd);
int stream_init_request(struct stream_request *req);
void stream_io_submit(struct stream_request *req);
int stream_loop_start(struct stream_loop *loop, int once);

#endif
