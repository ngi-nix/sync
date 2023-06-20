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
   * Eddsa public key.
   */
  struct SYNC_AccountPublicKeyP sync_pub;

  /**
   * Hash of the upload (all zeros if there was no upload).
   */
  const struct GNUNET_HashCode *upload_hash;

  /**
   * Hash of the previous upload (all zeros if there was no previous upload).
   */
  const struct GNUNET_HashCode *prev_upload_hash;

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

  /**
   * Reference to upload command we expect to download.
   */
  const char *upload_reference;

  /**
   * Expected status code.
   */
  unsigned int http_status;

};


/**
 * Function called with the results of a #SYNC_download().
 *
 * @param cls closure
 * @param dd details about the download operation
 */
static void
backup_download_cb (void *cls,
                    const struct SYNC_DownloadDetails *dd)
{
  struct BackupDownloadState *bds = cls;

  bds->download = NULL;
  if (dd->http_status != bds->http_status)
  {
    TALER_TESTING_unexpected_status (bds->is,
                                     dd->http_status);
  }
  if (NULL != bds->upload_reference)
  {
    if ( (MHD_HTTP_OK == dd->http_status) &&
         (0 != GNUNET_memcmp (&dd->details.ok.curr_backup_hash,
                              bds->upload_hash)) )
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bds->is);
      return;
    }
    if ( (MHD_HTTP_OK == dd->http_status) &&
         (0 != GNUNET_memcmp (&dd->details.ok.prev_backup_hash,
                              bds->prev_upload_hash)) )
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bds->is);
      return;
    }
  }
  TALER_TESTING_interpreter_next (bds->is);
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
  if (NULL != bds->upload_reference)
  {
    const struct TALER_TESTING_Command *upload_cmd;
    const struct SYNC_AccountPublicKeyP *sync_pub;

    upload_cmd = TALER_TESTING_interpreter_lookup_command
                   (is,
                   bds->upload_reference);
    if (NULL == upload_cmd)
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bds->is);
      return;
    }
    if (GNUNET_OK !=
        SYNC_TESTING_get_trait_hash (upload_cmd,
                                     SYNC_TESTING_TRAIT_HASH_CURRENT,
                                     &bds->upload_hash))
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bds->is);
      return;
    }
    if (GNUNET_OK !=
        SYNC_TESTING_get_trait_hash (upload_cmd,
                                     SYNC_TESTING_TRAIT_HASH_PREVIOUS,
                                     &bds->prev_upload_hash))
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bds->is);
      return;
    }
    if (GNUNET_OK !=
        SYNC_TESTING_get_trait_account_pub (upload_cmd,
                                            0,
                                            &sync_pub))
    {
      GNUNET_break (0);
      TALER_TESTING_interpreter_fail (bds->is);
      return;
    }
    bds->sync_pub = *sync_pub;
  }
  bds->download = SYNC_download (TALER_TESTING_interpreter_get_context (is),
                                 bds->sync_url,
                                 &bds->sync_pub,
                                 &backup_download_cb,
                                 bds);
  if (NULL == bds->download)
  {
    GNUNET_break (0);
    TALER_TESTING_interpreter_fail (bds->is);
    return;
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

  GNUNET_assert (NULL != upload_ref);
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


/**
 * Make the "backup download" command for a non-existent upload.
 *
 * @param label command label
 * @param sync_url base URL of the sync serving
 *        the policy store request.
 * @return the command
 */
struct TALER_TESTING_Command
SYNC_TESTING_cmd_backup_nx (const char *label,
                            const char *sync_url)
{
  struct BackupDownloadState *bds;
  struct GNUNET_CRYPTO_EddsaPrivateKey priv;

  bds = GNUNET_new (struct BackupDownloadState);
  bds->http_status = MHD_HTTP_NOT_FOUND;
  bds->sync_url = sync_url;
  GNUNET_CRYPTO_eddsa_key_create (&priv);
  GNUNET_CRYPTO_eddsa_key_get_public (&priv,
                                      &bds->sync_pub.eddsa_pub);
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
