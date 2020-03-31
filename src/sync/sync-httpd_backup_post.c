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
 * @file sync-httpd_backup_post.c
 * @brief functions to handle incoming requests for backups
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync-httpd.h"
#include <gnunet/gnunet_util_lib.h>
#include "sync-httpd_backup.h"
#include <taler/taler_json_lib.h>
#include <taler/taler_merchant_service.h>
#include <taler/taler_signatures.h>


/**
 * How long do we hold an HTTP client connection if
 * we are awaiting payment before giving up?
 */
#define CHECK_PAYMENT_TIMEOUT GNUNET_TIME_relative_multiply ( \
    GNUNET_TIME_UNIT_MINUTES, 30)


/**
 * Context for an upload operation.
 */
struct BackupContext
{

  /**
   * Context for cleanup logic.
   */
  struct TM_HandlerContext hc;

  /**
   * Signature of the account holder.
   */
  struct SYNC_AccountSignatureP account_sig;

  /**
   * Public key of the account holder.
   */
  struct SYNC_AccountPublicKeyP account;

  /**
   * Hash of the previous upload, or zeros if first upload.
   */
  struct GNUNET_HashCode old_backup_hash;

  /**
   * Hash of the upload we are receiving right now (as promised
   * by the client, to be verified!).
   */
  struct GNUNET_HashCode new_backup_hash;

  /**
   * Hash context for the upload.
   */
  struct GNUNET_HashContext *hash_ctx;

  /**
   * Kept in DLL for shutdown handling while suspended.
   */
  struct BackupContext *next;

  /**
   * Kept in DLL for shutdown handling while suspended.
   */
  struct BackupContext *prev;

  /**
   * Used while suspended for resumption.
   */
  struct MHD_Connection *con;

  /**
   * Upload, with as many bytes as we have received so far.
   */
  char *upload;

  /**
   * Used while we are awaiting proposal creation.
   */
  struct TALER_MERCHANT_ProposalOperation *po;

  /**
   * Used while we are waiting payment.
   */
  struct TALER_MERCHANT_CheckPaymentOperation *cpo;

  /**
   * HTTP response code to use on resume, if non-NULL.
   */
  struct MHD_Response *resp;

  /**
   * Order under which the client promised payment, or NULL.
   */
  const char *order_id;

  /**
   * Order ID for the client that we found in our database.
   */
  char *existing_order_id;

  /**
   * Timestamp of the order in @e existing_order_id. Used to
   * select the most recent unpaid offer.
   */
  struct GNUNET_TIME_Absolute existing_order_timestamp;

  /**
   * Expected total upload size.
   */
  size_t upload_size;

  /**
   * Current offset for the upload.
   */
  size_t upload_off;

  /**
   * HTTP response code to use on resume, if resp is set.
   */
  unsigned int response_code;

};


/**
 * Kept in DLL for shutdown handling while suspended.
 */
static struct BackupContext *bc_head;

/**
 * Kept in DLL for shutdown handling while suspended.
 */
static struct BackupContext *bc_tail;


/**
 * Service is shutting down, resume all MHD connections NOW.
 */
void
SH_resume_all_bc ()
{
  struct BackupContext *bc;

  while (NULL != (bc = bc_head))
  {
    GNUNET_CONTAINER_DLL_remove (bc_head,
                                 bc_tail,
                                 bc);
    MHD_resume_connection (bc->con);
    if (NULL != bc->po)
    {
      TALER_MERCHANT_proposal_cancel (bc->po);
      bc->po = NULL;
    }
    if (NULL != bc->cpo)
    {
      TALER_MERCHANT_check_payment_cancel (bc->cpo);
      bc->cpo = NULL;
    }
  }
}


/**
 * Function called to clean up a backup context.
 *
 * @param hc a `struct BackupContext`
 */
