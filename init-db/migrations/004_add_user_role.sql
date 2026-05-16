DROP PROCEDURE IF EXISTS apply_user_role_migration;

DELIMITER $$

CREATE PROCEDURE apply_user_role_migration()
BEGIN
  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'users'
      AND column_name = 'role'
  ) = 0 THEN
    ALTER TABLE users
      ADD COLUMN role VARCHAR(32) NOT NULL DEFAULT 'user' AFTER nickname;
  END IF;
END $$

DELIMITER ;

CALL apply_user_role_migration();
DROP PROCEDURE apply_user_role_migration;
