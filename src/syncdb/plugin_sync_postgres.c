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
 * @brief database helper functions for postgres used by the sync
 * @author Sree Harsha Totakura <sreeharsha@totakura.in>
 * @author Christian Grothoff
 * @author Marcello Stanisci
 */
#include "platform.h"
#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_pq_lib.h>
#include <taler/taler_pq_lib.h>
#include "sync_database_plugin.h"
#include "sync_database_lib.h"

/**
 * How often do we re-try if we run into a DB serialization error?
 */
#define MAX_RETRIES 3


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

static enum SYNC_DB_QueryStatus
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
    GNUNET_PQ_make_execute ("CREATE TABLE IF NOT EXISTS backups"
                            "("                            
                            "data BYTEA NOT NULL,"
                            ");"),
    GNUNET_PQ_EXECUTE_STATEMENT_END
  };
  struct GNUNET_PQ_PreparedStatement ps[] = {
    GNUNET_PQ_make_prepare ("backup_insert",
                            "INSERT INTO backups "
                            "(data"
                            ") VALUES "
                            "($1);",
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
  plugin->preflight = &postgres_preflight;
  plugin->rollback = &rollback;
  plugin->commit = &commit_transaction;
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
