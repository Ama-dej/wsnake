#include "functions.h"

uint32_t object_id = 2;

#define WL_NULL 0
#define WL_DISPLAY_OBJECT_ID 1

uint32_t wl_readint(uint32_t **ptr)
{
    uint32_t *tmp = *ptr;
    uint32_t val = *tmp;
    *ptr = tmp + 1;
    return val;
}

char *wl_readstring(uint32_t **ptr)
{
    uint32_t len = wl_readint(ptr);
    uint32_t *tmp = *ptr;

    uint32_t len4 = (len + 3) & ~0b11;
    char *s = malloc(len4);

    uint32_t * si = (uint32_t *)s;

    for (uint32_t i = 0; i < len4 / 4; i++)
        si[i] = tmp[i];

    *ptr = tmp + len4 / 4;
    return s;
}

uint32_t *wl_writeint(uint32_t *buffer, uint32_t val)
{
    *buffer = val;

    return buffer + 1;
}

uint32_t *wl_writestring(uint32_t *buffer, char *s)
{
    unsigned int len = strlen(s) + 1;
    buffer = wl_writeint(buffer, len);
    unsigned int len4 = (len + 3) & ~0b11;
    
    uint32_t *si = malloc(len4);
    memcpy(si, s, len);
    memset((uint8_t *)si + len, 0, len4 - len);

    for (unsigned int i = 0; i < len4 / 4; i++)
        buffer[i] = si[i];  

    free(si);
    return buffer + len4 / 4;
}

uint32_t *wl_writenewid(uint32_t *buffer, char *interface, uint32_t version)
{
    buffer = wl_writestring(buffer, interface);
    buffer = wl_writeint(buffer, version);

    return buffer;
}

int wl_display_get_registry(int sockfd)
{
    unsigned int grmsg_len = 12;

    uint32_t *grmsg = malloc(grmsg_len);
    uint32_t *tmp = grmsg;

    tmp = wl_writeint(tmp, WL_DISPLAY_OBJECT_ID);
    tmp = wl_writeint(tmp, (grmsg_len << 16) | WL_DISPLAY_GET_REGISTRY);
    tmp = wl_writeint(tmp, object_id);

    if (send(sockfd, grmsg, grmsg_len, 0) != grmsg_len) {
        printf("ERROR: wl_display_get_registry request failed.\n");
        exit(errno);
    }

    free(grmsg);

    return object_id++; 
}

int wl_registry_bind(int sockfd, uint32_t wl_registry_id, uint32_t name, char *interface, uint32_t version)
{
	unsigned int bmsg_len = 24 + ((strlen(interface) + 3) & ~0b11);
	
	uint32_t *bmsg = malloc(bmsg_len);
	uint32_t *tmp = bmsg;

	tmp = wl_writeint(tmp, wl_registry_id);
	tmp = wl_writeint(tmp, (bmsg_len << 16) | WL_REGISTRY_BIND);
	tmp = wl_writeint(tmp, name);
	tmp = wl_writenewid(tmp, interface, version);
	tmp = wl_writeint(tmp, object_id);

	if (send(sockfd, bmsg, bmsg_len, 0) != bmsg_len) {
		printf("wl_registry_bind: ni ratal poslat\n");
		exit(errno);
	}

	free(bmsg);

	printf("INFO: Bound %s to id %d.\n", interface, object_id);
	return object_id++;
}

int wl_shm_create_pool(int sockfd, uint32_t wl_shm_id, int shmfd, uint32_t size)
{
	unsigned int cpmsg_len = 16;

	uint32_t *cpmsg = malloc(cpmsg_len);
	uint32_t *tmp = cpmsg;

	tmp = wl_writeint(tmp, wl_shm_id);
	tmp = wl_writeint(tmp, (cpmsg_len << 16) | WL_SHM_CREATE_POOL);
	tmp = wl_writeint(tmp, object_id);
	tmp = wl_writeint(tmp, size);

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

	tmp = wl_writeint(tmp, wl_shm_pool_id);
	tmp = wl_writeint(tmp, (cbmsg_len << 16) | WL_SHM_POOL_CREATE_BUFFER);
	tmp = wl_writeint(tmp, object_id);
	tmp = wl_writeint(tmp, offset);
	tmp = wl_writeint(tmp, width);
	tmp = wl_writeint(tmp, height);
	tmp = wl_writeint(tmp, stride);
	tmp = wl_writeint(tmp, format);

	if (send(sockfd, cbmsg, cbmsg_len, 0) != cbmsg_len) {
		printf("wl_shm_pool_create_buffer: ni ratal poslat\n");
		exit(errno);
	}

	free(cbmsg);

	printf("INFO: Created wl_buffer with id %d.\n", object_id);
	return object_id++;
}

int wl_compositor_create_surface(int sockfd, uint32_t wl_compositor_id)
{
	unsigned int csmsg_len = 12;

	uint32_t *csmsg = malloc(csmsg_len);
	uint32_t *tmp = csmsg;

	tmp = wl_writeint(tmp, wl_compositor_id);
	tmp = wl_writeint(tmp, (csmsg_len << 16) | WL_COMPOSITOR_CREATE_SURFACE);
	tmp = wl_writeint(tmp, object_id);

	if (send(sockfd, csmsg, csmsg_len, 0) != csmsg_len) {
		printf("wl_compositor_create_surface: ni ratal poslat\n");
		exit(errno);
	}

	free(csmsg);

	printf("INFO: Created wl_surface with id %d.\n", object_id);
	return object_id++;
}

