/*
 * upnpavd - UPNP AV Daemon
 *
 * Copyright (C) 2009 - 2010 Alper Akcan, alper.akcan@gmail.com
 * Copyright (C) 2009 - 2010 CoreCodec, Inc., http://www.CoreCodec.com
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Any non-LGPL usage of this software or parts of this software is strictly
 * forbidden.
 *
 * Commercial non-LGPL licensing of this software is possible.
 * For more info contact CoreCodec through info@corecodec.com
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include <string.h>


#include "platform.h"
#include "ssdp.h"
#include "gena.h"
#include "upnp.h"

struct ssdp_s {
	int started;
	int stopped;
	int running;
	socket_t *socket;
	socket_t *announce;
	thread_t *thread;
	thread_cond_t *cond;
	thread_mutex_t *mutex;
	list_t devices;
	int (*callback) (void *cookie, ssdp_event_t *event);
	void *cookie;
	char *address;
	char *netmask;
};

typedef enum {
	SSDP_TYPE_UNKNOWN,
	SSDP_TYPE_NOTIFY,
	SSDP_TYPE_MSEARCH,
	SSDP_TYPE_ANSWER,
} ssdp_type_t;

typedef struct ssdp_request_notify_s {
	char *host;
	char *nt;
	char *nts;
	char *usn;
	char *al;
	char *location;
	char *server;
	char *cachecontrol;
} ssdp_request_notify_t;

typedef struct ssdp_request_search_s {
	char *s;
	char *host;
	char *man;
	char *st;
	char *mx;
} ssdp_request_search_t;

typedef struct ssdp_request_answer_s {
	char *usn;
	char *st;
	char *location;
	char *server;
	char *cachecontrol;
} ssdp_request_answer_t;

typedef struct ssdp_request_s {
	list_t head;
	ssdp_type_t type;
	union {
		ssdp_request_notify_t notify;
		ssdp_request_search_t search;
		ssdp_request_answer_t answer;
	} request;
} ssdp_request_t;

struct ssdp_device_s {
	list_t head;
	char *nt;
	char *usn;
	char *location;
	char *server;
	int age;
	int interval;
};

static const char *ssdp_ip = "239.255.255.250";
static const unsigned short ssdp_port = 1900;
static const unsigned int ssdp_buffer_length = 2500;
static const unsigned int ssdp_recv_timeout = 2000;
static const unsigned int ssdp_ttl = 4;
static const unsigned int ssdp_pause = 100;

static int ssdp_advertise_send (socket_t *sock, const char *buffer, const char *address, int port);
static char * ssdp_advertise_buffer (ssdp_device_t *device, int answer);

static char * ssdp_trim (char *buffer)
{
	int l;
	char *out;
	for (out = buffer; *out && (*out == ' ' || *out == '\t'); out++) {
	}
	for (l = strlen(out) - 1; l >= 0; l--) {
		if (out[l] == ' ' || out[l] == '\t') {
			out[l] = '\0';
		} else {
			break;
		}
	}
	for (; *out && (*out == '"' || *out == '\''); out++) {
	}
	for (l = strlen(out) - 1; l >= 0; l--) {
		if (out[l] == '"' || out[l] == '\'') {
			out[l] = '\0';
		} else {
			break;
		}
	}
	return out;
}

static ssdp_device_t * ssdp_upnpd_device_init (char *nt, char *usn, char *location, char *server, int age)
{
	ssdp_device_t *d;
	d = (ssdp_device_t *) malloc(sizeof(ssdp_device_t));
	if (d == NULL) {
		return NULL;
	}
	memset(d, 0, sizeof(ssdp_device_t));
	d->nt = strdup(nt);
	d->usn = strdup(usn);
	d->location = strdup(location);
	d->server = strdup(server);
	d->age = (age < (int) ssdp_recv_timeout) ? ssdp_recv_timeout : age;
	d->interval = d->age;
	if (d->nt == NULL ||
	    d->usn == NULL ||
	    d->location == NULL ||
	    d->server == NULL) {
		free(d->nt);
		free(d->usn);
		free(d->location);
		free(d->server);
		free(d);
		return NULL;
	}
	return d;
}

static int ssdp_upnpd_device_uninit (ssdp_device_t *device)
{
	free(device->nt);
	free(device->usn);
	free(device->location);
	free(device->server);
	free(device);
	return 0;
}

static ssdp_request_t * ssdp_request_init (void)
{
	ssdp_request_t *r;
	r = (ssdp_request_t *) malloc(sizeof(ssdp_request_t));
	if (r == NULL) {
		return NULL;
	}
	memset(r, 0, sizeof(ssdp_request_t));
	r->type = SSDP_TYPE_UNKNOWN;
	return r;
}

static int ssdp_request_uninit (ssdp_request_t *r)
{
	switch (r->type) {
		case SSDP_TYPE_MSEARCH:
			free(r->request.search.host);
			free(r->request.search.man);
			free(r->request.search.mx);
			free(r->request.search.s);
			free(r->request.search.st);
			break;
		case SSDP_TYPE_NOTIFY:
			free(r->request.notify.al);
			free(r->request.notify.location);
			free(r->request.notify.server);
			free(r->request.notify.cachecontrol);
			free(r->request.notify.host);
			free(r->request.notify.nt);
			free(r->request.notify.nts);
			free(r->request.notify.usn);
			break;
		case SSDP_TYPE_ANSWER:
			free(r->request.answer.location);
			free(r->request.answer.server);
			free(r->request.answer.cachecontrol);
			free(r->request.answer.usn);
			free(r->request.answer.st);
			break;
		case SSDP_TYPE_UNKNOWN:
			break;
	}
	free(r);
	return 0;
}

static int ssdp_request_mask (const char *address, const char *netmask, ssdp_request_t *request)
{
	uint32_t i;
	uint32_t m;
	uint32_t l;
	upnp_url_t url;
	const char *location;
	location = NULL;
	if (address == NULL || netmask == NULL) {
		return 0;
	}
	switch (request->type) {
		case SSDP_TYPE_NOTIFY:
			location = request->request.notify.location;
			break;
		case SSDP_TYPE_ANSWER:
			location = request->request.answer.location;
			break;
		default:
			return 0;
	}
	if (location == NULL) {
		return -1;
	}
	if (upnpd_upnp_url_parse(location, &url) != 0) {
		return -1;
	}
	if (upnpd_socket_inet_aton(address, &i) != 0 ||
	    upnpd_socket_inet_aton(netmask, &m) != 0 ||
	    upnpd_socket_inet_aton(url.host, &l) != 0) {
		upnpd_upnp_url_uninit(&url);
		return -1;
	}
	upnpd_upnp_url_uninit(&url);
	if ((l & m) != (i & m)) {
		return -1;
	}
	return 0;
}

static int ssdp_request_valid (const char *address, const char *netmask,ssdp_request_t *request)
{
	switch (request->type) {
		case SSDP_TYPE_MSEARCH:
			if (request->request.search.st == NULL ||
			    request->request.search.man == NULL) {
				return -1;
			}
			if (strcasecmp(request->request.search.man, "ssdp:discover") == 0) {
				return 0;
			}
			break;
		case SSDP_TYPE_NOTIFY:
			if (request->request.notify.nt == NULL ||
			    request->request.notify.nts == NULL ||
			    request->request.notify.usn == NULL) {
				debugf(_DBG, "notify not valid");
				return -1;
			}
			if (strcasecmp(request->request.notify.nts, "ssdp:alive") == 0 &&
			   (request->request.notify.al != NULL || request->request.notify.location != NULL)) {
				return ssdp_request_mask(address, netmask, request);
			} else if (strcasecmp(request->request.notify.nts, "ssdp:byebye") == 0) {
				return 0;
			}
			debugf(_DBG, "notify not valid");
			break;
		case SSDP_TYPE_ANSWER:
			if (request->request.answer.st == NULL ||
			    request->request.answer.location == NULL ||
			    request->request.answer.usn == NULL) {
				debugf(_DBG, "answer not valid");
				return-1;
			}
			return ssdp_request_mask(address, netmask, request);
		default:
			break;
	}
	return -1;
}

static ssdp_request_t * ssdp_parse (const char *address, const char *netmask, char *buffer, int length)
{
	char *ptr;
	char *line;
	ssdp_request_t *request;
	request = ssdp_request_init();
	if (request == NULL) {
		return NULL;
	}
	ptr = buffer;
	while (ptr < buffer + length) {
		line = ptr;
		while (ptr < buffer + length) {
			if (*ptr == '\r') {
				*ptr = '\0';
			} else if (*ptr == '\n') {
				*ptr++ = '\0';
				break;
			}
			ptr++;
		}
		if (strncasecmp(line, "M-SEARCH", 8) == 0) {
			if (request->type != SSDP_TYPE_UNKNOWN) {
				free(request);
				return NULL;
			}
			request->type = SSDP_TYPE_MSEARCH;
		} else if (strncasecmp(line, "NOTIFY", 6) == 0) {
			if (request->type != SSDP_TYPE_UNKNOWN) {
				free(request);
				return NULL;
			}
			request->type = SSDP_TYPE_NOTIFY;
		} else if (strncasecmp(line, "HTTP/1.1", 8) == 0 &&
			   strstr(line, "200") != NULL &&
			   (strstr(line, "OK") != NULL || strstr(line, "Ok") != NULL || strstr(line, "ok") != NULL)) {
			if (request->type != SSDP_TYPE_UNKNOWN) {
				free(request);
				return NULL;
			}
			request->type = SSDP_TYPE_ANSWER;
		}
		if (request->type == SSDP_TYPE_NOTIFY) {
			if (strncasecmp(line, "Host:", 5) == 0) {
				request->request.notify.host = strdup(ssdp_trim(line + 5));
			} else if (strncasecmp(line, "NT:", 3) == 0) {
				request->request.notify.nt = strdup(ssdp_trim(line + 3));
			} else if (strncasecmp(line, "NTS:", 4) == 0) {
				request->request.notify.nts = strdup(ssdp_trim(line + 4));
			} else if (strncasecmp(line, "USN:", 4) == 0) {
				request->request.notify.usn = strdup(ssdp_trim(line + 4));
			} else if (strncasecmp(line, "AL:", 3) == 0) {
				request->request.notify.al = strdup(ssdp_trim(line + 3));
			} else if (strncasecmp(line, "LOCATION:", 9) == 0) {
				request->request.notify.location = strdup(ssdp_trim(line + 9));
			} else if (strncasecmp(line, "Cache-Control:", 14) == 0) {
				request->request.notify.cachecontrol = strdup(ssdp_trim(line + 14));
			} else if (strncasecmp(line, "Server:", 7) == 0) {
				request->request.notify.server = strdup(ssdp_trim(line + 7));
			}
		} else if (request->type == SSDP_TYPE_MSEARCH) {
			if (strncasecmp(line, "S:", 2) == 0) {
				request->request.search.s = strdup(ssdp_trim(line + 2));
			} else if (strncasecmp(line, "Host:", 5) == 0) {
				request->request.search.host = strdup(ssdp_trim(line + 5));
			} else if (strncasecmp(line, "Man:", 4) == 0) {
				request->request.search.man = strdup(ssdp_trim(line + 4));
			} else if (strncasecmp(line, "ST:", 3) == 0) {
				request->request.search.st = strdup(ssdp_trim(line + 3));
			} else if (strncasecmp(line, "MX:", 3) == 0) {
				request->request.search.mx = strdup(ssdp_trim(line + 3));
			}
		} else if (request->type == SSDP_TYPE_ANSWER) {
			if (strncasecmp(line, "USN:", 4) == 0) {
				request->request.answer.usn = strdup(ssdp_trim(line + 4));
			} else if (strncasecmp(line, "ST:", 3) == 0) {
				request->request.answer.st = strdup(ssdp_trim(line + 3));
			} else if (strncasecmp(line, "LOCATION:", 9) == 0) {
				request->request.answer.location = strdup(ssdp_trim(line + 9));
			} else if (strncasecmp(line, "Server:", 7) == 0) {
				request->request.answer.server = strdup(ssdp_trim(line + 7));
			} else if (strncasecmp(line, "Cache-Control:", 14) == 0) {
				request->request.answer.cachecontrol = strdup(ssdp_trim(line + 14));
			}
		}
	}
	if (ssdp_request_valid(address, netmask, request) != 0) {
		ssdp_request_uninit(request);
		request = NULL;
	}
	return request;
}

static int ssdp_request_handler (ssdp_t *ssdp, ssdp_request_t *request, const char *address, int port)
{
	char *ptr;
	char *buffer;
	ssdp_event_t e;
	ssdp_device_t *d;
	memset(&e, 0, sizeof(ssdp_event_t));
	upnpd_thread_mutex_lock(ssdp->mutex);
	switch (request->type) {
		case SSDP_TYPE_MSEARCH:
			if (strcasecmp(request->request.search.st, "upnp:rootdevice") == 0 ||
			    strcasecmp(request->request.search.st, "ssdp:all") == 0) {
				list_for_each_entry(d, &ssdp->devices, head, ssdp_device_t) {
					buffer = ssdp_advertise_buffer(d, 1);
					if (buffer != NULL) {
						ssdp_advertise_send(ssdp->announce, buffer, address, port);
						free(buffer);
					}
				}
			} else {
				list_for_each_entry(d, &ssdp->devices, head, ssdp_device_t) {
					if (strncasecmp(d->nt, request->request.search.st, strlen(request->request.search.st)) == 0) {
						buffer = ssdp_advertise_buffer(d, 1);
						if (buffer != NULL) {
							ssdp_advertise_send(ssdp->announce, buffer, address, port);
							free(buffer);
						}
					}
				}
			}
			break;
		case SSDP_TYPE_NOTIFY:
			if (strcasecmp(request->request.notify.nts, "ssdp:alive") == 0) {
				e.type = SSDP_EVENT_TYPE_NOTIFY;
			} else if (strcasecmp(request->request.notify.nts, "ssdp:byebye") == 0) {
				e.type = SSDP_EVENT_TYPE_BYEBYE;
			}
			e.device = request->request.notify.nt;
			e.location = request->request.notify.location;
			e.expires = 100;
			ptr = strstr(request->request.notify.usn, "::");
			if (ptr != NULL) {
				*ptr = '\0';
			}
			e.uuid = request->request.notify.usn;
			upnpd_thread_mutex_unlock(ssdp->mutex);
			if (ssdp->callback != NULL) {
				ssdp->callback(ssdp->cookie, &e);
			}
			upnpd_thread_mutex_lock(ssdp->mutex);
			break;
		case SSDP_TYPE_ANSWER:
			e.type = SSDP_EVENT_TYPE_NOTIFY;
			e.device = request->request.answer.st;
			e.location = request->request.answer.location;
			e.expires = 100;
			ptr = strstr(request->request.answer.usn, "::");
			if (ptr != NULL) {
				*ptr = '\0';
			}
			e.uuid = request->request.answer.usn;
			upnpd_thread_mutex_unlock(ssdp->mutex);
			if (ssdp->callback != NULL) {
				ssdp->callback(ssdp->cookie, &e);
			}
			upnpd_thread_mutex_lock(ssdp->mutex);
			break;
		case SSDP_TYPE_UNKNOWN:
			break;
	}
	upnpd_thread_mutex_unlock(ssdp->mutex);
	return 0;
}

static void * ssdp_thread_loop (void *arg)
{
	int ret;
	int received;
	char *buf;
	char *buffer;
	ssdp_t *ssdp;
	ssdp_device_t *d;
	ssdp_request_t *request;
	unsigned long long times[2];

	int i;
	socket_t *psocket;
	poll_item_t pitem[2];
	char senderip[SOCKET_IP_LENGTH];
	int senderport;

	ssdp = (ssdp_t *) arg;

	upnpd_thread_mutex_lock(ssdp->mutex);
	ssdp->started = 1;
	ssdp->running = 1;
	upnpd_thread_mutex_unlock(ssdp->mutex);
	upnpd_thread_cond_signal(ssdp->cond);

	buffer = (char *) malloc(sizeof(char) * (ssdp_buffer_length + 1));
	if (buffer == NULL) {
		goto out;
	}

	times[0] = upnpd_time_gettimeofday();

	while (1) {
		upnpd_thread_mutex_lock(ssdp->mutex);
		times[1] = upnpd_time_gettimeofday();
		list_for_each_entry(d, &ssdp->devices, head, ssdp_device_t) {
			d->interval -= (times[1] - times[0]);
			if (d->interval < (d->age / 2) || d->interval > d->age) {
				buf = ssdp_advertise_buffer(d, 0);
				ssdp_advertise_send(ssdp->announce, buf, ssdp_ip, ssdp_port);
				free(buf);
				d->interval = d->age;
			}
		}
		times[0] = upnpd_time_gettimeofday();
		if (ssdp->running == 0 || ssdp->socket == NULL) {
			upnpd_thread_mutex_unlock(ssdp->mutex);
			break;
		}
		upnpd_thread_mutex_unlock(ssdp->mutex);
		pitem[0].item = ssdp->socket;
		pitem[0].events = POLL_EVENT_IN;
		pitem[1].item = ssdp->announce;
		pitem[1].events = POLL_EVENT_IN;
		ret = upnpd_socket_poll(pitem, 2, ssdp_recv_timeout);
		if (!ret) {
			continue;
		} else {
			if (pitem[0].revents != POLL_EVENT_IN && pitem[1].revents != POLL_EVENT_IN) {
				break;
			}
		}
		for (i = 0; i < 2; i++) {
			if (pitem[i].revents != POLL_EVENT_IN) {
				continue;
			}
			psocket = (socket_t *) pitem[i].item;
			received = upnpd_socket_recvfrom(psocket, buffer, ssdp_buffer_length, senderip, &senderport);
			if (received <= 0) {
				continue;
			}
			buffer[received] = '\0';
			request = ssdp_parse(ssdp->address, ssdp->netmask, buffer, received);
			if (request != NULL) {
				ssdp_request_handler(ssdp, request, (i == 0) ? senderip : NULL, (i == 0) ? senderport : 0);
				ssdp_request_uninit(request);
			}
		}
	}
out:
	upnpd_thread_mutex_lock(ssdp->mutex);
	ssdp->stopped = 1;
	ssdp->running = 0;
	if (buffer != NULL) {
		free(buffer);
	}
	upnpd_thread_cond_signal(ssdp->cond);
	upnpd_thread_mutex_unlock(ssdp->mutex);

	return NULL;
}

static int ssdp_init_server (ssdp_t *ssdp)
{
	ssdp->socket = upnpd_socket_open(SOCKET_TYPE_DGRAM, 1);
	ssdp->announce = upnpd_socket_open(SOCKET_TYPE_DGRAM, 0);
	if (ssdp->socket == NULL || ssdp->announce == NULL) {
		debugf(_DBG, "upnpd_socket_open() failed");
		upnpd_socket_close(ssdp->socket);
		upnpd_socket_close(ssdp->announce);
		return -1;
	}
	if (upnpd_socket_option_multicastttl(ssdp->announce, ssdp_ttl) < 0) {
		debugf(_DBG, "upnpd_socket_option_multicastttl() failed");
		upnpd_socket_close(ssdp->socket);
		upnpd_socket_close(ssdp->announce);
		return -2;
	}

	if (upnpd_socket_option_reuseaddr(ssdp->socket, 1) < 0) {
		debugf(_DBG, "upnpd_socket_option_reuseaddr() failed");
		upnpd_socket_close(ssdp->socket);
		upnpd_socket_close(ssdp->announce);
		return -2;
	}
	if (upnpd_socket_bind(ssdp->socket, ssdp_ip, ssdp_port) < 0) {
		debugf(_DBG, "upnpd_socket_bind() failed");
		upnpd_socket_close(ssdp->socket);
		upnpd_socket_close(ssdp->announce);
		return -3;
	}
	if (upnpd_socket_option_membership(ssdp->socket, ssdp_ip, 1) < 0) {
		debugf(_DBG, "upnpd_socket_option_membership() failed");
		upnpd_socket_close(ssdp->socket);
		upnpd_socket_close(ssdp->announce);
		return -4;
	}
	ssdp->mutex = upnpd_thread_mutex_init("ssdp->mutex", 0);
	ssdp->cond = upnpd_thread_cond_init("ssdp->cond");
	upnpd_thread_mutex_lock(ssdp->mutex);
	ssdp->thread = upnpd_thread_create("ssdp_thread_loop", ssdp_thread_loop, ssdp);
	while (ssdp->started == 0) {
		upnpd_thread_cond_wait(ssdp->cond, ssdp->mutex);
	}
	upnpd_thread_mutex_unlock(ssdp->mutex);
	return 0;
}

static int ssdp_advertise_send (socket_t *sock, const char *buffer, const char *address, int port)
{
	int c;
	int rc;
	if (buffer == NULL) {
		return -1;
	}
	for (c = 0; c < 2; c++) {
		rc = upnpd_socket_sendto(sock, buffer, strlen(buffer), address, port);
		upnpd_time_usleep(ssdp_pause * 1000);
	}
	return 0;
}

static char * ssdp_advertise_buffer (ssdp_device_t *device, int answer)
{
	int ret;
	char *buffer;
	const char *format_advertise =
		"NOTIFY * HTTP/1.1\r\n"
		"HOST: %s:%d\r\n"
		"CACHE-CONTROL: max-age=%d\r\n"
		"LOCATION: %s\r\n"
		"NT: %s\r\n"
		"NTS: ssdp:alive\r\n"
		"SERVER: %s\r\n"
		"USN: %s\r\n"
		"\r\n";
	const char *format_answer =
		"HTTP/1.1 200 OK\r\n"
		"EXT:\r\n"
		"CACHE-CONTROL: max-age=%d\r\n"
		"LOCATION: %s\r\n"
		"ST: %s\r\n"
		"SERVER: %s\r\n"
		"USN: %s\r\n"
		"CONTENT-LENGTH: 0\r\n"
		"\r\n";
	if (answer == 1) {
		ret = asprintf(
			&buffer,
			format_answer,
			device->age / 1000,
			device->location,
			device->nt,
			device->server,
			device->usn);
	} else {
		ret = asprintf(
			&buffer,
			format_advertise,
			ssdp_ip,
			ssdp_port,
			device->age / 1000,
			device->location,
			device->nt,
			device->server,
			device->usn);
	}
	if (ret < 0) {
		return NULL;
	}
	return buffer;
}

static char * ssdp_byebye_buffer (ssdp_device_t *device)
{
	int ret;
	char *buffer;
	const char *format_advertise =
		"NOTIFY * HTTP/1.1\r\n"
		"HOST: %s:%d\r\n"
		"NT: %s\r\n"
		"NTS: ssdp:byebye\r\n"
		"SERVER: %s\r\n"
		"USN: %s\r\n"
		"\r\n";
	ret = asprintf(
		&buffer,
		format_advertise,
		ssdp_ip,
		ssdp_port,
		device->nt,
		device->server,
		device->usn);
	if (ret < 0) {
		return NULL;
	}
	return buffer;
}

int upnpd_upnp_ssdp_search (ssdp_t *ssdp, const char *device, const int timeout)
{
	int t;
	int ret;
	char *buffer;
	const char *format_search =
		"M-SEARCH * HTTP/1.1\r\n"
		"ST: %s\r\n"
		"MX: %d\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"HOST: %s:%d\r\n"
		"\r\n";
	ret = asprintf(
		&buffer,
		format_search,
		device,
		timeout,
		ssdp_ip,
		ssdp_port);
	if (ret < 0) {
		return -1;
	}
	for (t = 0; t < timeout; t++) {
		upnpd_socket_sendto(ssdp->announce, buffer, strlen(buffer), ssdp_ip, ssdp_port);
		upnpd_time_usleep(ssdp_pause * 1000);
	}
	free(buffer);
	return 0;
}

int upnpd_upnp_ssdp_advertise (ssdp_t *ssdp)
{
	char *buffer;
	ssdp_device_t *d;
	upnpd_thread_mutex_lock(ssdp->mutex);
	list_for_each_entry(d, &ssdp->devices, head, ssdp_device_t) {
		buffer = ssdp_advertise_buffer(d, 0);
		ssdp_advertise_send(ssdp->announce, buffer, ssdp_ip, ssdp_port);
		free(buffer);
	}
	upnpd_thread_mutex_unlock(ssdp->mutex);
	return 0;
}

int upnpd_upnp_ssdp_byebye (ssdp_t *ssdp)
{
	char *buffer;
	ssdp_device_t *d;
	upnpd_thread_mutex_lock(ssdp->mutex);
	list_for_each_entry(d, &ssdp->devices, head, ssdp_device_t) {
		debugf(_DBG, "sending byebye notify for '%s'", d->nt);
		buffer = ssdp_byebye_buffer(d);
		ssdp_advertise_send(ssdp->announce, buffer, ssdp_ip, ssdp_port);
		free(buffer);
	}
	upnpd_thread_mutex_unlock(ssdp->mutex);
	return 0;
}

int upnpd_upnp_ssdp_register (ssdp_t *ssdp, char *nt, char *usn, char *location, char *server, int age)
{
	int ret;
	ssdp_device_t *d;
	upnpd_thread_mutex_lock(ssdp->mutex);
	printf("registering\n"
		"  nt      : %s\n"
		"  usn     : %s\n"
		"  location: %s\n"
		"  server  : %s\n"
		"  age     : %d\n",
		nt,
		usn,
		location,
		server,
		age);
	ret = -1;
	d = ssdp_upnpd_device_init(nt, usn, location, server, age);
	if (d != NULL) {
		ret = 0;
		list_add(&d->head, &ssdp->devices);
	}
	upnpd_thread_mutex_unlock(ssdp->mutex);
	return ret;
}

ssdp_t * upnpd_upnp_ssdp_init (const char *address, const char *netmask, int (*callback) (void *cookie, ssdp_event_t *event), void *cookie)
{
	ssdp_t *ssdp;
	ssdp = (ssdp_t *) malloc(sizeof(ssdp_t));
	if (ssdp == NULL) {
		return NULL;
	}
	memset(ssdp, 0, sizeof(ssdp_t));
	ssdp->address = (address) ? strdup(address) : NULL;
	ssdp->netmask = (netmask) ? strdup(netmask) : NULL;
	ssdp->cookie = cookie;
	ssdp->callback = callback;
	list_init(&ssdp->devices);
	if (ssdp_init_server(ssdp) != 0) {
		debugf(_DBG, "ssdp_init_server() failed");
		free(ssdp->address);
		free(ssdp->netmask);
		free(ssdp);
		return NULL;
	}
	return ssdp;
}

int upnpd_upnp_ssdp_uninit (ssdp_t *ssdp)
{
	ssdp_device_t *d, *dn;
	debugf(_DBG, "sending ssdp:byebye");
	upnpd_upnp_ssdp_byebye(ssdp);
	debugf(_DBG, "setting ssdp->running to 0");
	upnpd_thread_mutex_lock(ssdp->mutex);
	ssdp->running = 0;
	debugf(_DBG, "signalling ssdp->cond");
	upnpd_thread_cond_signal(ssdp->cond);
	while (ssdp->stopped == 0) {
		debugf(_DBG, "waiting for ssdp->stopped");
		upnpd_thread_cond_wait(ssdp->cond, ssdp->mutex);
	}
	debugf(_DBG, "joining ssdp->thread");
	upnpd_thread_join(ssdp->thread);
	list_for_each_entry_safe(d, dn, &ssdp->devices, head, ssdp_device_t) {
		list_del(&d->head);
		ssdp_upnpd_device_uninit(d);
	}
	upnpd_thread_mutex_unlock(ssdp->mutex);
	upnpd_thread_mutex_destroy(ssdp->mutex);
	upnpd_thread_cond_destroy(ssdp->cond);
	upnpd_socket_close(ssdp->socket);
	upnpd_socket_close(ssdp->announce);
	free(ssdp->address);
	free(ssdp->netmask);
	free(ssdp);
	return 0;
}
