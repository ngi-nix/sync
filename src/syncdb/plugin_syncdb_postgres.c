/*
  This file is part of TALER
  (C) 2014--2021 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of ANASTASISABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file sync/plugin_syncdb_postgres.c
 * @brief database helper functions for postgres used by sync
 * @author Christian Grothoff
 */
#include "platform.h"
#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_db_lib.h>
#include <gnunet/gnunet_pq_lib.h>
#include <taler/taler_pq_lib.h>
#include "sync_database_plugin.h"
#include "sync_database_lib.h"

/**
 * Type of the "cls" argument given to each of the functions in
 * our API.
 */
struct PostgresClosure
{

  /**
   * Postgres connection handle.
   */
  struct GNUNET_PQ_Context *conn;

  /**
   * Directory with SQL statements to run to create tables.
   */
  char *sql_dir;

  /**
   * Underlying configuration.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Name of the currently active transaction, NULL if none is active.
   */
  const char *transaction_name;

  /**
   * Currency we accept payments in.
   */
  char *currency;

  /**
   * Did we initialize the prepared statements
   * for this session?
   */
  bool init;

};


/**
 * Drop sync tables
 *
 * @param cls closure our `struct Plugin`
 * @return #GNUNET_OK upon success; #GNUNET_SYSERR upon failure
 */
static enum GNUNET_GenericReturnValue
postgres_drop_tables (void *cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_Context *conn;

  conn = GNUNET_PQ_connect_with_cfg (pg->cfg,
                                     "syncdb-postgres",
                                     "drop",
                                     NULL,
                                     NULL);
  if (NULL == conn)
    return GNUNET_SYSERR;
  GNUNET_PQ_disconnect (conn);
  return GNUNET_OK;
}


/**
 * Establish connection to the database.
 *
 * @param cls plugin context
 * @return #GNUNET_OK upon success; #GNUNET_SYSERR upon failure
 */
static enum GNUNET_GenericReturnValue
prepare_statements (void *cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_PreparedStatement ps[] = {
    GNUNET_PQ_make_prepare ("account_insert",
                            "INSERT INTO accounts "
                            "(account_pub"
                            ",expiration_date"
                            ") VALUES "
                            "($1,$2);",
                            2),
    GNUNET_PQ_make_prepare ("payment_insert",
                            "INSERT INTO payments "
                            "(account_pub"
                            ",order_id"
                            ",token"
                            ",timestamp"
                            ",amount_val"
                            ",amount_frac"
                            ") VALUES "
                            "($1,$2,$3,$4,$5,$6);",
                            6),
    GNUNET_PQ_make_prepare ("payment_done",
                            "UPDATE payments "
                            "SET"
                            " paid=TRUE "
                            "WHERE"
                            "  order_id=$1"
                            " AND"
                            "  account_pub=$2"
                            " AND"
                            "  paid=FALSE;",
                            2),
    GNUNET_PQ_make_prepare ("account_update",
                            "UPDATE accounts "
                            "SET"
                            " expiration_date=$1 "
                            "WHERE"
                            " account_pub=$2;",
                            2),
    GNUNET_PQ_make_prepare ("account_select",
                            "SELECT"
                            " expiration_date "
                            "FROM"
                            " accounts "
                            "WHERE"
                            " account_pub=$1;",
                            1),
    GNUNET_PQ_make_prepare ("payments_select",
                            "SELECT"
                            " account_pub"
                            ",order_id"
                            ",amount_val"
                            ",amount_frac"
                            " FROM payments"
                            " WHERE paid=FALSE;",
                            0),
    GNUNET_PQ_make_prepare ("payments_select_by_account",
                            "SELECT"
                            " timestamp"
                            ",order_id"
                            ",token"
                            ",amount_val"
                            ",amount_frac"
                            " FROM payments"
                            " WHERE"
                            "  paid=FALSE"
                            " AND"
                            "  account_pub=$1;",
                            1),
    GNUNET_PQ_make_prepare ("gc_accounts",
                            "DELETE FROM accounts "
                            "WHERE"
                            " expiration_date < $1;",
                            1),
    GNUNET_PQ_make_prepare ("gc_pending_payments",
                            "DELETE FROM payments "
                            "WHERE"
                            "  paid=FALSE"
                            " AND"
                            "  timestamp < $1;",
                            1),
    GNUNET_PQ_make_prepare ("backup_insert",
                            "INSERT INTO backups "
                            "(account_pub"
                            ",account_sig"
                            ",prev_hash"
                            ",backup_hash"
                            ",data"
                            ") VALUES "
                            "($1,$2,$3,$4,$5);",
                            5),
    GNUNET_PQ_make_prepare ("backup_update",
                            "UPDATE backups "
                            " SET"
                            " backup_hash=$1"
                            ",account_sig=$2"
                            ",prev_hash=$3"
                            ",data=$4"
                            " WHERE"
                            "   account_pub=$5"
                            "  AND"
                            "   backup_hash=$6;",
                            6),
    GNUNET_PQ_make_prepare ("backup_select_hash",
                            "SELECT "
                            " backup_hash "
                            "FROM"
                            " backups "
                            "WHERE"
                            " account_pub=$1;",
                            1),
    GNUNET_PQ_make_prepare ("backup_select",
                            "SELECT "
                            " account_sig"
                            ",prev_hash"
                            ",backup_hash"
                            ",data "
                            "FROM"
                            " backups "
                            "WHERE"
                            " account_pub=$1;",
                            1),
    GNUNET_PQ_make_prepare ("do_commit",
                            "COMMIT",
                            0),
    GNUNET_PQ_PREPARED_STATEMENT_END
  };
  enum GNUNET_GenericReturnValue ret;

  ret = GNUNET_PQ_prepare_statements (pg->conn,
                                      ps);
  if (GNUNET_OK != ret)
    return ret;
  pg->init = true;
  return GNUNET_OK;
}


