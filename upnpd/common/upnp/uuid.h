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

typedef struct uuid_gen_s uuid_gen_t;

struct uuid_gen_s {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_high_version;
	uint16_t clock_seq;
	unsigned char node[6];
	char uuid[50];
};

void upnpd_upnp_uuid_generate (uuid_gen_t *uuid);
