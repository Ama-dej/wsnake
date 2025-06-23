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
#include <stdbool.h>

#define WL_DISPLAY_OBJECT_ID 1
#include "functions.h"

uint32_t object_id = 2;

uint32_t wl_registry_id;
uint32_t wl_shm_id;
uint32_t wl_shm_pool_id;
uint32_t wl_buffer_id;
uint32_t wl_surface_id;
uint32_t wl_compositor_id;

bool wl_shm_bound = false;
bool wl_compositor_bound = false;

struct message_t {
	uint32_t id;
	uint32_t re_opcode_message_size;
};

uint32_t * write4(uint32_t *buffer, uint32_t val)
{
	*buffer = val;

	return buffer + 1;
}

uint32_t * writestring(uint32_t *buffer, char *s)
{
	int len = strlen(s) + 1;
	buffer = write4(buffer, len);
	int len4 = (len + 3) & ~0b11;

	uint32_t *si = malloc(len4);
	memcpy(si, s, len);
	memset((void *)(&si + len), 0, len4 - len);

	for (int i = 0; i < len4 / 4; i++)
		buffer[i] = si[i];	

	//printf("%s\n", buffer);

	free(si);
	return buffer + len4 / 4;
}

uint32_t read4(int sockfd)
{
	uint32_t tmp = 0;

	while (recv(sockfd, &tmp, sizeof(tmp), MSG_DONTWAIT | MSG_WAITALL) != sizeof(tmp)) {
		printf("read4: ni ratal prebrat\n");
	}

	/*if (recv(sockfd, &tmp, sizeof(tmp), MSG_DONTWAIT | MSG_WAITALL) != sizeof(tmp)) {
		printf("read4: ni ratal prebrat\n");
		exit(errno);
	}*/

	return tmp;
}

char * readstring(int sockfd)
{
	uint32_t len = read4(sockfd);
	uint32_t len4 = (len + 3) & ~0b11;
	char *s = malloc(len4);

	while (recv(sockfd, s, len4, MSG_DONTWAIT | MSG_WAITALL) != len4) {
		printf("readstring: ni ratal prebrat\n");
	}

	/*
	if (recv(sockfd, s, len4, MSG_DONTWAIT | MSG_WAITALL) != len4) {
		printf("readstring: ni ratal prebrat\n");
		free(s);
		exit(errno);
	}*/

	return s;
}

int wl_registry_bind(int sockfd, uint32_t name, char *interface, uint32_t version)
{
	//uint32_t bmsg[(8 + 4 + (strlen(interface) + 3) + 4 + 4) >> 2];
	
	int bmsg_len = 8 + 4 + 4 + ((strlen(interface) + 3) & ~0b11) + 4 + 4;

	uint32_t *bmsg = malloc(bmsg_len);
	uint32_t *tmp = bmsg;

	tmp = write4(tmp, wl_registry_id);
	tmp = write4(tmp, (bmsg_len << 16) | WL_REGISTRY_BIND);
	tmp = write4(tmp, name);
	tmp = writestring(tmp, interface);
	tmp = write4(tmp, version);
	tmp = write4(tmp, object_id);

	/*
	char *a = (char *)bmsg;

	for (int i = 0; i < bmsg_len / 4; i++) {
		printf("%d\n", bmsg[i]);
	} putchar('\n');

	printf("test %d %d\n", (char *)tmp - (char *)bmsg, bmsg_len);
	*/

	while (send(sockfd, bmsg, bmsg_len, 0) != bmsg_len) {
		printf("wl_registry_bind: ni ratal poslat\n");
		//exit(errno);
	}

	//printf("%d\n", send(sockfd, bmsg, bmsg_len, 0));

	//printf("jajca\n");

	free(bmsg);
	//printf("mlek\n");
	return object_id++;
}

