#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <stdbool.h>

#include <sys/mman.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/socket.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "wayland.c"
#include "functions.h"

#define GRID_SIZE 8

#define WIDTH 800
#define HEIGHT 800
#define PIXEL_SIZE 4
#define BLOCK_WIDTH (WIDTH / GRID_SIZE)
#define BLOCK_HEIGHT (HEIGHT / GRID_SIZE)

enum direction_t {
	LEFT,
	RIGHT,
	UP,
	DOWN
};

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

bool wl_shm_flag = false;
bool wl_compositor_flag = false;
bool xdg_wm_base_flag = false;
bool wl_seat_flag = false;

bool surfaces_flag = false;
bool change_surface = false;

char key_buffer[4];
int key_buffer_head = 0;
int key_buffer_tail = 0;

#define FDBUFFER_LEN 32
int fdbuffer[FDBUFFER_LEN];
int fdbuffer_index = 0;
int next_fd = 0;

int snake_x[GRID_SIZE * GRID_SIZE];
int snake_y[GRID_SIZE * GRID_SIZE];
int length = 1;

int fruit_x = GRID_SIZE / 2;
int fruit_y = GRID_SIZE / 2;

int fd;

enum direction_t direction = DOWN;

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

	return shm;
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
	uint32_t coord_i = 0;
	uint32_t coord = 0;

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

void write_key(char c)
{
	if ((key_buffer_head + 1) % 4 != key_buffer_tail) {
		key_buffer[key_buffer_head] = c;
		key_buffer_head = (key_buffer_head + 1) % 4;	
	}
}

char get_key()
{
	if (key_buffer_tail != key_buffer_head) {
		char c = key_buffer[key_buffer_tail++];
		key_buffer_tail %= 4;
		return c;
	} else {
		return -1;
	}
}

void quit(char *msg, void * shm, int shmfd, int sockfd, int code)
{
	wl_keyboard_release(sockfd, wl_keyboard_id);
	wl_seat_release(sockfd, wl_seat_id);
	munmap(shm, WIDTH * HEIGHT * PIXEL_SIZE * 2);
	close(shmfd);
	close(sockfd);
	printf("%s", msg);
	exit(code);
}

