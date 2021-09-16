--
-- This file is part of TALER
-- Copyright (C) 2021 Taler Systems SA
--
-- TALER is free software; you can redistribute it and/or modify it under the
-- terms of the GNU General Public License as published by the Free Software
-- Foundation; either version 3, or (at your option) any later version.
--
-- TALER is distributed in the hope that it will be useful, but WITHOUT ANY
-- WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
-- A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License along with
-- TALER; see the file COPYING.  If not, see <http://www.gnu.org/licenses/>
--

-- Everything in one big transaction
BEGIN;

-- Check patch versioning is in place.
SELECT _v.register_patch('sync-0001', NULL, NULL);

CREATE TABLE IF NOT EXISTS accounts
  (account_pub BYTEA PRIMARY KEY CHECK (length(account_pub)=32)
  ,expiration_date INT8 NOT NULL);

CREATE INDEX IF NOT EXISTS accounts_expire ON
  accounts (expiration_date);


CREATE TABLE IF NOT EXISTS payments
  (account_pub BYTEA CHECK (length(account_pub)=32)
  ,order_id VARCHAR PRIMARY KEY
  ,token BYTEA CHECK (length(token)=16)
  ,timestamp INT8 NOT NULL
  ,amount_val INT8 NOT NULL
  ,amount_frac INT4 NOT NULL
  ,paid BOOLEAN NOT NULL DEFAULT FALSE);

CREATE INDEX IF NOT EXISTS payments_timestamp ON
  payments (paid,timestamp);


CREATE TABLE IF NOT EXISTS backups
  (account_pub BYTEA PRIMARY KEY REFERENCES accounts (account_pub) ON DELETE CASCADE
  ,account_sig BYTEA NOT NULL CHECK (length(account_sig)=64)
  ,prev_hash BYTEA NOT NULL CHECK (length(prev_hash)=64)
  ,backup_hash BYTEA NOT NULL CHECK (length(backup_hash)=64)
  ,data BYTEA NOT NULL);


-- Complete transaction
COMMIT;
