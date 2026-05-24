# Backup and Restore Runbook

## Scope

This runbook covers the single-host Docker Compose deployment:

- MySQL logical dumps with `mysqldump --single-transaction --routines --triggers --events`
- MySQL binary log archiving for point-in-time recovery
- MinIO object mirroring to an S3-compatible backup bucket

Recovery targets:

- RPO: <= 24 hours for scheduled backups
- RTO: <= 2 hours for a single-host restore

## Production Setup

1. Copy `.env.production.example` to `.env.production`.
2. Replace the non-secret backup values and create Docker Secrets for credentials:
   - `BACKUP_S3_ENDPOINT`
   - `BACKUP_S3_BUCKET`
   - `BACKUP_S3_PREFIX`
   - `zimage_mysql_root_password`
   - `zimage_minio_password`
   - `zimage_backup_s3_access_key`
   - `zimage_backup_s3_secret_key`
3. Keep `.env.production` to `*_FILE` paths and secret names; run `chmod 600 .env.production` on the deployment host. Use least-privilege S3 credentials. The mirror job needs object read/write/list access for `BACKUP_S3_BUCKET`; avoid delete permission unless remote retention cleanup is required.
4. Start the normal production stack:

   ```bash
   docker compose --env-file .env.production -f docker-compose.yml -f docker-compose.prod.yml up -d --build
   ```

5. Start the backup profile:

   ```bash
   docker compose --env-file .env.production -f docker-compose.yml -f docker-compose.prod.yml --profile ops up -d mysql-backup mc-mirror
   ```

Default schedules are UTC:

- MySQL dump: `MYSQL_BACKUP_TIME_UTC=02:00`
- MinIO and backup-volume mirror: `MINIO_MIRROR_TIME_UTC=03:00`
- Local MySQL dump retention: `MYSQL_BACKUP_RETENTION_DAYS=7`
- Remote MySQL backup retention: `BACKUP_S3_RETENTION_DAYS=30`

## Manual Backup Check

Run an immediate MySQL backup:

```bash
MYSQL_BACKUP_RUN_ONCE=true docker compose --env-file .env.production -f docker-compose.yml -f docker-compose.prod.yml --profile ops run --rm mysql-backup
```

Run an immediate MinIO/S3 mirror:

```bash
MINIO_MIRROR_RUN_ONCE=true docker compose --env-file .env.production -f docker-compose.yml -f docker-compose.prod.yml --profile ops run --rm mc-mirror
```

Confirm that:

- `mysql-backups` contains a fresh `zimage-<database>-<timestamp>.sql.gz`
- `mysql-backups/binlogs` contains `mysql-bin.*` files
- the remote bucket contains `${BACKUP_S3_PREFIX}/minio/${MINIO_BUCKET}`
- the remote bucket contains `${BACKUP_S3_PREFIX}/mysql`

Confirm binary logging is enabled:

```bash
docker compose --env-file .env.production -f docker-compose.yml -f docker-compose.prod.yml exec mysql sh -c 'mysql -uroot -p"$(cat /run/secrets/mysql_root_password)" -e "SHOW BINARY LOGS;"'
```

## Restore Drill

Perform this weekly against a throwaway test database and quarterly as a full restore exercise.

1. Download or locate a dump file:

   ```bash
   zimage-image_generator-YYYYMMDDTHHMMSSZ.sql.gz
   ```

2. Restore the dump into a test MySQL instance:

   ```bash
   MYSQL_HOST=127.0.0.1 \
   MYSQL_PORT=3306 \
   MYSQL_DATABASE=image_generator_restore \
   MYSQL_USER=root \
   MYSQL_PASSWORD_FILE=/secure/path/mysql-root-password \
   sh scripts/restore-mysql.sh /backups/zimage-image_generator-YYYYMMDDTHHMMSSZ.sql.gz
   ```

3. For point-in-time recovery, inspect the dump header for the recorded source log file and position, then provide only the required archived binlogs:

   ```bash
   MYSQL_HOST=127.0.0.1 \
   MYSQL_PORT=3306 \
   MYSQL_DATABASE=image_generator_restore \
   MYSQL_USER=root \
   MYSQL_PASSWORD_FILE=/secure/path/mysql-root-password \
   sh scripts/restore-mysql.sh \
     /backups/zimage-image_generator-YYYYMMDDTHHMMSSZ.sql.gz \
     --binlog-dir /backups/binlogs-after-dump \
     --until "2026-05-21 10:30:00"
   ```

4. Verify row counts and a sample of image records:

   ```sql
   SELECT COUNT(*) FROM users;
   SELECT status, COUNT(*) FROM image_generations GROUP BY status;
   SELECT id, storage_key, created_at FROM image_generations ORDER BY created_at DESC LIMIT 10;
   ```

5. Restore MinIO objects into a test bucket and confirm `storage_key` values from MySQL exist in object storage.

## Operational Notes

- `--single-transaction` assumes InnoDB tables. Re-check the schema before introducing non-InnoDB tables.
- Binary logs consume disk. `MYSQL_BINLOG_EXPIRE_SECONDS=1209600` keeps 14 days by default.
- Treat downloaded backups and local secret files as sensitive; keep permissions restricted on the deployment host.
- A backup that has not been restored is only a hope with a timestamp. Keep the weekly restore check on the calendar.
