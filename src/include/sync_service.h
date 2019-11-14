/*
  This file is part of TALER
  Copyright (C) 2019 Taler Systems SA

  Anastasis is free software; you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free Software
  Foundation; either version 3, or (at your option) any later version.

  Anastasis is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License along with
  Anastasis; see the file COPYING.LIB.  If not, see <http://www.gnu.org/licenses/>
*/
/**
 * @file include/sync_service.h
 * @brief C interface of libsync, a C library to use sync's HTTP API
 * @author Christian Grothoff
 */
#ifndef _SYNC_SERVICE_H
#define _SYNC_SERVICE_H

#include <gnunet/gnunet_util_lib.h>
#include <gnunet/gnunet_curl_lib.h>
#include <jansson.h>


/**
 * Private key identifying an account.
 */
struct SYNC_AccountPrivateKeyP
{
  /**
   * We use EdDSA.
   */
  struct GNUNET_CRYPTO_EddsaPrivateKey eddsa_priv;
};


/**
 * Public key identifying an account.
 */
struct SYNC_AccountPublicKeyP
{
  /**
   * We use EdDSA.
   */
  struct GNUNET_CRYPTO_EddsaPrivateKey eddsa_priv;
};


/**
 * Signature made with an account's public key.
 */
struct SYNC_AccountSignatureP
{
  /**
   * We use EdDSA.
   */
  struct GNUNET_CRYPTO_EddsaSignature eddsa_sig;
};


struct SYNC_UploadOperation;

struct SYNC_UploadOperation *
SYNC_upload (struct GNUNET_CURL_Context *ctx,
             const char *base_url,
             ...);


void
SYNC_upload_cancel (struct SYNC_UploadOperation *uo);


struct SYNC_DownloadOperation;

struct SYNC_DownloadOperation *
SYNC_download (struct GNUNET_CURL_Context *ctx,
               const char *base_url,
               ...);


void
SYNC_download_cancel (struct SYNC_DownloadOperation *uo);


#endif  /* _SYNC_SERVICE_H */
