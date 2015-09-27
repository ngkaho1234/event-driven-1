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


static struct stream_loop g_loop;
static struct stream g_stream;
static struct stream_request g_accept;

#define BUFFER_LEN 4096
static struct sockaddr_in accept_content = {0};

static void receiver(struct stream_request *req);

static void sender(struct stream_request *req)
{
	req->request = REQ_READ;
	req->buffer.len = BUFFER_LEN;
	req->callback = &receiver;
	stream_io_submit(req);
}

static void receiver(struct stream_request *req)
{
	if (req->result.len == 0) {
		free(req->buffer.buf);
		stream_deactivate(req->stream->loop, req->stream);
		close(req->stream->fd);
		free(req->stream);
		return;
	}
	req->request = REQ_WRITE;
	req->buffer.len = req->result.len;
	req->callback = &sender;
	stream_io_submit(req);
}

static void acceptor(struct stream_request *req)
{
	int flags;
	struct stream *newsocket = malloc(sizeof(struct stream) + sizeof(struct stream_request));
	struct stream_request *newreq;
	memset(newsocket, 0, sizeof(struct stream) + sizeof(struct stream_request));
	newreq = (struct stream_request *)(newsocket + 1);
	printf("new socket: %p\n", req->result.newsocket);
	stream_init(newsocket, req->result.newsocket);
	stream_init_request(newreq);

	stream_activate(&g_loop, newsocket);

	newreq->stream = newsocket;
	newreq->request = REQ_READ;
	newreq->buffer.buf = malloc(BUFFER_LEN);
	newreq->buffer.len = BUFFER_LEN;
	newreq->callback = &receiver;

	stream_io_submit(newreq);
	stream_io_submit(req);
}

int main(int argc, char **argv)
{
	int ret;
	int flags;
	int sockfd;
	struct sockaddr_in sa;
	unsigned long sndbuf;
	int ulong_size;
	sa.sin_family = PF_INET;
	sa.sin_port = htons(1200);
	sa.sin_addr.s_addr = INADDR_ANY;

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	ret = stream_loop_init(&g_loop, 2);
	printf("new socket: %p\n", sockfd);
	bind(sockfd, (struct sockaddr *)&sa, sizeof(struct sockaddr));
	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	ret = listen(sockfd, 5);
	if (ret < 0)
		return -errno;

	stream_init(&g_stream, sockfd);
	stream_activate(&g_loop, &g_stream);
	printf("Errcode: %d\n", g_stream.errcode);

	stream_init_request(&g_accept);

	g_accept.stream = &g_stream;
	g_accept.request = REQ_ACCEPT;
	g_accept.buffer.buf = &accept_content;
	g_accept.buffer.len = sizeof(struct sockaddr_in);
	g_accept.callback = &acceptor;

	stream_io_submit(&g_accept);
	while(!stream_loop_start(&g_loop, 0));
}
