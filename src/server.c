#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define SOCK_PATH_HEAD  "/tmp/.scnm_"

static char *
make_sock_path(pid_t pid)
{
	char *path;
	const size_t len = sizeof(SOCK_PATH_HEAD) + 8;

	path = malloc(len);

	if (path == NULL)
		return NULL;

	(void)snprintf(path, len, SOCK_PATH_HEAD "%08x", (unsigned int)pid);

	return path;
}

static inline void
init_unix_sockaddr(struct sockaddr_un *addr, const char *socket_path)
{
	memset(addr, 0, sizeof(*addr));

	addr->sun_family = AF_UNIX;
	strncpy(addr->sun_path, socket_path, sizeof(addr->sun_path) - 1);
}


void
server_loop(pid_t pid)
{
	int err;
	int fd_max;

	fd_set master;

	int fd;
	int listener;

	ssize_t nbytes;
	char buf[512];

	struct sockaddr_un addr;

	const char *socket_path = make_socket_path(pid);

	if (socket_path == NULL)
		return;

	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	listener = socket(AF_UNIX, SOCK_STREAM, 0);

	if (listener < 0) {
		free(socket_path);
		return;
	}

	init_unix_sockaddr(&addr, socket_path);

	/* Always remove stale conenctions first. */
	unlink(socket_path);

	free(socket_path);

	err = bind(listener, (struct sockaddr *)&addr, sizeof(addr));

	if (err < 0) {
		close(listener);
		return;
	}

	err = listen(listener, 10);

	if (err == -1) {
		close(listener);
		return;
	}

	FD_SET(listener, &master);

	fd_max = listener;

	for (;;) {
		int i;
		fd_set read_fds = master;

		err = select(fd_max + 1, &read_fds, NULL, NULL, NULL);

		if (err == -1) {
			return;
		}

		for (i = 0; i <= fd_max; ++i) {
			int j;

			if (!FD_ISSET(i, &read_fds))
				continue;

			/* New connection */
			if (i == listener) {
				fd = accept(listener, NULL, NULL);

				if (fd == -1) {
					/* accept failure */
					continue;
				}

				FD_SET(fd, &master);

				if (fd > fd_max)
					fd_max = fd;

				continue;
			}

			/* Old connection */
			nbytes = read(i, buf, sizeof(buf));

			if (nbytes <= 0) {
				if (nbytes == 0)
					; /* closed */
				close(i);
				FD_CLR(i, &master);
				continue;
			}

			/* send back... */
			err = write(i, buf, nbytes);

			if (err == -1)
				continue;
		}
	}
}

void
client(pid_t pid)
{
	int err;
	int sock;

	struct sockaddr_un addr;

	const char *socket_path = make_socket_path(pid);

	if (socket_path == NULL)
		return;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if (sock == -1) {
		free(socket_path);
		return;
	}

	init_unix_sockaddr(&addr, socket_path);
	free(socket_path);

	err = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

	if (err == -1) {
		close(sock);
		return;
	}

	/* write loop or something. */

	close(sock);
}
