/*
 * rhebar-btbridge - cầu nối 127.0.0.1:<port> -> cổng Bluetooth SPP (/dev/cu.*) trên macOS.
 *
 * macOS không cho cài CUPS backend bên thứ ba (SIP), nên máy in Bluetooth được
 * thêm với URI socket://127.0.0.1:<port>; daemon này nhận dữ liệu in và chuyển
 * sang cổng serial Bluetooth. Mỗi lệnh in mở cổng (macOS tự kết nối BT khi mở)
 * và đóng khi xong để nhả kết nối.
 *
 * Qua Bluetooth máy in không chặn được luồng dữ liệu như USB: gửi liền nhiều trang
 * thì các trang sau bị rơi trong lúc máy đang in. Vì vậy dữ liệu TSPL được gửi từng
 * trang (cắt sau mỗi dòng PRINT) và sau mỗi trang chờ máy in xong, bằng lệnh trạng
 * thái <ESC>!? (bit 0x20 = đang in). Máy không trả lời trạng thái thì chờ theo thời
 * gian ước tính từ SIZE và SPEED.
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
#define PAGE_TIMEOUT_S 60      /* chờ tối đa mỗi trang (hết giấy, kẹt...) */
#define STATUS_PRINTING 0x20
#define STATUS_ERRORS 0xCF     /* nắp mở, kẹt, hết giấy/ruy băng, lỗi khác */

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

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_s(double s)
{
	struct timespec ts;
	if (s <= 0)
		return;
	ts.tv_sec = (time_t)s;
	ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
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

/* Đọc 1 byte trạng thái (byte cuối nhận được) trong timeout_ms; -1 nếu không có. */
static int read_status(int fd, int timeout_ms)
{
	unsigned char buf[64];
	int last = -1;
	struct pollfd p = {fd, POLLIN, 0};
	while (poll(&p, 1, timeout_ms) > 0 && (p.revents & POLLIN)) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n <= 0)
			break;
		last = buf[n - 1];
		timeout_ms = 50; /* gom phần còn lại nếu trả lời bị tách */
	}
	return last;
}

typedef struct {
	int ser;
	const char *dev;
	int status_ok;      /* -1 chưa biết, 0 máy không trả lời, 1 có trả lời */
	double height_mm;   /* từ SIZE */
	double speed_ips;   /* từ SPEED */
	char line[64];      /* đầu dòng (SIZE, SPEED) */
	size_t line_len;
	int line_overflow;
	char tail[24];      /* cuối dòng: PRINT có thể dính sau dữ liệu BITMAP */
	size_t tail_len;
	int pages;
} job;

static double estimate_s(const job *j)
{
	return j->height_mm / (j->speed_ips * 25.4) + 1.5;
}

/* Chờ máy in in xong trang vừa gửi. Đồng thời bảo đảm mọi byte trước đó đã tới máy:
 * trả lời trạng thái chỉ có sau khi máy đã nhận hết dữ liệu đứng trước lệnh hỏi. */
static void wait_page_done(job *j)
{
	double start = now_s(), deadline = start + PAGE_TIMEOUT_S;
	int seen_printing = 0, last_err = 0;

	if (j->status_ok != 0) {
		sleep_s(0.3);
		while (now_s() < deadline) {
			int st;
			read_status(j->ser, 0); /* bỏ byte cũ */
			if (write_all(j->ser, "\x1b!?", 3) < 0)
				return;
			st = read_status(j->ser, j->status_ok == 1 ? 3000 : 1500);
			if (st < 0) {
				if (j->status_ok == -1) {
					j->status_ok = 0;
					logmsg("printer does not answer <ESC>!? on %s, using timed pacing", j->dev, 0);
					break;
				}
				continue;
			}
			j->status_ok = 1;
			if (st & STATUS_PRINTING) {
				seen_printing = 1;
			} else if (st & STATUS_ERRORS) {
				if (st != last_err)
					logmsg("printer status error 0x%02lx%s", "", st);
				last_err = st;
			} else if (seen_printing || now_s() - start > 1.5) {
				return;
			}
			sleep_s(0.2);
		}
		if (j->status_ok == 1) {
			logmsg("page wait timed out on %s", j->dev, 0);
			return;
		}
	}
	sleep_s(estimate_s(j));
}

