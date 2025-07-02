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
#include <sys/shm.h>
#include <sys/time.h>
#include <unistd.h>

#define WL_DISPLAY_OBJECT_ID 1
#define WL_NULL 0

#define GRID_SIZE 8

#define WIDTH 800
#define HEIGHT 800
#define PIXEL_SIZE 4
#define BLOCK_WIDTH (WIDTH / GRID_SIZE)
#define BLOCK_HEIGHT (HEIGHT / GRID_SIZE)

#include "functions.h"

enum direction_t {
	LEFT,
	RIGHT,
	UP,
	DOWN
};

uint32_t object_id = 2;

uint32_t wl_registry_id = WL_NULL;
uint32_t wl_shm_id = WL_NULL;
uint32_t wl_shm_pool_id = WL_NULL;
uint32_t wl_surface_id = WL_NULL;
uint32_t wl_compositor_id = WL_NULL;
uint32_t xdg_wm_base_id = WL_NULL;
uint32_t xdg_surface_id = WL_NULL;
uint32_t xdg_toplevel_id = WL_NULL;
uint32_t wl_seat_id = WL_NULL;
uint32_t wl_keyboard_id = WL_NULL;

uint32_t wl_buffer_id = WL_NULL;
uint32_t wl_buffer2_id = WL_NULL;

uint32_t wl_shm_version;

bool wl_shm_flag = false;
bool wl_compositor_flag = false;
bool xdg_wm_base_flag = false;
bool wl_seat_flag = false;

bool surfaces_flag = false;
bool change_surface = false;

#define FDBUFFER_LEN 32
int fdbuffer[FDBUFFER_LEN];
int fdbuffer_index = 0;
int next_fd = 0;

int snake_x[GRID_SIZE * GRID_SIZE];
int snake_y[GRID_SIZE * GRID_SIZE];
int length = 1;

int fruit_x = 1;
int fruit_y = 0;

int fd;

enum direction_t direction = DOWN;
enum direction_t pending_direction = DOWN;

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

	free(si);
	return buffer + len4 / 4;
}

uint32_t * writenewid(uint32_t *buffer, char *interface, uint32_t version)
{
	buffer = writestring(buffer, interface);
	buffer = write4(buffer, version);

	return buffer;
}

uint32_t read4(uint32_t **ptr)
{
	uint32_t *tmp = *ptr;
	uint32_t val = *tmp;
	*ptr = tmp + 1;
	return val;
}

char * readstring(uint32_t **ptr)
{
	uint32_t len = read4(ptr);
	uint32_t *tmp = *ptr;

	uint32_t len4 = (len + 3) & ~0b11;
	char *s = malloc(len4);

	uint32_t * si = (uint32_t *)s;

	for (int i = 0; i < len4 / 4; i++)
		si[i] = tmp[i];

	*ptr = tmp + len4 / 4;
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

	printf("INFO: Bound %s to id %d.\n", interface, object_id);
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

	iov[0].iov_base = cpmsg;
	iov[0].iov_len = cpmsg_len;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	if (sendmsg(sockfd, &msg, 0) < 0) {

		printf("wl_shm_create_pool: ni ratal poslat msg\n");
		exit(errno);
	}

	free(cpmsg);

	printf("INFO: Created wl_shm_pool with id %d.\n", object_id);
	return object_id++;
}

int wl_shm_pool_create_buffer(int sockfd, uint32_t wl_shm_pool_id, uint32_t offset, uint32_t width, uint32_t height, uint32_t stride, uint32_t format)
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

	printf("INFO: Created wl_buffer with id %d.\n", object_id);
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

	printf("INFO: Created wl_surface with id %d.\n", object_id);
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

	printf("INFO: Pong!\n");
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

	printf("INFO: Created xdg_surface with id %d.\n", object_id);
	return object_id++;
}

int xdg_surface_get_toplevel(int sockfd, uint32_t xdg_surface_id)
{
	unsigned int gtmsg_len = 12;

	uint32_t *gtmsg = malloc(gtmsg_len);
	uint32_t *tmp = gtmsg;

	tmp = write4(tmp, xdg_surface_id);
	tmp = write4(tmp, (gtmsg_len << 16) | XDG_SURFACE_GET_TOPLEVEL);
	tmp = write4(tmp, object_id);

	if (send(sockfd, gtmsg, gtmsg_len, 0) != gtmsg_len) {
		printf("xdg_surface_get_toplevel: ni ratal poslat\n");
		exit(errno);
	}

	free(gtmsg);

	printf("INFO: Got xdg_toplevel with id %d.\n", object_id);
	return object_id++;
}

