#!/bin/sh

set -eu

host="${MYSQL_HOST:-mysql}"
port="${MYSQL_PORT:-3306}"
database="${MYSQL_DATABASE:?MYSQL_DATABASE is required}"
user="${MYSQL_USER:?MYSQL_USER is required}"
password="${MYSQL_PASSWORD:?MYSQL_PASSWORD is required}"
migrations_dir="${MIGRATIONS_DIR:-/migrations}"

mysql_exec() {
  mysql --protocol=tcp -h"${host}" -P"${port}" -u"${user}" -p"${password}" "${database}" "$@"
}

run_migration_file() {
  file="$1"
  # Use mysql's `source` command so client-side commands in migration files,
  # such as `DELIMITER`, are parsed explicitly by the mysql client.
  mysql_exec -e "source ${file}"
}

wait_for_mysql() {
  attempts=0

  until mysql --protocol=tcp -h"${host}" -P"${port}" -u"${user}" -p"${password}" -e "SELECT 1" >/dev/null 2>&1; do
    attempts=$((attempts + 1))

    if [ "${attempts}" -ge 30 ]; then
      echo "MySQL did not become ready in time." >&2
      exit 1
    fi

    sleep 2
  done
}

wait_for_mysql

mysql_exec -e "CREATE TABLE IF NOT EXISTS schema_migrations (version VARCHAR(128) PRIMARY KEY, applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP)"

applied_count="$(mysql_exec -Nse "SELECT COUNT(*) FROM schema_migrations")"
legacy_table_count="$(mysql_exec -Nse "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name IN ('users', 'image_generations')")"

if [ "${applied_count}" -eq 0 ] && [ "${legacy_table_count}" -gt 0 ]; then
  echo "Bootstrapping legacy database into versioned migrations with 001_initial_schema."
  mysql_exec -e "INSERT INTO schema_migrations (version) VALUES ('001_initial_schema') ON DUPLICATE KEY UPDATE applied_at = applied_at"
fi

found_migration=0

for file in "${migrations_dir}"/*.sql; do
  if [ ! -f "${file}" ]; then
    continue
  fi

  found_migration=1
  version="$(basename "${file}" .sql)"
  already_applied="$(mysql_exec -Nse "SELECT COUNT(*) FROM schema_migrations WHERE version = '${version}'")"

  if [ "${already_applied}" -gt 0 ]; then
    echo "Skipping ${version}; already applied."
    continue
  fi

  echo "Applying ${version}..."
  run_migration_file "${file}"
  mysql_exec -e "INSERT INTO schema_migrations (version) VALUES ('${version}')"
done

if [ "${found_migration}" -eq 0 ]; then
  echo "No migration files were found in ${migrations_dir}." >&2
  exit 1
fi

echo "Database migrations are up to date."
