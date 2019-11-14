/*
  This file is part of TALER
  (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  TALER is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file backup/sync-httpd.c
 * @brief HTTP serving layer intended to provide basic backup operations
 * @author Christian Grothoff
 */
#include "platform.h"
#include <microhttpd.h>
#include <gnunet/gnunet_util_lib.h>
#include "sync-httpd_responses.h"
#include "sync-httpd.h"
#include "sync-httpd_parsing.h"
#include "sync-httpd_mhd.h"
#include "sync_database_lib.h"
#include "sync-httpd_backup.h"
#include "sync-httpd_terms.h"

/**
 * Backlog for listen operation on unix-domain sockets.
 */
#define UNIX_BACKLOG 500

/**
 * The port we are running on
 */
static long long unsigned port;

/**
 * Should a "Connection: close" header be added to each HTTP response?
 */
int SH_sync_connection_close;

/**
 * Upload limit to the service, in megabytes.
 */
unsigned long long int SH_upload_limit_mb;

/**
 * Annual fee for the backup account.
 */
struct TALER_Amount SH_annual_fee;

/**
 * Task running the HTTP server.
 */
static struct GNUNET_SCHEDULER_Task *mhd_task;

/**
 * Global return code
 */
static int result;

/**
 * The MHD Daemon
 */
static struct MHD_Daemon *mhd;

/**
 * Path for the unix domain-socket
 * to run the daemon on.
 */
static char *serve_unixpath;

/**
 * File mode for unix-domain socket.
 */
static mode_t unixpath_mode;

/**
 * Connection handle to the our database
 */
struct SYNC_DatabasePlugin *db;


/**
 * Return #GNUNET_YES if given a valid correlation ID and
 * #GNUNET_NO otherwise.
 *
 * @returns #GNUNET_YES iff given a valid correlation ID
 */
static int
is_valid_correlation_id (const char *correlation_id)
{
  if (strlen (correlation_id) >= 64)
    return GNUNET_NO;
  for (size_t i = 0; i < strlen (correlation_id); i++)
    if (! (isalnum (correlation_id[i]) || (correlation_id[i] == '-')))
      return GNUNET_NO;
  return GNUNET_YES;
}


/**
 * A client has requested the given url using the given method
 * (#MHD_HTTP_METHOD_GET, #MHD_HTTP_METHOD_PUT,
 * #MHD_HTTP_METHOD_DELETE, #MHD_HTTP_METHOD_POST, etc).  The callback
 * must call MHD callbacks to provide content to give back to the
 * client and return an HTTP status code (i.e. #MHD_HTTP_OK,
 * #MHD_HTTP_NOT_FOUND, etc.).
 *
 * @param cls argument given together with the function
 *        pointer when the handler was registered with MHD
 * @param url the requested url
 * @param method the HTTP method used (#MHD_HTTP_METHOD_GET,
 *        #MHD_HTTP_METHOD_PUT, etc.)
 * @param version the HTTP version string (i.e.
 *        #MHD_HTTP_VERSION_1_1)
 * @param upload_data the data being uploaded (excluding HEADERS,
 *        for a POST that fits into memory and that is encoded
 *        with a supported encoding, the POST data will NOT be
 *        given in upload_data and is instead available as
 *        part of #MHD_get_connection_values; very large POST
 *        data *will* be made available incrementally in
 *        @a upload_data)
 * @param upload_data_size set initially to the size of the
 *        @a upload_data provided; the method must update this
 *        value to the number of bytes NOT processed;
 * @param con_cls pointer that the callback can set to some
 *        address and that will be preserved by MHD for future
 *        calls for this request; since the access handler may
 *        be called many times (i.e., for a PUT/POST operation
 *        with plenty of upload data) this allows the application
 *        to easily associate some request-specific state.
 *        If necessary, this state can be cleaned up in the
 *        global #MHD_RequestCompletedCallback (which
 *        can be set with the #MHD_OPTION_NOTIFY_COMPLETED).
 *        Initially, `*con_cls` will be NULL.
 * @return #MHD_YES if the connection was handled successfully,
 *         #MHD_NO if the socket must be closed due to a serios
 *         error while handling the request
 */