/* Dòng kết thúc bằng "PRINT", "PRINT n" hoặc "PRINT n,m" (bỏ \r cuối)? */
static int tail_is_print(const job *j)
{
	char t[sizeof(j->tail) + 1];
	char *p, *q, *found = NULL;
	size_t len = j->tail_len;
	memcpy(t, j->tail, len);
	while (len && t[len - 1] == '\r')
		len--;
	t[len] = 0;
	for (p = t; (p = strstr(p, "PRINT")) != NULL; p++)
		found = p;
	if (!found)
		return 0;
	for (q = found + 5; *q; q++)
		if (*q != ' ' && *q != ',' && (*q < '0' || *q > '9'))
			return 0;
	return found[5] == 0 || found[5] == ' ';
}

/* Ghi nhận một dòng TSPL vừa kết thúc; trả về 1 nếu là PRINT (hết trang). */
static int end_line(job *j)
{
	char *l = j->line;
	int is_print = tail_is_print(j);
	j->tail_len = 0;
	if (!j->line_overflow) {
		l[j->line_len] = 0;
		if (!strncmp(l, "SIZE ", 5)) {
			double w, h;
			const char *c = strchr(l, ',');
			if (c && sscanf(c + 1, "%lf", &h) == 1 && sscanf(l + 5, "%lf", &w) == 1)
				j->height_mm = strstr(c, "mm") ? h : h * 25.4;
		} else if (!strncmp(l, "SPEED ", 6)) {
			double s;
			if (sscanf(l + 6, "%lf", &s) == 1 && s > 0)
				j->speed_ips = s;
		}
	}
	j->line_len = 0;
	j->line_overflow = 0;
	return is_print;
}

static void handle(int client, const char *dev)
{
	char buf[4096];
	long total = 0;
	ssize_t n;
	job j;

	memset(&j, 0, sizeof(j));
	j.ser = -1;
	j.dev = dev;
	j.status_ok = -1;
	j.height_mm = 150;
	j.speed_ips = 4;

	while ((n = read(client, buf, sizeof(buf))) != 0) {
		ssize_t seg = 0, i;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		/* mở cổng khi có dữ liệu đầu tiên: tránh giữ kết nối BT khi CUPS chỉ dò máy */
		if (j.ser < 0 && (j.ser = open_serial(dev)) < 0) {
			logmsg("cannot open %s, job dropped", dev, 0);
			return;
		}
		for (i = 0; i < n; i++) {
			if (buf[i] != '\n') {
				if (j.line_len < sizeof(j.line) - 1)
					j.line[j.line_len++] = buf[i];
				else
					j.line_overflow = 1;
				if (j.tail_len == sizeof(j.tail)) {
					memmove(j.tail, j.tail + 1, sizeof(j.tail) - 1);
					j.tail_len--;
				}
				j.tail[j.tail_len++] = buf[i];
				continue;
			}
			if (end_line(&j)) {
				if (write_all(j.ser, buf + seg, (size_t)(i + 1 - seg)) < 0)
					goto write_failed;
				seg = i + 1;
				j.pages++;
				wait_page_done(&j);
			}
		}
		if (seg < n && write_all(j.ser, buf + seg, (size_t)(n - seg)) < 0)
			goto write_failed;
		total += n;
	}

	if (j.ser >= 0) {
		if (j.pages == 0) {
			tcdrain(j.ser);
			sleep_s(2);
		}
		close(j.ser);
	}
	logmsg("job done via %s: %ld bytes", dev, total);
	logmsg("  pages: %s%ld", "", j.pages);
	return;

write_failed:
	logmsg("write to %s failed (%ld)", dev, errno);
	close(j.ser);
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
