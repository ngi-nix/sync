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
 * @file lib/testing_api_cmd_backup_upload.c
 * @brief command to upload data to the sync backend service.
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync_service.h"
#include "sync_testing_lib.h"
#include <taler/taler_util.h>
#include <taler/taler_testing_lib.h>

/**
 * State for a "backup upload" CMD.
 */
struct BackupUploadState
{

  /**
   * The backup data we are uploading.
   */
  const void *backup;

  /**
   * Number of bytes in @e backup.
   */
  size_t backup_size;

  /**
   * Expected status code.
   */
  unsigned int http_status;

  /**
   * Eddsa private key.
   */
  struct SYNC_AccountPrivateKeyP sync_priv;

  /**
   * The /backups POST operation handle.
   */
  struct SYNC_UploadOperation *uo;

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
 * Function called with the results of a #SYNC_upload().
 *
 * @param cls closure
 * @param ec Taler error code
 * @param http_status HTTP status of the request
 * @param ud details about the upload operation
 */
static void
backup_upload_cb (void *cls,
                  enum TALER_ErrorCode ec,
                  unsigned int http_status,
                  const struct SYNC_UploadDetails *ud)
{
  struct BackupUploadState *bus = cls;

  // FIXME: next!
}


/**
 * Run a "backup upload" CMD.
 *
 * @param cls closure.
 * @param cmd command currently being run.
 * @param is interpreter state.
 */
static void
backup_upload_run (void *cls,
                   const struct TALER_TESTING_Command *cmd,
                   struct TALER_TESTING_Interpreter *is)
{
  struct BackupUploadState *bus = cls;

  bus->is = is;
  bus->uo = SYNC_upload (is->ctx,
                         bus->sync_url,
                         &bus->sync_priv,
                         NULL /* prev hash */,
                         bus->backup_size,
                         bus->backup,
                         GNUNET_NO /* payment req */,
                         NULL /* pay order id */,
                         &backup_upload_cb,
                         bus);
  if (NULL == bus->uo)
  {
    // FIMXE: fail!
  }
}


/**
 * Free the state of a "backup upload" CMD, and possibly
 * cancel it if it did not complete.
 *
 * @param cls closure.
 * @param cmd command being freed.
 */
static void
backup_upload_cleanup (void *cls,
                       const struct TALER_TESTING_Command *cmd)
{
  struct BackupUploadState *bus = cls;

  if (NULL != bus->uo)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Command '%s' did not complete (backup upload)\n",
                cmd->label);
    SYNC_upload_cancel (bus->uo);
    bus->uo = NULL;
  }
  GNUNET_free (bus);
}


/**
 * Make the "policy store" command.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @param http_status expected HTTP status.
 * @param pub account identifier
 * @param payment_id payment identifier
 * @param policy_data recovery data to post
 *
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_upload (const char *label,
                                const char *sync_url,
                                unsigned int http_status,
                                const void *backup_data,
                                size_t backup_data_size)
{
  struct BackupUploadState *bus;

  bus = GNUNET_new (struct BackupUploadState);
  bus->http_status = http_status;
  bus->sync_url = sync_url;
  bus->backup = backup_data;
  bus->backup_size = backup_data_size;
  {
    struct TALER_TESTING_Command cmd = {
      .cls = bus,
      .label = label,
      .run = &backup_upload_run,
      .cleanup = &backup_upload_cleanup
    };

    return cmd;
  }
}