static void
cleanup_ctx (struct TM_HandlerContext *hc)
{
  struct BackupContext *bc = (struct BackupContext *) hc;

  if (NULL != bc->po)
    TALER_MERCHANT_proposal_cancel (bc->po);
  if (NULL != bc->hash_ctx)
    GNUNET_CRYPTO_hash_context_abort (bc->hash_ctx);
  if (NULL != bc->resp)
    MHD_destroy_response (bc->resp);
  GNUNET_free_non_null (bc->existing_order_id);
  GNUNET_free_non_null (bc->upload);
  GNUNET_free (bc);
}


/**
 * Transmit a payment request for @a order_id on @a connection
 *
 * @param connection MHD connection
 * @param order_id our backend's order ID
 * @return MHD repsonse to use
 */
static struct MHD_Response *
make_payment_request (const char *order_id)
{
  struct MHD_Response *resp;

  /* request payment via Taler */
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Creating payment request for order `%s'\n",
              order_id);
  resp = MHD_create_response_from_buffer (0,
                                          NULL,
                                          MHD_RESPMEM_PERSISTENT);
  TALER_MHD_add_global_headers (resp);
  {
    char *hdr;

    /* TODO: support instances? */
    GNUNET_asprintf (&hdr,
                     "taler://pay/%s/-/-/%s",
                     SH_backend_url,
                     order_id);
    GNUNET_break (MHD_YES ==
                  MHD_add_response_header (resp,
                                           "Taler",
                                           hdr));
    GNUNET_free (hdr);
  }
  return resp;
}


/**
 * Callbacks of this type are used to serve the result of submitting a
 * /contract request to a merchant.
 *
 * @param cls our `struct BackupContext`
 * @param http_status HTTP response code, 200 indicates success;
 *                    0 if the backend's reply is bogus (fails to follow the protocol)
 * @param ec taler-specific error code
 * @param obj raw JSON reply, or error details if the request failed
 * @param order_id order id of the newly created order
 */
static void
proposal_cb (void *cls,
             unsigned int http_status,
             enum TALER_ErrorCode ec,
             const json_t *obj,
             const char *order_id)
{
  struct BackupContext *bc = cls;
  enum SYNC_DB_QueryStatus qs;

  bc->po = NULL;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Resuming connection with order `%s'\n",
              order_id);
  GNUNET_CONTAINER_DLL_remove (bc_head,
                               bc_tail,
                               bc);
  MHD_resume_connection (bc->con);
  SH_trigger_daemon ();
  if (MHD_HTTP_OK != http_status)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Backend returned status %u/%u\n",
                http_status,
                (unsigned int) ec);
    GNUNET_break (0);
    bc->resp = TALER_MHD_make_json_pack ("{s:I, s:s, s:I, s:I}",
                                         "code",
                                         (json_int_t)
                                         TALER_EC_SYNC_PAYMENT_CREATE_BACKEND_ERROR,
                                         "hint",
                                         "Failed to setup order with merchant backend",
                                         "backend-ec", (json_int_t) ec,
                                         "backend-http-status",
                                         (json_int_t) http_status);
    GNUNET_assert (NULL != bc->resp);
    bc->response_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Storing payment request for order `%s'\n",
              order_id);
  qs = db->store_payment_TR (db->cls,
                             &bc->account,
                             order_id,
                             &SH_annual_fee);
  if (0 >= qs)
  {
    GNUNET_break (0);
    bc->resp = TALER_MHD_make_error (TALER_EC_SYNC_PAYMENT_CREATE_DB_ERROR,
                                     "Failed to persist payment request in sync database");
    GNUNET_assert (NULL != bc->resp);
    bc->response_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Obtained fresh order `%s'\n",
              order_id);
  bc->resp = make_payment_request (order_id);
  GNUNET_assert (NULL != bc->resp);
  bc->response_code = MHD_HTTP_PAYMENT_REQUIRED;
}


