/*
  This file is part of SYNC
  Copyright (C) 2014-2019 Taler Systems SA

  SYNC is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3, or
  (at your option) any later version.

  SYNC is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with SYNC; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file lib/testing_api_cmd_backup_download.c
 * @brief command to download data to the sync backend service.
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync_service.h"
#include "sync_testing_lib.h"
#include <taler/taler_util.h>
#include <taler/taler_testing_lib.h>

/**
 * State for a "backup download" CMD.
 */
struct BackupDownloadState
{

  /**
   * Reference to upload command we expect to download.
   */
  const char *upload_reference;

  /**
   * Expected status code.
   */
  unsigned int http_status;

  /**
   * Eddsa public key.
   */
  struct SYNC_AccountPublicKeyP sync_pub;

  /**
   * The /backups POST operation handle.
   */
  struct SYNC_DownloadOperation *download;

  /**
   * URL of the sync backend.
   */
  const char *sync_url;

  /**
   * The interpreter state.
   */
  struct TALER_TESTING_Interpreter *is;

};


/**
 * Function called with the results of a #SYNC_download().
 *
 * @param cls closure
 * @param http_status HTTP status of the request
 * @param ud details about the download operation
 */
static void
backup_download_cb (void *cls,
                    unsigned int http_status,
                    const struct SYNC_DownloadDetails *ud)
{
  struct BackupDownloadState *bds = cls;

  // FIXME: next!
}


/**
 * Run a "backup download" CMD.
 *
 * @param cls closure.
 * @param cmd command currently being run.
 * @param is interpreter state.
 */
static void
backup_download_run (void *cls,
                     const struct TALER_TESTING_Command *cmd,
                     struct TALER_TESTING_Interpreter *is)
{
  struct BackupDownloadState *bds = cls;

  bds->is = is;
  bds->download = SYNC_download (is->ctx,
                                 bds->sync_url,
                                 &bds->sync_pub,
                                 &backup_download_cb,
                                 bds);
  if (NULL == bds->download)
  {
    // FIMXE: fail!
  }
}


/**
 * Free the state of a "backup download" CMD, and possibly
 * cancel it if it did not complete.
 *
 * @param cls closure.
 * @param cmd command being freed.
 */
static void
backup_download_cleanup (void *cls,
                         const struct TALER_TESTING_Command *cmd)
{
  struct BackupDownloadState *bds = cls;

  if (NULL != bds->download)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Command '%s' did not complete (backup download)\n",
                cmd->label);
    SYNC_download_cancel (bds->download);
    bds->download = NULL;
  }
  GNUNET_free (bds);
}


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
                                  const char *upload_ref)
{
  struct BackupDownloadState *bds;

  bds = GNUNET_new (struct BackupDownloadState);
  bds->http_status = http_status;
  bds->sync_url = sync_url;
  bds->upload_reference = upload_ref;
  {
    struct TALER_TESTING_Command cmd = {
      .cls = bds,
      .label = label,
      .run = &backup_download_run,
      .cleanup = &backup_download_cleanup
    };

    return cmd;
  }
}
