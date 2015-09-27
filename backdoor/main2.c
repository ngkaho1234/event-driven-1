#include <stream_io.h>

#include <sys/ioctl.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "stream_io.h"

#include <malloc.h>
#include <string.h>
#include <unistd.h>

#define __USE_XOPEN
#include <stdlib.h>

struct channel_ctx {
	struct stream from;
	struct stream *to;
	struct stream_request req;
};

static struct stream_loop g_loop;
static struct stream g_stream;

#define BUFFER_LEN 4096
static struct sockaddr_in accept_content = {0};

static void after_close(struct stream *s)
{
	free(s);
	exit(0);
}

static void after_read(struct stream_request *req);

static void after_write(struct stream_request *req)
{
	struct channel_ctx *cctx;
	cctx = container_of(req, struct channel_ctx, req);

	if (req->result.errcode || req->result.len == 0) {
		free(req->buffer.buf);
		stream_deactivate(req->stream->loop, &cctx->from);
		close(cctx->from.fd);
		return;
	}
	req->stream = &cctx->from;
	req->request = REQ_READ;
	req->buffer.len = BUFFER_LEN;
	req->callback = &after_read;
	stream_io_submit(req);
}

static void after_read(struct stream_request *req)
{
	struct channel_ctx *cctx;
	cctx = container_of(req, struct channel_ctx, req);

	if (req->result.len == 0) {
		free(req->buffer.buf);
		stream_deactivate(req->stream->loop, &cctx->from);
		close(cctx->from.fd);
		return;
	}
	req->stream = cctx->to;
	req->request = REQ_WRITE;
	req->buffer.len = req->result.len;
	req->callback = &after_write;
	stream_io_submit(req);
}

static int set_nonblock(int sock, int nonblock)
{
	int flags;
	flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0)
		return flags;
	return fcntl(sock, F_SETFL, (nonblock)?(flags | O_NONBLOCK):(flags & ~O_NONBLOCK));
}

static int set_cloexec(int sock, int cloexec)
{
	int flags;
	flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0)
		return flags;
	return fcntl(sock, F_SETFL, (cloexec)?(flags | O_CLOEXEC):(flags & ~O_CLOEXEC));
}

static void acceptor(int sockfd)
{
	int stdfd[3];
	struct channel_ctx *stdin_side, *stdout_side, *server_side;

	stdin_side = malloc(sizeof(struct channel_ctx));
	stdout_side = malloc(sizeof(struct channel_ctx));
	server_side = malloc(sizeof(struct channel_ctx));

	memset(stdin_side, 0, sizeof(struct channel_ctx));
	memset(stdout_side, 0, sizeof(struct channel_ctx));
	memset(server_side, 0, sizeof(struct channel_ctx));

	stream_init(&stdin_side->from, fileno(stdin));
	stream_init(&stdout_side->from, fileno(stdout));
	stream_init(&server_side->from, sockfd);

	stdin_side->from.close_callback = after_close;
	stdout_side->from.close_callback = after_close;
	server_side->from.close_callback = after_close;

	stdin_side->to = &server_side->from;
	server_side->to = &stdout_side->from;

	stream_init_request(&stdin_side->req);
	stream_init_request(&server_side->req);

	stream_activate(&g_loop, &stdin_side->from);
	stream_activate(&g_loop, &stdout_side->from);
	stream_activate(&g_loop, &server_side->from);

	stdin_side->req.stream = &stdin_side->from;
	stdin_side->req.request = REQ_READ;
	stdin_side->req.buffer.buf = malloc(BUFFER_LEN);
	stdin_side->req.buffer.len = BUFFER_LEN;
	stdin_side->req.callback = after_read;

	server_side->req.stream = &server_side->from;
	server_side->req.request = REQ_READ;
	server_side->req.buffer.buf = malloc(BUFFER_LEN);
	server_side->req.buffer.len = BUFFER_LEN;
	server_side->req.callback = after_read;

	set_nonblock(fileno(stdin), 1);
	set_nonblock(fileno(stdout), 1);
	set_nonblock(sockfd, 1);

	stream_io_submit(&stdin_side->req);
	stream_io_submit(&server_side->req);

	while(!stream_loop_start(&g_loop, 0));
}

int main(int argc, char **argv)
{
	int ret;
	int flags;
	int sockfd;
	struct sockaddr_in sa;
	unsigned long sndbuf;
	sa.sin_family = PF_INET;
	sa.sin_port = htons(1304);
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	ret = stream_loop_init(&g_loop, 2);
	if (ret < 0)
		return -errno;
	ret = connect(sockfd, (struct sockaddr *)&sa, sizeof(sa));
	if (ret < 0)
		return -errno;
	set_nonblock(sockfd, 1);

	stream_init(&g_stream, sockfd);
	stream_activate(&g_loop, &g_stream);

	acceptor(sockfd);
}
