#include <stream_io.h>

#include <sys/ioctl.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <malloc.h>
#include <string.h>
#include <unistd.h>

#define __USE_XOPEN
#include <stdlib.h>

struct channel_ctx {
	struct stream from;
	struct stream *to;
	struct stream_request req;
	int pid;
};

static struct stream_loop g_loop;
static struct stream g_stream;
static struct stream_request g_accept;

#define BUFFER_LEN 4096
static struct sockaddr_in accept_content = {0};

static void after_close(struct stream *s)
{
	struct channel_ctx *cctx = container_of(s, struct channel_ctx, from);

	if (cctx->pid) {
		siginfo_t info;
		printf("Killing pid: %u\n", cctx->pid);
		waitid(P_PID, cctx->pid, &info, WEXITED);
	}
	free(s);
	printf("Resources are freed.\n");
}

static void after_read(struct stream_request *req);

static void after_write(struct stream_request *req)
{
	struct channel_ctx *cctx;
	cctx = container_of(req, struct channel_ctx, req);

	if (req->result.errcode || req->result.len == 0) {
		free(req->buffer.buf);
		close(cctx->from.fd);
		close(cctx->to->fd);
		stream_deactivate(req->stream->loop, &cctx->from);
		stream_deactivate(req->stream->loop, cctx->to);
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

	if (req->result.errcode || req->result.len == 0) {
		free(req->buffer.buf);
		close(cctx->from.fd);
		close(cctx->to->fd);
		stream_deactivate(req->stream->loop, &cctx->from);
		stream_deactivate(req->stream->loop, cctx->to);
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

static void acceptor(struct stream_request *req)
{
	int pid;
	int ptmx_fd = -1;
	int tunnel[2], stdfd[3];
	struct channel_ctx *client_side, *server_side;

	client_side = malloc(sizeof(struct channel_ctx));
	server_side = malloc(sizeof(struct channel_ctx));

	memset(client_side, 0, sizeof(struct channel_ctx));
	memset(server_side, 0, sizeof(struct channel_ctx));

	fprintf(stderr, "new socket: %p\n", req->result.newsocket);

	ptmx_fd = open("/dev/ptmx", O_RDWR|O_CLOEXEC);
	grantpt(ptmx_fd);
	unlockpt(ptmx_fd);

	tunnel[0] = ptmx_fd;
	tunnel[1] = open(ptsname(ptmx_fd), O_RDWR);
	stream_init(&client_side->from, req->result.newsocket);
	stream_init(&server_side->from, tunnel[0]);

	client_side->from.close_callback = after_close;
	server_side->from.close_callback = after_close;

	client_side->pid = 0;
	server_side->pid = 0;

	client_side->to = &server_side->from;
	server_side->to = &client_side->from;

	stream_init_request(&client_side->req);
	stream_init_request(&server_side->req);

	stream_activate(&g_loop, &client_side->from);
	stream_activate(&g_loop, &server_side->from);

	client_side->req.stream = &client_side->from;
	client_side->req.request = REQ_READ;
	client_side->req.buffer.buf = malloc(BUFFER_LEN);
	client_side->req.buffer.len = BUFFER_LEN;
	client_side->req.callback = after_read;

	server_side->req.stream = &server_side->from;
	server_side->req.request = REQ_READ;
	server_side->req.buffer.buf = malloc(BUFFER_LEN);
	server_side->req.buffer.len = BUFFER_LEN;
	server_side->req.callback = after_read;

	set_nonblock(tunnel[0], 1);
	set_nonblock(req->result.newsocket, 1);

	pid = fork();
	if (pid == 0) {
		const char *exec_path = "/bin/bash";

		close(stdfd[0] = fileno(stdin));
		close(stdfd[1] = fileno(stdout));
		close(stdfd[2] = fileno(stderr));

		dup2(tunnel[1], stdfd[0]);
		dup2(tunnel[1], stdfd[1]);
		dup2(tunnel[1], stdfd[2]);

		setsid();
		execl(exec_path, exec_path, "-i", NULL);
	}
	server_side->pid = pid;

	close(tunnel[1]);

	stream_io_submit(req);
	stream_io_submit(&client_side->req);
	stream_io_submit(&server_side->req);
}

static void prevent_broken_pipe(void)
{
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, 0);
}

static void set_reuseaddr(int sock)
{
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
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
	sa.sin_port = htons(1304);
	sa.sin_addr.s_addr = INADDR_ANY;

	prevent_broken_pipe();

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	ret = stream_loop_init(&g_loop, 2);
	fprintf(stderr, "new socket: %p\n", sockfd);
	bind(sockfd, (struct sockaddr *)&sa, sizeof(struct sockaddr));
	set_nonblock(sockfd, 1);
	set_cloexec(sockfd, 1);
	ret = listen(sockfd, 5);
	if (ret < 0)
		return -errno;

	stream_init(&g_stream, sockfd);
	stream_activate(&g_loop, &g_stream);
	fprintf(stderr, "Errcode: %d\n", g_stream.errcode);

	stream_init_request(&g_accept);

	g_accept.stream = &g_stream;
	g_accept.request = REQ_ACCEPT;
	g_accept.buffer.buf = &accept_content;
	g_accept.buffer.len = sizeof(struct sockaddr_in);
	g_accept.callback = &acceptor;

	stream_io_submit(&g_accept);
	while(!stream_loop_start(&g_loop, 0));
}
