// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power button -> display off/on for the Galaxy C7 running phosh.
 *
 * elogind is configured with HandlePowerKey=ignore so the button cannot
 * power the phone off, but that also means nothing blanks the screen.  This
 * daemon waits for the pm8941 pwrkey input device (it can appear after the
 * service starts), then runs /usr/bin/c7-screen as the session user.
 *
 * c7-screen tries wlopm (wlr-output-power-management, the protocol phoc
 * uses for blanking), wlr-randr and the gsd-power brightness call, and logs
 * every attempt to /var/log/c7-screen.log.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/input.h>

#define SESSION_UID 1000
#define SESSION_RUNTIME "/run/user/1000"
#define LOGFILE "/var/log/c7-pwrkey.log"

static int logfd = -1;

static void logmsg(const char *fmt, ...)
{
	va_list ap;

	if (logfd < 0)
		return;
	va_start(ap, fmt);
	vdprintf(logfd, fmt, ap);
	va_end(ap);
	dprintf(logfd, "\n");
}

static int find_power_device(char *out, size_t len)
{
	char path[64], name[256];
	int i;

	for (i = 0; i < 32; i++) {
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
			if (strcasestr(name, "pwrkey") ||
			    strcasestr(name, "power button")) {
				close(fd);
				snprintf(out, len, "%s", path);
				return 0;
			}
		}
		close(fd);
	}
	return -1;
}

static pid_t toggle_screen(void)
{
	pid_t pid = fork();

	if (pid != 0) {
		if (pid < 0)
			logmsg("fork failed: %s", strerror(errno));
		return pid;
	}

	if (setgid(SESSION_UID) || setuid(SESSION_UID))
		_exit(1);
	setenv("XDG_RUNTIME_DIR", SESSION_RUNTIME, 1);
	execl("/usr/bin/c7-screen", "c7-screen", "toggle", (char *)NULL);
	_exit(127);
}

int main(int argc, char **argv)
{
	char dev[64];
	struct input_event ev;
	int fd;

	logfd = open(LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
	logmsg("c7-pwrkey starting (pid %d)", getpid());

	if (argc > 1 && strcmp(argv[1], "--toggle") == 0) {
		pid_t pid = toggle_screen();

		if (pid > 0)
			waitpid(pid, NULL, 0);
		return 0;
	}

	for (;;) {
		if (find_power_device(dev, sizeof(dev)) < 0) {
			sleep(2);
			continue;
		}
		logmsg("using %s", dev);
		fd = open(dev, O_RDONLY);
		if (fd < 0) {
			logmsg("open %s: %s", dev, strerror(errno));
			sleep(2);
			continue;
		}
		while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
			if (ev.type != EV_KEY || ev.code != KEY_POWER ||
			    ev.value != 1)
				continue;
			logmsg("power key pressed");
			toggle_screen();
		}
		close(fd);
		sleep(1);
	}
}