void wl_surface_commit(int sockfd, uint32_t wl_surface_id)
{
	unsigned int cmsg_len = 8;

	uint32_t *cmsg = malloc(cmsg_len);
	uint32_t *tmp = cmsg;

	tmp = write4(tmp, wl_surface_id);
	tmp = write4(tmp, (cmsg_len << 16) | WL_SURFACE_COMMIT);

	if (send(sockfd, cmsg, cmsg_len, 0) != cmsg_len) {
		printf("wl_surface_commit: ni ratal poslat\n");
		exit(errno);
	}

	free(cmsg);

	printf("INFO: Commited surface with id %d.\n", wl_surface_id);
	return;
}

void wl_surface_attach(int sockfd, uint32_t wl_surface_id, uint32_t wl_buffer_id)
{
	unsigned int amsg_len = 20;

	uint32_t *amsg = malloc(amsg_len);
	uint32_t *tmp = amsg;

	tmp = write4(tmp, wl_surface_id);
	tmp = write4(tmp, (amsg_len << 16) | WL_SURFACE_ATTACH);
	tmp = write4(tmp, wl_buffer_id);
	tmp = write4(tmp, 0);
	tmp = write4(tmp, 0);

	if (send(sockfd, amsg, amsg_len, 0) != amsg_len) {
		printf("wl_surface_attach: ni ratal poslat\n");
		exit(errno);
	}

	free(amsg);

	printf("INFO: Attached wl_buffer with id %d to wl_surface with id %d.\n", wl_buffer_id, wl_surface_id);
	return;
}

void xdg_surface_ack_configure(int sockfd, uint32_t xdg_surface_id, uint32_t serial)
{
	unsigned int acmsg_len = 12;

	uint32_t *acmsg = malloc(acmsg_len);
	uint32_t *tmp = acmsg;

	tmp = write4(tmp, xdg_surface_id);
	tmp = write4(tmp, (acmsg_len << 16) | XDG_SURFACE_ACK_CONFIGURE);
	tmp = write4(tmp, serial);

	if (send(sockfd, acmsg, acmsg_len, 0) != acmsg_len) {
		printf("xdg_surface_ack_configure: ni ratal poslat\n");
		exit(errno);
	}

	free(acmsg);

	printf("INFO: Acknowledged configure %d.\n", serial);
	return;
}

void xdg_surface_set_window_geometry(int sockfd, uint32_t xdg_surface_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	unsigned int swgmsg_len = 24;

	uint32_t *swgmsg = malloc(swgmsg_len);
	uint32_t *tmp = swgmsg;

	tmp = write4(tmp, xdg_surface_id);
	tmp = write4(tmp, (swgmsg_len << 16) | XDG_SURFACE_SET_WINDOW_GEOMETRY);
	tmp = write4(tmp, x);
	tmp = write4(tmp, y);
	tmp = write4(tmp, width);
	tmp = write4(tmp, height);

	if (send(sockfd, swgmsg, swgmsg_len, 0) != swgmsg_len) {
		printf("xdg_surface_set_window_geometry: ni ratal poslat\n");
		exit(errno);
	}

	free(swgmsg);

	return;
}

int create_shm()
{
	int shmfd = shm_open("shm0239dk023k90f", O_RDWR | O_CREAT, 0600);

	if (shmfd == -1) {
		printf("ERROR: Failed to open shm.\n");
		exit(errno);
	}

	if (posix_fallocate(shmfd, 0, WIDTH * HEIGHT * PIXEL_SIZE * 2) != 0) {
		printf("ERROR: Failed to allocate space for shm.\n");
		exit(errno);
	}

	return shmfd;
}