int main()
{
	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	int shmfd = open("shm", O_RDWR | O_CREAT, 0666);

	if (sockfd == -1 || shmfd == -1) {
		printf("sockfd == -1\n");
		return -1;
	}

	if (posix_fallocate(shmfd, 0, 40000) != 0) {
		printf("ni ratal alocirat shm datoteke\n");
		return -1;
	}

	void *shm = mmap(0, 40000, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);

	if (shm == (void *)-1) {
		printf("aaaa mmap ni ratu\n");
		return -1;
	}

	// šalabajzersko
	struct sockaddr_un addr = {AF_UNIX, "/run/user/1000/wayland-0"};

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		printf("ni ratal povezat :(\n");
		return -1;
	}

	uint32_t gr_msg[3] = {WL_DISPLAY_OBJECT_ID, (12 << 16) | WL_DISPLAY_GET_REGISTRY, object_id};
	wl_registry_id = object_id++;

	if (send(sockfd, gr_msg, sizeof(gr_msg), MSG_DONTWAIT) != sizeof(gr_msg)) {
		printf("ni ratal poslat prošnje\n");
		return -1;
	}

	char buffer[1100];

	while (1) {
		if (wl_shm_bound && false) {
			uint32_t wspmsg[5] = {wl_shm_id, (20 << 16) | WL_SHM_CREATE_POOL, object_id, shmfd, 40000};
			wl_shm_pool_id = object_id++;

			while (send(sockfd, wspmsg, sizeof(wspmsg), MSG_DONTWAIT) != sizeof(wspmsg)) {
				printf("ni ratal poslat prošnje\n");
				return -1;
				//exit(errno);
			}
			
			printf("INFO: Created wl_shm_pool with id %d.\n", wl_shm_pool_id);

			uint32_t cbmsg[8] = {wl_shm_pool_id, (32 << 16) | WL_SHM_POOL_CREATE_BUFFER, object_id, 0, 100, 100, 100, 1};
			wl_buffer_id = object_id++;

			if (send(sockfd, cbmsg, sizeof(cbmsg), MSG_DONTWAIT) != sizeof(cbmsg)) {
				printf("ni ratal poslat prošnje\n");
				return -1;
			}

			printf("INFO: Created wl_buffer with id %d.\n", wl_buffer_id);

			wl_shm_bound = false;
		}

		struct message_t msg = {0, 0};
		
		if (recv(sockfd, &msg, sizeof(msg), MSG_WAITALL) != sizeof(msg))
			continue;

		int msg_size = msg.re_opcode_message_size >> 16;
		int msg_opcode = msg.re_opcode_message_size & 0xFFFF; 

		//printf(" %d %d %x\n", WL_REGISTRY_GLOBAL, msg_opcode, msg.re_opcode_message_size);

		if (msg.id == wl_registry_id && msg_opcode == WL_REGISTRY_GLOBAL) {
			uint32_t name = read4(sockfd);
			char *interface = readstring(sockfd);
			uint32_t version = read4(sockfd);

			//printf("%s\n", interface);

			if (strcmp(interface, "wl_shm") == 0) {
				wl_shm_id = wl_registry_bind(sockfd, name, interface, version);
				wl_shm_bound = true;
				printf("INFO: Bound wl_shm to id %d.\n", wl_shm_id);
			}

			if (strcmp(interface, "wl_compositor") == 0) {
				wl_compositor_id = wl_registry_bind(sockfd, name, interface, version);
				wl_compositor_bound = true;
				printf("INFO: Bound wl_compositor to id %d.\n", wl_compositor_id);
			}

			free(interface);
		} else if (msg.id == WL_DISPLAY_OBJECT_ID && msg_opcode == WL_DISPLAY_ERROR) {
			uint32_t object_id = read4(sockfd);
			uint32_t code = read4(sockfd);
			char *s = readstring(sockfd);	
			printf("ERROR: object_id %d, error %d, %s\n", object_id, code, s);	
			free(s);
		} else {
			while (recv(sockfd, buffer, msg_size - sizeof(msg), MSG_DONTWAIT | MSG_WAITALL) != msg_size - sizeof(msg));
		}
	}

	close(sockfd);
	return 0;
}
