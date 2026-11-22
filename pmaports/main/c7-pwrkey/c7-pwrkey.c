// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power button -> display off/on for the Galaxy C7 running phosh.
 *
 * elogind is configured with HandlePowerKey=ignore so the button cannot
 * power the phone off by accident, but that also means nothing blanks the
 * screen.  This small daemon reads KEY_POWER from the pm8941 pwrkey device
 * and toggles the DSI output through wlr-randr as the session user.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define STATE_FILE "/run/c7-pwrkey.off"
#define SESSION_UID 1000
#define SESSION_RUNTIME "/run/user/1000"
#define OUTPUT "DSI-1"

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
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
		    strstr(name, "pwrkey")) {
			close(fd);
			snprintf(out, len, "%s", path);
			return 0;
		}
		close(fd);
	}
	return -1;
}

static int screen_is_off(void)
{
	return access(STATE_FILE, F_OK) == 0;
}

static void toggle_screen(int was_off)
{
	pid_t pid = fork();

	if (pid != 0)
		return;

	if (setgid(SESSION_UID) || setuid(SESSION_UID))
		_exit(1);
	setenv("XDG_RUNTIME_DIR", SESSION_RUNTIME, 1);
	if (!getenv("WAYLAND_DISPLAY"))
		setenv("WAYLAND_DISPLAY", "wayland-0", 1);

	execl("/usr/bin/wlr-randr", "wlr-randr", "--output", OUTPUT,
	      was_off ? "--on" : "--off", (char *)NULL);
	_exit(127);
}

int main(void)
{
	char dev[64];
	struct input_event ev;

	if (find_power_device(dev, sizeof(dev)) < 0) {
		fprintf(stderr, "c7-pwrkey: no pwrkey input device\n");
		return 1;
	}

	int fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("c7-pwrkey: open");
		return 1;
	}

	for (;;) {
		ssize_t n = read(fd, &ev, sizeof(ev));

		if (n != sizeof(ev))
			continue;
		if (ev.type != EV_KEY || ev.code != KEY_POWER || ev.value != 1)
			continue;

		int was_off = screen_is_off();

		if (was_off)
			unlink(STATE_FILE);
		else if (creat(STATE_FILE, 0644) < 0)
			continue;
		toggle_screen(was_off);
	}
}