void * map_shm(int shmfd)
{
	void *shm = mmap(NULL, WIDTH * HEIGHT * PIXEL_SIZE * 2, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
	
	if (shm == (void *)-1) {
		printf("ERROR: Failed to mmap shm.\n");
		exit(errno);
	}

bool wl_seat_flag = false;

	return shm;
}

int wl_display_get_registry(int sockfd)
{
	unsigned int grmsg_len = 12;

	uint32_t *grmsg = malloc(grmsg_len);
	uint32_t *tmp = grmsg;

	tmp = write4(tmp, WL_DISPLAY_OBJECT_ID);
	tmp = write4(tmp, (grmsg_len << 16) | WL_DISPLAY_GET_REGISTRY);
	tmp = write4(tmp, object_id);

	if (send(sockfd, grmsg, grmsg_len, 0) != grmsg_len) {
		printf("ERROR: wl_display_get_registry request failed.\n");
		exit(errno);
	}

	free(grmsg);

	return object_id++;	
}

int connect_to_wl_socket()
{
	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (sockfd == -1) {
		printf("ERROR: Failed to connect to wayland socket.\n");
		exit(errno);
	}

	struct sockaddr_un addr = {AF_UNIX, "/run/user/1000/wayland-0"};

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		printf("ERROR: Failed to connect to wayland socket.\n");
		exit(errno);
	}

	return sockfd;
}

int getfd(struct cmsghdr *cmptr)
{
	int fd = -1;

	if (cmptr != NULL
		&& cmptr->cmsg_len == CMSG_LEN(sizeof(int))
		&& cmptr->cmsg_level == SOL_SOCKET 
		&& cmptr->cmsg_type == SCM_RIGHTS) {

		fd = *(int *)CMSG_DATA(cmptr);

		printf("INFO: Got new file descriptor %d.\n", fd);
	}
	
	return fd;
}

int wl_seat_get_keyboard(int sockfd)
{
	unsigned int gkmsg_len = 12;

	uint32_t *gkmsg = malloc(gkmsg_len);
	uint32_t *tmp = gkmsg;

	tmp = write4(tmp, wl_seat_id);
	tmp = write4(tmp, (gkmsg_len << 16) | WL_SEAT_GET_KEYBOARD);
	tmp = write4(tmp, object_id);

	if (send(sockfd, gkmsg, gkmsg_len, 0) != gkmsg_len) {
		printf("wl_seat_get_keyboard: ni ratal poslat\n");
		exit(errno);
	}

	free(gkmsg);

	printf("INFO: Got wl_keyboard with id %d.\n", object_id);
	return object_id++;
}

void wl_surface_damage_buffer(int sockfd, uint32_t wl_surface_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	unsigned int dbmsg_len = 24;

	uint32_t *dbmsg = malloc(dbmsg_len);
	uint32_t *tmp = dbmsg;

	tmp = write4(tmp, wl_surface_id);
	tmp = write4(tmp, (dbmsg_len << 16) | WL_SURFACE_DAMAGE_BUFFER);
	tmp = write4(tmp, x);
	tmp = write4(tmp, y);
	tmp = write4(tmp, width);
	tmp = write4(tmp, height);

	if (send(sockfd, dbmsg, dbmsg_len, 0) != dbmsg_len) {
		printf("wl_surface_damage_buffer: ni ratal poslat\n");
		exit(errno);
	}

	free(dbmsg);

	printf("INFO: Damaged buffer from (%d, %d) to (%d, %d).\n", x, y, x + width, y + height);
	return;
}

void paint_block(uint32_t *pixel_buffer, int x, int y, uint32_t xrgb)
{
	for (int xf = x * BLOCK_WIDTH; xf < (x + 1) * BLOCK_WIDTH; xf++) {
		for (int yf = y * BLOCK_HEIGHT; yf < (y + 1) * BLOCK_HEIGHT; yf++) {
			pixel_buffer[xf + yf * WIDTH] = xrgb;
		}
	}

	return;
}

uint32_t new_fruit_coords()
{
	uint32_t coords[GRID_SIZE * GRID_SIZE - length];
	int coord_i = 0;
	int coord = 0;

	while (coord_i < sizeof(coords) / sizeof(uint32_t)) {
		bool free = true;

		for (int i = 0; i < length; i++) {
			uint32_t pos = snake_x[i] + snake_y[i] * GRID_SIZE;
			if (pos == coord) {
				free = false;
				break;
			}
		}

		if (free)
			coords[coord_i++] = coord;

		coord++;
	}

	return coords[random() % (sizeof(coords) / sizeof(uint32_t))];
}