/**
 * Connect to the database if the connection does not exist yet.
 *
 * @param pg the plugin-specific state
 * @param skip_prepare true if we should skip prepared statement setup
 * @return #GNUNET_OK on success
 */
static enum GNUNET_GenericReturnValue
internal_setup (struct PostgresClosure *pg,
                bool skip_prepare)
{
  if (NULL == pg->conn)
  {
#if AUTO_EXPLAIN
    /* Enable verbose logging to see where queries do not
       properly use indices */
    struct GNUNET_PQ_ExecuteStatement es[] = {
      GNUNET_PQ_make_try_execute ("LOAD 'auto_explain';"),
      GNUNET_PQ_make_try_execute ("SET auto_explain.log_min_duration=50;"),
      GNUNET_PQ_make_try_execute ("SET auto_explain.log_timing=TRUE;"),
      GNUNET_PQ_make_try_execute ("SET auto_explain.log_analyze=TRUE;"),
      /* https://wiki.postgresql.org/wiki/Serializable suggests to really
         force the default to 'serializable' if SSI is to be used. */
      GNUNET_PQ_make_try_execute (
        "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE;"),
      GNUNET_PQ_make_try_execute ("SET enable_sort=OFF;"),
      GNUNET_PQ_make_try_execute ("SET enable_seqscan=OFF;"),
      GNUNET_PQ_EXECUTE_STATEMENT_END
    };
#else
    struct GNUNET_PQ_ExecuteStatement *es = NULL;
#endif
    struct GNUNET_PQ_Context *db_conn;

    db_conn = GNUNET_PQ_connect_with_cfg (pg->cfg,
                                          "syncdb-postgres",
                                          NULL,
                                          es,
                                          NULL);
    if (NULL == db_conn)
      return GNUNET_SYSERR;
    pg->conn = db_conn;
  }
  if (NULL == pg->transaction_name)
    GNUNET_PQ_reconnect_if_down (pg->conn);
  if (pg->init)
    return GNUNET_OK;
  if (skip_prepare)
    return GNUNET_OK;
  return prepare_statements (pg);
}


