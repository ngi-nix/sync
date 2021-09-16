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

-- This script DROPs all of the tables we create.
--
-- Unlike the other SQL files, it SHOULD be updated to reflect the
-- latest requirements for dropping tables.

-- Drops for 0001.sql
DROP TABLE IF EXISTS payments CASCADE;
DROP TABLE IF EXISTS backups CASCADE;
DROP TABLE IF EXISTS accounts CASCADE;

-- Unregister patch (0001.sql)
SELECT _v.unregister_patch('sync-0001');

-- And we're out of here...
COMMIT;
