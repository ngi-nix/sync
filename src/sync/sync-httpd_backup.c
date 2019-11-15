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
#include "sync-httpd_backup.h"
#include "sync-httpd_responses.h"

/**
 * @param connection the MHD connection to handle
 * @param account public key of the account the request is for
 * @param[in,out] con_cls the connection's closure (can be updated)
 * @return MHD result code
 */
int
sync_handler_backup_get (struct MHD_Connection *connection,
                         const struct SYNC_AccountPublicKeyP *account,
                         void **con_cls)
{
  struct SYNC_AccountSignatureP account_sig;
  struct GNUNET_HashCode backup_hash;
  struct GNUNET_HashCode prev_hash;
  size_t backup_size;
  void *backup;
  enum SYNC_DB_QueryStatus qs;
  struct MHD_Response *resp;
  const char *inm;
  struct GNUNET_HashCode inm_h;
  int ret;

  inm = MHD_lookup_connection_value (connection,
                                     MHD_HEADER_KIND,
                                     MHD_HTTP_HEADER_IF_NONE_MATCH);
  qs = db->lookup_account_TR (db->cls,
                              account,
                              &backup_hash);
  switch (qs)
  {
  case SYNC_DB_OLD_BACKUP_MISSMATCH:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                             "unexpected return status (backup missmatch)");
  case SYNC_DB_PAYMENT_REQUIRED:
    return SH_RESPONSE_reply_not_found (connection,
                                        TALER_EC_SYNC_ACCOUNT_UNKNOWN,
                                        "account");
  case SYNC_DB_HARD_ERROR:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_SYNC_DB_FETCH_ERROR,
                                             "hard database failure");
  case SYNC_DB_SOFT_ERROR:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_SYNC_DB_FETCH_ERROR,
                                             "soft database failure");
  case SYNC_DB_NO_RESULTS:
    resp = MHD_create_response_from_buffer (0,
                                            NULL,
                                            MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response (connection,
                              MHD_HTTP_NO_CONTENT,
                              resp);
    MHD_destroy_response (resp);
    return ret;
  case SYNC_DB_ONE_RESULT:
    if (NULL != inm)
    {
      if (GNUNET_OK !=
          GNUNET_STRINGS_string_to_data (inm,
                                         strlen (inm),
                                         &inm_h,
                                         sizeof (inm_h)))
      {
        GNUNET_break_op (0);
        return SH_RESPONSE_reply_bad_request (connection,
                                              TALER_EC_SYNC_BAD_ETAG,
                                              "Etag is not a base32-encoded SHA-512 hash");
      }
      if (0 == GNUNET_memcmp (&inm_h,
                              &backup_hash))
      {
        resp = MHD_create_response_from_buffer (0,
                                                NULL,
                                                MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response (connection,
                                  MHD_HTTP_NOT_MODIFIED,
                                  resp);
        MHD_destroy_response (resp);
        return ret;
      }
    }
    /* We have a result, should fetch and return it! */
    break;
  }

  qs = db->lookup_backup_TR (db->cls,
                             account,
                             &account_sig,
                             &prev_hash,
                             &backup_hash,
                             &backup_size,
                             &backup);
  switch (qs)
  {
  case SYNC_DB_OLD_BACKUP_MISSMATCH:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                             "unexpected return status (backup missmatch)");
  case SYNC_DB_PAYMENT_REQUIRED:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                             "unexpected return status (payment required)");
  case SYNC_DB_HARD_ERROR:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_SYNC_DB_FETCH_ERROR,
                                             "hard database failure");
  case SYNC_DB_SOFT_ERROR:
    GNUNET_break (0);
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_SYNC_DB_FETCH_ERROR,
                                             "soft database failure");
  case SYNC_DB_NO_RESULTS:
    GNUNET_break (0);
    /* Note: can theoretically happen due to non-transactional nature if
       the backup expired / was gc'ed JUST between the two SQL calls.
       But too rare to handle properly, as doing a transaction would be
       expensive. Just admit to failure ;-) */
    return SH_RESPONSE_reply_internal_error (connection,
                                             TALER_EC_SYNC_DB_FETCH_ERROR,
                                             "unexpected empty result set (try again?)");
  case SYNC_DB_ONE_RESULT:
    /* interesting case below */
    break;
  }
  resp = MHD_create_response_from_buffer (backup_size,
                                          backup,
                                          MHD_RESPMEM_MUST_FREE);
  {
    char *sig_s;
    char *prev_s;
    char *etag;

    sig_s = GNUNET_STRINGS_data_to_string_alloc (&account_sig,
                                                 sizeof (account_sig));
    prev_s = GNUNET_STRINGS_data_to_string_alloc (&prev_hash,
                                                  sizeof (prev_hash));
    etag = GNUNET_STRINGS_data_to_string_alloc (&backup_hash,
                                                sizeof (backup_hash));
    GNUNET_break (MHD_YES ==
                  MHD_add_response_header (resp,
                                           "X-Sync-Signature",
                                           sig_s));
    GNUNET_break (MHD_YES ==
                  MHD_add_response_header (resp,
                                           "X-Sync-Previous",
                                           prev_s));
    GNUNET_break (MHD_YES ==
                  MHD_add_response_header (resp,
                                           MHD_HTTP_HEADER_ETAG,
                                           etag));
    GNUNET_free (etag);
    GNUNET_free (prev_s);
    GNUNET_free (sig_s);
  }
  ret = MHD_queue_response (connection,
                            MHD_HTTP_NOT_MODIFIED,
                            resp);
  MHD_destroy_response (resp);
  return ret;
}


/**
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param account public key of the account the request is for
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @return MHD result code
 */
int
sync_handler_backup_post (struct MHD_Connection *connection,
                          void **con_cls,
                          const struct SYNC_AccountPublicKeyP *account,
                          const char *upload_data,
                          size_t *upload_data_size)
{
  struct SYNC_AccountSignatureP account_sig;

  return MHD_NO;
}
