/*
 * rhebar-btbridge - cầu nối 127.0.0.1:<port> -> cổng Bluetooth SPP (/dev/cu.*) trên macOS.
 *
 * macOS không cho cài CUPS backend bên thứ ba (SIP), nên máy in Bluetooth được
 * thêm với URI socket://127.0.0.1:<port>; daemon này nhận dữ liệu in và chuyển
 * sang cổng serial Bluetooth. Mỗi lệnh in mở cổng (macOS tự kết nối BT khi mở)
 * và đóng khi xong để nhả kết nối.
 *
 * Usage: rhebar-btbridge <serial-device> [port]
 */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

#define OPEN_TIMEOUT_S 30

static void logmsg(const char *fmt, const char *a, long b)
{
	time_t t = time(NULL);
	char ts[32];
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));
	fprintf(stderr, "%s ", ts);
	fprintf(stderr, fmt, a, b);
	fputc('\n', stderr);
	fflush(stderr);
}

static int open_serial(const char *dev)
{
	int fd = -1, waited = 0;
	struct termios tio;

	while (waited < OPEN_TIMEOUT_S) {
		fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
		if (fd >= 0)
			break;
		logmsg("open %s failed (%ld), retrying", dev, errno);
		sleep(2);
		waited += 2;
	}
	if (fd < 0)
		return -1;
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
	if (tcgetattr(fd, &tio) == 0) {
		cfmakeraw(&tio);
		cfsetispeed(&tio, B115200);
		cfsetospeed(&tio, B115200);
		tio.c_cflag |= CLOCAL | CREAD;
		tcsetattr(fd, TCSANOW, &tio);
	}
	return fd;
}

static int write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				struct pollfd p = {fd, POLLOUT, 0};
				poll(&p, 1, 1000);
				continue;
			}
			return -1;
		}
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static void handle(int client, const char *dev)
{
	char buf[4096];
	long total = 0;
	int ser = -1;
	ssize_t n;

	while ((n = read(client, buf, sizeof(buf))) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		/* mở cổng khi có dữ liệu đầu tiên: tránh giữ kết nối BT khi CUPS chỉ dò máy */
		if (ser < 0 && (ser = open_serial(dev)) < 0) {
			logmsg("cannot open %s, job dropped", dev, 0);
			return;
		}
		if (write_all(ser, buf, (size_t)n) < 0) {
			logmsg("write to %s failed (%ld)", dev, errno);
			break;
		}
		total += n;
	}
	if (ser >= 0) {
		tcdrain(ser);
		sleep(1); /* cho buffer RFCOMM đẩy hết trước khi ngắt kết nối */
		close(ser);
	}
	logmsg("job done via %s: %ld bytes", dev, total);
}

int main(int argc, char *argv[])
{
	const char *dev;
	int port = 9190, srv, one = 1;
	struct sockaddr_in addr;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s <serial-device> [port]\n", argv[0]);
		return 1;
	}
	dev = argv[1];
	if (argc == 3)
		port = atoi(argv[2]);
	signal(SIGPIPE, SIG_IGN);

	srv = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(srv, 4) < 0) {
		perror("bind/listen");
		return 1;
	}
	logmsg("forwarding to %s from 127.0.0.1:%ld", dev, port);

	for (;;) {
		int c = accept(srv, NULL, NULL);
		if (c < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			sleep(1);
			continue;
		}
		handle(c, dev);
		close(c);
	}
}