/**
 * Do a pre-flight check that we are not in an uncommitted transaction.
 * If we are, try to commit the previous transaction and output a warning.
 * Does not return anything, as we will continue regardless of the outcome.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 * @return #GNUNET_OK if everything is fine
 *         #GNUNET_NO if a transaction was rolled back
 *         #GNUNET_SYSERR on hard errors
 */
static enum GNUNET_GenericReturnValue
postgres_preflight (void *cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_ExecuteStatement es[] = {
    GNUNET_PQ_make_execute ("ROLLBACK"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };

  if (! pg->init)
  {
    if (GNUNET_OK !=
        internal_setup (pg,
                        false))
      return GNUNET_SYSERR;
  }
  if (NULL == pg->transaction_name)
    return GNUNET_OK; /* all good */
  if (GNUNET_OK ==
      GNUNET_PQ_exec_statements (pg->conn,
                                 es))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "BUG: Preflight check rolled back transaction `%s'!\n",
                pg->transaction_name);
  }
  else
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "BUG: Preflight check failed to rollback transaction `%s'!\n",
                pg->transaction_name);
  }
  pg->transaction_name = NULL;
  return GNUNET_NO;
}


/**
 * Check that the database connection is still up.
 *
 * @param cls a `struct PostgresClosure` with connection to check
 */
static void
check_connection (void *cls)
{
  struct PostgresClosure *pg = cls;

  GNUNET_PQ_reconnect_if_down (pg->conn);
}


/**
 * Start a transaction.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 * @param name unique name identifying the transaction (for debugging),
 *             must point to a constant
 * @return #GNUNET_OK on success
 */
static enum GNUNET_GenericReturnValue
begin_transaction (void *cls,
                   const char *name)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_ExecuteStatement es[] = {
    GNUNET_PQ_make_execute ("START TRANSACTION ISOLATION LEVEL SERIALIZABLE"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };

  check_connection (pg);
  postgres_preflight (pg);
  pg->transaction_name = name;
  if (GNUNET_OK !=
      GNUNET_PQ_exec_statements (pg->conn,
                                 es))
  {
    TALER_LOG_ERROR ("Failed to start transaction\n");
    GNUNET_break (0);
    return GNUNET_SYSERR;
  }
  return GNUNET_OK;
}


/**
 * Roll back the current transaction of a database connection.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 */
static void
rollback (void *cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_ExecuteStatement es[] = {
    GNUNET_PQ_make_execute ("ROLLBACK"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };

  if (GNUNET_OK !=
      GNUNET_PQ_exec_statements (pg->conn,
                                 es))
  {
    TALER_LOG_ERROR ("Failed to rollback transaction\n");
    GNUNET_break (0);
  }
  pg->transaction_name = NULL;
}


/**
 * Commit the current transaction of a database connection.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 * @return transaction status code
 */
static enum GNUNET_DB_QueryStatus
commit_transaction (void *cls)
{
  struct PostgresClosure *pg = cls;
  enum GNUNET_DB_QueryStatus qs;
  struct GNUNET_PQ_QueryParam no_params[] = {
    GNUNET_PQ_query_param_end
  };

  qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                           "do_commit",
                                           no_params);
  pg->transaction_name = NULL;
  return qs;
}


/**
 * Function called to perform "garbage collection" on the
 * database, expiring records we no longer require.  Deletes
 * all user records that are not paid up (and by cascade deletes
 * the associated recovery documents). Also deletes expired
 * truth and financial records older than @a fin_expire.
 *
 * @param cls closure
 * @param expire_backups backups older than the given time stamp should be garbage collected
 * @param expire_pending_payments payments still pending from since before
 *            this value should be garbage collected
 * @return transaction status
 */
static enum GNUNET_DB_QueryStatus
postgres_gc (void *cls,
             struct GNUNET_TIME_Absolute expire_backups,
             struct GNUNET_TIME_Absolute expire_pending_payments)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_QueryParam params[] = {
    TALER_PQ_query_param_absolute_time (&expire_backups),
    GNUNET_PQ_query_param_end
  };
  struct GNUNET_PQ_QueryParam params2[] = {
    TALER_PQ_query_param_absolute_time (&expire_pending_payments),
    GNUNET_PQ_query_param_end
  };
  enum GNUNET_DB_QueryStatus qs;

  check_connection (pg);
  postgres_preflight (pg);
  qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                           "gc_accounts",
                                           params);
  if (qs < 0)
    return qs;
  return GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                             "gc_pending_payments",
                                             params2);
}


