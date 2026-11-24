# tty1 autologin: start the GNOME session.
if [ -x /usr/local/bin/c7-start-gnome ]; then
	exec /usr/local/bin/c7-start-gnome
fi
