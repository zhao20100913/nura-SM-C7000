# tty1 autologin: start the Deepin desktop (X11).
if [ -x /usr/local/bin/c7-start-deepin ]; then
	exec /usr/local/bin/c7-start-deepin
fi
