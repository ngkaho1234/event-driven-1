#include <unistd.h>
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

#include <mad.h>
#include <alsa/asoundlib.h>

#include "stream_io.h"

#include <malloc.h>
#include <string.h>

snd_pcm_t *playback_device;
int play_error;

static struct stream_loop g_loop;
static struct stream g_stream;
static struct stream_request g_accept;


void output(struct mad_header const *header, struct mad_pcm *pcm);


#define MPEG_BUFSZ 20000
#define BUFFER_LEN MPEG_BUFSZ
#define PLAY_BUFFER_PART 6
#define PLAY_BUFFER_SIZE BUFFER_LEN * PLAY_BUFFER_PART
#define STREAM_TOTAL_SIZE (sizeof(struct stream) + sizeof(struct stream_request) +  \
					  sizeof(int) + sizeof(char *)) 

static struct sockaddr accept_content = {0};

static void receiver(struct stream_request *req);


int play_pipe[2];
static struct stream g_play;
pthread_t play_thread_id;

static void play_thread(void)
{
	int size_got = 0;
	unsigned char *play_buffer = malloc(PLAY_BUFFER_SIZE), *play_off;
	struct mad_stream mad_stream;
	struct mad_frame mad_frame;
	struct mad_synth mad_synth;
	play_off = play_buffer;

	mad_stream_init(&mad_stream);
	mad_synth_init(&mad_synth);
	mad_frame_init(&mad_frame);

	while(1) {
		int len;
		len = read(play_pipe[0], play_off, BUFFER_LEN);
		size_got += len;
		play_off += len;
		if (size_got < BUFFER_LEN)
			continue;

		mad_stream_buffer(&mad_stream, play_buffer, BUFFER_LEN);
		while (1) {
			if (mad_frame_decode(&mad_frame, &mad_stream)) {
				if (MAD_RECOVERABLE(mad_stream.error)) {
					continue;
				} else if (mad_stream.error == MAD_ERROR_BUFLEN) {
					memmove(play_buffer, mad_stream.next_frame, play_buffer + PLAY_BUFFER_SIZE - mad_stream.next_frame);
					play_off -= mad_stream.next_frame - play_buffer;
					size_got -= mad_stream.next_frame - play_buffer;
					if (play_off < play_buffer) {
						play_off = play_buffer;
						size_got = 0;
					}
					break;
				} else {
					memmove(play_buffer, play_buffer + BUFFER_LEN, PLAY_BUFFER_SIZE - BUFFER_LEN);
					play_off -= BUFFER_LEN;
					size_got -= BUFFER_LEN;
					if (play_off < play_buffer) {
						play_off = play_buffer;
						size_got = 0;
					}
					break;
				}
			}
			/*Synthesize PCM data of frame*/
			mad_synth_frame(&mad_synth, &mad_frame);
			output(&mad_frame.header, &mad_synth.pcm);
		}

	}
	free(play_buffer);
	mad_stream_finish(&mad_stream);
	mad_synth_finish(&mad_synth);
	mad_frame_finish(&mad_frame);
}

static void sender(struct stream_request *req)
{
	if (req->result.len == 0) {
		free(req->buffer.buf);
		stream_deactivate(req->stream->loop, req->stream);
		close(req->stream->fd);
		return;
	}
	req->request = REQ_READ;
	req->buffer.len = BUFFER_LEN;
	req->callback = &receiver;
	stream_io_submit(req);
}

static void music_output(struct stream_request *req)
{
	struct stream *s;
	int *size_got;
	char **buffer_addr;

	s = (struct stream *)req - 1;
	size_got = (int *)(req + 1);
	buffer_addr = (char **)(size_got + 1);

	req->stream = s;
	req->buffer.buf = *buffer_addr;
	req->request = REQ_WRITE;
	req->buffer.len = 1;
	req->callback = &sender;
	stream_io_submit(req);
}

static void receiver(struct stream_request *req)
{
	int *size_got;
	char **buffer_addr;

	size_got = (int *)(req + 1);
	buffer_addr = (char **)(size_got + 1);

	if (req->result.len == 0) {
		free(*buffer_addr);
		stream_deactivate(req->stream->loop, req->stream);
		close(req->stream->fd);
		return;
	}

	printf("Recv len: %d\nbuf len: %d\n\n", req->result.len, req->buffer.len);

	*size_got += req->result.len;
	if (*size_got < BUFFER_LEN) {
		req->request = REQ_READ;
		req->buffer.len = BUFFER_LEN - *size_got;
		req->buffer.buf += req->result.len;
		req->callback = &receiver;
		stream_io_submit(req);
		return;
	} else {
		*size_got = 0;
		req->buffer.buf = *buffer_addr;
	}

out:
	req->stream = &g_play;
	req->buffer.buf = req->buffer.buf;
	req->request = REQ_WRITE;
	req->buffer.len = BUFFER_LEN;
	req->callback = &music_output;
	stream_io_submit(req);
}

