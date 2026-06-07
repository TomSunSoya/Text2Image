#!/bin/sh

set -eu

usage() {
  cat >&2 <<'EOF'
Usage:
  restore-mysql.sh <dump.sql.gz> [--binlog-dir <dir>] [--until "YYYY-MM-DD HH:MM:SS"]

Environment:
  MYSQL_HOST        default mysql
  MYSQL_PORT        default 3306
  MYSQL_DATABASE    required
  MYSQL_USER        required
  MYSQL_PASSWORD    required unless MYSQL_PASSWORD_FILE is set
  MYSQL_PASSWORD_FILE

Notes:
  Restore the gzip dump first. If --binlog-dir is provided, the script then
  applies archived mysql-bin.* files in lexical order, stopping at --until when
  supplied. Include only binlogs that start at the dump's recorded source
  position; check the commented SOURCE_LOG_FILE / SOURCE_LOG_POS lines inside
  the dump before using PITR.
EOF
}

if [ "$#" -lt 1 ]; then
  usage
  exit 1
fi

dump_file="$1"
shift

binlog_dir=""
until_time=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --binlog-dir)
      binlog_dir="${2:?--binlog-dir requires a value}"
      shift 2
      ;;
    --until)
      until_time="${2:?--until requires a value}"
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [ ! -f "${dump_file}" ]; then
  echo "Dump file does not exist: ${dump_file}" >&2
  exit 1
fi

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
user="${MYSQL_USER:?MYSQL_USER is required}"
password="$(read_secret MYSQL_PASSWORD)"

mysql_args() {
  printf '%s\n' --protocol=tcp "-h${host}" "-P${port}" "-u${user}" "${database}"
}

mysql_server_args() {
  printf '%s\n' --protocol=tcp "-h${host}" "-P${port}" "-u${user}"
}

echo "Restoring ${dump_file} into ${database} on ${host}:${port}."
MYSQL_PWD="${password}" mysql $(mysql_server_args) -e "CREATE DATABASE IF NOT EXISTS \`${database}\`"
gzip -dc "${dump_file}" | MYSQL_PWD="${password}" mysql $(mysql_args)

if [ -n "${binlog_dir}" ]; then
  if [ ! -d "${binlog_dir}" ]; then
    echo "Binlog directory does not exist: ${binlog_dir}" >&2
    exit 1
  fi

  set -- "${binlog_dir}"/mysql-bin.[0-9]*
  if [ ! -f "$1" ]; then
    echo "No mysql-bin.* files found in ${binlog_dir}." >&2
    exit 1
  fi

  for binlog_file in "$@"; do
    echo "Applying ${binlog_file}."
    if [ -n "${until_time}" ]; then
      mysqlbinlog --stop-datetime="${until_time}" "${binlog_file}" | MYSQL_PWD="${password}" mysql $(mysql_args)
    else
      mysqlbinlog "${binlog_file}" | MYSQL_PWD="${password}" mysql $(mysql_args)
    fi
  done
fi

echo "Restore finished."
