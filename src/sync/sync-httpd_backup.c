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


/**
 * Handle request on @a connection for retrieval of the latest
 * backup of @a account.
 *
 * @param connection the MHD connection to handle
 * @param account public key of the account the request is for
 * @return MHD result code
 */
MHD_RESULT
SH_backup_get (struct MHD_Connection *connection,
               const struct SYNC_AccountPublicKeyP *account)
{
  struct GNUNET_HashCode backup_hash;
  enum SYNC_DB_QueryStatus qs;
  MHD_RESULT ret;

  qs = db->lookup_account_TR (db->cls,
                              account,
                              &backup_hash);
  switch (qs)
  {
  case SYNC_DB_OLD_BACKUP_MISSING:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                       "unexpected return status (backup missing)");
  case SYNC_DB_OLD_BACKUP_MISMATCH:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                       "unexpected return status (backup mismatch)");
  case SYNC_DB_PAYMENT_REQUIRED:
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_NOT_FOUND,
                                       TALER_EC_SYNC_ACCOUNT_UNKNOWN,
                                       "account");
  case SYNC_DB_HARD_ERROR:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_SYNC_DB_FETCH_ERROR,
                                       "hard database failure");
  case SYNC_DB_SOFT_ERROR:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_SYNC_DB_FETCH_ERROR,
                                       "soft database failure");
  case SYNC_DB_NO_RESULTS:
    {
      struct MHD_Response *resp;

      resp = MHD_create_response_from_buffer (0,
                                              NULL,
                                              MHD_RESPMEM_PERSISTENT);
      TALER_MHD_add_global_headers (resp);
      ret = MHD_queue_response (connection,
                                MHD_HTTP_NO_CONTENT,
                                resp);
      MHD_destroy_response (resp);
    }
    return ret;
  case SYNC_DB_ONE_RESULT:
    {
      const char *inm;

      inm = MHD_lookup_connection_value (connection,
                                         MHD_HEADER_KIND,
                                         MHD_HTTP_HEADER_IF_NONE_MATCH);
      if (NULL != inm)
      {
        struct GNUNET_HashCode inm_h;

        if (GNUNET_OK !=
            GNUNET_STRINGS_string_to_data (inm,
                                           strlen (inm),
                                           &inm_h,
                                           sizeof (inm_h)))
        {
          GNUNET_break_op (0);
          return TALER_MHD_reply_with_error (connection,
                                             MHD_HTTP_BAD_REQUEST,
                                             TALER_EC_SYNC_BAD_IF_NONE_MATCH,
                                             "Etag does not include a base32-encoded SHA-512 hash");
        }
        if (0 == GNUNET_memcmp (&inm_h,
                                &backup_hash))
        {
          struct MHD_Response *resp;

          resp = MHD_create_response_from_buffer (0,
                                                  NULL,
                                                  MHD_RESPMEM_PERSISTENT);
          TALER_MHD_add_global_headers (resp);
          ret = MHD_queue_response (connection,
                                    MHD_HTTP_NOT_MODIFIED,
                                    resp);
          MHD_destroy_response (resp);
          return ret;
        }
      }
    }
    /* We have a result, should fetch and return it! */
    break;
  }
  return SH_return_backup (connection,
                           account,
                           MHD_HTTP_OK);
}


/**
 * Return the current backup of @a account on @a connection
 * using @a default_http_status on success.
 *
 * @param connection MHD connection to use
 * @param account account to query
 * @param default_http_status HTTP status to queue response
 *  with on success (#MHD_HTTP_OK or #MHD_HTTP_CONFLICT)
 * @return MHD result code
 */
MHD_RESULT
SH_return_backup (struct MHD_Connection *connection,
                  const struct SYNC_AccountPublicKeyP *account,
                  unsigned int default_http_status)
{
  enum SYNC_DB_QueryStatus qs;
  struct MHD_Response *resp;
  MHD_RESULT ret;
  struct SYNC_AccountSignatureP account_sig;
  struct GNUNET_HashCode backup_hash;
  struct GNUNET_HashCode prev_hash;
  size_t backup_size;
  void *backup;

  qs = db->lookup_backup_TR (db->cls,
                             account,
                             &account_sig,
                             &prev_hash,
                             &backup_hash,
                             &backup_size,
                             &backup);
  switch (qs)
  {
  case SYNC_DB_OLD_BACKUP_MISSING:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                       "unexpected return status (backup missing)");
  case SYNC_DB_OLD_BACKUP_MISMATCH:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                       "unexpected return status (backup mismatch)");
  case SYNC_DB_PAYMENT_REQUIRED:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_INTERNAL_INVARIANT_FAILURE,
                                       "unexpected return status (payment required)");
  case SYNC_DB_HARD_ERROR:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_SYNC_DB_FETCH_ERROR,
                                       "hard database failure");
  case SYNC_DB_SOFT_ERROR:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_SYNC_DB_FETCH_ERROR,
                                       "soft database failure");
  case SYNC_DB_NO_RESULTS:
    GNUNET_break (0);
    /* Note: can theoretically happen due to non-transactional nature if
       the backup expired / was gc'ed JUST between the two SQL calls.
       But too rare to handle properly, as doing a transaction would be
       expensive. Just admit to failure ;-) */
    return TALER_MHD_reply_with_error (connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_SYNC_DB_FETCH_ERROR,
                                       "unexpected empty result set (try again?)");
  case SYNC_DB_ONE_RESULT:
    /* interesting case below */
    break;
  }
  resp = MHD_create_response_from_buffer (backup_size,
                                          backup,
                                          MHD_RESPMEM_MUST_FREE);
  TALER_MHD_add_global_headers (resp);
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
                                           "Sync-Signature",
                                           sig_s));
    GNUNET_break (MHD_YES ==
                  MHD_add_response_header (resp,
                                           "Sync-Previous",
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
                            default_http_status,
                            resp);
  MHD_destroy_response (resp);
  return ret;
}
