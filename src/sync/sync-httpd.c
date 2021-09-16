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
 * @file sync/sync-httpd.c
 * @brief HTTP serving layer intended to provide basic backup operations
 * @author Christian Grothoff
 */
#include "platform.h"
#include <microhttpd.h>
#include <gnunet/gnunet_util_lib.h>
#include "sync_util.h"
#include "sync-httpd.h"
#include "sync-httpd_mhd.h"
#include "sync_database_lib.h"
#include "sync-httpd_backup.h"
#include "sync-httpd_config.h"

/**
 * Backlog for listen operation on unix-domain sockets.
 */
#define UNIX_BACKLOG 500


/**
 * Should a "Connection: close" header be added to each HTTP response?
 */
static int SH_sync_connection_close;

/**
 * Upload limit to the service, in megabytes.
 */
unsigned long long int SH_upload_limit_mb;

/**
 * Annual fee for the backup account.
 */
struct TALER_Amount SH_annual_fee;

/**
 * Our Taler backend to process payments.
 */
char *SH_backend_url;

/**
 * Our fulfillment URL.
 */
char *SH_fulfillment_url;

/**
 * Our context for making HTTP requests.
 */
struct GNUNET_CURL_Context *SH_ctx;

/**
 * Reschedule context for #SH_ctx.
 */
static struct GNUNET_CURL_RescheduleContext *rc;

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
 * Connection handle to the our database
 */
struct SYNC_DatabasePlugin *db;

/**
 * Username and password to use for client authentication
 * (optional).
 */
static char *userpass;

/**
 * Type of the client's TLS certificate (optional).
 */
static char *certtype;

/**
 * File with the client's TLS certificate (optional).
 */
static char *certfile;

/**
 * File with the client's TLS private key (optional).
 */
static char *keyfile;

/**
 * This value goes in the Authorization:-header.
 */
static char *apikey;

/**
 * Passphrase to decrypt client's TLS private key file (optional).
 */
static char *keypass;

/**
 * Amount of insurance.
 */
struct TALER_Amount SH_insurance;


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
 *         #MHD_NO if the socket must be closed due to a serious
 *         error while handling the request
 */
static MHD_RESULT
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
      &SH_handler_config, MHD_HTTP_FOUND },
    { "/config", MHD_HTTP_METHOD_GET, "text/json",
      NULL, 0,
      &SH_handler_config, MHD_HTTP_OK },
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
  struct SYNC_AccountPublicKeyP account_pub;

  (void) cls;
  (void) version;
  hc = *con_cls;
  if (NULL == hc)
  {
    GNUNET_async_scope_fresh (&aid);
    /* We only read the correlation ID on the first callback for every client */
    correlation_id = MHD_lookup_connection_value (connection,
                                                  MHD_HEADER_KIND,
                                                  "Sync-Correlation-Id");
    if ((NULL != correlation_id) &&
        (GNUNET_YES != GNUNET_CURL_is_valid_scope_id (correlation_id)))
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
                    "/backups/",
                    strlen ("/backups/")))
  {
    const char *ac = &url[strlen ("/backups/")];

    if (GNUNET_OK !=
        GNUNET_CRYPTO_eddsa_public_key_from_string (ac,
                                                    strlen (ac),
                                                    &account_pub.eddsa_pub))
    {
      GNUNET_break_op (0);
      return TALER_MHD_reply_with_error (connection,
                                         MHD_HTTP_BAD_REQUEST,
                                         TALER_EC_GENERIC_PARAMETER_MALFORMED,
                                         ac);
    }
    if (0 == strcasecmp (method,
                         MHD_HTTP_METHOD_OPTIONS))
    {
      return TALER_MHD_reply_cors_preflight (connection);
    }
    if (0 == strcasecmp (method,
                         MHD_HTTP_METHOD_GET))
    {
      return SH_backup_get (connection,
                            &account_pub);
    }
    if (0 == strcasecmp (method,
                         MHD_HTTP_METHOD_POST))
    {
      int ret;

      ret = SH_backup_post (connection,
                            con_cls,
                            &account_pub,
                            upload_data,
                            upload_data_size);
      hc = *con_cls;
      if (NULL != hc)
      {
        /* Store the async context ID, so we can restore it if
         * we get another callback for this request. */
        hc->async_scope_id = aid;
      }
      return ret;
    }
  }
  for (unsigned int i = 0; NULL != handlers[i].url; i++)
  {
    struct SH_RequestHandler *rh = &handlers[i];

    if (0 == strcmp (url,
                     rh->url))
    {
      if (0 == strcasecmp (method,
                           MHD_HTTP_METHOD_OPTIONS))
      {
        return TALER_MHD_reply_cors_preflight (connection);
      }
      if ( (NULL == rh->method) ||
           (0 == strcasecmp (method,
                             rh->method)) )
      {
        MHD_RESULT ret;

        ret = rh->handler (rh,
                           connection,
                           con_cls,
                           upload_data,
                           upload_data_size);
        hc = *con_cls;
        if (NULL != hc)
        {
          /* Store the async context ID, so we can restore it if
           * we get another callback for this request. */
          hc->async_scope_id = aid;
        }
        return ret;
      }
    }
  }
  return SH_MHD_handler_static_response (&h404,
                                         connection,
                                         con_cls,
                                         upload_data,
                                         upload_data_size);
}


