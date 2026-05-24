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

minio_host="${MINIO_HOST:-minio}"
minio_port="${MINIO_PORT:-9000}"
minio_user="${MINIO_ROOT_USER:?MINIO_ROOT_USER is required}"
minio_password="$(read_secret MINIO_ROOT_PASSWORD)"
minio_bucket="${MINIO_BUCKET:-zimage}"
mysql_backup_dir="${MYSQL_BACKUP_DIR:-/mysql-backups}"
backup_endpoint="${BACKUP_S3_ENDPOINT:?BACKUP_S3_ENDPOINT is required}"
backup_bucket="${BACKUP_S3_BUCKET:?BACKUP_S3_BUCKET is required}"
backup_access_key="$(read_secret BACKUP_S3_ACCESS_KEY)"
backup_secret_key="$(read_secret BACKUP_S3_SECRET_KEY)"
backup_prefix="${BACKUP_S3_PREFIX:-zimage}"
remote_retention_days="${BACKUP_S3_RETENTION_DAYS:-30}"
schedule_time="${MINIO_MIRROR_TIME_UTC:-03:00}"
run_once="${MINIO_MIRROR_RUN_ONCE:-false}"
run_immediately="${MINIO_MIRROR_RUN_IMMEDIATELY:-false}"

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

reject_placeholder() {
  name="$1"
  value="$2"
  case "${value}" in
    *CHANGE_ME* | "")
      log "${name} must be replaced before enabling mc-mirror."
      exit 1
      ;;
  esac
}

configure_aliases() {
  reject_placeholder "BACKUP_S3_ENDPOINT" "${backup_endpoint}"
  reject_placeholder "BACKUP_S3_BUCKET" "${backup_bucket}"
  reject_placeholder "BACKUP_S3_ACCESS_KEY" "${backup_access_key}"
  reject_placeholder "BACKUP_S3_SECRET_KEY" "${backup_secret_key}"

  mc alias set local "http://${minio_host}:${minio_port}" "${minio_user}" "${minio_password}"
  mc alias set backup "${backup_endpoint}" "${backup_access_key}" "${backup_secret_key}" --api S3v4
  mc mb --ignore-existing "backup/${backup_bucket}"
}

run_mirror() {
  configure_aliases

  log "Mirroring MinIO bucket ${minio_bucket} to ${backup_bucket}/${backup_prefix}/minio/${minio_bucket}."
  mc mirror --overwrite --remove "local/${minio_bucket}" "backup/${backup_bucket}/${backup_prefix}/minio/${minio_bucket}"

  if [ -d "${mysql_backup_dir}" ]; then
    log "Mirroring MySQL backup volume to ${backup_bucket}/${backup_prefix}/mysql."
    mc mirror --overwrite "${mysql_backup_dir}" "backup/${backup_bucket}/${backup_prefix}/mysql"
    mc rm --recursive --force --older-than "${remote_retention_days}d" "backup/${backup_bucket}/${backup_prefix}/mysql" || true
  fi
}

case "${run_once}" in
  true | 1 | yes)
    run_mirror
    exit 0
    ;;
esac

case "${run_immediately}" in
  true | 1 | yes)
    run_mirror
    ;;
esac

while :; do
  sleep_seconds="$(seconds_until_schedule)"
  log "Sleeping ${sleep_seconds}s until next MinIO mirror at ${schedule_time} UTC."
  sleep "${sleep_seconds}"
  run_mirror
done