/**
 * Store payment. Used to begin a payment, not indicative
 * that the payment actually was made. (That is done
 * when we increment the account's lifetime.)
 *
 * @param cls closure
 * @param account_pub account to store @a backup under
 * @param order_id order we create
 * @param token claim token to use, NULL for none
 * @param amount how much we asked for
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_store_payment (void *cls,
                        const struct SYNC_AccountPublicKeyP *account_pub,
                        const char *order_id,
                        const struct TALER_ClaimTokenP *token,
                        const struct TALER_Amount *amount)
{
  struct PostgresClosure *pg = cls;
  enum GNUNET_DB_QueryStatus qs;
  struct TALER_ClaimTokenP tok;
  struct GNUNET_TIME_Absolute now = GNUNET_TIME_absolute_get ();
  struct GNUNET_PQ_QueryParam params[] = {
    GNUNET_PQ_query_param_auto_from_type (account_pub),
    GNUNET_PQ_query_param_string (order_id),
    GNUNET_PQ_query_param_auto_from_type (&tok),
    GNUNET_PQ_query_param_absolute_time (&now),
    TALER_PQ_query_param_amount (amount),
    GNUNET_PQ_query_param_end
  };

  if (NULL == token)
    memset (&tok, 0, sizeof (tok));
  else
    tok = *token;
  check_connection (pg);
  postgres_preflight (pg);
  qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                           "payment_insert",
                                           params);
  switch (qs)
  {
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    GNUNET_break (0);
    return SYNC_DB_NO_RESULTS;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    return SYNC_DB_ONE_RESULT;
  case GNUNET_DB_STATUS_HARD_ERROR:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }
}


/**
 * Closure for #payment_by_account_cb.
 */
struct PaymentIteratorContext
{
  /**
   * Function to call on each result
   */
  SYNC_DB_PaymentPendingIterator it;

  /**
   * Closure for @e it.
   */
  void *it_cls;

  /**
   * Plugin context.
   */
  struct PostgresClosure *pg;

  /**
   * Query status to return.
   */
  enum GNUNET_DB_QueryStatus qs;

};


/**
 * Helper function for #postgres_lookup_pending_payments_by_account().
 * To be called with the results of a SELECT statement
 * that has returned @a num_results results.
 *
 * @param cls closure of type `struct PaymentIteratorContext *`
 * @param result the postgres result
 * @param num_result the number of results in @a result
 */
static void
payment_by_account_cb (void *cls,
                       PGresult *result,
                       unsigned int num_results)
{
  struct PaymentIteratorContext *pic = cls;

  for (unsigned int i = 0; i < num_results; i++)
  {
    struct GNUNET_TIME_Absolute timestamp;
    char *order_id;
    struct TALER_Amount amount;
    struct TALER_ClaimTokenP token;
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_absolute_time ("timestamp",
                                           &timestamp),
      GNUNET_PQ_result_spec_string ("order_id",
                                    &order_id),
      GNUNET_PQ_result_spec_auto_from_type ("token",
                                            &token),
      TALER_PQ_result_spec_amount ("amount",
                                   pic->pg->currency,
                                   &amount),
      GNUNET_PQ_result_spec_end
    };

    if (GNUNET_OK !=
        GNUNET_PQ_extract_result (result,
                                  rs,
                                  i))
    {
      GNUNET_break (0);
      pic->qs = GNUNET_DB_STATUS_HARD_ERROR;
      return;
    }
    pic->qs = i + 1;
    pic->it (pic->it_cls,
             timestamp,
             order_id,
             &token,
             &amount);
    GNUNET_PQ_cleanup_result (rs);
  }
}