/**
 * Function called on all pending payments for the right
 * account.
 *
 * @param cls closure, our `struct BackupContext`
 * @param timestamp for how long have we been waiting
 * @param order_id order id in the backend
 * @param amount how much is the order for
 */
static void
ongoing_payment_cb (void *cls,
                    struct GNUNET_TIME_Absolute timestamp,
                    const char *order_id,
                    const struct TALER_Amount *amount)
{
  struct BackupContext *bc = cls;

  (void) amount;
  if (0 != TALER_amount_cmp (amount,
                             &SH_annual_fee))
    return; /* can't re-use, fees changed */
  if ( (NULL == bc->existing_order_id) ||
       (bc->existing_order_timestamp.abs_value_us < timestamp.abs_value_us) )
  {
    GNUNET_free_non_null (bc->existing_order_id);
    bc->existing_order_id = GNUNET_strdup (order_id);
    bc->existing_order_timestamp = timestamp;
  }
}


/**
 * Callback to process a GET /check-payment request
 *
 * @param cls our `struct BackupContext`
 * @param http_status HTTP status code for this request
 * @param obj raw response body
 * @param paid #GNUNET_YES if the payment is settled, #GNUNET_NO if not
 *        settled, $GNUNET_SYSERR on error
 *        (note that refunded payments are returned as paid!)
 * @param refunded #GNUNET_YES if there is at least on refund on this payment,
 *        #GNUNET_NO if refunded, #GNUNET_SYSERR or error
 * @param refunded_amount amount that was refunded, NULL if there
 *        was no refund
 * @param taler_pay_uri the URI that instructs the wallets to process
 *                      the payment
 */
static void
check_payment_cb (void *cls,
                  unsigned int http_status,
                  const json_t *obj,
                  int paid,
                  int refunded,
                  struct TALER_Amount *refund_amount,
                  const char *taler_pay_uri)
{
  struct BackupContext *bc = cls;

  /* refunds are not supported, verify */
  bc->cpo = NULL;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Payment status checked: %s\n",
              paid ? "paid" : "unpaid");
  GNUNET_CONTAINER_DLL_remove (bc_head,
                               bc_tail,
                               bc);
  MHD_resume_connection (bc->con);
  SH_trigger_daemon ();
  GNUNET_break ( (GNUNET_NO == refunded) &&
                 (NULL == refund_amount) );
  if (paid)
  {
    enum SYNC_DB_QueryStatus qs;

    qs = db->increment_lifetime_TR (db->cls,
                                    &bc->account,
                                    bc->order_id,
                                    GNUNET_TIME_UNIT_YEARS); /* always annual */
    if (0 <= qs)
      return; /* continue as planned */
    GNUNET_break (0);
    bc->resp = TALER_MHD_make_error (TALER_EC_SYNC_PAYMENT_CONFIRM_DB_ERROR,
                                     "Failed to persist payment confirmation in sync database");
    GNUNET_assert (NULL != bc->resp);
    bc->response_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
    return; /* continue as planned */
  }
  if (NULL != bc->existing_order_id)
  {
    /* repeat payment request */
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Repeating payment request\n");
    bc->resp = make_payment_request (bc->existing_order_id);
    GNUNET_assert (NULL != bc->resp);
    bc->response_code = MHD_HTTP_PAYMENT_REQUIRED;
    return;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Timeout waiting for payment\n");
  bc->resp = TALER_MHD_make_error (TALER_EC_SYNC_PAYMENT_TIMEOUT,
                                   "Timeout awaiting promised payment");
  GNUNET_assert (NULL != bc->resp);
  bc->response_code = MHD_HTTP_REQUEST_TIMEOUT;
}


/**
 * Helper function used to ask our backend to await
 * a payment for the user's account.
 *
 * @param bc context to begin payment for.
 * @param timeout when to give up trying
 * @param order_id which order to check for the payment
 */
