/*
  This file is part of TALER
  (C) 2014--2019 Taler Systems SA

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
   * Underlying configuration.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Name of the currently active transaction, NULL if none is active.
   */
  const char *transaction_name;

};


/**
 * Drop sync tables
 *
 * @param cls closure our `struct Plugin`
 * @return #GNUNET_OK upon success; #GNUNET_SYSERR upon failure
 */
static int
postgres_drop_tables (void *cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_ExecuteStatement es[] = {
    GNUNET_PQ_make_try_execute ("DROP TABLE IF EXISTS accounts CASCADE;"),
    GNUNET_PQ_make_try_execute ("DROP TABLE IF EXISTS backups;"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };

  return GNUNET_PQ_exec_statements (pg->conn,
                                    es);
}


/**
 * Check that the database connection is still up.
 *
 * @param pg connection to check
 */
static void
check_connection (void *cls)
{
  struct PostgresClosure *pg = cls;

  GNUNET_PQ_reconnect_if_down (pg->conn);
}


/**
 * Do a pre-flight check that we are not in an uncommitted transaction.
 * If we are, try to commit the previous transaction and output a warning.
 * Does not return anything, as we will continue regardless of the outcome.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 */
static void
postgres_preflight (void *cls)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_ExecuteStatement es[] = {
    GNUNET_PQ_make_execute ("COMMIT"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };

  if (NULL == pg->transaction_name)
    return; /* all good */
  if (GNUNET_OK ==
      GNUNET_PQ_exec_statements (pg->conn,
                                 es))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "BUG: Preflight check committed transaction `%s'!\n",
                pg->transaction_name);
  }
  else
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "BUG: Preflight check failed to commit transaction `%s'!\n",
                pg->transaction_name);
  }
  pg->transaction_name = NULL;
}


/**
 * Start a transaction.
 *
 * @param cls the `struct PostgresClosure` with the plugin-specific state
 * @param name unique name identifying the transaction (for debugging),
 *             must point to a constant
 * @return #GNUNET_OK on success
 */
static int
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
 * @return #GNUNET_OK on success
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
  enum SYNC_DB_QueryStatus qs;
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
 * @param expire backups older than the given time stamp should be garbage collected
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_gc (void *cls,
             struct GNUNET_TIME_Absolute expire)
{
  struct PostgresClosure *pg = cls;
  struct GNUNET_PQ_QueryParam params[] = {
    TALER_PQ_query_param_absolute_time (&expire),
    GNUNET_PQ_query_param_end
  };

  check_connection (pg);
  postgres_preflight (pg);
  return (enum SYNC_DB_QueryStatus)
         GNUNET_PQ_eval_prepared_non_select (pg->conn,
                                             "gc",
                                             params);
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

  check_connection (pg);
  postgres_preflight (pg);
  {
    struct GNUNET_PQ_QueryParam params[] = {
      GNUNET_PQ_query_param_auto_from_type (account_pub),
      GNUNET_PQ_query_param_auto_from_type (account_sig),
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
    return SYNC_DB_OLD_BACKUP_MISSMATCH;
  /* backup identical to what was provided, no change */
  return GNUNET_DB_STATUS_SUCCESS_NO_RESULTS;
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
    /* Well, trying to update where there is no original
       is a hard erorr, even though an odd one */
    return SYNC_DB_HARD_ERROR;
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
    /* backup identical to what was provided, no change */
    return GNUNET_DB_STATUS_SUCCESS_NO_RESULTS;
  if (0 == GNUNET_memcmp (&bh,
                          old_backup_hash))
    /* all constraints seem satisified, original error must
       have been a hard error */
    return GNUNET_DB_STATUS_HARD_ERROR;
  /* previous backup does not match old_backup_hash */
  return SYNC_DB_OLD_BACKUP_MISSMATCH;
}


/**
 * Obtain backup.
 *
 * @param cls closure
 * @param account_pub account to store @a backup under
 * @param account_sig[OUT] set to signature affirming storage request
 * @param backup_hash[OUT] set to hash of @a backup
 * @param backup_size[OUT] set to number of bytes in @a backup
 * @param backup[OUT] set to raw data to backup, caller MUST FREE
 */
static enum SYNC_DB_QueryStatus
postgres_lookup_backup (void *cls,
                        const struct SYNC_AccountPublicKeyP *account_pub,
                        struct SYNC_AccountSignatureP *account_sig,
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
 * @param lifetime for how long is the account now paid (increment)
 * @return transaction status
 */
static enum SYNC_DB_QueryStatus
postgres_increment_lifetime (void *cls,
                             const struct SYNC_AccountPublicKeyP *account_pub,
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
    return GNUNET_DB_STATUS_HARD_ERROR;
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
  struct GNUNET_PQ_ExecuteStatement es[] = {
    /* Orders created by the frontend, not signed or given a nonce yet.
       The contract terms will change (nonce will be added) when moved to the
       contract terms table */
    GNUNET_PQ_make_execute ("CREATE TABLE IF NOT EXISTS accounts"
                            "("
                            "account_pub BYTEA PRIMARY KEY CHECK (length(account_pub)=32),"
                            "expiration_date INT8 NOT NULL"
                            ");"),
    GNUNET_PQ_make_execute ("CREATE TABLE IF NOT EXISTS backups"
                            "("
                            "account_pub BYTEA PRIMARY KEY REFERENCES accounts (account_pub),"
                            "account_sig BYTEA NOT NULL CHECK (length(account_sig)=64),"
                            "backup_hash BYTEA NOT NULL CHECK (length(backup_hash)=64),"
                            "data BYTEA NOT NULL"
                            ");"),
    /* index for gc */
    GNUNET_PQ_make_try_execute (
      "CREATE INDEX accounts_expire ON "
      "accounts (expiration_date);"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };
  struct GNUNET_PQ_PreparedStatement ps[] = {
    GNUNET_PQ_make_prepare ("account_insert",
                            "INSERT INTO accounts "
                            "("
                            "account_pub,"
                            "expiration_date"
                            ") VALUES "
                            "($1,$2);",
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
    GNUNET_PQ_make_prepare ("gc",
                            "DELETE FROM accounts "
                            "WHERE"
                            " expiration_date < $1;",
                            1),
    GNUNET_PQ_make_prepare ("backup_insert",
                            "INSERT INTO backups "
                            "(account_pub"
                            ",account_sig"
                            ",backup_hash"
                            ",data"
                            ") VALUES "
                            "($1,$2,$3,$4);",
                            4),
    GNUNET_PQ_make_prepare ("backup_update",
                            "UPDATE backups "
                            " SET"
                            " backup_hash=$1"
                            ",account_sig=$2"
                            ",data=$3"
                            " WHERE"
                            "   account_pub=$4"
                            "  AND"
                            "   backup_hash=$5;",
                            5),
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

  pg = GNUNET_new (struct PostgresClosure);
  pg->cfg = cfg;
  pg->conn = GNUNET_PQ_connect_with_cfg (cfg,
                                         "syncdb-postgres",
                                         es,
                                         ps);
  if (NULL == pg->conn)
  {
    GNUNET_free (pg);
    return NULL;
  }
  plugin = GNUNET_new (struct SYNC_DatabasePlugin);
  plugin->cls = pg;
  plugin->drop_tables = &postgres_drop_tables;
  plugin->gc = &postgres_gc;
  plugin->store_backup_TR = &postgres_store_backup;
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
  GNUNET_free (pg);
  GNUNET_free (plugin);
  return NULL;
}


/* end of plugin_syncdb_postgres.c */