/**
 * Lookup pending payments by account.
 *
 * @param cls closure
 * @param account_pub account to look for pending payments under
 * @param it iterator to call on all pending payments
 * @param it_cls closure for @a it
 * @return transaction status
 */
static enum GNUNET_DB_QueryStatus
postgres_lookup_pending_payments_by_account (void *cls,
                                             const struct
                                             SYNC_AccountPublicKeyP *account_pub,
                                             SYNC_DB_PaymentPendingIterator it,
                                             void *it_cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_QueryParam params[] = {
    GNUNET_PQ_query_param_auto_from_type (account_pub),
    GNUNET_PQ_query_param_end
  };
  struct PaymentIteratorContext pic = {
    .it = it,
    .it_cls = it_cls,
    .pg = pg
  };
  enum GNUNET_DB_QueryStatus qs;

  check_connection (pg);
  postgres_preflight (pg);
  qs = GNUNET_PQ_eval_prepared_multi_select (pg->conn,
                                             "payments_select_by_account",
                                             params,
                                             &payment_by_account_cb,
                                             &pic);
  if (qs > 0)
    return pic.qs;
  GNUNET_break (GNUNET_DB_STATUS_HARD_ERROR != qs);
  return qs;
}


/**
 * Store backup. Only applicable for the FIRST backup under
 * an @a account_pub. Use @e update_backup_TR to update an
 * existing backup.
 *
 * @param cls closure
 * @param account_pub account to store @a backup under
 * @param account_sig signature affirming storage request
 * @param backup_hash hash of @a backup
 * @param backup_size number of bytes in @a backup
 * @param backup raw data to backup
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_store_backup (void *cls,
                       const struct SYNC_AccountPublicKeyP *account_pub,
                       const struct SYNC_AccountSignatureP *account_sig,
                       const struct GNUNET_HashCode *backup_hash,
                       size_t backup_size,
                       const void *backup)
{
  struct PostgresClosure *pg = cls;
  enum GNUNET_DB_QueryStatus qs;
  struct GNUNET_HashCode bh;
  static struct GNUNET_HashCode no_previous_hash;

  check_connection (pg);
  postgres_preflight (pg);
  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_auto_from_type (account_sig),
      GNUNET_PQ_query_param_auto_from_type (&no_previous_hash),
      GNUNET_PQ_query_param_auto_from_type (backup_hash),
      GNUNET_PQ_query_param_fixed_size (backup,
                                        backup_size),
      GNUNET_PQ_query_param_end
    };

    qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                             "backup_insert",
                                             params);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    GNUNET_break (0);
    return SYNC_DB_NO_RESULTS;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    return SYNC_DB_ONE_RESULT;
  case GNUNET_DB_STATUS_HARD_ERROR:
    /* handle interesting case below */
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* First, check if account exists */
  {
    struct GNUNET_TIME_Absolute ed;
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_end
    };
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_auto_from_type ("expiration_date",
                                            &ed),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "account_select",
                                                   params,
                                                   rs);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    return SYNC_DB_PAYMENT_REQUIRED;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    /* handle interesting case below */
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* account exists, check if existing backup conflicts */
  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_end
    };
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_auto_from_type ("backup_hash",
                                            &bh),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "backup_select_hash",
                                                   params,
                                                   rs);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    /* original error must have been a hard error, oddly enough */
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    /* handle interesting case below */
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* had an existing backup, is it identical? */
  if (0 != GNUNET_memcmp (&bh,
                          backup_hash))
    /* previous conflicting backup exists */
    return SYNC_DB_OLD_BACKUP_MISMATCH;
  /* backup identical to what was provided, no change */
  return SYNC_DB_NO_RESULTS;
}


