/*
  This file is part of SYNC
  Copyright (C) 2014-2019 Taler Systems SA

  SYNC is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3, or
  (at your option) any later version.

  SYNC is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  SYNCABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with SYNC; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file lib/testing_api_helpers.c
 * @brief helper functions for test library.
 * @author Christian Grothoff
 * @author Marcello Stanisci
 */
#include "platform.h"
#include <taler/taler_testing_lib.h>
#include "sync_testing_lib.h"
#include <gnunet/gnunet_curl_lib.h>


/**
 * Start the sync backend process.  Assume the port
 * is available and the database is clean.  Use the "prepare
 * sync" function to do such tasks.
 *
 * @param config_filename configuration filename.
 *
 * @return the process, or NULL if the process could not
 *         be started.
 */
struct GNUNET_OS_Process *
SYNC_TESTING_run_sync (const char *config_filename,
                       const char *sync_url)
{
  struct GNUNET_OS_Process *sync_proc;
  unsigned int iter;
  char *wget_cmd;

  sync_proc
    = GNUNET_OS_start_process (GNUNET_OS_INHERIT_STD_ALL,
                               NULL, NULL, NULL,
                               "sync-httpd",
                               "sync-httpd",
                               "--log=INFO",
                               "-c", config_filename,
                               NULL);
  if (NULL == sync_proc)
  {
    GNUNET_break (0);
    return NULL;
  }
  GNUNET_asprintf (&wget_cmd,
                   "wget -q -t 1 -T 1"
                   " %s"
                   " -o /dev/null -O /dev/null",
                   sync_url);

  /* give child time to start and bind against the socket */
  fprintf (stderr,
           "Waiting for `sync-httpd' to be ready\n");
  iter = 0;
  do
  {
    if (10 == iter)
    {
      fprintf (stderr,
               "Failed to launch"
               " `sync-httpd' (or `wget')\n");
      GNUNET_OS_process_kill (sync_proc,
                              SIGTERM);
      GNUNET_OS_process_wait (sync_proc);
      GNUNET_OS_process_destroy (sync_proc);
      GNUNET_break (0);
      return NULL;
    }
    fprintf (stderr, ".\n");
    sleep (1);
    iter++;
  }
  while (0 != system (wget_cmd));
  GNUNET_free (wget_cmd);
  fprintf (stderr, "\n");
  return sync_proc;
}


/**
 * Prepare the sync execution.  Create tables and check if
 * the port is available.
 *
 * @param config_filename configuration filename.
 * @return the base url, or NULL upon errors.  Must be freed
 *         by the caller.
 */
char *
SYNC_TESTING_prepare_sync (const char *config_filename)
{
  struct GNUNET_CONFIGURATION_Handle *cfg;
  unsigned long long port;
  struct GNUNET_OS_Process *dbinit_proc;
  enum GNUNET_OS_ProcessStatusType type;
  unsigned long code;
  char *base_url;

  cfg = GNUNET_CONFIGURATION_create ();
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_load (cfg,
                                 config_filename))
  {
    GNUNET_break (0);
    return NULL;
  }
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (cfg,
                                             "sync",
                                             "PORT",
                                             &port))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "sync",
                               "PORT");
    GNUNET_CONFIGURATION_destroy (cfg);
    GNUNET_break (0);
    return NULL;
  }
  GNUNET_CONFIGURATION_destroy (cfg);
  if (GNUNET_OK !=
      GNUNET_NETWORK_test_port_free (IPPROTO_TCP,
                                     (uint16_t) port))
  {
    fprintf (stderr,
             "Required port %llu not available, skipping.\n",
             port);
    GNUNET_break (0);
    return NULL;
  }

  /* DB preparation */
  if (NULL == (dbinit_proc = GNUNET_OS_start_process (
                 GNUNET_OS_INHERIT_STD_ALL,
                 NULL, NULL, NULL,
                 "sync-dbinit",
                 "sync-dbinit",
                 "-c", config_filename,
                 "-r",
                 NULL)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Failed to run sync-dbinit."
                " Check your PATH.\n");
    GNUNET_break (0);
    return NULL;
  }

  if (GNUNET_SYSERR ==
      GNUNET_OS_process_wait_status (dbinit_proc,
                                     &type,
                                     &code))
  {
    GNUNET_OS_process_destroy (dbinit_proc);
    GNUNET_break (0);
    return NULL;
  }
  if ( (type == GNUNET_OS_PROCESS_EXITED) &&
       (0 != code) )
  {
    fprintf (stderr,
             "Failed to setup database\n");
    GNUNET_break (0);
    return NULL;
  }
  if ( (type != GNUNET_OS_PROCESS_EXITED) ||
       (0 != code) )
  {
    fprintf (stderr,
             "Unexpected error running"
             " `sync-dbinit'!\n");
    GNUNET_break (0);
    return NULL;
  }
  GNUNET_OS_process_destroy (dbinit_proc);
  GNUNET_asprintf (&base_url,
                   "http://localhost:%llu/",
                   port);
  return base_url;
}
