#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#ifndef CONFIG_EVENT_WIN32_BUILD
 #include <unistd.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <sys/socket.h>
 #include <sys/epoll.h>
 #include <netdb.h>
 #include <ctype.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <sys/ioctl.h>
 #include <stdarg.h>
 #include <fcntl.h>
 #include <pthread.h>
 #include <sys/syscall.h>
 #include <sys/eventfd.h>

 #include "stream_io.h"
#else
 #include <winsock2.h>
#endif

#include <stdio.h>


static struct stream_loop g_loop;
static struct stream g_stream;
static struct stream_request g_read, g_write;

#define MPEG_BUFSZ 20000
#define BUFFER_LEN MPEG_BUFSZ

static char *file_mmap, *file_ptr;

int g_fd;
uint64_t file_size, file_remains;

static void sender(struct stream_request *req);

static void reader(struct stream_request *req)
{
	if (file_remains <= 0) {
		close(req->stream->fd);
		exit(0);
	}

	g_write.stream = &g_stream;
	g_write.request = REQ_WRITE;
	g_write.buffer.buf = file_ptr;
	g_write.buffer.len = (file_remains < BUFFER_LEN)?file_remains:BUFFER_LEN;
	g_write.callback = &sender;

	stream_io_submit(&g_write);
	printf("Sending.\n");
	file_remains -= (file_remains < BUFFER_LEN)?file_remains:BUFFER_LEN;
	file_ptr += (file_remains < BUFFER_LEN)?file_remains:BUFFER_LEN;
}

static void sender(struct stream_request *req)
{
	static char buffer;
	g_read.stream = &g_stream;
	g_read.request = REQ_READ;
	g_read.buffer.buf = &buffer;
	g_read.buffer.len = 1;
	g_read.callback = &reader;

	printf("Reading.!\n");

	stream_io_submit(&g_read);
}

int main(int argc, char **argv)
{
	int ret;
	int flags;
	int sockfd;
	struct sockaddr_in sa;
	unsigned long sndbuf;
	int ulong_size;
	static char buf;
	struct stat64 stat_buf;
	sa.sin_family = PF_INET;
	sa.sin_port = htons(1200);
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (argc != 2)
		return 1;

	g_fd = open(argv[1], O_RDONLY);
	if (!g_fd)
		return 2;

	fstat64(g_fd, &stat_buf);
	file_size = stat_buf.st_size;
	file_remains = file_size;
	file_mmap = mmap(0, file_size, PROT_READ, MAP_SHARED, g_fd, 0);
	file_ptr = file_mmap;

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	ret = stream_loop_init(&g_loop, 2);
	if (ret < 0)
		return -errno;
	if (ret < 0)
		return -errno;
	
	ret = connect(sockfd, (struct sockaddr *)&sa, sizeof(sa));
	if (ret < 0)
		return -errno;

	printf("Connected!\n");
	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	stream_init(&g_stream, sockfd);
	stream_activate(&g_loop, &g_stream);
	printf("Errcode: %d\n", g_stream.errcode);

	stream_init_request(&g_read);
	stream_init_request(&g_write);

	g_read.stream = &g_stream;
	g_read.request = REQ_READ;
	g_read.buffer.buf = &buf;
	g_read.buffer.len = 1;
	g_read.callback = &reader;

	stream_io_submit(&g_read);

	while(!stream_loop_start(&g_loop, 0));
}
