#ifndef CONFIG_EVENT_WIN32_BUILD
 #include <unistd.h>
 #include <sys/types.h>
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
 #include <sched.h>
 #include <signal.h>
 #include <sys/syscall.h>
 #include <sys/eventfd.h>

 #include <errno.h>
 #include <libaio.h>
#else
 #include <winsock2.h>
#endif

#include <malloc.h>

static char send_content[] = "GET / HTTP/1.1\r\nHost: www.google.com\r\n\r\n";
static char recv_content[4096] = {0};

struct iocb iocb_core[2];
struct iocb *aio_iocb[2] = {iocb_core, iocb_core + 1};
io_context_t ioctx;
struct io_event aio_event[2];

#define READ 0
#define WRITE 1

static void read_cb(io_context_t ctx, struct iocb *iocb, long res1, long res2)
{
	printf("%s: res1: %d, res2: %d\n");
	io_submit(ioctx, 1, aio_iocb + READ);
}

static void write_cb(io_context_t ctx, struct iocb *iocb, long res1, long res2)
{
	printf("%s: res1: %d, res2: %d\n");
	io_submit(ioctx, 1, aio_iocb + WRITE);
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
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");

	io_setup(200, &ioctx);

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	ret = connect(sockfd, (struct sockaddr *)&sa, sizeof(sa));
	if (ret < 0)
		return -errno;

	printf("Connected!\n");
	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

	io_prep_pread(iocb_core + READ, sockfd, recv_content, sizeof(recv_content), 0);
	io_prep_pwrite(iocb_core + WRITE, sockfd, send_content, sizeof(send_content), 0);
	io_set_callback(iocb_core + READ, read_cb);
	io_set_callback(iocb_core + WRITE, write_cb);
	ret = io_submit(ioctx, 2, aio_iocb);

	if (errno)
		perror("main");
	while((ret = io_getevents(ioctx, 1, 2, aio_event, NULL)) >= 0 || ret == -EINTR);
	if (errno)
		perror("main");
}