static void finish_write_pipe(struct stream_request *req)
{
}

static void acceptor(struct stream_request *req)
{
	int flags;
	struct stream *newsocket = malloc(STREAM_TOTAL_SIZE);
	struct stream_request *newreq;
	int *size_got;
	char **buffer_addr;

	memset(newsocket, 0, STREAM_TOTAL_SIZE);

	newreq = (struct stream_request *)(newsocket + 1);
	size_got = (int *)(newreq + 1);
	buffer_addr = (char **)(size_got + 1);

	printf("new socket: %p\n", req->result.newsocket);
	stream_init(newsocket, req->result.newsocket);
	stream_init_request(newreq);

	stream_activate(&g_loop, newsocket);

	newreq->stream = newsocket;
	newreq->request = REQ_WRITE;
	newreq->buffer.buf = malloc(BUFFER_LEN);
	newreq->buffer.len = 1;
	newreq->callback = &sender;
	*buffer_addr = newreq->buffer.buf;

	stream_io_submit(newreq);
	stream_io_submit(req);
}

static void close_stream(struct stream *s)
{
	free(s);
}

void output(struct mad_header const *header, struct mad_pcm *pcm) {
	int err;
	register int nsamples = pcm->length;
	if (pcm->channels == 2) {
		snd_pcm_sframes_t frames;
		void *bufs[2] = {pcm->samples[0], pcm->samples[1]};
		frames = snd_pcm_writen(playback_device, bufs, pcm->length);
		if (frames < 0)
			frames = snd_pcm_recover(playback_device, frames, 0);
		if (frames < 0) {
			printf("snd_pcm_writei failed: %s\n", snd_strerror(frames));
		}
		if (frames > 0 && frames < (long)pcm->length)
			printf("Short write (expected %li, wrote %li)\n", (long)pcm->length, frames);
	} else {
		printf("Mono not supported!");
	}
}

int main(int argc, char **argv)
{
	int ret;
	int flags;
	int sockfd;
	struct sockaddr_in sa;
	unsigned long sndbuf;
	int ulong_size;
	if (snd_pcm_open(&playback_device, "default", SND_PCM_STREAM_PLAYBACK, 0)) {
		printf("snd_pcm_opem() failed!\n");
		return 255;
	}
	if (snd_pcm_set_params(playback_device,
				SND_PCM_FORMAT_S32_LE,
				SND_PCM_ACCESS_RW_NONINTERLEAVED,
				2,
				44100,
				1,
				100000)) { /* 500 usec */
		printf("Playback open error!\n");
		exit(EXIT_FAILURE);
	}

	sa.sin_family = PF_INET;
	sa.sin_port = htons(1200);
	sa.sin_addr.s_addr = INADDR_ANY;

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	ret = stream_loop_init(&g_loop, 2);
	printf("new socket: %p\n", sockfd);
	bind(sockfd, (struct sockaddr *)&sa, sizeof(struct sockaddr));
	ret = listen(sockfd, 5);
	if (ret < 0)
		return -errno;

	pipe(play_pipe);
	stream_init(&g_stream, sockfd);
	stream_init(&g_play, play_pipe[1]);
	stream_activate(&g_loop, &g_stream);
	stream_activate(&g_loop, &g_play);
	g_stream.close_callback = close_stream;
	g_play.close_callback = close_stream;
	printf("Errcode: %d\n", g_stream.errcode);

	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

	stream_init_request(&g_accept);

	g_accept.stream = &g_stream;
	g_accept.request = REQ_ACCEPT;
	g_accept.buffer.buf = &accept_content;
	g_accept.buffer.len = sizeof(struct sockaddr);
	g_accept.callback = &acceptor;

	stream_io_submit(&g_accept);
	pthread_create(&play_thread_id, NULL, play_thread, NULL);
	sleep(0);
	while(!stream_loop_start(&g_loop, 0));

	if (playback_device)
		snd_pcm_close(playback_device);
}
