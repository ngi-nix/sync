/*
  This file is part of TALER
  Copyright (C) 2014, 2015 GNUnet e.V. and INRIA

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
 * @file sync-httpd_mhd.h
 * @brief helpers for MHD interaction, used to generate simple responses
 * @author Florian Dold
 * @author Benedikt Mueller
 * @author Christian Grothoff
 */
#ifndef sync_HTTPD_MHD_H
#define sync_HTTPD_MHD_H
#include <gnunet/gnunet_util_lib.h>
#include <microhttpd.h>
#include "sync-httpd.h"


/**
 * Function to call to handle the request by sending
 * back static data from the @a rh.
 *
 * @param rh context of the handler
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @param mi merchant backend instance, NULL is allowed in this case!
 * @return MHD result code
 */
MHD_RESULT
SH_MHD_handler_static_response (struct SH_RequestHandler *rh,
                                struct MHD_Connection *connection,
                                void **connection_cls,
                                const char *upload_data,
                                size_t *upload_data_size);


/**
 * Function to call to handle the request by sending
 * back a redirect to the AGPL source code.
 *
 * @param rh context of the handler
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @param mi merchant backend instance, never NULL
 * @return MHD result code
 */
MHD_RESULT
SH_MHD_handler_agpl_redirect (struct SH_RequestHandler *rh,
                              struct MHD_Connection *connection,
                              void **connection_cls,
                              const char *upload_data,
                              size_t *upload_data_size);


#endif