int main()
{
	struct timeval tm;
	gettimeofday(&tm, NULL);
	srandom(tm.tv_sec + tm.tv_usec * 1000000ul);

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
		int64_t difference = now - before;

		if (first && change_surface) {
			if (wl_shm_pool_id == WL_NULL)
				wl_shm_pool_id = wl_shm_create_pool(sockfd, wl_shm_id, shmfd, WIDTH * HEIGHT * PIXEL_SIZE * 2);
			if (wl_buffer_id == WL_NULL)
				wl_buffer_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, 0, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);
			if (wl_buffer2_id == WL_NULL)
				wl_buffer2_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, WIDTH * HEIGHT * PIXEL_SIZE, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);

			wl_surface_attach(sockfd, wl_surface_id, *cur_buffer_id);
			wl_surface_commit(sockfd, wl_surface_id);

			first = false;
		}

		if (difference >= interval) {
			if (surfaces_flag) {
				if (wl_shm_pool_id == WL_NULL)
					wl_shm_pool_id = wl_shm_create_pool(sockfd, wl_shm_id, shmfd, WIDTH * HEIGHT * PIXEL_SIZE * 2);
				if (wl_buffer_id == WL_NULL)
					wl_buffer_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, 0, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);
				if (wl_buffer2_id == WL_NULL)
					wl_buffer2_id = wl_shm_pool_create_buffer(sockfd, wl_shm_pool_id, WIDTH * HEIGHT * PIXEL_SIZE, WIDTH, HEIGHT, WIDTH * PIXEL_SIZE, 1);

				char key = get_key();
				
				if (key != -1) {
					switch (key) {
						case 'w': //w
							if (direction != DOWN)
								direction = UP;
							break;
						case 'a': //a
							if (direction != RIGHT)
								direction = LEFT;
							break;
						case 's': //s
							if (direction != UP)
								direction = DOWN;
							break;
						case 'd': //d
							if (direction != LEFT)
								direction = RIGHT;
							break;
						default:
							break;
					}
				}

				for (int i = 0; i < WIDTH * HEIGHT; i++) {
					cur_buffer[i] = prev_buffer[i];	
				}

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
	
				if (snake_x[0] < 0 || snake_x[0] >= GRID_SIZE || snake_y[0] < 0 || snake_y[0] >= GRID_SIZE) {
					quit("INFO: You died.\n", shm, shmfd, sockfd, 0);
				}

				wl_surface_damage_buffer(sockfd, wl_surface_id, fruit_x * BLOCK_WIDTH, fruit_y * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT);
				paint_block(cur_buffer, fruit_x, fruit_y, 0xFF0000);

				wl_surface_damage_buffer(sockfd, wl_surface_id, snake_x[0] * BLOCK_WIDTH, snake_y[0] * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT);
				paint_block(cur_buffer, snake_x[0], snake_y[0], 0xFFFFFF);
	
				wl_surface_attach(sockfd, wl_surface_id, *cur_buffer_id);
				wl_surface_commit(sockfd, wl_surface_id);

				for (int i = 1; i < length; i++) {
					if (snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i])
						quit("INFO: You died.\n", shm, shmfd, sockfd, 0);
				}

				if (snake_x[0] == fruit_x && snake_y[0] == fruit_y) {
					uint32_t packed_coord = new_fruit_coords();
					uint32_t x = packed_coord % GRID_SIZE;
					uint32_t y = packed_coord / GRID_SIZE;

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
			wl_surface_id = wl_compositor_create_surface(sockfd, wl_compositor_id);
			xdg_surface_id = xdg_wm_base_get_xdg_surface(sockfd, xdg_wm_base_id, wl_surface_id);
			xdg_toplevel_id = xdg_surface_get_toplevel(sockfd, xdg_surface_id);

			wl_surface_commit(sockfd, wl_surface_id);

			wl_compositor_flag = false;
			xdg_wm_base_flag = false;
			first = true;
			surfaces_flag = true;
			continue;
		}

		if (wl_seat_flag) {
			wl_keyboard_id = wl_seat_get_keyboard(sockfd, wl_seat_id);

			wl_seat_flag = false;
			continue;
		}

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
		}

		uint32_t *tmp = msg_contents;
		uint32_t **ptr = &tmp;

		if (object_id == wl_registry_id && msg_opcode == WL_REGISTRY_GLOBAL) {
			uint32_t name = wl_readint(ptr);
			char *interface = wl_readstring(ptr);
			uint32_t version = wl_readint(ptr);

			if (strcmp(interface, "wl_shm") == 0) {
				wl_shm_id = wl_registry_bind(sockfd, wl_registry_id, name, interface, version);
				wl_shm_flag = true;
			} else if (strcmp(interface, "wl_compositor") == 0) {
				wl_compositor_id = wl_registry_bind(sockfd, wl_registry_id, name, interface, version);
				wl_compositor_flag = true;
			} else if (strcmp(interface, "xdg_wm_base") == 0) {
				xdg_wm_base_id = wl_registry_bind(sockfd, wl_registry_id, name, interface, version);
				xdg_wm_base_flag = true;
			} else if (strcmp(interface, "wl_seat") == 0) {
				wl_seat_id = wl_registry_bind(sockfd, wl_registry_id, name, interface, version);
				wl_seat_flag = true;
			}

			free(interface);
		} else if (object_id == WL_DISPLAY_OBJECT_ID && msg_opcode == WL_DISPLAY_ERROR) {
			uint32_t object_id = wl_readint(ptr);
			uint32_t code = wl_readint(ptr);
			char *s = wl_readstring(ptr);	

			printf("ERROR: object_id %d, error %d, %s\n", object_id, code, s);	
			free(s);
			return -1;
		} else if (object_id == xdg_wm_base_id && msg_opcode == XDG_WM_BASE_PING) {
			uint32_t serial = wl_readint(ptr);	
			printf("INFO: Ping!\n");
			
			xdg_wm_base_pong(sockfd, xdg_wm_base_id, serial);
		} else if (object_id == xdg_surface_id && msg_opcode == XDG_SURFACE_CONFIGURE) { 
			uint32_t serial = wl_readint(ptr);
			printf("INFO (xdg_surface): Suggested surface configuration change %d.\n", serial);

			xdg_surface_ack_configure(sockfd, xdg_surface_id, serial);
			change_surface = true;
		} else if (object_id == wl_shm_id && msg_opcode == WL_SHM_FORMAT) {
	  		uint32_t format = wl_readint(ptr);

			printf("INFO (wl_shm): Available pixel format 0x%x.\n", format);	
		} else if (object_id == xdg_toplevel_id && msg_opcode == XDG_TOPLEVEL_CLOSE) {
			// TODO: reč wl_seatu da ga zapuščaš
			quit("Goodbye...\n", shm, shmfd, sockfd, 0);
		} else if (object_id == wl_buffer_id && msg_opcode == WL_BUFFER_RELEASE) {
			printf("INFO: Compositor released wl_buffer %d.\n", wl_buffer_id);	

		} else if (object_id == wl_buffer2_id && msg_opcode == WL_BUFFER_RELEASE) {
			printf("INFO: Compositor released wl_buffer %d.\n", wl_buffer2_id);	

		} else if (object_id == wl_keyboard_id && msg_opcode == WL_KEYBOARD_KEYMAP) {
			fd = fdbuffer[next_fd++];
			next_fd %= FDBUFFER_LEN;

			uint32_t format = wl_readint(ptr);
			uint32_t size = wl_readint(ptr);

			char *keymap = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);

			if (keymap == (void *)-1) {
				printf("ERROR: Failed to map keymap data.\n");
				exit(errno);
			}

			//printf("%s", keymap); // <- Spam.

			munmap(keymap, size);

			printf("INFO: Read keymap data.\n");
		} else if (object_id == wl_keyboard_id && msg_opcode == WL_KEYBOARD_KEY) {
			uint32_t serial = wl_readint(ptr);
			uint32_t time = wl_readint(ptr);
			uint32_t key = wl_readint(ptr) + 8; // +8? Because it says so in the protocol spec.
			uint32_t state = wl_readint(ptr);

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
						write_key('w');
						break;
					case 38: //a
						write_key('a');
						break;
					case 39: //s
						write_key('s');
						break;
					case 40: //d
						write_key('d');
						break;
					case 9: //ESC
						quit("Goodbye...\n", shm, shmfd, sockfd, 0);
					default:
						break;
				}
			}
		} else {
			//printf("%d %d\n", object_id, msg_opcode);
		}

		if (msg_contents != NULL)
			free(msg_contents);
	}

	close(sockfd);
	return 0;
}
