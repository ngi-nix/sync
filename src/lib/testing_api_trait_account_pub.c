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
  General Public License for more details.

  You should have received a copy of the GNU General Public
  License along with TALER; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>
*/
/**
 * @file lib/testing_api_trait_account_pub.c
 * @brief traits to offer a account_pub
 * @author Christian Grothoff
 */
#include "platform.h"
#include "sync_service.h"
#include "sync_testing_lib.h"

#define SYNC_TESTING_TRAIT_ACCOUNT_PUB "sync-account_pub"


/**
 * Obtain an account public key from @a cmd.
 *
 * @param cmd command to extract the public key from.
 * @param index the public key's index number.
 * @param n[out] set to the public key coming from @a cmd.
 * @return #GNUNET_OK on success.
 */
int
SYNC_TESTING_get_trait_account_pub
  (const struct TALER_TESTING_Command *cmd,
  unsigned int index,
  const struct SYNC_AccountPublicKeyP **pub)
{
  return cmd->traits (cmd->cls,
                      (const void **) pub,
                      SYNC_TESTING_TRAIT_ACCOUNT_PUB,
                      index);
}


/**
 * Offer an account public key.
 *
 * @param index usually zero
 * @param h the account_pub to offer.
 * @return #GNUNET_OK on success.
 */
struct TALER_TESTING_Trait
TALER_TESTING_make_trait_account_pub
  (unsigned int index,
  const struct SYNC_AccountPublicKeyP *h)
{
  struct TALER_TESTING_Trait ret = {
    .index = index,
    .trait_name = SYNC_TESTING_TRAIT_ACCOUNT_PUB,
    .ptr = (const void *) h
  };
  return ret;
}


/* end of testing_api_trait_account_pub.c */
