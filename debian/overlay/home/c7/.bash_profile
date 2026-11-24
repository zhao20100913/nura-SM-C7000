# Start the phone shell on the first virtual console after the serial/console
# autologin.  Everything else (ssh, serial) still gets a normal shell.
if [ "$(tty 2>/dev/null)" = "/dev/tty1" ] && [ -z "${WAYLAND_DISPLAY:-}" ] \
	&& [ -z "${C7_SESSION_STARTED:-}" ]; then
	export C7_SESSION_STARTED=1
	if command -v denial-session >/dev/null 2>&1; then
		exec /usr/local/bin/c7-start-denial
	fi
	exec /usr/local/bin/c7-start-phosh
fi
