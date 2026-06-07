#!/bin/sh

set -eu

read_secret() {
  name="$1"
  file_var_name="${name}_FILE"
  eval "file_path=\"\${${file_var_name}:-}\""
  if [ -n "${file_path}" ]; then
    if [ ! -r "${file_path}" ]; then
      echo "${file_var_name} points to unreadable secret file: ${file_path}" >&2
      exit 1
    fi
    value="$(tr -d '\r' <"${file_path}")"
    if [ -z "${value}" ]; then
      echo "${file_var_name} points to empty secret file: ${file_path}" >&2
      exit 1
    fi
    printf '%s' "${value}"
    return
  fi

  eval "value=\"\${${name}:-}\""
  if [ -z "${value}" ]; then
    echo "${name} or ${file_var_name} is required" >&2
    exit 1
  fi
  printf '%s' "${value}"
}

host="${MYSQL_HOST:-mysql}"
port="${MYSQL_PORT:-3306}"
database="${MYSQL_DATABASE:?MYSQL_DATABASE is required}"
user="${MYSQL_BACKUP_USER:-root}"
password="$(read_secret MYSQL_BACKUP_PASSWORD)"
backup_dir="${MYSQL_BACKUP_DIR:-/backups}"
binlog_source_dir="${MYSQL_BINLOG_SOURCE_DIR:-/mysql-data}"
schedule_time="${MYSQL_BACKUP_TIME_UTC:-02:00}"
retention_days="${MYSQL_BACKUP_RETENTION_DAYS:-7}"
run_once="${MYSQL_BACKUP_RUN_ONCE:-false}"
run_immediately="${MYSQL_BACKUP_RUN_IMMEDIATELY:-false}"

log() {
  printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"
}

to_int() {
  value="$(printf '%s' "$1" | sed 's/^0*//')"
  if [ -z "${value}" ]; then
    value=0
  fi
  printf '%s' "${value}"
}

seconds_until_schedule() {
  target_hour="$(to_int "${schedule_time%:*}")"
  target_minute="$(to_int "${schedule_time#*:}")"
  now_hour="$(to_int "$(date -u +%H)")"
  now_minute="$(to_int "$(date -u +%M)")"
  now_second="$(to_int "$(date -u +%S)")"

  target_seconds=$((target_hour * 3600 + target_minute * 60))
  now_seconds=$((now_hour * 3600 + now_minute * 60 + now_second))

  if [ "${target_seconds}" -le "${now_seconds}" ]; then
    target_seconds=$((target_seconds + 86400))
  fi

  printf '%s' "$((target_seconds - now_seconds))"
}

mysql_args() {
  printf '%s\n' --protocol=tcp "-h${host}" "-P${port}" "-u${user}"
}

wait_for_mysql() {
  attempts=0

  until MYSQL_PWD="${password}" mysql $(mysql_args) -e "SELECT 1" >/dev/null 2>&1; do
    attempts=$((attempts + 1))
    if [ "${attempts}" -ge 30 ]; then
      log "MySQL did not become ready in time."
      exit 1
    fi
    sleep 2
  done
}

copy_binlogs() {
  if [ ! -d "${binlog_source_dir}" ]; then
    log "Binlog source directory ${binlog_source_dir} does not exist; skipping binlog archive."
    return
  fi

  mkdir -p "${backup_dir}/binlogs"
  find "${binlog_source_dir}" -maxdepth 1 -type f -name 'mysql-bin.[0-9]*' -exec cp -n {} "${backup_dir}/binlogs/" \;
  find "${backup_dir}/binlogs" -type f -name 'mysql-bin.[0-9]*' -mtime "+${retention_days}" -delete
}

run_backup() {
  timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
  tmp_file="${backup_dir}/zimage-${database}-${timestamp}.sql.gz.tmp"
  output_file="${backup_dir}/zimage-${database}-${timestamp}.sql.gz"

  mkdir -p "${backup_dir}"
  log "Starting MySQL backup for ${database}."

  MYSQL_PWD="${password}" mysqldump $(mysql_args) \
    --single-transaction \
    --routines \
    --triggers \
    --events \
    --source-data=2 \
    --set-gtid-purged=OFF \
    "${database}" | gzip >"${tmp_file}"

  mv "${tmp_file}" "${output_file}"
  log "Wrote ${output_file}."

  if ! MYSQL_PWD="${password}" mysqladmin $(mysql_args) flush-logs >/dev/null 2>&1; then
    log "Could not flush MySQL binlogs; archived dump is still valid, PITR coverage may be incomplete."
  fi
  copy_binlogs

  find "${backup_dir}" -maxdepth 1 -type f -name "zimage-${database}-*.sql.gz" -mtime "+${retention_days}" -delete
}

wait_for_mysql

case "${run_once}" in
  true | 1 | yes)
    run_backup
    exit 0
    ;;
esac

case "${run_immediately}" in
  true | 1 | yes)
    run_backup
    ;;
esac

while :; do
  sleep_seconds="$(seconds_until_schedule)"
  log "Sleeping ${sleep_seconds}s until next MySQL backup at ${schedule_time} UTC."
  sleep "${sleep_seconds}"
  run_backup
done
