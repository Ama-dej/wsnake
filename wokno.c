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

int wl_registry_id = -1;
int wl_shm_id = -1;
int wl_shm_pool_id = -1;
int wl_buffer_id = -1;
int wl_surface_id = -1;
int wl_compositor_id = -1;
int xdg_wm_base_id = -1;
int xdg_surface_id = -1;
int xdg_toplevel_id = -1;

uint32_t wl_shm_version;

bool wl_shm_flag = false;
bool wl_compositor_flag = false;
bool xdg_wm_base_flag = false;
bool wl_surface_flag = false;

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
	unsigned int len = strlen(s) + 1;
	buffer = write4(buffer, len);
	unsigned int len4 = (len + 3) & ~0b11;
	
	uint32_t *si = malloc(len4);
	memcpy(si, s, len);
	memset((uint8_t *)si + len, 0, len4 - len);

	for (int i = 0; i < len4 / 4; i++)
		buffer[i] = si[i];	

	//printf("%s\n", buffer);

	free(si);
	return buffer + len4 / 4;
}

uint32_t * writenewid(uint32_t *buffer, char *interface, uint32_t version)
{
	buffer = writestring(buffer, interface);
	buffer = write4(buffer, version);

	return buffer;
}

uint32_t read4(int sockfd)
{
	uint32_t tmp = 0;

	if (recv(sockfd, &tmp, sizeof(tmp), MSG_WAITALL) != sizeof(tmp)) {
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

	if (recv(sockfd, s, len4, MSG_WAITALL) != len4) {
		printf("readstring: ni ratal prebrat\n");
		free(s);
		exit(errno);
	}

	return s;
}

int wl_registry_bind(int sockfd, uint32_t name, char *interface, uint32_t version)
{
	unsigned int bmsg_len = 24 + ((strlen(interface) + 3) & ~0b11);
	
	uint32_t *bmsg = malloc(bmsg_len);
	uint32_t *tmp = bmsg;

	tmp = write4(tmp, wl_registry_id);
	tmp = write4(tmp, (bmsg_len << 16) | WL_REGISTRY_BIND);
	tmp = write4(tmp, name);
	tmp = writenewid(tmp, interface, version);
	tmp = write4(tmp, object_id);

	if (send(sockfd, bmsg, bmsg_len, 0) != bmsg_len) {
		printf("wl_registry_bind: ni ratal poslat\n");
		exit(errno);
	}

	free(bmsg);

	return object_id++;
}

int wl_shm_create_pool(int sockfd, int shmfd, uint32_t size)
{
	unsigned int cpmsg_len = 16;

	uint32_t *cpmsg = malloc(cpmsg_len);
	uint32_t *tmp = cpmsg;

	tmp = write4(tmp, wl_shm_id);
	tmp = write4(tmp, (cpmsg_len << 16) | WL_SHM_CREATE_POOL);
	tmp = write4(tmp, object_id);
	tmp = write4(tmp, size);

	struct msghdr msg;
	struct iovec iov[1];

	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;

	struct cmsghdr *cmptr;

	msg.msg_control = control_un.control;
	msg.msg_controllen = sizeof(control_un.control);

	cmptr = CMSG_FIRSTHDR(&msg);
	cmptr->cmsg_len = CMSG_LEN(sizeof(int));
	cmptr->cmsg_level = SOL_SOCKET;
	cmptr->cmsg_type = SCM_RIGHTS;
	*((int *)CMSG_DATA(cmptr)) = shmfd;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	iov[0].iov_base = "";
	iov[0].iov_len = 1;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	if (sendmsg(sockfd, &msg, 0) < 0) {
		printf("wl_shm_create_pool: ni ratal poslat msg\n");
		exit(errno);
	}

	if (send(sockfd, cpmsg, cpmsg_len, 0) != cpmsg_len) {
		printf("wl_shm_create_pool: ni ratal poslat\n");
		exit(errno);
	}

	free(cpmsg);

	return object_id++;
}

int wl_shm_pool_create_buffer(int sockfd, uint32_t offset, uint32_t width, uint32_t height, uint32_t stride, uint32_t format)
{
	unsigned int cbmsg_len = 32;

	uint32_t *cbmsg = malloc(cbmsg_len);
	uint32_t *tmp = cbmsg;

	tmp = write4(tmp, wl_shm_pool_id);
	tmp = write4(tmp, (cbmsg_len << 16) | WL_SHM_POOL_CREATE_BUFFER);
	tmp = write4(tmp, object_id);
	tmp = write4(tmp, offset);
	tmp = write4(tmp, width);
	tmp = write4(tmp, height);
	tmp = write4(tmp, stride);
	tmp = write4(tmp, format);

	if (send(sockfd, cbmsg, cbmsg_len, 0) != cbmsg_len) {
		printf("wl_shm_pool_create_buffer: ni ratal poslat\n");
		exit(errno);
	}

	free(cbmsg);

	return object_id++;
}

int wl_compositor_create_surface(int sockfd)
{
	unsigned int csmsg_len = 12;

	uint32_t *csmsg = malloc(csmsg_len);
	uint32_t *tmp = csmsg;

	tmp = write4(tmp, wl_compositor_id);
	tmp = write4(tmp, (csmsg_len << 16) | WL_COMPOSITOR_CREATE_SURFACE);
	tmp = write4(tmp, object_id);

	if (send(sockfd, csmsg, csmsg_len, 0) != csmsg_len) {
		printf("wl_compositor_create_surface: ni ratal poslat\n");
		exit(errno);
	}

	free(csmsg);

	return object_id++;
}

void xdg_wm_base_pong(int sockfd, uint32_t serial)
{
	unsigned int pmsg_len = 12;

	uint32_t *pmsg = malloc(pmsg_len);
	uint32_t *tmp = pmsg;

	tmp = write4(tmp, xdg_wm_base_id);
	tmp = write4(tmp, (pmsg_len << 16) | XDG_WM_BASE_PONG);
	tmp = write4(tmp, serial);

	if (send(sockfd, pmsg, pmsg_len, 0) != pmsg_len) {
		printf("xdg_wm_base_pong: ni ratal poslat\n");
		exit(errno);
	}

	free(pmsg);

	return;
}

int xdg_wm_base_get_xdg_surface(int sockfd, uint32_t wl_surface_id)
{
	unsigned int gxsmsg_len = 16;

	uint32_t *gxsmsg = malloc(gxsmsg_len);
	uint32_t *tmp = gxsmsg;

	tmp = write4(tmp, xdg_wm_base_id);
	tmp = write4(tmp, (gxsmsg_len << 16) | XDG_WM_BASE_GET_XDG_SURFACE);
	tmp = write4(tmp, object_id);
	tmp = write4(tmp, wl_surface_id);

	if (send(sockfd, gxsmsg, gxsmsg_len, 0) != gxsmsg_len) {
		printf("xdg_wm_base_get_xdg_surface: ni ratal poslat\n");
		exit(errno);
	}

	free(gxsmsg);

	return object_id++;
}

int xdg_surface_get_toplevel(int sockfd)
{
	unsigned int gtmsg_len = 12;

	uint32_t *gtmsg = malloc(gtmsg_len);
	uint32_t *tmp = gtmsg;

	tmp = write4(tmp, xdg_wm_base_id);
	tmp = write4(tmp, (gtmsg_len << 16) | XDG_WM_BASE_GET_XDG_SURFACE);
	tmp = write4(tmp, object_id);

	if (send(sockfd, gtmsg, gtmsg_len, 0) != gtmsg_len) {
		printf("xdg_surface_get_toplevel: ni ratal poslat\n");
		exit(errno);
	}

	free(gtmsg);

	return object_id++;
}

void wl_surface_commit(int sockfd)
{
	unsigned int cmsg_len = 8;

	uint32_t *cmsg = malloc(cmsg_len);
	uint32_t *tmp = cmsg;

	tmp = write4(tmp, xdg_wm_base_id);
	tmp = write4(tmp, (cmsg_len << 16) | XDG_WM_BASE_GET_XDG_SURFACE);

	if (send(sockfd, cmsg, cmsg_len, 0) != cmsg_len) {
		printf("wl_surface_commit: ni ratal poslat\n");
		exit(errno);
	}

	free(cmsg);

	return;
}

void wl_surface_attach(int sockfd, uint32_t wl_buffer_id)
{
	unsigned int amsg_len = 20;

	uint32_t *amsg = malloc(amsg_len);
	uint32_t *tmp = amsg;

	tmp = write4(tmp, xdg_wm_base_id);
	tmp = write4(tmp, (amsg_len << 16) | XDG_WM_BASE_GET_XDG_SURFACE);
	tmp = write4(tmp, wl_buffer_id);
	tmp = write4(tmp, 0);
	tmp = write4(tmp, 0);

	if (send(sockfd, amsg, amsg_len, 0) != amsg_len) {
		printf("wl_surface_attach: ni ratal poslat\n");
		exit(errno);
	}

	free(amsg);

	return;
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

	if (send(sockfd, gr_msg, sizeof(gr_msg), 0) != sizeof(gr_msg)) {
		printf("ni ratal poslat prošnje\n");
		return -1;
	}

	char buffer[1100];

	while (1) {
		if (wl_shm_flag) {
			wl_shm_pool_id = wl_shm_create_pool(sockfd, shmfd, 40000);
			printf("INFO: Created wl_shm_pool with id %d.\n", wl_shm_pool_id);

			wl_buffer_id = wl_shm_pool_create_buffer(sockfd, 0, 100, 100, 100 * 4, 1); // format
			printf("INFO: Created wl_buffer with id %d.\n", wl_buffer_id);

			wl_shm_flag = false;
		}

		if (wl_compositor_flag) {
			wl_surface_id = wl_compositor_create_surface(sockfd);
			printf("INFO: Created wl_surface with id %d.\n", wl_surface_id);

			wl_compositor_flag = false;
			wl_surface_flag = true;
		}

		if (xdg_wm_base_flag && wl_surface_flag) {
			xdg_surface_id = xdg_wm_base_get_xdg_surface(sockfd, wl_surface_id);
			printf("INFO: Created xdg_surface with id %d.\n", xdg_surface_id);

			xdg_toplevel_id = xdg_surface_get_toplevel(sockfd);
			printf("INFO: Got xdg_toplevel with id %d.\n", xdg_toplevel_id);

			//wl_surface_attach(sockfd, wl_buffer_id);
			wl_surface_commit(sockfd);
			printf("INFO: Commited surface contents.\n");

			xdg_wm_base_flag = false;
			wl_surface_flag = false;	
		}

		struct message_t msg = {0, 0};
		
		if (recv(sockfd, &msg, sizeof(msg), MSG_WAITALL | MSG_DONTWAIT) != sizeof(msg))
			continue;

		int msg_size = msg.re_opcode_message_size >> 16;
		int msg_opcode = msg.re_opcode_message_size & 0xFFFF; 

		printf("%d %d\n", msg.id, msg_opcode);

		//printf(" %d %d %x\n", WL_REGISTRY_GLOBAL, msg_opcode, msg.re_opcode_message_size);

		if (msg.id == wl_registry_id && msg_opcode == WL_REGISTRY_GLOBAL) {
			uint32_t name = read4(sockfd);
			char *interface = readstring(sockfd);
			uint32_t version = read4(sockfd);

			//printf("%s\n", interface);

			if (strcmp(interface, "wl_shm") == 0) {
				wl_shm_id = wl_registry_bind(sockfd, name, interface, version);
				wl_shm_version = version;
				wl_shm_flag = true;
				printf("INFO: Bound wl_shm to id %d.\n", wl_shm_id);
			} else if (strcmp(interface, "wl_compositor") == 0) {
				wl_compositor_id = wl_registry_bind(sockfd, name, interface, version);
				wl_compositor_flag = true;
				printf("INFO: Bound wl_compositor to id %d.\n", wl_compositor_id);
			} else if (strcmp(interface, "xdg_wm_base") == 0) {
				xdg_wm_base_id = wl_registry_bind(sockfd, name, interface, version);
				xdg_wm_base_flag = true;
				printf("INFO: Bound xdg_wm_base to id %d.\n", xdg_wm_base_id);
			}

			free(interface);
		} else if (msg.id == WL_DISPLAY_OBJECT_ID && msg_opcode == WL_DISPLAY_ERROR) {
			uint32_t object_id = read4(sockfd);
			uint32_t code = read4(sockfd);
			char *s = readstring(sockfd);	

			printf("ERROR: object_id %d, error %d, %s\n", object_id, code, s);	
			free(s);
			return -1;
		} else if (msg.id == xdg_wm_base_id && msg_opcode == XDG_WM_BASE_PING) {
			uint32_t serial = read4(sockfd);	
			
			xdg_wm_base_pong(sockfd, serial);

			printf("INFO (xdg_wm_base): Ping! Pong!\n");
		} else if (msg.id == xdg_surface_id && msg_opcode == XDG_SURFACE_CONFIGURE) { 
			printf("INFO: fagergerg\n");
		} else if (msg.id == wl_shm_id && msg_opcode == WL_SHM_FORMAT) {
	  		uint32_t format = read4(sockfd);

			printf("INFO (wl_shm): Available pixel format 0x%x\n", format);	
		} else if (msg.id == xdg_toplevel_id && msg_opcode == XDG_TOPLEVEL_CLOSE) {
			printf("Goodbye...\n");
			close(sockfd);
			return 0; 
		} else {
			while (recv(sockfd, buffer, msg_size - sizeof(msg), MSG_WAITALL) != msg_size - sizeof(msg));
		}
	}

	close(sockfd);
	return 0;
}
