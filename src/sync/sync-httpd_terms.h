/*
  This file is part of TALER
  (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file sync/sync-httpd_terms.h
 * @brief headers for /terms handler
 * @author Christian Grothoff Dold
 */
#ifndef SYNC_HTTPD_TERMS_H
#define SYNC_HTTPD_TERMS_H
#include <microhttpd.h>
#include "sync-httpd.h"

/**
 * Manages a /terms call.
 *
 * @param rh context of the handler
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @return MHD result code
 */
int
SH_handler_terms (struct SH_RequestHandler *rh,
                  struct MHD_Connection *connection,
                  void **connection_cls,
                  const char *upload_data,
                  size_t *upload_data_size);

#endif
