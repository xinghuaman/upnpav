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

/** @defgroup parser XML Parser API
  * @brief XML parse and handling api.
  *
  * @example
  *
  * @code
  * @endcode
  */

/** @addtogroup parser */
/*@{*/

/** @brief parses the given buffer
  *
  * @param *buffer   - buffer to parse
  * @param len       - buffer length
  * @param *callback - callback function
  * @param *context  - callback context
  * @returns 0 on success, -1 on error
  */
int upnpd_xml_parse_buffer_callback (const char *buffer, unsigned int len, int (*callback) (void *context, const char *path, const char *name, const char **atrr, const char *value), void *context);

/*@}*/