int main()
{
	int sockfd = connect_to_wl_socket();

	int shmfd = create_shm();
	void *shm = map_shm(shmfd);

	uint32_t *pixel_buffer = shm;

	for (int i = 0; i < WIDTH * HEIGHT * 2; i++) {
		uint8_t r = 0;
		uint8_t g = 0;
		uint8_t b = 0;
		
		pixel_buffer[i] = (r << 16) | (g << 8) | b; 
	}

	wl_registry_id = wl_display_get_registry(sockfd);

	uint32_t *cur_buffer = shm;
	uint32_t *prev_buffer = (uint32_t *)shm + WIDTH * HEIGHT;
	uint32_t *cur_buffer_id = &wl_buffer_id;
	uint32_t *prev_buffer_id = &wl_buffer2_id;

	int cntr = 0;

	bool first = false;

	struct timezone tz = {0, 0};
	struct timeval tv;

	memset(snake_x, -1, sizeof(snake_x));
	memset(snake_y, -1, sizeof(snake_y));

	snake_x[0] = 0;
	snake_y[0] = 0;

	int msec = 0;
	int interval = 250;
	gettimeofday(&tv, &tz);
	uint64_t before = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	while (1) {
		gettimeofday(&tv, &tz);
		uint64_t now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
		uint64_t difference = now - before;

		if (first && change_surface) {
			if (wl_shm_pool_id == WL_NULL)
				wl_shm_pool_id = wl_shm_create_pool(sockfd, shmfd, WIDTH * HEIGHT * PIXEL_SIZE * 2);
			if (wl_buffer_id == WL_NULL)
				wl_buffer_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, 0, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);
			if (wl_buffer2_id == WL_NULL)
				wl_buffer2_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, WIDTH * HEIGHT * PIXEL_SIZE, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);

			wl_surface_attach(sockfd, wl_surface_id, *cur_buffer_id);
			wl_surface_commit(sockfd, wl_surface_id);

			first = false;
		}

		// printf("%lu\n", difference);

		if (difference >= interval) {
			if (surfaces_flag) {
				if (wl_shm_pool_id == WL_NULL)
					wl_shm_pool_id = wl_shm_create_pool(sockfd, shmfd, WIDTH * HEIGHT * PIXEL_SIZE * 2);
				if (wl_buffer_id == WL_NULL)
					wl_buffer_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, 0, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);
				if (wl_buffer2_id == WL_NULL)
					wl_buffer2_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, WIDTH * HEIGHT * PIXEL_SIZE, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);

				direction = pending_direction;

				for (int i = 0; i < WIDTH * HEIGHT; i++) {
					cur_buffer[i] = prev_buffer[i];	
				}

				//wl_surface_damage_buffer(sockfd, wl_surface_id, 0, 0, WIDTH, HEIGHT);
				
				if (snake_x[length - 1] != -1) {
					wl_surface_damage_buffer(sockfd, wl_surface_id, snake_x[length - 1] * BLOCK_WIDTH, snake_y[length - 1] * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT);
					paint_block(cur_buffer, snake_x[length - 1], snake_y[length - 1], 0);
				}	

				for (int i = length - 1; i > 0; i--) {
					snake_x[i] = snake_x[i - 1];
					snake_y[i] = snake_y[i - 1];
				}

				switch (direction) {
					case UP: //w
						snake_y[0]--;
						break;
					case LEFT: //a
						snake_x[0]--;
						break;
					case DOWN: //s
						snake_y[0]++;
						break;
					case RIGHT: //d
						snake_x[0]++;
						break;
					default:
						break;
				}
	
				wl_surface_damage_buffer(sockfd, wl_surface_id, fruit_x * BLOCK_WIDTH, fruit_y * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT);
				paint_block(cur_buffer, fruit_x, fruit_y, 0xFF0000);

				wl_surface_damage_buffer(sockfd, wl_surface_id, snake_x[0] * BLOCK_WIDTH, snake_y[0] * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT);
				paint_block(cur_buffer, snake_x[0], snake_y[0], 0xFFFFFF);
	
				wl_surface_attach(sockfd, wl_surface_id, *cur_buffer_id);
				wl_surface_commit(sockfd, wl_surface_id);

				for (int i = 1; i < length; i++) {
					if (snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i]) {
						printf("INFO: You died.\n");
						munmap(shm, WIDTH * HEIGHT * PIXEL_SIZE * 2);
						close(shmfd);
						close(sockfd);
						return 0;
					}
				}

				if (snake_x[0] == fruit_x && snake_y[0] == fruit_y) {
					// in premakn sadež
					
					uint32_t packed_coord = new_fruit_coords();
					uint32_t x = packed_coord % GRID_SIZE;
					uint32_t y = packed_coord / GRID_SIZE;

					printf("%d\n", packed_coord);

					printf("x: %d y: %d\n", x, y);

					fruit_x = x;
					fruit_y = y;

					length++;
				}
	
				uint32_t *tmp = cur_buffer;
				cur_buffer = prev_buffer;
				prev_buffer = tmp;
	
				tmp = cur_buffer_id;
				cur_buffer_id = prev_buffer_id;
				prev_buffer_id = tmp;
	
				change_surface = false;
				cntr++;
			}

			before = now;
		}

		if (wl_compositor_flag && xdg_wm_base_flag) {
			wl_surface_id = wl_compositor_create_surface(sockfd);
			xdg_surface_id = xdg_wm_base_get_xdg_surface(sockfd, wl_surface_id);
			xdg_toplevel_id = xdg_surface_get_toplevel(sockfd, xdg_surface_id);

			wl_surface_commit(sockfd, wl_surface_id);

			wl_compositor_flag = false;
			xdg_wm_base_flag = false;
			first = true;
			surfaces_flag = true;
			continue;
		}

		if (wl_seat_flag) {
			wl_keyboard_id = wl_seat_get_keyboard(sockfd);

			wl_seat_flag = false;
			continue;
		}

		// TODO: ta del z pridobivanjem sporočila se da sprement v prisrčno funkcijo
		struct msghdr mmsg;
		struct iovec iov[1];
		uint32_t hdr[2];

		union {
			struct cmsghdr cm;
			char control[CMSG_SPACE(sizeof(int))];
		} control_un;
		struct cmsghdr *cmptr;

		mmsg.msg_control = control_un.control;
		mmsg.msg_controllen = sizeof(control_un.control);

		mmsg.msg_name = NULL;
		mmsg.msg_namelen = 0;

		iov[0].iov_base = hdr;
		iov[0].iov_len = sizeof(hdr); 
		mmsg.msg_iov = iov;
		mmsg.msg_iovlen = 1;

		int status = recvmsg(sockfd, &mmsg, MSG_DONTWAIT | MSG_WAITALL);

		if (status != sizeof(hdr) || status == EWOULDBLOCK || status == EAGAIN) {
			usleep(5);
			continue;
		} else if (status < 0) {
			printf("ERROR: Failed to receive message header.\n");
			return -1;
		}	

		if ((fd = getfd(CMSG_FIRSTHDR(&mmsg))) != -1) {
			fdbuffer[fdbuffer_index++] = fd;
			fdbuffer_index %= FDBUFFER_LEN;
		}

		uint32_t object_id = hdr[0];
		uint16_t msg_size = hdr[1] >> 16;
		uint16_t msg_opcode = hdr[1] & 0xFFFF; 

		//printf("%d %d %d\n", object_id, msg_opcode, msg_size);

		uint32_t *msg_contents = NULL;

		if (msg_size > 8) {
			msg_contents = malloc(msg_size - sizeof(hdr));
	
			iov[0].iov_base = msg_contents;
			iov[0].iov_len = msg_size - sizeof(hdr);
	
			if (recvmsg(sockfd, &mmsg, MSG_WAITALL) <= 0) {
				printf("ERROR: Failed to receive message contents.\n");
				return -1;
			}

			if ((fd = getfd(CMSG_FIRSTHDR(&mmsg))) != -1) {
				fdbuffer[fdbuffer_index++] = fd;
				fdbuffer_index %= FDBUFFER_LEN;
			}
	
			/* for (int i = 0; i < (msg_size - sizeof(hdr)) / 4; i++) {
				printf("%x\n", msg_contents[i]);
			} */
		}

		uint32_t *tmp = msg_contents;
		uint32_t **ptr = &tmp;

		if (object_id == wl_registry_id && msg_opcode == WL_REGISTRY_GLOBAL) {
			uint32_t name = read4(ptr);
			char *interface = readstring(ptr);
			uint32_t version = read4(ptr);

			//printf("%s\n", interface);

			if (strcmp(interface, "wl_shm") == 0) {
				wl_shm_id = wl_registry_bind(sockfd, name, interface, version);
				wl_shm_version = version;
				wl_shm_flag = true;
			} else if (strcmp(interface, "wl_compositor") == 0) {
				wl_compositor_id = wl_registry_bind(sockfd, name, interface, version);
				wl_compositor_flag = true;
			} else if (strcmp(interface, "xdg_wm_base") == 0) {
				xdg_wm_base_id = wl_registry_bind(sockfd, name, interface, version);
				xdg_wm_base_flag = true;
			} else if (strcmp(interface, "wl_seat") == 0) {
				wl_seat_id = wl_registry_bind(sockfd, name, interface, version);
				wl_seat_flag = true;
			}

			free(interface);
		} else if (object_id == WL_DISPLAY_OBJECT_ID && msg_opcode == WL_DISPLAY_ERROR) {
			uint32_t object_id = read4(ptr);
			uint32_t code = read4(ptr);
			char *s = readstring(ptr);	

			printf("ERROR: object_id %d, error %d, %s\n", object_id, code, s);	
			free(s);
			return -1;
		} else if (object_id == xdg_wm_base_id && msg_opcode == XDG_WM_BASE_PING) {
			uint32_t serial = read4(ptr);	
			printf("INFO: Ping!\n");
			
			xdg_wm_base_pong(sockfd, serial);
		} else if (object_id == xdg_surface_id && msg_opcode == XDG_SURFACE_CONFIGURE) { 
			uint32_t serial = read4(ptr);
			printf("INFO (xdg_surface): Suggested surface configuration change %d.\n", serial);

			xdg_surface_ack_configure(sockfd, xdg_surface_id, serial);
			change_surface = true;
		} else if (object_id == wl_shm_id && msg_opcode == WL_SHM_FORMAT) {
	  		uint32_t format = read4(ptr);

			printf("INFO (wl_shm): Available pixel format 0x%x.\n", format);	
		} else if (object_id == xdg_toplevel_id && msg_opcode == XDG_TOPLEVEL_CLOSE) {
			printf("Goodbye...\n");

			// TODO: reč wl_seatu da ga zapuščaš
			munmap(shm, WIDTH * HEIGHT * PIXEL_SIZE * 2);
			close(shmfd);
			close(sockfd);
			return 0; 
		} else if (object_id == wl_buffer_id && msg_opcode == WL_BUFFER_RELEASE) {
			printf("INFO: Compositor released wl_buffer %d.\n", wl_buffer_id);	

		} else if (object_id == wl_buffer2_id && msg_opcode == WL_BUFFER_RELEASE) {
			printf("INFO: Compositor released wl_buffer %d.\n", wl_buffer2_id);	

		} else if (object_id == wl_keyboard_id && msg_opcode == WL_KEYBOARD_KEYMAP) {
			fd = fdbuffer[next_fd++];
			next_fd %= FDBUFFER_LEN;

			uint32_t format = read4(ptr);
			uint32_t size = read4(ptr);

			char *keymap = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);

			if (keymap == (void *)-1) {
				printf("ERROR: Failed to map keymap data.\n");
				exit(errno);
			}

			//printf("%s", keymap); // <- Spam.

			munmap(keymap, size);

			printf("INFO: Read keymap data.\n");
		} else if (object_id == wl_keyboard_id && msg_opcode == WL_KEYBOARD_KEY) {
			uint32_t serial = read4(ptr);
			uint32_t time = read4(ptr);
			uint32_t key = read4(ptr) + 8; // ne vem zakaj +8, pač tko piše v wayland specifikaciji
			uint32_t state = read4(ptr);

			char *state_s = "???";

			if (state == 0)
				state_s = "released";
			else if (state == 1)
			  	state_s = "pressed";	

			/* DEBUG */
			switch (key) {
				case 25: //w
					printf("INFO: Key w was %s.\n", state_s);
					break;
				case 38: //a
					printf("INFO: Key a was %s.\n", state_s);
					break;
				case 39: //s
					printf("INFO: Key s was %s.\n", state_s);
					break;
				case 40: //d
					printf("INFO: Key d was %s.\n", state_s);
					break;
				default:
					printf("INFO: Key %d was %s.\n", key, state_s);
					break;
			}

			if (state == 1) {
				switch (key) {
					case 25: //w
						if (direction != DOWN)
							pending_direction = UP;
						break;
					case 38: //a
						if (direction != RIGHT)
							pending_direction = LEFT;
						break;
					case 39: //s
						if (direction != UP)
							pending_direction = DOWN;
						break;
					case 40: //d
						if (direction != LEFT)
							pending_direction = RIGHT;
						break;
					default:
						break;
				}
			}
		} else {
			printf("%d %d\n", object_id, msg_opcode);
		}

		if (msg_contents != NULL)
			free(msg_contents);
	}

	close(sockfd);
	return 0;
}
