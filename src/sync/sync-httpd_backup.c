/*
  This file is part of TALER
  Copyright (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file sync-httpd_backup.c
 * @brief functions to handle incoming requests for backups
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync-httpd.h"
#include <gnunet/gnunet_util_lib.h>

/**
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @return MHD result code
 */
int
sync_handler_backup_get (struct MHD_Connection *connection,
                         const char *url,
                         void **con_cls)
{
  return MHD_NO;
}


/**
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @return MHD result code
 */
int
sync_handler_backup_post (struct MHD_Connection *connection,
                          void **con_cls,
                          const char *url,
                          const char *upload_data,
                          size_t *upload_data_size)
{
  return MHD_NO;
}
