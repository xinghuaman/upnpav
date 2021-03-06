/*
 * upnpavd - UPNP AV Daemon
 *
 * Copyright (C) 2009 - 20010 Alper Akcan, alper.akcan@gmail.com
 * Copyright (C) 2009 - 20010 CoreCodec, Inc., http://www.CoreCodec.com
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

typedef struct metadata_s metadata_t;
typedef struct metadata_audio_s metadata_audio_t;
typedef struct metadata_video_s metadata_video_t;
typedef struct metadata_image_s metadata_image_t;
typedef struct metadata_snapshot_s metadata_snapshot_t;

typedef enum {
	METADATA_TYPE_UNKNOWN,
	METADATA_TYPE_CONTAINER,
	METADATA_TYPE_AUDIO,
	METADATA_TYPE_VIDEO,
	METADATA_TYPE_IMAGE,
} metadata_type_t;

struct metadata_s {
	metadata_type_t type;
	char *pathname;
	char *basename;
	char *mimetype;
	char *dlnainfo;
	char *date;
	char *title;
	char *artist;
	char *album;
	char *genre;
	char *duration;
	unsigned long long size;
};

metadata_t * upnpd_metadata_init (const char *path);
int upnpd_metadata_uninit (metadata_t *metadata);

metadata_snapshot_t * upnpd_metadata_snapshot_init (const char *path, int width, int height);
int upnpd_metadata_snapshot_uninit (metadata_snapshot_t *snapshot);
unsigned char * upnpd_metadata_snapshot_obtain (metadata_snapshot_t *snapshot, unsigned int seconds);