/**
 * Update backup.
 *
 * @param cls closure
 * @param account_pub account to store @a backup under
 * @param account_sig signature affirming storage request
 * @param old_backup_hash hash of the previous backup (must match)
 * @param backup_hash hash of @a backup
 * @param backup_size number of bytes in @a backup
 * @param backup raw data to backup
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_update_backup (void *cls,
                        const struct SYNC_AccountPublicKeyP *account_pub,
                        const struct GNUNET_HashCode *old_backup_hash,
                        const struct SYNC_AccountSignatureP *account_sig,
                        const struct GNUNET_HashCode *backup_hash,
                        size_t backup_size,
                        const void *backup)
{
  struct PostgresClosure *pg = cls;
  enum GNUNET_DB_QueryStatus qs;
  struct GNUNET_HashCode bh;

  check_connection (pg);
  postgres_preflight (pg);
  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (backup_hash),
      GNUNET_PQ_query_param_auto_from_type (account_sig),
      GNUNET_PQ_query_param_auto_from_type (old_backup_hash),
      GNUNET_PQ_query_param_fixed_size (backup,
                                        backup_size),
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_auto_from_type (old_backup_hash),
      GNUNET_PQ_query_param_end
    };

    qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                             "backup_update",
                                             params);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    /* handle interesting case below */
    break;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    return SYNC_DB_ONE_RESULT;
  case GNUNET_DB_STATUS_HARD_ERROR:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* First, check if account exists */
  {
    struct GNUNET_TIME_Absolute ed;
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_end
    };
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_auto_from_type ("expiration_date",
                                            &ed),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "account_select",
                                                   params,
                                                   rs);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    return SYNC_DB_PAYMENT_REQUIRED;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    /* handle interesting case below */
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* account exists, check if existing backup conflicts */
  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_end
    };
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_auto_from_type ("backup_hash",
                                            &bh),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "backup_select_hash",
                                                   params,
                                                   rs);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    return SYNC_DB_OLD_BACKUP_MISSING;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    /* handle interesting case below */
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* had an existing backup, is it identical? */
  if (0 == GNUNET_memcmp (&bh,
                          backup_hash))
  {
    /* backup identical to what was provided, no change */
    return SYNC_DB_NO_RESULTS;
  }
  if (0 == GNUNET_memcmp (&bh,
                          old_backup_hash))
    /* all constraints seem satisfied, original error must
       have been a hard error */
    return SYNC_DB_HARD_ERROR;
  /* previous backup does not match old_backup_hash */
  return SYNC_DB_OLD_BACKUP_MISMATCH;
}


/**
 * Lookup an account and associated backup meta data.
 *
 * @param cls closure
 * @param account_pub account to store @a backup under
 * @param backup_hash[OUT] set to hash of @a backup
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_lookup_account (void *cls,
                         const struct SYNC_AccountPublicKeyP *account_pub,
                         struct GNUNET_HashCode *backup_hash)
{
  struct PostgresClosure *pg = cls;
  enum GNUNET_DB_QueryStatus qs;
  struct GNUNET_PQ_QueryParam params[] = {
    GNUNET_PQ_query_param_auto_from_type (account_pub),
    GNUNET_PQ_query_param_end
  };

  check_connection (pg);
  postgres_preflight (pg);
  {
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_auto_from_type ("backup_hash",
                                            backup_hash),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "backup_select_hash",
                                                   params,
                                                   rs);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    break; /* handle interesting case below */
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    return SYNC_DB_ONE_RESULT;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  /* check if account exists */
  {
    struct GNUNET_TIME_Absolute expiration;
    struct GNUNET_PQ_ResultSpec rs[] = {
      GNUNET_PQ_result_spec_auto_from_type ("expiration_date",
                                            &expiration),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "account_select",
                                                   params,
                                                   rs);
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    /* indicates: no account */
    return SYNC_DB_PAYMENT_REQUIRED;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    /* indicates: no backup */
    return SYNC_DB_NO_RESULTS;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }
}


/**
 * Obtain backup.
 *
 * @param cls closure
 * @param account_pub account to store @a backup under
 * @param account_sig[OUT] set to signature affirming storage request
 * @param prev_hash[OUT] set to hash of previous @a backup, all zeros if none
 * @param backup_hash[OUT] set to hash of @a backup
 * @param backup_size[OUT] set to number of bytes in @a backup
 * @param backup[OUT] set to raw data to backup, caller MUST FREE
 */