void xdg_wm_base_pong(int sockfd, uint32_t xdg_wm_base_id, uint32_t serial)
{
	unsigned int pmsg_len = 12;

	uint32_t *pmsg = malloc(pmsg_len);
	uint32_t *tmp = pmsg;

	tmp = wl_writeint(tmp, xdg_wm_base_id);
	tmp = wl_writeint(tmp, (pmsg_len << 16) | XDG_WM_BASE_PONG);
	tmp = wl_writeint(tmp, serial);

	if (send(sockfd, pmsg, pmsg_len, 0) != pmsg_len) {
		printf("xdg_wm_base_pong: ni ratal poslat\n");
		exit(errno);
	}

	free(pmsg);

	printf("INFO: Pong!\n");
	return;
}

int xdg_wm_base_get_xdg_surface(int sockfd, uint32_t xdg_wm_base_id, uint32_t wl_surface_id)
{
	unsigned int gxsmsg_len = 16;

	uint32_t *gxsmsg = malloc(gxsmsg_len);
	uint32_t *tmp = gxsmsg;

	tmp = wl_writeint(tmp, xdg_wm_base_id);
	tmp = wl_writeint(tmp, (gxsmsg_len << 16) | XDG_WM_BASE_GET_XDG_SURFACE);
	tmp = wl_writeint(tmp, object_id);
	tmp = wl_writeint(tmp, wl_surface_id);

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

	tmp = wl_writeint(tmp, xdg_surface_id);
	tmp = wl_writeint(tmp, (gtmsg_len << 16) | XDG_SURFACE_GET_TOPLEVEL);
	tmp = wl_writeint(tmp, object_id);

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

	tmp = wl_writeint(tmp, wl_surface_id);
	tmp = wl_writeint(tmp, (cmsg_len << 16) | WL_SURFACE_COMMIT);

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

	tmp = wl_writeint(tmp, wl_surface_id);
	tmp = wl_writeint(tmp, (amsg_len << 16) | WL_SURFACE_ATTACH);
	tmp = wl_writeint(tmp, wl_buffer_id);
	tmp = wl_writeint(tmp, 0);
	tmp = wl_writeint(tmp, 0);

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

	tmp = wl_writeint(tmp, xdg_surface_id);
	tmp = wl_writeint(tmp, (acmsg_len << 16) | XDG_SURFACE_ACK_CONFIGURE);
	tmp = wl_writeint(tmp, serial);

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

	tmp = wl_writeint(tmp, xdg_surface_id);
	tmp = wl_writeint(tmp, (swgmsg_len << 16) | XDG_SURFACE_SET_WINDOW_GEOMETRY);
	tmp = wl_writeint(tmp, x);
	tmp = wl_writeint(tmp, y);
	tmp = wl_writeint(tmp, width);
	tmp = wl_writeint(tmp, height);

	if (send(sockfd, swgmsg, swgmsg_len, 0) != swgmsg_len) {
		printf("xdg_surface_set_window_geometry: ni ratal poslat\n");
		exit(errno);
	}

	free(swgmsg);

	return;
}

int wl_seat_get_keyboard(int sockfd, uint32_t wl_seat_id)
{
	unsigned int gkmsg_len = 12;

	uint32_t *gkmsg = malloc(gkmsg_len);
	uint32_t *tmp = gkmsg;

	tmp = wl_writeint(tmp, wl_seat_id);
	tmp = wl_writeint(tmp, (gkmsg_len << 16) | WL_SEAT_GET_KEYBOARD);
	tmp = wl_writeint(tmp, object_id);

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

	tmp = wl_writeint(tmp, wl_surface_id);
	tmp = wl_writeint(tmp, (dbmsg_len << 16) | WL_SURFACE_DAMAGE_BUFFER);
	tmp = wl_writeint(tmp, x);
	tmp = wl_writeint(tmp, y);
	tmp = wl_writeint(tmp, width);
	tmp = wl_writeint(tmp, height);

	if (send(sockfd, dbmsg, dbmsg_len, 0) != dbmsg_len) {
		printf("wl_surface_damage_buffer: ni ratal poslat\n");
		exit(errno);
	}

	free(dbmsg);

	printf("INFO: Damaged buffer from (%d, %d) to (%d, %d).\n", x, y, x + width, y + height);
	return;
}

void wl_keyboard_release(int sockfd, uint32_t wl_keyboard_id)
{
	unsigned int rmsg_len = 8;

	uint32_t *rmsg = malloc(rmsg_len);
	uint32_t *tmp = rmsg;

	tmp = wl_writeint(tmp, wl_keyboard_id);
	tmp = wl_writeint(tmp, (rmsg_len << 16) | WL_KEYBOARD_RELEASE);

	if (send(sockfd, rmsg, rmsg_len, 0) != rmsg_len) {
		printf("wl_seat_release: ni ratal poslat\n");
		exit(errno);
	}

	free(rmsg);

	printf("INFO: Released wl_keyboard with id %d.\n", wl_keyboard_id);
	return;
}

void wl_seat_release(int sockfd, uint32_t wl_seat_id)
{
	unsigned int rmsg_len = 8;

	uint32_t *rmsg = malloc(rmsg_len);
	uint32_t *tmp = rmsg;

	tmp = wl_writeint(tmp, wl_seat_id);
	tmp = wl_writeint(tmp, (rmsg_len << 16) | WL_SEAT_RELEASE);

	if (send(sockfd, rmsg, rmsg_len, 0) != rmsg_len) {
		printf("wl_seat_release: ni ratal poslat\n");
		exit(errno);
	}

	free(rmsg);

	printf("INFO: Released wl_seat with id %d.\n", wl_seat_id);
	return;
}
