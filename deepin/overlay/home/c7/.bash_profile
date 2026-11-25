# Only tty1 gets the graphical session; SSH/serial stay shells.
if [ "$(tty 2>/dev/null)" = "/dev/tty1" ]; then
	if [ -x /usr/local/bin/c7-start-xfce ]; then
		exec /usr/local/bin/c7-start-xfce
	fi
fi
