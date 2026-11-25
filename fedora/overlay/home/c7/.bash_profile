# Only tty1 gets the graphical session.  SSH and the serial console are
# login shells too; without this guard they would run startx/phosh and hang
# with no usable command prompt.
if [ "$(tty 2>/dev/null)" = "/dev/tty1" ]; then
# tty1 autologin: start the Phosh session.
if [ -x /usr/local/bin/c7-start-phosh ]; then
	exec /usr/local/bin/c7-start-phosh
fi
fi
