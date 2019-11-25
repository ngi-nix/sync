/*
  This file is part of TALER
  (C) 2018, 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3, or
  (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with TALER; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file include/sync_testing_lib.h
 * @brief API for writing an interpreter to test SYNC components
 * @author Christian Grothoff <christian@grothoff.org>
 */
#ifndef SYNC_TESTING_LIB_H
#define SYNC_TESTING_LIB_H

#include "sync_service.h"
#include <gnunet/gnunet_json_lib.h>
#include <microhttpd.h>


/**
 * Start the sync backend process.  Assume the port
 * is available and the database is clean.  Use the "prepare
 * sync" function to do such tasks.
 *
 * @param config_filename configuration filename.
 *
 * @return the process, or NULL if the process could not
 *         be started.
 */
struct GNUNET_OS_Process *
TALER_TESTING_run_sync (const char *config_filename,
                        const char *sync_url);


/**
 * Prepare the sync execution.  Create tables and check if
 * the port is available.
 *
 * @param config_filename configuration filename.
 * @return the base url, or NULL upon errors.  Must be freed
 *         by the caller.
 */
char *
TALER_TESTING_prepare_sync (const char *config_filename);


/**
 * Make the "backup download" command.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @param http_status expected HTTP status.
 * @param upload_ref reference to upload command
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_download (const char *label,
                                  const char *sync_url,
                                  unsigned int http_status,
                                  const char *upload_ref);
#endif
