DROP PROCEDURE IF EXISTS apply_image_base64_migration;

DELIMITER $$

CREATE PROCEDURE apply_image_base64_migration()
BEGIN
  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'image_base64'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN image_base64 LONGTEXT DEFAULT NULL AFTER storage_key;
  END IF;
END $$

DELIMITER ;

CALL apply_image_base64_migration();
DROP PROCEDURE apply_image_base64_migration;