static void
await_payment (struct BackupContext *bc,
               struct GNUNET_TIME_Relative timeout,
               const char *order_id)
{
  GNUNET_CONTAINER_DLL_insert (bc_head,
                               bc_tail,
                               bc);
  MHD_suspend_connection (bc->con);
  bc->order_id = order_id;
  bc->cpo = TALER_MERCHANT_check_payment (SH_ctx,
                                          SH_backend_url,
                                          order_id,
                                          NULL /* our payments are NOT session-bound */,
                                          timeout,
                                          &check_payment_cb,
                                          bc);
  SH_trigger_curl ();
}


/**
 * Helper function used to ask our backend to begin
 * processing a payment for the user's account.
 * May perform asynchronous operations by suspending the connection
 * if required.
 *
 * @param bc context to begin payment for.
 * @param pay_req #GNUNET_YES if payment was explicitly requested,
 *                #GNUNET_NO if payment is needed
 * @return MHD status code
 */
static int
begin_payment (struct BackupContext *bc,
               int pay_req)
{
  json_t *order;
  enum GNUNET_DB_QueryStatus qs;

  qs = db->lookup_pending_payments_by_account_TR (db->cls,
                                                  &bc->account,
                                                  &ongoing_payment_cb,
                                                  bc);
  if (qs < 0)
  {
    struct MHD_Response *resp;
    int ret;

    resp = TALER_MHD_make_error (TALER_EC_SYNC_PAYMENT_CHECK_ORDER_DB_ERROR,
                                 "Failed to check for existing orders in sync database");
    ret = MHD_queue_response (bc->con,
                              MHD_HTTP_INTERNAL_SERVER_ERROR,
                              resp);
    GNUNET_break (MHD_YES == ret);
    MHD_destroy_response (resp);
    return ret;
  }
  if (NULL != bc->existing_order_id)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Have existing order, waiting for `%s' to complete\n",
                bc->existing_order_id);
    await_payment (bc,
                   GNUNET_TIME_UNIT_ZERO /* no long polling */,
                   bc->existing_order_id);
    return MHD_YES;
  }
  GNUNET_CONTAINER_DLL_insert (bc_head,
                               bc_tail,
                               bc);
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Suspending connection while creating order at `%s'\n",
              SH_backend_url);
  MHD_suspend_connection (bc->con);
  order = json_pack ("{s:o, s:s, s:s}",
                     "amount", TALER_JSON_from_amount (&SH_annual_fee),
                     "summary", "annual fee for sync service",
                     "fulfillment_url", SH_fulfillment_url);
  bc->po = TALER_MERCHANT_order_put (SH_ctx,
                                     SH_backend_url,
                                     order,
                                     &proposal_cb,
                                     bc);
  SH_trigger_curl ();
  json_decref (order);
  return MHD_YES;
}


/**
 * We got some query status from the DB.  Handle the error cases.
 * May perform asynchronous operations by suspending the connection
 * if required.
 *
 * @param bc connection to handle status for
 * @param qs query status to handle
 * @return #MHD_YES or #MHD_NO
 */
static int
handle_database_error (struct BackupContext *bc,
                       enum SYNC_DB_QueryStatus qs)
{
  switch (qs)
  {
  case SYNC_DB_OLD_BACKUP_MISSING:
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Update failed: no existing backup\n");
    return TALER_MHD_reply_with_error (bc->con,
                                       MHD_HTTP_NOT_FOUND,
                                       TALER_EC_SYNC_PREVIOUS_BACKUP_UNKNOWN,
                                       "Cannot update, no existing backup known");
  case SYNC_DB_OLD_BACKUP_MISMATCH:
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Conflict detected, returning existing backup\n");
    return SH_return_backup (bc->con,
                             &bc->account,
                             MHD_HTTP_CONFLICT);
  case SYNC_DB_PAYMENT_REQUIRED:
    {
      const char *order_id;

      order_id = MHD_lookup_connection_value (bc->con,
                                              MHD_GET_ARGUMENT_KIND,
                                              "paying");
      if (NULL == order_id)
      {
        GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                    "Payment required, starting payment process\n");
        return begin_payment (bc,
                              GNUNET_NO);
      }
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  "Payment required, awaiting completion of `%s'\n",
                  order_id);
      await_payment (bc,
                     CHECK_PAYMENT_TIMEOUT,
                     order_id);
    }
    return MHD_YES;
  case SYNC_DB_HARD_ERROR:
  case SYNC_DB_SOFT_ERROR:
    GNUNET_break (0);
    return TALER_MHD_reply_with_error (bc->con,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       TALER_EC_SYNC_DATABASE_FETCH_ERROR,
                                       "failed to fetch existing record from database");
  case SYNC_DB_NO_RESULTS:
    GNUNET_assert (0);
    return MHD_NO;
  /* intentional fall-through! */
  case SYNC_DB_ONE_RESULT:
    GNUNET_assert (0);
    return MHD_NO;
  }
  GNUNET_break (0);
  return MHD_NO;
}