static enum SYNC_DB_QueryStatus
postgres_lookup_backup (void *cls,
                        const struct SYNC_AccountPublicKeyP *account_pub,
                        struct SYNC_AccountSignatureP *account_sig,
                        struct GNUNET_HashCode *prev_hash,
                        struct GNUNET_HashCode *backup_hash,
                        size_t *backup_size,
                        void **backup)
{
  struct PostgresClosure *pg = cls;
  enum GNUNET_DB_QueryStatus qs;
  struct GNUNET_PQ_QueryParam params[] = {
    GNUNET_PQ_query_param_auto_from_type (account_pub),
    GNUNET_PQ_query_param_end
  };
  struct GNUNET_PQ_ResultSpec rs[] = {
    GNUNET_PQ_result_spec_auto_from_type ("account_sig",
                                          account_sig),
    GNUNET_PQ_result_spec_auto_from_type ("prev_hash",
                                          prev_hash),
    GNUNET_PQ_result_spec_auto_from_type ("backup_hash",
                                          backup_hash),
    GNUNET_PQ_result_spec_variable_size ("data",
                                         backup,
                                         backup_size),
    GNUNET_PQ_result_spec_end
  };

  check_connection (pg);
  postgres_preflight (pg);
  qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                 "backup_select",
                                                 params,
                                                 rs);
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    return SYNC_DB_NO_RESULTS;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    return SYNC_DB_ONE_RESULT;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }
}


/**
 * Increment account lifetime.
 *
 * @param cls closure
 * @param account_pub which account received a payment
 * @param order_id order which was paid, must be unique and match pending payment
 * @param lifetime for how long is the account now paid (increment)
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_increment_lifetime (void *cls,
                             const struct SYNC_AccountPublicKeyP *account_pub,
                             const char *order_id,
                             struct GNUNET_TIME_Relative lifetime)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_TIME_Absolute expiration;
  enum GNUNET_DB_QueryStatus qs;

  check_connection (pg);
  if (GNUNET_OK !=
      begin_transaction (pg,
                         "increment lifetime"))
  {
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }

  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_string (order_id),
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_end
    };

    qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                             "payment_done",
                                             params);
    switch (qs)
    {
    case GNUNET_DB_STATUS_HARD_ERROR:
      GNUNET_break (0);
      rollback (pg);
      return SYNC_DB_HARD_ERROR;
    case GNUNET_DB_STATUS_SOFT_ERROR:
      GNUNET_break (0);
      rollback (pg);
      return SYNC_DB_SOFT_ERROR;
    case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
      rollback (pg);
      return SYNC_DB_NO_RESULTS;
    case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
      break;
    }
  }

  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_end
    };
    struct GNUNET_PQ_ResultSpec rs[] = {
      TALER_PQ_result_spec_absolute_time ("expiration_date",
                                          &expiration),
      GNUNET_PQ_result_spec_end
    };

    qs = GNUNET_PQ_eval_prepared_singleton_select (pg->conn,
                                                   "account_select",
                                                   params,
                                                   rs);
  }

  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    rollback (pg);
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    rollback (pg);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    {
      struct GNUNET_PQ_QueryParam params[] = {
        GNUNET_PQ_query_param_auto_from_type (account_pub),
        GNUNET_PQ_query_param_absolute_time (&expiration),
        GNUNET_PQ_query_param_end
      };

      expiration = GNUNET_TIME_relative_to_absolute (lifetime);
      qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                               "account_insert",
                                               params);
    }
    break;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    {
      struct GNUNET_PQ_QueryParam params[] = {
        GNUNET_PQ_query_param_absolute_time (&expiration),
        GNUNET_PQ_query_param_auto_from_type (account_pub),
        GNUNET_PQ_query_param_end
      };

      expiration = GNUNET_TIME_absolute_add (expiration,
                                             lifetime);
      qs = GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                               "account_update",
                                               params);
    }
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    rollback (pg);
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    rollback (pg);
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    GNUNET_break (0);
    rollback (pg);
    return SYNC_DB_NO_RESULTS;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    break;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }
  qs = commit_transaction (pg);
  switch (qs)
  {
  case GNUNET_DB_STATUS_HARD_ERROR:
    return SYNC_DB_HARD_ERROR;
  case GNUNET_DB_STATUS_SOFT_ERROR:
    GNUNET_break (0);
    return SYNC_DB_SOFT_ERROR;
  case GNUNET_DB_STATUS_SUCCESS_NO_RESULTS:
    return SYNC_DB_ONE_RESULT;
  case GNUNET_DB_STATUS_SUCCESS_ONE_RESULT:
    return SYNC_DB_ONE_RESULT;
  default:
    GNUNET_break (0);
    return SYNC_DB_HARD_ERROR;
  }
}


/**
 * Initialize tables.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 * @return #GNUNET_OK upon success; #GNUNET_SYSERR upon failure
 */
