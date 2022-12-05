/*
  This file is part of TALER
  Copyright (C) 2014, 2015, 2016 GNUnet e.V. and INRIA

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
 * @file include/platform.h
 * @brief This file contains the includes and definitions which are used by the
 *        rest of the modules
 * @author Sree Harsha Totakura <sreeharsha@totakura.in>
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

/* Include our configuration header */
#ifndef HAVE_USED_CONFIG_H
# define HAVE_USED_CONFIG_H
# ifdef HAVE_CONFIG_H
#  include "sync_config.h"
# endif
#endif


#if (GNUNET_EXTRA_LOGGING >= 1)
#define VERBOSE(cmd) cmd
#else
#define VERBOSE(cmd) do { break; } while (0)
#endif

/* Include the features available for GNU source */
#define _GNU_SOURCE

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef __clang__
#undef HAVE_STATIC_ASSERT
#endif

/**
 * These may be expensive, but good for debugging...
 */
#define ALLOW_EXTRA_CHECKS GNUNET_YES

/**
 * For strptime (glibc2 needs this).
 */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 499
#endif

#ifndef _REENTRANT
#define _REENTRANT
#endif

/* configuration options */

#define VERBOSE_STATS 0

#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IP_H
#include <netinet/ip.h>         /* superset of previous */
#endif
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <grp.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>             /* for mallinfo on GNU */
#endif
#include <unistd.h>             /* KLB_FIX */
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>             /* KLB_FIX */
#include <fcntl.h>
#include <math.h>
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <time.h>
#ifdef BSD
#include <net/if.h>
#endif
#if defined(BSD) && defined(__FreeBSD__) && defined(__FreeBSD_kernel__)
#include <semaphore.h>
#endif
#ifdef DARWIN
#include <dlfcn.h>
#include <semaphore.h>
#include <net/if.h>
#endif
#if defined(__linux__) || defined(GNU)
#include <net/if.h>
#endif
#ifdef SOLARIS
#include <sys/sockio.h>
#include <sys/filio.h>
#include <sys/loadavg.h>
#include <semaphore.h>
#endif
#if HAVE_UCRED_H
#include <ucred.h>
#endif
#if HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif
#if HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#include <errno.h>
#include <limits.h>

#if HAVE_VFORK_H
#include <vfork.h>
#endif

#include <ctype.h>
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#if HAVE_ENDIAN_H
#include <endian.h>
#endif
#if HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif

#define DIR_SEPARATOR '/'
#define DIR_SEPARATOR_STR "/"
#define PATH_SEPARATOR ':'
#define PATH_SEPARATOR_STR ":"
#define NEWLINE "\n"

#include <locale.h>
#include <sys/mman.h>

/* FreeBSD_kernel is not defined on the now discontinued kFreeBSD  */
#if defined(BSD) && defined(__FreeBSD__) && defined(__FreeBSD_kernel__)
#define __BYTE_ORDER BYTE_ORDER
#define __BIG_ENDIAN BIG_ENDIAN
#endif

#ifdef DARWIN
#define __BYTE_ORDER BYTE_ORDER
#define __BIG_ENDIAN BIG_ENDIAN
/* not available on darwin, override configure */
#undef HAVE_STAT64
#undef HAVE_MREMAP
#endif

#if ! HAVE_ATOLL
long long
atoll (const char *nptr);

#endif

#if ENABLE_NLS
#include "langinfo.h"
#endif

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t) (-1))
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

/**
 * AI_NUMERICSERV not defined in windows.  Then we just do without.
 */
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0
#endif


#if defined(__sparc__)
#define MAKE_UNALIGNED(val) ({ __typeof__((val)) __tmp; memmove (&__tmp, &(val), \
                                                                 sizeof((val))); \
                               __tmp; })
#else
#define MAKE_UNALIGNED(val) val
#endif


#ifndef PATH_MAX
/**
 * Assumed maximum path length.
 */
#define PATH_MAX 4096
#endif

#if HAVE_THREAD_LOCAL_GCC
#define GNUNET_THREAD_LOCAL __thread
#else
#define GNUNET_THREAD_LOCAL
#endif


/* Do not use shortcuts for gcrypt mpi */
#define GCRYPT_NO_MPI_MACROS 1

/* Do not use deprecated functions from gcrypt */
#define GCRYPT_NO_DEPRECATED 1


/* LSB-style exit status codes */
#ifndef EXIT_INVALIDARGUMENT
#define EXIT_INVALIDARGUMENT 2
#endif

#ifndef EXIT_NOTIMPLEMENTED
#define EXIT_NOTIMPLEMENTED 3
#endif

#ifndef EXIT_NOPERMISSION
#define EXIT_NOPERMISSION 4
#endif

#ifndef EXIT_NOTINSTALLED
#define EXIT_NOTINSTALLED 5
#endif

#ifndef EXIT_NOTCONFIGURED
#define EXIT_NOTCONFIGURED 6
#endif

#ifndef EXIT_NOTRUNNING
#define EXIT_NOTRUNNING 7
#endif


/* Ignore MHD deprecations for now as we want to be compatible
   to "ancient" MHD releases. */
#define MHD_NO_DEPRECATION 1

#endif  /* PLATFORM_H_ */

/* end of platform.h */