static int
url_handler (void *cls,
             struct MHD_Connection *connection,
             const char *url,
             const char *method,
             const char *version,
             const char *upload_data,
             size_t *upload_data_size,
             void **con_cls)
{
  static struct SH_RequestHandler handlers[] = {
    /* Landing page, tell humans to go away. */
    { "/", MHD_HTTP_METHOD_GET, "text/plain",
      "Hello, I'm sync. This HTTP server is not for humans.\n", 0,
      &SH_MHD_handler_static_response, MHD_HTTP_OK },
    { "/agpl", MHD_HTTP_METHOD_GET, "text/plain",
      NULL, 0,
      &SH_MHD_handler_agpl_redirect, MHD_HTTP_FOUND },
    { "/terms", MHD_HTTP_METHOD_GET, "text/plain",
      NULL, 0,
      &SH_handler_terms, MHD_HTTP_OK },
    {NULL, NULL, NULL, NULL, 0, 0 }
  };
  static struct SH_RequestHandler h404 = {
    "", NULL, "text/html",
    "<html><title>404: not found</title></html>", 0,
    &SH_MHD_handler_static_response, MHD_HTTP_NOT_FOUND
  };

  struct TM_HandlerContext *hc;
  struct GNUNET_AsyncScopeId aid;
  const char *correlation_id = NULL;

  hc = *con_cls;

  if (NULL == hc)
  {
    GNUNET_async_scope_fresh (&aid);
    /* We only read the correlation ID on the first callback for every client */
    correlation_id = MHD_lookup_connection_value (connection,
                                                  MHD_HEADER_KIND,
                                                  "sync-Correlation-Id");
    if ((NULL != correlation_id) &&
        (GNUNET_YES != is_valid_correlation_id (correlation_id)))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  "illegal incoming correlation ID\n");
      correlation_id = NULL;
    }
  }
  else
  {
    aid = hc->async_scope_id;
  }

  GNUNET_SCHEDULER_begin_async_scope (&aid);

  if (NULL != correlation_id)
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Handling request for (%s) URL '%s', correlation_id=%s\n",
                method,
                url,
                correlation_id);
  else
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Handling request (%s) for URL '%s'\n",
                method,
                url);
  if (0 == strncmp (url,
                    "/backup/",
                    strlen ("/backup/")))
  {
    // return handle_policy (...);
    if (0 == strcmp (method, MHD_HTTP_METHOD_GET))
    {
      return sync_handler_backup_get (connection,
                                      url,
                                      con_cls);
    }
    if (0 == strcmp (method, MHD_HTTP_METHOD_POST))
    {
      return sync_handler_backup_post (connection,
                                       con_cls,
                                       url,
                                       upload_data,
                                       upload_data_size);
    }
  }
  for (unsigned int i = 0; NULL != handlers[i].url; i++)
  {
    struct SH_RequestHandler *rh = &handlers[i];

    if ( (0 == strcmp (url,
                       rh->url)) &&
         ( (NULL == rh->method) ||
           (0 == strcmp (method,
                         rh->method)) ) )
    {
      int ret;

      ret = rh->handler (rh,
                         connection,
                         con_cls,
                         upload_data,
                         upload_data_size);
      hc = *con_cls;
      if (NULL != hc)
      {
        hc->rh = rh;
        /* Store the async context ID, so we can restore it if
         * we get another callack for this request. */
        hc->async_scope_id = aid;
      }
      return ret;
    }
  }
  return SH_MHD_handler_static_response (&h404,
                                         connection,
                                         con_cls,
                                         upload_data,
                                         upload_data_size);
}


/**
 * Shutdown task (magically invoked when the application is being
 * quit)
 *
 * @param cls NULL
 */
static void
do_shutdown (void *cls)
{
  if (NULL != mhd_task)
  {
    GNUNET_SCHEDULER_cancel (mhd_task);
    mhd_task = NULL;
  }
  if (NULL != mhd)
  {
    MHD_stop_daemon (mhd);
    mhd = NULL;
  }
  if (NULL != db)
  {
    SYNC_DB_plugin_unload (db);
    db = NULL;
  }
}


