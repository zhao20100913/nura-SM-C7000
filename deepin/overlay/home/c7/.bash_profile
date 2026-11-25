# Only tty1 gets the graphical session.  SSH and the serial console are
# login shells too; without this guard they would run startx/phosh and hang
# with no usable command prompt.
if [ "$(tty 2>/dev/null)" = "/dev/tty1" ]; then
# tty1 autologin: start the Deepin desktop (X11).
if [ -x /usr/local/bin/c7-start-deepin ]; then
	exec /usr/local/bin/c7-start-deepin
fi
fi
