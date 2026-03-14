export const IMAGE_ACTIVE_STATUSES = ['queued', 'pending', 'generating']
export const IMAGE_TERMINAL_STATUSES = ['success', 'failed', 'cancelled', 'timeout']
export const IMAGE_RETRYABLE_STATUSES = ['failed', 'timeout', 'cancelled']

export const normalizeImageStatus = (status) => String(status || '').toLowerCase()

export const isImageActiveStatus = (status) => IMAGE_ACTIVE_STATUSES.includes(normalizeImageStatus(status))

export const isImageTerminalStatus = (status) => IMAGE_TERMINAL_STATUSES.includes(normalizeImageStatus(status))

export const canCancelImageTask = (status) => IMAGE_ACTIVE_STATUSES.includes(normalizeImageStatus(status))

export const canRetryImageTask = (task) => {
  const status = normalizeImageStatus(task?.status)
  const retryCount = Number(task?.retryCount ?? task?.retry_count ?? 0)
  const maxRetries = Number(task?.maxRetries ?? task?.max_retries ?? 0)

  if (!IMAGE_RETRYABLE_STATUSES.includes(status)) {
    return false
  }

  if (!Number.isFinite(maxRetries) || maxRetries <= 0) {
    return false
  }

  return retryCount < maxRetries
}

export const canDownloadImageTask = (status) => normalizeImageStatus(status) === 'success'

export const getImageStatusTagType = (status) => {
  const normalized = normalizeImageStatus(status)

  if (normalized === 'success') return 'success'
  if (normalized === 'failed' || normalized === 'timeout') return 'danger'
  if (normalized === 'queued' || normalized === 'pending' || normalized === 'generating') return 'warning'
  if (normalized === 'cancelled') return 'info'

  return 'info'
}