/**
 * Handle a client POSTing a backup to us.
 *
 * @param connection the MHD connection to handle
 * @param[in,out] connection_cls the connection's closure (can be updated)
 * @param account public key of the account the request is for
 * @param upload_data upload data
 * @param[in,out] upload_data_size number of bytes (left) in @a upload_data
 * @return MHD result code
 */
int
SH_backup_post (struct MHD_Connection *connection,
                void **con_cls,
                const struct SYNC_AccountPublicKeyP *account,
                const char *upload_data,
                size_t *upload_data_size)
{
  struct BackupContext *bc;

  bc = *con_cls;
  if (NULL == bc)
  {
    /* first call, setup internals */
    bc = GNUNET_new (struct BackupContext);
    bc->hc.cc = &cleanup_ctx;
    bc->con = connection;
    bc->account = *account;
    *con_cls = bc;

    /* now setup 'bc' */
    {
      const char *lens;
      unsigned long len;

      lens = MHD_lookup_connection_value (connection,
                                          MHD_HEADER_KIND,
                                          MHD_HTTP_HEADER_CONTENT_LENGTH);
      if ( (NULL == lens) ||
           (1 !=
            sscanf (lens,
                    "%lu",
                    &len)) )
      {
        GNUNET_break_op (0);
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           TALER_EC_SYNC_BAD_CONTENT_LENGTH,
                                           (NULL == lens)
                                           ? "Content-length value missing"
                                           : "Content-length value malformed");
      }
      if (len / 1024 / 1024 >= SH_upload_limit_mb)
      {
        GNUNET_break_op (0);
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_PAYLOAD_TOO_LARGE,
                                           TALER_EC_SYNC_BAD_CONTENT_LENGTH,
                                           "Content-length value not acceptable");
      }
      bc->upload = GNUNET_malloc_large (len);
      if (NULL == bc->upload)
      {
        GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                             "malloc");
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_PAYLOAD_TOO_LARGE,
                                           TALER_EC_SYNC_OUT_OF_MEMORY_ON_CONTENT_LENGTH,
                                           "Server out of memory, try again later");
      }
      bc->upload_size = (size_t) len;
    }
    {
      const char *im;

      im = MHD_lookup_connection_value (connection,
                                        MHD_HEADER_KIND,
                                        MHD_HTTP_HEADER_IF_MATCH);
      if ( (NULL != im) &&
           (GNUNET_OK !=
            GNUNET_STRINGS_string_to_data (im,
                                           strlen (im),
                                           &bc->old_backup_hash,
                                           sizeof (bc->old_backup_hash))) )
      {
        GNUNET_break_op (0);
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           TALER_EC_SYNC_BAD_IF_MATCH,
                                           "If-Match does not include not a base32-encoded SHA-512 hash");
      }
    }
    {
      const char *sig_s;

      sig_s = MHD_lookup_connection_value (connection,
                                           MHD_HEADER_KIND,
                                           "Sync-Signature");
      if ( (NULL == sig_s) ||
           (GNUNET_OK !=
            GNUNET_STRINGS_string_to_data (sig_s,
                                           strlen (sig_s),
                                           &bc->account_sig,
                                           sizeof (bc->account_sig))) )
      {
        GNUNET_break_op (0);
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           TALER_EC_SYNC_BAD_SYNC_SIGNATURE,
                                           "Sync-Signature does not include a base32-encoded EdDSA signature");
      }
    }
    {
      const char *etag;

      etag = MHD_lookup_connection_value (connection,
                                          MHD_HEADER_KIND,
                                          MHD_HTTP_HEADER_IF_NONE_MATCH);
      if ( (NULL == etag) ||
           (GNUNET_OK !=
            GNUNET_STRINGS_string_to_data (etag,
                                           strlen (etag),
                                           &bc->new_backup_hash,
                                           sizeof (bc->new_backup_hash))) )
      {
        GNUNET_break_op (0);
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           TALER_EC_SYNC_BAD_IF_NONE_MATCH,
                                           "If-none-match does not include not a base32-encoded SHA-512 hash");
      }
    }
    /* validate signature */
    {
      struct SYNC_UploadSignaturePS usp;

      usp.purpose.size = htonl (sizeof (struct SYNC_UploadSignaturePS));
      usp.purpose.purpose = htonl (TALER_SIGNATURE_SYNC_BACKUP_UPLOAD);
      usp.old_backup_hash = bc->old_backup_hash;
      usp.new_backup_hash = bc->new_backup_hash;
      if (GNUNET_OK !=
          GNUNET_CRYPTO_eddsa_verify (TALER_SIGNATURE_SYNC_BACKUP_UPLOAD,
                                      &usp.purpose,
                                      &bc->account_sig.eddsa_sig,
                                      &account->eddsa_pub))
      {
        GNUNET_break_op (0);
        return TALER_MHD_reply_with_error (connection,
                                           MHD_HTTP_FORBIDDEN,
                                           TALER_EC_SYNC_INVALID_SIGNATURE,
                                           "Account signature does not match upload");
      }
    }
    /* get ready to hash (done here as we may go async for payments next) */
    bc->hash_ctx = GNUNET_CRYPTO_hash_context_start ();

    /* Check database to see if the transaction is permissible */
    {
      struct GNUNET_HashCode hc;
      enum SYNC_DB_QueryStatus qs;

      qs = db->lookup_account_TR (db->cls,
                                  account,
                                  &hc);
      if (qs < 0)
        return handle_database_error (bc,
                                      qs);
      if (SYNC_DB_NO_RESULTS == qs)
        memset (&hc, 0, sizeof (hc));
      if (0 == GNUNET_memcmp (&hc,
                              &bc->new_backup_hash))
      {
        /* Refuse upload: we already have that backup! */
        struct MHD_Response *resp;
        int ret;

        resp = MHD_create_response_from_buffer (0,
                                                NULL,
                                                MHD_RESPMEM_PERSISTENT);
        TALER_MHD_add_global_headers (resp);
        ret = MHD_queue_response (connection,
                                  MHD_HTTP_NOT_MODIFIED,
                                  resp);
        GNUNET_break (MHD_YES == ret);
        MHD_destroy_response (resp);
        return ret;
      }
      if (0 != GNUNET_memcmp (&hc,
                              &bc->old_backup_hash))
      {
        /* Refuse upload: if-none-match failed! */
        GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                    "Conflict detected, returning existing backup\n");
        return SH_return_backup (connection,
                                 account,
                                 MHD_HTTP_CONFLICT);
      }
    }
    /* check if the client insists on paying */
    {
      const char *order_req;

      order_req = MHD_lookup_connection_value (connection,
                                               MHD_GET_ARGUMENT_KIND,
                                               "pay");
      if (NULL != order_req)
      {
        GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                    "Payment requested, starting payment process\n");
        return begin_payment (bc,
                              GNUNET_YES);
      }
    }
    /* ready to begin! */
    return MHD_YES;
  }
  /* handle upload */
  if (0 != *upload_data_size)
  {
    /* check MHD invariant */
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Processing %u bytes of upload data\n",
                (unsigned int) *upload_data_size);
    GNUNET_assert (bc->upload_off + *upload_data_size <= bc->upload_size);
    memcpy (&bc->upload[bc->upload_off],
            upload_data,
            *upload_data_size);
    bc->upload_off += *upload_data_size;
    GNUNET_CRYPTO_hash_context_read (bc->hash_ctx,
                                     upload_data,
                                     *upload_data_size);
    *upload_data_size = 0;
    return MHD_YES;
  }
  else if ( (0 == bc->upload_off) &&
            (0 != bc->upload_size) &&
            (NULL == bc->resp) )
  {
    /* wait for upload */
    return MHD_YES;
  }
  if (NULL != bc->resp)
  {
    int ret;

    /* We generated a response asynchronously, queue that */
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Returning asynchronously generated response with HTTP status %u\n",
                bc->response_code);
    ret = MHD_queue_response (connection,
                              bc->response_code,
                              bc->resp);
    GNUNET_break (MHD_YES == ret);
    MHD_destroy_response (bc->resp);
    bc->resp = NULL;
    return ret;
  }

  /* finished with upload, check hash */
  {
    struct GNUNET_HashCode our_hash;

    GNUNET_CRYPTO_hash_context_finish (bc->hash_ctx,
                                       &our_hash);
    bc->hash_ctx = NULL;
    if (0 != GNUNET_memcmp (&our_hash,
                            &bc->new_backup_hash))
    {
      GNUNET_break_op (0);
      return TALER_MHD_reply_with_error (connection,
                                         MHD_HTTP_BAD_REQUEST,
                                         TALER_EC_SYNC_INVALID_UPLOAD,
                                         "Data uploaded does not match Etag promise");
    }
  }

  /* store backup to database */
  {
    enum SYNC_DB_QueryStatus qs;

    if (0 == GNUNET_is_zero (&bc->old_backup_hash))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  "Uploading first backup to account\n");
      qs = db->store_backup_TR (db->cls,
                                account,
                                &bc->account_sig,
                                &bc->new_backup_hash,
                                bc->upload_size,
                                bc->upload);
    }
    else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  "Uploading existing backup of account\n");
      qs = db->update_backup_TR (db->cls,
                                 account,
                                 &bc->old_backup_hash,
                                 &bc->account_sig,
                                 &bc->new_backup_hash,
                                 bc->upload_size,
                                 bc->upload);
    }
    if (qs < 0)
      return handle_database_error (bc,
                                    qs);
    if (0 == qs)
    {
      /* database says nothing actually changed, 304 (could
         theoretically happen if another equivalent upload succeeded
         since we last checked!) */
      struct MHD_Response *resp;
      int ret;

      resp = MHD_create_response_from_buffer (0,
                                              NULL,
                                              MHD_RESPMEM_PERSISTENT);
      TALER_MHD_add_global_headers (resp);
      ret = MHD_queue_response (connection,
                                MHD_HTTP_NOT_MODIFIED,
                                resp);
      GNUNET_break (MHD_YES == ret);
      MHD_destroy_response (resp);
      return ret;
    }
  }

  /* generate main (204) standard success reply */
  {
    struct MHD_Response *resp;
    int ret;

    resp = MHD_create_response_from_buffer (0,
                                            NULL,
                                            MHD_RESPMEM_PERSISTENT);
    TALER_MHD_add_global_headers (resp);
    ret = MHD_queue_response (connection,
                              MHD_HTTP_NO_CONTENT,
                              resp);
    GNUNET_break (MHD_YES == ret);
    MHD_destroy_response (resp);
    return ret;
  }
}
