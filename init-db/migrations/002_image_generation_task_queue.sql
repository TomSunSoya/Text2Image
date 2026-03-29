DROP PROCEDURE IF EXISTS apply_image_generation_task_queue_migration;

DELIMITER $$

CREATE PROCEDURE apply_image_generation_task_queue_migration()
BEGIN
  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'retry_count'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN retry_count INT NOT NULL DEFAULT 0 AFTER status;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'max_retries'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN max_retries INT NOT NULL DEFAULT 3 AFTER retry_count;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'failure_code'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN failure_code VARCHAR(64) DEFAULT NULL AFTER max_retries;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'worker_id'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN worker_id VARCHAR(128) DEFAULT NULL AFTER failure_code;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'thumbnail_url'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN thumbnail_url VARCHAR(500) DEFAULT NULL AFTER image_url;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'storage_key'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN storage_key VARCHAR(255) DEFAULT NULL AFTER thumbnail_url;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'started_at'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN started_at DATETIME DEFAULT NULL AFTER created_at;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'cancelled_at'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN cancelled_at DATETIME DEFAULT NULL AFTER completed_at;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.columns
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND column_name = 'lease_expires_at'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD COLUMN lease_expires_at DATETIME DEFAULT NULL AFTER cancelled_at;
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.statistics
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND index_name = 'idx_status_created_at'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD INDEX idx_status_created_at (status, created_at);
  END IF;

  IF (
    SELECT COUNT(*)
    FROM information_schema.statistics
    WHERE table_schema = DATABASE()
      AND table_name = 'image_generations'
      AND index_name = 'idx_status_lease'
  ) = 0 THEN
    ALTER TABLE image_generations
      ADD INDEX idx_status_lease (status, lease_expires_at);
  END IF;
END $$

DELIMITER ;

CALL apply_image_generation_task_queue_migration();
DROP PROCEDURE apply_image_generation_task_queue_migration;
