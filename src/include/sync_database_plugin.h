/*
  This file is part of Sync
  Copyright (C) 2019 Taler Systems SA

  Sync is free software; you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  Sync is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  Sync; see the file COPYING.GPL.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file include/sync_database_plugin.h
 * @brief database access for Sync
 * @author Christian Grothoff
 */
#ifndef TALER_SYNC_DATABASE_PLUGIN_H
#define TALER_SYNC_DATABASE_PLUGIN_H

#include <gnunet/gnunet_util_lib.h>
#include <sync_error_codes.h>
#include "sync_service.h"
#include <jansson.h>
#include <taler/taler_util.h>

/**
 * Handle to interact with the database.
 *
 * Functions ending with "_TR" run their OWN transaction scope
 * and MUST NOT be called from within a transaction setup by the
 * caller.  Functions ending with "_NT" require the caller to
 * setup a transaction scope.  Functions without a suffix are
 * simple, single SQL queries that MAY be used either way.
 */
struct SYNC_DatabasePlugin
{

  /**
   * Closure for all callbacks.
   */
  void *cls;

  /**
   * Name of the library which generated this plugin.  Set by the
   * plugin loader.
   */
  char *library_name;

  /**
   * Drop sync tables. Used for testcases.
   *
   * @param cls closure
   * @return #GNUNET_OK upon success; #GNUNET_SYSERR upon failure
   */
  int
  (*drop_tables) (void *cls);

  /**
   * Function called to perform "garbage collection" on the
   * database, expiring records we no longer require.  Deletes
   * all user records that are not paid up (and by cascade deletes
   * the associated recovery documents). Also deletes expired
   * truth and financial records older than @a fin_expire.
   *
   * @param cls closure
   * @param fin_expire financial records older than the given
   *        time stamp should be garbage collected (usual
   *        values might be something like 6-10 years in the past)
   * @return transaction status
   */
  enum SYNC_DB_QueryStatus
  (*gc)(void *cls,
        struct GNUNET_TIME_Absolute fin_expire);

  /**
  * Do a pre-flight check that we are not in an uncommitted transaction.
  * If we are, try to commit the previous transaction and output a warning.
  * Does not return anything, as we will continue regardless of the outcome.
  *
  * @param cls the `struct PostgresClosure` with the plugin-specific state
  */
  void
  (*preflight) (void *cls);

  /**
  * Check that the database connection is still up.
  *
  * @param pg connection to check
  */
  void
  (*check_connection) (void *cls);

  /**
   * Store backup.
   *
   * @param cls closure
   * @return transaction status
   */
  enum GNUNET_DB_QueryStatus
  (*store_backup)(void *cls,
                  ...);

  /**
   * Obtain backup.
   *
   * @param cls closure
   */
  enum GNUNET_DB_QueryStatus
  (*lookup_backup)(void *cls,
                   ...);

};
#endif