/**
 * Shutdown task. Invoked when the application is being terminated.
 *
 * @param cls NULL
 */
static void
do_shutdown (void *cls)
{
  (void) cls;
  SH_resume_all_bc ();
  if (NULL != mhd_task)
  {
    GNUNET_SCHEDULER_cancel (mhd_task);
    mhd_task = NULL;
  }
  if (NULL != SH_ctx)
  {
    GNUNET_CURL_fini (SH_ctx);
    SH_ctx = NULL;
  }
  if (NULL != rc)
  {
    GNUNET_CURL_gnunet_rc_destroy (rc);
    rc = NULL;
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

  (void) cls;
  (void) connection;
  if (NULL == hc)
    return;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Finished handling request with status %d\n",
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
  (void) cls;
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
    mhd_task = GNUNET_SCHEDULER_add_now (&run_daemon,
                                         NULL);
  }
  else
  {
    triggered = 1;
  }
}


/**
 * Kick GNUnet Curl scheduler to begin curl interactions.
 */
void
SH_trigger_curl ()
{
  GNUNET_CURL_gnunet_scheduler_reschedule (&rc);
}


/**
 * Function that queries MHD's select sets and
 * starts the task waiting for them.
 *
 * @param daemon_handle HTTP server to prepare to run
 */
static struct GNUNET_SCHEDULER_Task *
prepare_daemon (void)
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
  enum TALER_MHD_GlobalOptions go;
  uint16_t port;

  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Starting sync-httpd\n");
  go = TALER_MHD_GO_NONE;
  if (SH_sync_connection_close)
    go |= TALER_MHD_GO_FORCE_CONNECTION_CLOSE;
  TALER_MHD_setup (go);
  result = EXIT_NOTCONFIGURED;
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
      TALER_config_get_amount (config,
                               "sync",
                               "INSURANCE",
                               &SH_insurance))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "sync",
                               "INSURANCE");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  if (GNUNET_OK !=
      TALER_config_get_amount (config,
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
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (config,
                                             "sync",
                                             "PAYMENT_BACKEND_URL",
                                             &SH_backend_url))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "sync",
                               "PAYMENT_BACKEND_URL");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (config,
                                             "sync",
                                             "FULFILLMENT_URL",
                                             &SH_fulfillment_url))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
                               "sync",
                               "BASE_URL");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }

  /* setup HTTP client event loop */
  SH_ctx = GNUNET_CURL_init (&GNUNET_CURL_gnunet_scheduler_reschedule,
                             &rc);
  rc = GNUNET_CURL_gnunet_rc_create (SH_ctx);
  if (NULL != userpass)
    GNUNET_CURL_set_userpass (SH_ctx,
                              userpass);
  if (NULL != keyfile)
    GNUNET_CURL_set_tlscert (SH_ctx,
                             certtype,
                             certfile,
                             keyfile,
                             keypass);
  if (GNUNET_OK ==
      GNUNET_CONFIGURATION_get_value_string (config,
                                             "sync",
                                             "API_KEY",
                                             &apikey))
  {
    char *auth_header;

    GNUNET_asprintf (&auth_header,
                     "%s: %s",
                     MHD_HTTP_HEADER_AUTHORIZATION,
                     apikey);
    if (GNUNET_OK !=
        GNUNET_CURL_append_header (SH_ctx,
                                   auth_header))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Failed to set %s header, trying without\n",
                  MHD_HTTP_HEADER_AUTHORIZATION);
    }
    GNUNET_free (auth_header);
  }

  if (NULL ==
      (db = SYNC_DB_plugin_load (config)))
  {
    result = EXIT_NOTINSTALLED;
    GNUNET_SCHEDULER_shutdown ();
    return;
  }

  fh = TALER_MHD_bind (config,
                       "sync",
                       &port);
  if ( (0 == port) &&
       (-1 == fh) )
  {
    result = EXIT_NOPERMISSION;
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  mhd = MHD_start_daemon (MHD_USE_SUSPEND_RESUME | MHD_USE_DUAL_STACK,
                          port,
                          NULL, NULL,
                          &url_handler, NULL,
                          MHD_OPTION_LISTEN_SOCKET, fh,
                          MHD_OPTION_NOTIFY_COMPLETED,
                          &handle_mhd_completion_callback, NULL,
                          MHD_OPTION_CONNECTION_TIMEOUT,
                          (unsigned int) 10 /* 10s */,
                          MHD_OPTION_END);
  if (NULL == mhd)
  {
    result = EXIT_FAILURE;
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Failed to launch HTTP service, exiting.\n");
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  result = EXIT_SUCCESS;
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
    GNUNET_GETOPT_option_string ('A',
                                 "auth",
                                 "USERNAME:PASSWORD",
                                 "use the given USERNAME and PASSWORD for client authentication",
                                 &userpass),
    GNUNET_GETOPT_option_flag ('C',
                               "connection-close",
                               "force HTTP connections to be closed after each request",
                               &SH_sync_connection_close),
    GNUNET_GETOPT_option_string ('k',
                                 "key",
                                 "KEYFILE",
                                 "file with the private TLS key for TLS client authentication",
                                 &keyfile),
    GNUNET_GETOPT_option_string ('p',
                                 "pass",
                                 "KEYFILEPASSPHRASE",
                                 "passphrase needed to decrypt the TLS client private key file",
                                 &keypass),
    GNUNET_GETOPT_option_string ('t',
                                 "type",
                                 "CERTTYPE",
                                 "type of the TLS client certificate, defaults to PEM if not specified",
                                 &certtype),
    GNUNET_GETOPT_OPTION_END
  };
  enum GNUNET_GenericReturnValue ret;

  /* FIRST get the libtalerutil initialization out
     of the way. Then throw that one away, and force
     the SYNC defaults to be used! */
  (void) TALER_project_data_default ();
  GNUNET_OS_init (SYNC_project_data_default ());
  ret = GNUNET_PROGRAM_run (argc, argv,
                            "sync-httpd",
                            "sync HTTP interface",
                            options,
                            &run, NULL);
  if (GNUNET_NO == ret)
    return EXIT_SUCCESS;
  if (GNUNET_SYSERR == ret)
    return EXIT_INVALIDARGUMENT;
  return result;
}
