/*
  This file is part of TALER
  Copyright (C) 2019 Taler Systems SA

  TALER is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3, or (at your
  option) any later version.

  TALER is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Privlic License for more details.

  You should have received a copy of the GNU General Privlic
  License along with TALER; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file lib/testing_api_trait_account_priv.c
 * @brief traits to offer a account_priv
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync_service.h"
#include "sync_testing_lib.h"

#define SYNC_TESTING_TRAIT_ACCOUNT_PRIV "sync-account_priv"


/**
 * Obtain an account private key from @a cmd.
 *
 * @param cmd command to extract the private key from.
 * @param index the private key's index number.
 * @param n[out] set to the private key coming from @a cmd.
 * @return #GNUNET_OK on success.
 */
int
SYNC_TESTING_get_trait_account_priv
  (const struct TALER_TESTING_Command *cmd,
  unsigned int index,
  const struct SYNC_AccountPrivateKeyP **priv)
{
  return cmd->traits (cmd->cls,
                      (const void **) priv,
                      SYNC_TESTING_TRAIT_ACCOUNT_PRIV,
                      index);
}


/**
 * Offer an account private key.
 *
 * @param index usually zero
 * @param priv the account_priv to offer.
 * @return #GNUNET_OK on success.
 */
struct TALER_TESTING_Trait
SYNC_TESTING_make_trait_account_priv
  (unsigned int index,
  const struct SYNC_AccountPrivateKeyP *priv)
{
  struct TALER_TESTING_Trait ret = {
    .index = index,
    .trait_name = SYNC_TESTING_TRAIT_ACCOUNT_PRIV,
    .ptr = (const void *) priv
  };
  return ret;
}


/* end of testing_api_trait_account_priv.c */
