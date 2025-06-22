#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>

#define WL_DISPLAY_OBJECT_ID 1
#include "functions.h"

uint32_t object_id = 2;

uint32_t wl_registry_id;
uint32_t wl_shm_id;
uint32_t wl_shm_pool_id;

struct message_t {
	uint32_t id;
	uint32_t re_opcode_message_size;
};

int wl_registry_bind(int sockfd, uint32_t name)
{
	uint32_t bind_msg[4] = {wl_registry_id, (16 << 16) | WL_REGISTRY_BIND, name, object_id};

	if (send(sockfd, bind_msg, sizeof(bind_msg), MSG_DONTWAIT) != sizeof(bind_msg)) {
		printf("wl_registry_bind: ni ratal poslat\n");
		exit(errno);
	}

	return object_id++;
}

uint32_t read4(int sockfd)
{
	uint32_t tmp = 0;

	if (recv(sockfd, &tmp, sizeof(tmp), MSG_DONTWAIT | MSG_WAITALL) != sizeof(tmp)) {
		printf("read4: ni ratal prebrat\n");
		exit(errno);
	}

	return tmp;
}

char * readstring(int sockfd)
{
	uint32_t len = read4(sockfd);
	uint32_t len4 = (len + 3) & ~0b11;
	char *s = malloc(len4);

	if (recv(sockfd, s, len4, MSG_DONTWAIT | MSG_WAITALL) != len4) {
		printf("readstring: ni ratal prebrat\n");
		free(s);
		exit(errno);
	}

	return s;
}

int main()
{
	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	int shmfd = open("shm", O_RDWR | O_CREAT, 0666);

	if (sockfd == -1 || shmfd == -1) {
		printf("sockfd == -1\n");
		exit(errno);
	}

	if (posix_fallocate(shmfd, 0, 40000) != 0) {
		printf("ni ratal alocirat shm datoteke\n");
		return -1;
	}

	void *shm = mmap(0, 40000, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);

	if (shm == (void *)-1) {
		printf("aaaa mmap ni ratu\n");
		exit(errno);
	}

	// šalabajzersko
	struct sockaddr_un addr = {AF_UNIX, "/run/user/1000/wayland-0"};

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		printf("ni ratal povezat :(\n");
		exit(errno);
	}

	uint32_t gr_msg[3] = {WL_DISPLAY_OBJECT_ID, (12 << 16) | WL_DISPLAY_GET_REGISTRY, object_id};
	wl_registry_id = object_id++;

	if (send(sockfd, gr_msg, sizeof(gr_msg), MSG_DONTWAIT) != sizeof(gr_msg)) {
		printf("ni ratal poslat prošnje\n");
		exit(errno);
	}

	char buffer[1100];

	while (1) {
		struct message_t msg = {0, 0};
		
		if (recv(sockfd, &msg, sizeof(msg), MSG_DONTWAIT | MSG_WAITALL) != sizeof(msg))
			continue;

		int msg_size = msg.re_opcode_message_size >> 16;
		int msg_opcode = msg.re_opcode_message_size & 0xFFFF; 

		// printf(" %d %d %x\n", WL_REGISTRY_GLOBAL, msg_opcode, msg.re_opcode_message_size);

		if (msg.id == wl_registry_id && msg_opcode == WL_REGISTRY_GLOBAL) {
			uint32_t name = read4(sockfd);
			char *interface = readstring(sockfd);
			uint32_t version = read4(sockfd);

			printf("%s\n", interface);

			if (strcmp(interface, "wl_shm") == 0) {
				wl_shm_id = wl_registry_bind(sockfd, name);
				printf("bajndu sm wl_shm\n");

				int wspmsg[5] = {wl_shm_id, (20 << 16) | WL_SHM_CREATE_POOL, object_id, shmfd, 40000};
				wl_shm_pool_id = object_id++;

				if (send(sockfd, wspmsg, sizeof(wspmsg), MSG_DONTWAIT) != sizeof(wspmsg)) {
					printf("ni ratal poslat prošnje\n");
					exit(errno);
				}
			}

			free(interface);
		} else if (false) {
			
		} else {
			while (recv(sockfd, buffer, msg_size - sizeof(msg), MSG_DONTWAIT | MSG_WAITALL) != msg_size - sizeof(msg));
		}
	}

	close(sockfd);
	return 0;
}