static enum GNUNET_GenericReturnValue
postgres_create_tables (void *cls)
{
  struct PostgresClosure *pc = cls;
  struct GNUNET_PQ_Context *conn;

  conn = GNUNET_PQ_connect_with_cfg (pc->cfg,
                                     "syncdb-postgres",
                                     "sync-",
                                     NULL,
                                     NULL);
  if (NULL == conn)
    return GNUNET_SYSERR;
  GNUNET_PQ_disconnect (conn);
  return GNUNET_OK;
}


/**
 * Initialize Postgres database subsystem.
 *
 * @param cls a configuration instance
 * @return NULL on error, otherwise a `struct TALER_SYNCDB_Plugin`
 */
void *
libsync_plugin_db_postgres_init (void *cls)
{
  struct GNUNET_CONFIGURATION_Handle *cfg = cls;
  struct PostgresClosure *pg;
  struct SYNC_DatabasePlugin *plugin;

  pg = GNUNET_new (struct PostgresClosure);
  pg->cfg = cfg;
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_filename (cfg,
                                               "syncdb-postgres",
                                               "SQL_DIR",
                                               &pg->sql_dir))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "syncdb-postgres",
                               "SQL_DIR");
    GNUNET_free (pg);
    return NULL;
  }
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (cfg,
                                             "taler",
                                             "CURRENCY",
                                             &pg->currency))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "taler",
                               "CURRENCY");
    GNUNET_free (pg->sql_dir);
    GNUNET_free (pg);
    return NULL;
  }
  if (GNUNET_OK !=
      internal_setup (pg,
                      true))
  {
    GNUNET_free (pg->currency);
    GNUNET_free (pg->sql_dir);
    GNUNET_free (pg);
    return NULL;
  }
  plugin = GNUNET_new (struct SYNC_DatabasePlugin);
  plugin->cls = pg;
  plugin->create_tables = &postgres_create_tables;
  plugin->drop_tables = &postgres_drop_tables;
  plugin->preflight = &postgres_preflight;
  plugin->gc = &postgres_gc;
  plugin->store_payment_TR = &postgres_store_payment;
  plugin->lookup_pending_payments_by_account_TR =
    &postgres_lookup_pending_payments_by_account;
  plugin->store_backup_TR = &postgres_store_backup;
  plugin->lookup_account_TR = &postgres_lookup_account;
  plugin->lookup_backup_TR = &postgres_lookup_backup;
  plugin->update_backup_TR = &postgres_update_backup;
  plugin->increment_lifetime_TR = &postgres_increment_lifetime;
  return plugin;
}


/**
 * Shutdown Postgres database subsystem.
 *
 * @param cls a `struct SYNC_DB_Plugin`
 * @return NULL (always)
 */
void *
libsync_plugin_db_postgres_done (void *cls)
{
  struct SYNC_DatabasePlugin *plugin = cls;
  struct PostgresClosure *pg = plugin->cls;

  GNUNET_PQ_disconnect (pg->conn);
  GNUNET_free (pg->sql_dir);
  GNUNET_free (pg);
  GNUNET_free (plugin);
  return NULL;
}


/* end of plugin_syncdb_postgres.c */
