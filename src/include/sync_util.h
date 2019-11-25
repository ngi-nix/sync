/*
  This file is part of SYNC
  Copyright (C) 2014, 2015 GNUnet e.V.

  SYNC is free software; you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  SYNC is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  SYNC; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file include/sync_util.h
 * @brief Interface for common utility functions
 * @author Christian Grothoff
 */
#ifndef SYNC_UTIL_H
#define SYNC_UTIL_H

#include <gnunet/gnunet_util_lib.h>

/**
 * Return default project data used by Sync.
 */
const struct GNUNET_OS_ProjectData *
SYNC_project_data_default (void);


#endif