/**
 * Function called whenever MHD is done with a request.  If the
 * request was a POST, we may have stored a `struct Buffer *` in the
 * @a con_cls that might still need to be cleaned up.  Call the
 * respective function to free the memory.
 *
 * @param cls client-defined closure
 * @param connection connection handle
 * @param con_cls value as set by the last call to
 *        the #MHD_AccessHandlerCallback
 * @param toe reason for request termination
 * @see #MHD_OPTION_NOTIFY_COMPLETED
 * @ingroup request
 */
static void
handle_mhd_completion_callback (void *cls,
                                struct MHD_Connection *connection,
                                void **con_cls,
                                enum MHD_RequestTerminationCode toe)
{
  struct TM_HandlerContext *hc = *con_cls;

  if (NULL == hc)
    return;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Finished handling request for `%s' with status %d\n",
              hc->rh->url,
              (int) toe);
  hc->cc (hc);
  *con_cls = NULL;
}


/**
 * Function that queries MHD's select sets and
 * starts the task waiting for them.
 */
static struct GNUNET_SCHEDULER_Task *
prepare_daemon (void);


/**
 * Set if we should immediately #MHD_run again.
 */
static int triggered;


/**
 * Call MHD to process pending requests and then go back
 * and schedule the next run.
 *
 * @param cls the `struct MHD_Daemon` of the HTTP server to run
 */
static void
run_daemon (void *cls)
{
  mhd_task = NULL;
  do {
    triggered = 0;
    GNUNET_assert (MHD_YES == MHD_run (mhd));
  } while (0 != triggered);
  mhd_task = prepare_daemon ();
}


/**
 * Kick MHD to run now, to be called after MHD_resume_connection().
 * Basically, we need to explicitly resume MHD's event loop whenever
 * we made progress serving a request.  This function re-schedules
 * the task processing MHD's activities to run immediately.
 */
void
SH_trigger_daemon ()
{
  if (NULL != mhd_task)
  {
    GNUNET_SCHEDULER_cancel (mhd_task);
    mhd_task = NULL;
    run_daemon (NULL);
  }
  else
  {
    triggered = 1;
  }
}


/**
 * Function that queries MHD's select sets and
 * starts the task waiting for them.
 *
 * @param daemon_handle HTTP server to prepare to run
 */
static struct GNUNET_SCHEDULER_Task *
prepare_daemon ()
{
  struct GNUNET_SCHEDULER_Task *ret;
  fd_set rs;
  fd_set ws;
  fd_set es;
  struct GNUNET_NETWORK_FDSet *wrs;
  struct GNUNET_NETWORK_FDSet *wws;
  int max;
  MHD_UNSIGNED_LONG_LONG timeout;
  int haveto;
  struct GNUNET_TIME_Relative tv;

  FD_ZERO (&rs);
  FD_ZERO (&ws);
  FD_ZERO (&es);
  wrs = GNUNET_NETWORK_fdset_create ();
  wws = GNUNET_NETWORK_fdset_create ();
  max = -1;
  GNUNET_assert (MHD_YES ==
                 MHD_get_fdset (mhd,
                                &rs,
                                &ws,
                                &es,
                                &max));
  haveto = MHD_get_timeout (mhd, &timeout);
  if (haveto == MHD_YES)
    tv.rel_value_us = (uint64_t) timeout * 1000LL;
  else
    tv = GNUNET_TIME_UNIT_FOREVER_REL;
  GNUNET_NETWORK_fdset_copy_native (wrs, &rs, max + 1);
  GNUNET_NETWORK_fdset_copy_native (wws, &ws, max + 1);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Adding run_daemon select task\n");
  ret = GNUNET_SCHEDULER_add_select (GNUNET_SCHEDULER_PRIORITY_HIGH,
                                     tv,
                                     wrs,
                                     wws,
                                     &run_daemon,
                                     NULL);
  GNUNET_NETWORK_fdset_destroy (wrs);
  GNUNET_NETWORK_fdset_destroy (wws);
  return ret;
}


/**
 * Main function that will be run by the scheduler.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be
 *        NULL!)
 * @param config configuration
 */
static void
run (void *cls,
     char *const *args,
     const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *config)
{
  int fh;

  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Starting sync-httpd\n");
  result = GNUNET_SYSERR;
  GNUNET_SCHEDULER_add_shutdown (&do_shutdown,
                                 NULL);
  GNUNET_assert (GNUNET_OK ==
                 GNUNET_log_setup ("sync-httpd",
                                   "WARNING",
                                   NULL));
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (config,
                                             "sync",
                                             "UPLOAD_LIMIT_MB",
                                             &SH_upload_limit_mb))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "sync",
                               "UPLOAD_LIMIT_MB");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  if (GNUNET_OK !=
      TALER_config_get_denom (config,
                              "sync",
                              "ANNUAL_FEE",
                              &SH_annual_fee))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "sync",
                               "ANNUAL_FEE");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }

  if (NULL ==
      (db = SYNC_DB_plugin_load (config)))
  {
    GNUNET_SCHEDULER_shutdown ();
    return;
  }

  {
    const char *choices[] = {"tcp",
                             "unix",
                             NULL};

    const char *serve_type;

    if (GNUNET_OK !=
        GNUNET_CONFIGURATION_get_value_choice (config,
                                               "sync",
                                               "SERVE",
                                               choices,
                                               &serve_type))
    {
      GNUNET_log_config_invalid (GNUNET_ERROR_TYPE_ERROR,
                                 "sync",
                                 "SERVE",
                                 "serve type required");
      GNUNET_SCHEDULER_shutdown ();
      return;
    }

    if (0 == strcmp (serve_type, "unix"))
    {
      struct sockaddr_un *un;
      char *mode;
      struct GNUNET_NETWORK_Handle *nh;

      if (GNUNET_OK !=
          GNUNET_CONFIGURATION_get_value_filename (config,
                                                   "sync",
                                                   "unixpath",
                                                   &serve_unixpath))
      {
        GNUNET_log_config_invalid (GNUNET_ERROR_TYPE_ERROR,
                                   "sync",
                                   "unixpath",
                                   "unixpath required");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }

      if (strlen (serve_unixpath) >= sizeof (un->sun_path))
      {
        fprintf (stderr,
                 "Invalid configuration: unix path too long\n");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }

      if (GNUNET_OK !=
          GNUNET_CONFIGURATION_get_value_string (config,
                                                 "sync",
                                                 "UNIXPATH_MODE",
                                                 &mode))
      {
        GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                                   "sync",
                                   "UNIXPATH_MODE");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }
      errno = 0;
      unixpath_mode = (mode_t) strtoul (mode, NULL, 8);
      if (0 != errno)
      {
        GNUNET_log_config_invalid (GNUNET_ERROR_TYPE_ERROR,
                                   "sync",
                                   "UNIXPATH_MODE",
                                   "must be octal number");
        GNUNET_free (mode);
        GNUNET_SCHEDULER_shutdown ();
        return;
      }
      GNUNET_free (mode);

      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  "Creating listen socket '%s' with mode %o\n",
                  serve_unixpath, unixpath_mode);

      if (GNUNET_OK != GNUNET_DISK_directory_create_for_file (serve_unixpath))
      {
        GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_ERROR,
                                  "mkdir",
                                  serve_unixpath);
      }

      un = GNUNET_new (struct sockaddr_un);
      un->sun_family = AF_UNIX;
      strncpy (un->sun_path,
               serve_unixpath,
               sizeof (un->sun_path) - 1);

      GNUNET_NETWORK_unix_precheck (un);

      if (NULL == (nh = GNUNET_NETWORK_socket_create (AF_UNIX,
                                                      SOCK_STREAM,
                                                      0)))
      {
        GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                             "socket(AF_UNIX)");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }
      if (GNUNET_OK !=
          GNUNET_NETWORK_socket_bind (nh,
                                      (void *) un,
                                      sizeof (struct sockaddr_un)))
      {
        GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                             "bind(AF_UNIX)");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }
      if (GNUNET_OK !=
          GNUNET_NETWORK_socket_listen (nh,
                                        UNIX_BACKLOG))
      {
        GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                             "listen(AF_UNIX)");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }

      fh = GNUNET_NETWORK_get_fd (nh);
      GNUNET_NETWORK_socket_free_memory_only_ (nh);
      if (0 != chmod (serve_unixpath,
                      unixpath_mode))
      {
        GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                             "chmod");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }
      port = 0;
    }
    else if (0 == strcmp (serve_type, "tcp"))
    {
      char *bind_to;

      if (GNUNET_SYSERR ==
          GNUNET_CONFIGURATION_get_value_number (config,
                                                 "sync",
                                                 "PORT",
                                                 &port))
      {
        GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                                   "sync",
                                   "PORT");
        GNUNET_SCHEDULER_shutdown ();
        return;
      }
      if (GNUNET_OK ==
          GNUNET_CONFIGURATION_get_value_string (config,
                                                 "sync",
                                                 "BIND_TO",
                                                 &bind_to))
      {
        char port_str[6];
        struct addrinfo hints;
        struct addrinfo *res;
        int ec;
        struct GNUNET_NETWORK_Handle *nh;

        GNUNET_snprintf (port_str,
                         sizeof (port_str),
                         "%u",
                         (uint16_t) port);
        memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE
#ifdef AI_IDN
                         | AI_IDN
#endif
        ;
        if (0 !=
            (ec = getaddrinfo (bind_to,
                               port_str,
                               &hints,
                               &res)))
        {
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                      "Failed to resolve BIND_TO address `%s': %s\n",
                      bind_to,
                      gai_strerror (ec));
          GNUNET_free (bind_to);
          GNUNET_SCHEDULER_shutdown ();
          return;
        }
        GNUNET_free (bind_to);

        if (NULL == (nh = GNUNET_NETWORK_socket_create (res->ai_family,
                                                        res->ai_socktype,
                                                        res->ai_protocol)))
        {
          GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                               "socket");
          freeaddrinfo (res);
          GNUNET_SCHEDULER_shutdown ();
          return;
        }
        if (GNUNET_OK !=
            GNUNET_NETWORK_socket_bind (nh,
                                        res->ai_addr,
                                        res->ai_addrlen))
        {
          GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                               "bind");
          freeaddrinfo (res);
          GNUNET_SCHEDULER_shutdown ();
          return;
        }
        freeaddrinfo (res);
        if (GNUNET_OK !=
            GNUNET_NETWORK_socket_listen (nh,
                                          UNIX_BACKLOG))
        {
          GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR,
                               "listen");
          GNUNET_SCHEDULER_shutdown ();
          return;
        }
        fh = GNUNET_NETWORK_get_fd (nh);
        GNUNET_NETWORK_socket_free_memory_only_ (nh);
      }
      else
      {
        fh = -1;
      }
    }
    else
    {
      // not reached
      GNUNET_assert (0);
    }
  }
  mhd = MHD_start_daemon (MHD_USE_SUSPEND_RESUME | MHD_USE_DUAL_STACK,
                          port,
                          NULL, NULL,
                          &url_handler, NULL,
                          MHD_OPTION_LISTEN_SOCKET, fh,
                          MHD_OPTION_NOTIFY_COMPLETED,
                          &handle_mhd_completion_callback, NULL,
                          MHD_OPTION_CONNECTION_TIMEOUT, (unsigned
                                                          int) 10 /* 10s */,
                          MHD_OPTION_END);
  if (NULL == mhd)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Failed to launch HTTP service, exiting.\n");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  result = GNUNET_OK;
  mhd_task = prepare_daemon ();
}


/**
 * The main function of the serve tool
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc,
      char *const *argv)
{
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_option_flag ('C',
                               "connection-close",
                               "force HTTP connections to be closed after each request",
                               &SH_sync_connection_close),

    GNUNET_GETOPT_OPTION_END
  };

  if (GNUNET_OK !=
      GNUNET_PROGRAM_run (argc, argv,
                          "sync-httpd",
                          "sync HTTP interface",
                          options, &run, NULL))
    return 3;
  return (GNUNET_OK == result) ? 0 : 1;
}
