/*
     This file is part of GNU Taler.
     Copyright (C) 2019 Taler Systems SA

     Sync is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     Sync is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with Sync; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/

/**
 * @file os_installation.c
 * @brief initialize libgnunet OS subsystem for Sync.
 * @author Christian Grothoff
 */
#include "platform.h"
#include <gnunet/gnunet_util_lib.h>


/**
 * Default project data used for installation path detection
 * for GNU Sync.
 */
static const struct GNUNET_OS_ProjectData sync_pd = {
  .libname = "libsyncutil",
  .project_dirname = "sync",
  .binary_name = "sync-httpd",
  .env_varname = "SYNC_PREFIX",
  .base_config_varname = "SYNC_BASE_CONFIG",
  .bug_email = "taler@lists.gnu.org",
  .homepage = "http://www.gnu.org/s/taler/",
  .config_file = "sync.conf",
  .user_config_file = "~/.config/sync.conf",
  .version = PACKAGE_VERSION,
  .is_gnu = 1,
  .gettext_domain = "sync",
  .gettext_path = NULL,
};


/**
 * Return default project data used by Sync.
 */
const struct GNUNET_OS_ProjectData *
SYNC_project_data_default (void)
{
  return &sync_pd;
}


/**
 * Initialize libsyncutil.
 */
void __attribute__ ((constructor))
SYNC_OS_init ()
{
  GNUNET_OS_init (&sync_pd);
}


/* end of os_installation.c */
