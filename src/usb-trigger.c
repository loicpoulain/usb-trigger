#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/wait.h>

#include <linux/types.h>
#include <linux/netlink.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define USB_SUBSYSTEM "usb"
#define UEVENT_ADD "add@"
#define UEVENT_DEL "remove@"
#define SYSFS_USB_VENDOR_ID "idVendor"
#define SYSFS_USB_PRODUCT_ID "idProduct"

static int open_socket(void)
{
	int fd;
	struct sockaddr_nl nls = {
		.nl_family = AF_NETLINK,
		.nl_pid = getpid(),
		.nl_groups = -1,
	};

	fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (fd < 0)
		return -1;

	if (bind(fd, (void *)&nls, sizeof(nls)) < 0)
		return -1;

	return fd;
}

static int check_vid(char *usbdev, char *evid)
{
	char path[1024];
	char vid[4];
	int fd, ret;

	/* Check VID */
	sprintf(path, "/sys/%s/%s", usbdev, SYSFS_USB_VENDOR_ID);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;

	if (read(fd, vid, sizeof(vid)) != sizeof(vid)) {
		ret = -1;
	} else {
		ret = strncmp(vid, evid, sizeof(vid));
	}

	close(fd);
	return ret;
}

static int check_pid(char *usbdev, char *epid)
{
	char path[1024];
	char pid[4];
	int fd, ret;

	/* Check VID */
	sprintf(path, "/sys/%s/%s", usbdev, SYSFS_USB_PRODUCT_ID);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;

	if (read(fd, pid, sizeof(pid)) != sizeof(pid)) {
		ret = -1;
	} else {
		ret = strncmp(pid, epid, sizeof(pid));
	}

	close(fd);
	return ret;
}

static void execute(char *cmd)
{
	char *args[100] = {}, *saveptr;
	int i = 0;

	args[i++] = strtok_r(cmd, " ", &saveptr);
	while ((args[i++] = strtok_r(NULL, " ", &saveptr)));

	execvp(args[0], args);
}

static void killwait(int pid)
{
	int status;

	/* TODO: epoll + signalfd / SIGCHLD */
	if (!waitpid(pid, &status, WNOHANG)) {
		kill(pid, SIGKILL);
		wait(NULL); /* should not block*/
	}
}

void usage(char *name)
{
	printf("Usage: %s --vid <vid> --pid <pid> --exec <cmd>\n" \
	       "options:\n" \
	       "   -V, --vid=VID   Specify USB vendor ID\n" \
	       "   -P, --pid=PID   Specify USB product ID\n" \
	       "   -E, --exec=CMD  Specify program to execute on detection\n" \
	       , name);
}

static const struct option main_options[] = {
	{ "vid", required_argument, NULL, 'V' },
	{ "pid", required_argument, NULL, 'P' },
	{ "exec", required_argument, NULL, 'E' },
	{ "help", no_argument, NULL, 'h' },
	{ },
};

int main(int argc, char *argv[])
{
	char *evid = NULL, *epid = NULL, *cmd = NULL;
	struct epoll_event event;
	struct epoll_event events[5];
	char evbuf[512], device[512];
	int efd, sfd;
	int cpid = 0;

	for (;;) {
		int opt = getopt_long(argc, argv, "V:P:E:h",
				      main_options, NULL);
		if (opt < 0)
			break;

		switch (opt) {
		default:
		case 'V':
			evid = optarg;
			if (evid[0] == '0' &&
			    (evid[1] == 'x' || evid[1] == 'X'))
				evid = &evid[2];
			if (evid[0] == 'h' || evid[0] == 'H')
				evid = &evid[1];
			break;
		case 'P':
			epid = optarg;
			if (epid[0] == '0' &&
			    (epid[1] == 'x' || epid[1] == 'X'))
				epid = &epid[2];
			if (epid[0] == 'h' || epid[0] == 'H')
				epid = &epid[1];
			break;
		case 'E':
			cmd = optarg;
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		}
	}

	if (cmd == NULL || evid == NULL || epid == NULL) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	sfd = open_socket();
	if (sfd < 0) {
		perror("Unable to open uevent netlink socket");
		exit(EXIT_FAILURE);
	}

	efd = epoll_create1(0);
	if (efd < 0) {
		perror("Unable to create epoll");
		close(sfd);
		exit(EXIT_FAILURE);
	}

	event.data.fd = sfd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0) {
		perror("Unable to add sfd events to epoll");
		close(sfd);
		close(efd);
		exit(EXIT_FAILURE);
	}

	do {
		int n, i;

		n = epoll_wait(efd, events, ARRAY_SIZE(events), -1);
		if (n < 0) {
			perror("epoll error");
			break;
		}

		for (i = 0; i < n; i++) {
			if (read(events[i].data.fd, evbuf, sizeof(evbuf)) < 0) {
				perror("Unable to read uevent socket");
				break;
			}

			if (!strstr(evbuf, USB_SUBSYSTEM))
				continue;

			if (!strncmp(UEVENT_DEL, evbuf, strlen(UEVENT_DEL)) &&
			    !strcmp(device, &evbuf[strlen(UEVENT_DEL)]) &&
			    cpid) {
				/* device removed, kill child */
				killwait(cpid);
				device[0] = '\0';
				cpid = 0;
			}

			if (strncmp(UEVENT_ADD, evbuf, strlen(UEVENT_ADD)))
				continue;

			if (check_vid(&evbuf[strlen(UEVENT_ADD)], evid))
				continue;

			if (check_pid(&evbuf[strlen(UEVENT_ADD)], epid))
				continue;

			/* device detected */
			strcpy(device, &evbuf[strlen(UEVENT_ADD)]);

			if (cpid) { /* Already running */
				killwait(cpid);
				cpid = 0;
			}

			cpid = fork();
			if (cpid < 0) {
				perror("Unable to fork process");
				continue;
			} else if (!cpid) { /* child process */
				execute(cmd);
				perror("Unable to execute command");
				exit(EXIT_FAILURE);
			}
		}
	} while (1);

	close(efd);
	close(sfd);

	return 0;
}
