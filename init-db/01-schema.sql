CREATE TABLE IF NOT EXISTS schema_migrations (
  version VARCHAR(128) PRIMARY KEY,
  applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS users (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(64) NOT NULL,
  email VARCHAR(255) NOT NULL,
  password VARCHAR(255) NOT NULL,
  nickname VARCHAR(128) NOT NULL DEFAULT '',
  enabled BOOLEAN NOT NULL DEFAULT TRUE,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  UNIQUE KEY uq_users_username (username),
  UNIQUE KEY uq_users_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS image_generations (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  request_id VARCHAR(64) DEFAULT NULL,
  prompt TEXT,
  negative_prompt TEXT,
  num_steps INT DEFAULT NULL,
  height INT DEFAULT NULL,
  width INT DEFAULT NULL,
  seed INT DEFAULT NULL,
  status VARCHAR(20) DEFAULT NULL,
  retry_count INT NOT NULL DEFAULT 0,
  max_retries INT NOT NULL DEFAULT 3,
  failure_code VARCHAR(64) DEFAULT NULL,
  worker_id VARCHAR(128) DEFAULT NULL,
  image_url VARCHAR(500) DEFAULT NULL,
  thumbnail_url VARCHAR(500) DEFAULT NULL,
  storage_key VARCHAR(255) DEFAULT NULL,
  image_base64 LONGTEXT DEFAULT NULL,
  error_message TEXT DEFAULT NULL,
  generation_time DOUBLE DEFAULT NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  started_at DATETIME DEFAULT NULL,
  completed_at DATETIME DEFAULT NULL,
  cancelled_at DATETIME DEFAULT NULL,
  lease_expires_at DATETIME DEFAULT NULL,
  CONSTRAINT fk_image_generations_user
    FOREIGN KEY (user_id) REFERENCES users (id)
    ON DELETE CASCADE,
  INDEX idx_user_id (user_id),
  INDEX idx_request_id (request_id),
  INDEX idx_created_at (created_at),
  INDEX idx_status_created_at (status, created_at),
  INDEX idx_status_lease (status, lease_expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO schema_migrations (version)
VALUES
  ('001_initial_schema'),
  ('002_image_generation_task_queue'),
  ('003_add_image_base64')
-- Keep this baseline list in sync with init-db/migrations/ so fresh installs
-- record every migration that is already folded into this latest schema.
ON DUPLICATE KEY UPDATE applied_at = applied_at;
