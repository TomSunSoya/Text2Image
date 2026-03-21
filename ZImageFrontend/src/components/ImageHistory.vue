<template>
  <div class="image-history">
    <el-card>
      <template #header>
        <div class="card-header">
          <div>
            <span class="title">Generation History</span>
            <p v-if="hasActiveTasks" class="subtitle">检测到进行中的任务，等待服务端推送更新。</p>
          </div>
          <div class="header-actions">
            <el-select v-model="selectedStatus" size="small" style="width: 180px" @change="handleStatusChange">
              <el-option
                v-for="option in statusOptions"
                :key="option.value"
                :label="option.label"
                :value="option.value"
              />
            </el-select>
            <el-button type="primary" size="small" @click="loadHistory">Refresh</el-button>
          </div>
        </div>
      </template>

      <el-table
        ref="tableRef"
        :data="historyList"
        row-key="id"
        style="width: 100%"
        v-loading="loading"
        @expand-change="handleExpandChange"
        @row-click="handleRowClick"
      >
        <el-table-column type="expand">
          <template #default="{ row }">
            <el-skeleton :loading="row.__detailLoading || row.__binaryLoading" animated>
              <template #template>
                <div class="expand-content skeleton-content">
                  <div class="image-preview skeleton-preview">
                    <el-skeleton-item variant="image" class="preview-skeleton" />
                  </div>
                  <div class="details">
                    <el-skeleton-item variant="h3" class="skeleton-title" />
                    <el-skeleton-item variant="text" class="skeleton-line" />
                    <el-skeleton-item variant="text" class="skeleton-line" />
                    <el-skeleton-item variant="text" class="skeleton-line short" />
                  </div>
                </div>
              </template>

              <div class="expand-content">
                <div class="image-preview">
                  <el-image
                    v-if="buildImageUrl(row)"
                    :src="buildImageUrl(row)"
                    fit="contain"
                    style="max-height: 400px"
                    :preview-src-list="[buildImageUrl(row)]"
                    preview-teleported
                    :z-index="3000"
                  />
                  <el-empty v-else description="No image data" />
                </div>
                <div class="details">
                  <p><strong>Prompt:</strong> {{ row.prompt }}</p>
                  <p v-if="row.negativePrompt"><strong>Negative Prompt:</strong> {{ row.negativePrompt }}</p>
                  <p><strong>Request ID:</strong> {{ row.requestId || '-' }}</p>
                  <p><strong>Params:</strong> {{ row.width }} x {{ row.height }}, steps: {{ row.numSteps }}</p>
                  <p><strong>Retry:</strong> {{ row.retryCount || 0 }} / {{ row.maxRetries || 0 }}</p>
                  <p v-if="row.failureCode"><strong>Failure Code:</strong> {{ row.failureCode }}</p>
                  <p v-if="row.errorMessage"><strong>Error:</strong> {{ row.errorMessage }}</p>
                  <p v-if="row.completedAt"><strong>Completed At:</strong> {{ formatDate(row.completedAt) }}</p>
                  <p v-if="row.cancelledAt"><strong>Cancelled At:</strong> {{ formatDate(row.cancelledAt) }}</p>
                </div>
              </div>
            </el-skeleton>
          </template>
        </el-table-column>

        <el-table-column label="ID" prop="id" width="80" />

        <el-table-column label="Prompt" prop="prompt" min-width="240">
          <template #default="{ row }">
            <el-text line-clamp="2">{{ row.prompt }}</el-text>
          </template>
        </el-table-column>

        <el-table-column label="Size" width="140">
          <template #default="{ row }">
            {{ row.width }} x {{ row.height }}
          </template>
        </el-table-column>

        <el-table-column label="Status" width="120">
          <template #default="{ row }">
            <el-tag :type="getStatusType(row.status)">
              {{ row.status }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="Duration" width="120">
          <template #default="{ row }">
            {{ row.generationTime ? `${row.generationTime.toFixed(2)}s` : '-' }}
          </template>
        </el-table-column>

        <el-table-column label="Created At" width="180">
          <template #default="{ row }">
            {{ formatDate(row.createdAt) }}
          </template>
        </el-table-column>

        <el-table-column label="Actions" min-width="260">
          <template #default="{ row }">
            <div class="row-actions">
              <el-button
                type="primary"
                size="small"
                :disabled="row.__detailLoading || row.__binaryLoading || Boolean(row.__actionType) || !canDownloadTask(row)"
                @click.stop="handleDownload(row)"
              >
                Download
              </el-button>
              <el-button
                v-if="canCancelTask(row)"
                type="warning"
                size="small"
                :loading="row.__actionType === 'cancel'"
                :disabled="Boolean(row.__actionType)"
                @click.stop="handleCancel(row)"
              >
                Cancel
              </el-button>
              <el-button
                v-else-if="canRetryTask(row)"
                type="success"
                size="small"
                :loading="row.__actionType === 'retry'"
                :disabled="Boolean(row.__actionType)"
                @click.stop="handleRetry(row)"
              >
                Retry
              </el-button>
              <el-button
                type="danger"
                size="small"
                :loading="row.__actionType === 'delete'"
                :disabled="Boolean(row.__actionType) || !canDeleteTask(row)"
                @click.stop="handleDelete(row)"
              >
                Delete
              </el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :total="total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
        style="margin-top: 20px; justify-content: center"
      />
    </el-card>
  </div>
</template>

<script setup>
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { imageApi } from '@/api/image'
import { subscribeTaskEvents } from '@/utils/taskSocket'
import {
  canCancelImageTask,
  canDownloadImageTask,
  canRetryImageTask,
  getImageStatusTagType,
  isImageActiveStatus,
  isImageTerminalStatus
} from '@/utils/imageTask'

const tableRef = ref(null)
const loading = ref(false)
const historyList = ref([])
const currentPage = ref(1)
const pageSize = ref(10)
const total = ref(0)
const selectedStatus = ref('all')

const statusOptions = [
  { label: 'All Statuses', value: 'all' },
  { label: 'Queued', value: 'queued' },
  { label: 'Pending', value: 'pending' },
  { label: 'Generating', value: 'generating' },
  { label: 'Success', value: 'success' },
  { label: 'Failed', value: 'failed' },
  { label: 'Cancelled', value: 'cancelled' },
  { label: 'Timeout', value: 'timeout' }
]

const hasActiveTasks = computed(() => historyList.value.some(row => isImageActiveStatus(row.status)))

let socketUnsubscribe = null
let refreshTimer = 0

const normalizeRow = (item, previousRow = null) => {
  const merged = previousRow ? { ...previousRow, ...item } : { ...item }

  return {
    ...merged,
    __detailLoaded: Boolean(merged?.imageBase64) || Boolean(previousRow?.__detailLoaded),
    __detailLoading: false,
    __binaryLoading: false,
    __actionType: '',
    __imageObjectUrl: previousRow?.__imageObjectUrl || ''
  }
}

const revokeRowObjectUrl = (row) => {
  if (row?.__imageObjectUrl && row.__imageObjectUrl.startsWith('blob:')) {
    URL.revokeObjectURL(row.__imageObjectUrl)
  }

  if (row) {
    row.__imageObjectUrl = ''
  }
}

const revokeAllObjectUrls = () => {
  historyList.value.forEach(revokeRowObjectUrl)
}

const rebuildHistoryList = (rawList) => {
  const previousMap = new Map(historyList.value.map(row => [row.id, row]))
  const nextIds = new Set(rawList.map(item => item.id))

  historyList.value.forEach(row => {
    if (!nextIds.has(row.id)) {
      revokeRowObjectUrl(row)
    }
  })

  return rawList.map(item => normalizeRow(item, previousMap.get(item.id)))
}

const clearRefreshTimer = () => {
  if (refreshTimer) {
    window.clearTimeout(refreshTimer)
    refreshTimer = null
  }
}

const scheduleSocketRefresh = () => {
  if (loading.value || refreshTimer) {
    return
  }

  refreshTimer = window.setTimeout(async () => {
    refreshTimer = null
    await loadHistory({ silent: true })
  }, 150)
}

const handleTaskSocketEvent = (event) => {
  if (event?.type !== 'image.task.updated') {
    return
  }

  scheduleSocketRefresh()
}

const loadHistory = async (options = {}) => {
  const { silent = false } = options

  if (!silent) {
    loading.value = true
  }

  try {
    const params = {
      page: currentPage.value - 1,
      size: pageSize.value
    }

    const response = selectedStatus.value === 'all'
      ? await imageApi.getMyImages(params)
      : await imageApi.getImagesByStatus(selectedStatus.value, params)

    const rawList = response?.data?.content || []
    historyList.value = rebuildHistoryList(rawList)
    total.value = response?.data?.totalElements || 0
  } catch (error) {
    if (!silent) {
      ElMessage.error(error?.message || 'Failed to load history')
    } else {
      console.error('Auto refresh history failed:', error)
    }
  } finally {
    if (!silent) {
      loading.value = false
    }
  }
}

const handleStatusChange = () => {
  currentPage.value = 1
  loadHistory()
}

const handleSizeChange = () => {
  currentPage.value = 1
  loadHistory()
}

const handleCurrentChange = () => {
  loadHistory()
}

const buildImageUrl = (row) => {
  if (row?.imageBase64) {
    return `data:image/png;base64,${String(row.imageBase64).replace(/\s/g, '')}`
  }

  if (row?.__imageObjectUrl) {
    return row.__imageObjectUrl
  }

  return ''
}

const ensureRowDetail = async (row) => {
  if (!row?.id || row.__detailLoaded || row.__detailLoading) {
    return
  }

  row.__detailLoading = true
  try {
    const response = await imageApi.getImageById(row.id)
    const detail = response?.data || {}
    Object.assign(row, detail)
    row.__detailLoaded = true
  } catch (error) {
    ElMessage.error(error?.message || 'Failed to load image detail')
  } finally {
    row.__detailLoading = false
  }
}

const ensureBinaryLoaded = async (row) => {
  if (!row?.id || row.__binaryLoading || row.__imageObjectUrl || row?.imageBase64) {
    return
  }

  if (!canDownloadImageTask(row?.status) || !row?.imageUrl) {
    return
  }

  row.__binaryLoading = true
  try {
    const response = await imageApi.getImageBinary(row.id)
    const blob = response?.data
    if (!(blob instanceof Blob)) {
      throw new Error('invalid image binary response')
    }

    revokeRowObjectUrl(row)
    row.__imageObjectUrl = URL.createObjectURL(blob)
  } catch (error) {
    ElMessage.error(error?.message || 'Failed to load image preview')
  } finally {
    row.__binaryLoading = false
  }
}

const ensureRowVisual = async (row) => {
  if (!row) {
    return
  }

  if (!row.__detailLoaded) {
    await ensureRowDetail(row)
  }

  if (!row.imageBase64) {
    await ensureBinaryLoaded(row)
  }
}

const handleExpandChange = (row, expandedRows) => {
  const expanded = Array.isArray(expandedRows) && expandedRows.some(item => item.id === row.id)
  if (expanded) {
    ensureRowVisual(row)
  }
}

const handleRowClick = (row, column, event) => {
  const target = event?.target
  if (!(target instanceof HTMLElement)) {
    return
  }

  if (target.closest('.el-button, .el-table__expand-icon, .el-image, .el-image__inner, a, button')) {
    return
  }

  tableRef.value?.toggleRowExpansion(row)
}

const handleDownload = async (row) => {
  if (!buildImageUrl(row)) {
    await ensureRowVisual(row)
  }

  const imageUrl = buildImageUrl(row)
  if (!imageUrl) {
    ElMessage.warning('No image available for this record')
    return
  }

  const link = document.createElement('a')
  link.href = imageUrl
  link.download = `generated_${row.requestId || row.id}.png`
  link.click()
  ElMessage.success('Download started')
}

const handleCancel = async (row) => {
  if (!row?.id || row.__actionType) {
    return
  }

  try {
    await ElMessageBox.confirm('Cancel this task?', 'Confirm', {
      confirmButtonText: 'Cancel Task',
      cancelButtonText: 'Keep Running',
      type: 'warning'
    })
  } catch (error) {
    return
  }

  row.__actionType = 'cancel'
  try {
    await imageApi.cancelImage(row.id)
    ElMessage.success('Task cancelled')
    await loadHistory()
  } catch (error) {
    console.error('Cancel task failed:', error)
  } finally {
    row.__actionType = ''
  }
}

const handleRetry = async (row) => {
  if (!row?.id || row.__actionType) {
    return
  }

  row.__actionType = 'retry'
  try {
    await imageApi.retryImage(row.id)
    ElMessage.success('Task requeued')
    currentPage.value = 1
    await loadHistory()
  } catch (error) {
    console.error('Retry task failed:', error)
  } finally {
    row.__actionType = ''
  }
}

const handleDelete = async (row) => {
  if (!row?.id || row.__actionType || !canDeleteTask(row)) {
    return
  }

  try {
    await ElMessageBox.confirm('Delete this record?', 'Confirm', {
      confirmButtonText: 'Delete',
      cancelButtonText: 'Cancel',
      type: 'warning'
    })
  } catch (error) {
    return
  }

  row.__actionType = 'delete'
  try {
    await imageApi.deleteImage(row.id)
    ElMessage.success('Deleted successfully')

    if (historyList.value.length === 1 && currentPage.value > 1) {
      currentPage.value -= 1
    }

    await loadHistory()
  } catch (error) {
    console.error('Delete task failed:', error)
  } finally {
    row.__actionType = ''
  }
}

const canCancelTask = (row) => canCancelImageTask(row?.status)

const canRetryTask = (row) => canRetryImageTask(row)

const canDownloadTask = (row) => canDownloadImageTask(row?.status)

const canDeleteTask = (row) => isImageTerminalStatus(row?.status)

const getStatusType = (status) => getImageStatusTagType(status)

const parseDate = (dateString) => {
  if (!dateString) {
    return null
  }

  const normalized = typeof dateString === 'string' ? dateString.replace(' ', 'T') : dateString
  const date = new Date(normalized)
  return Number.isNaN(date.getTime()) ? null : date
}

const formatDate = (dateString) => {
  const date = parseDate(dateString)
  return date ? date.toLocaleString('zh-CN') : '-'
}

onMounted(() => {
  loadHistory()
  socketUnsubscribe = subscribeTaskEvents(handleTaskSocketEvent)
})

onBeforeUnmount(() => {
  clearRefreshTimer()
  socketUnsubscribe?.()
  socketUnsubscribe = null
  revokeAllObjectUrls()
})
</script>

<style scoped>
.image-history {
  padding: 20px;
  max-width: 1400px;
  margin: 0 auto;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
}

.title {
  font-size: 18px;
  font-weight: bold;
}

.subtitle {
  margin: 6px 0 0;
  font-size: 13px;
  color: #d97706;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

.expand-content {
  padding: 20px;
  display: flex;
  gap: 20px;
}

.skeleton-content {
  align-items: stretch;
}

.image-preview {
  flex: 1;
  display: flex;
  justify-content: center;
  align-items: center;
  background: #f5f7fa;
  border-radius: 8px;
  padding: 10px;
}

.skeleton-preview {
  min-height: 320px;
}

.preview-skeleton {
  width: 100%;
  height: 320px;
}

.details {
  flex: 1;
}

.skeleton-title {
  width: 55%;
  height: 28px;
  margin-bottom: 24px;
}

.skeleton-line {
  width: 100%;
  height: 16px;
  margin-bottom: 14px;
}

.skeleton-line.short {
  width: 72%;
}

.details p {
  margin: 10px 0;
  line-height: 1.6;
}

.row-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

:deep(.el-table__row) {
  cursor: pointer;
}

:deep(.el-table__row .el-button) {
  cursor: pointer;
}

:deep(.el-table__row .el-button:focus-visible) {
  outline: none;
}

@media (max-width: 768px) {
  .card-header {
    flex-direction: column;
    align-items: stretch;
  }

  .header-actions {
    width: 100%;
  }

  .expand-content {
    flex-direction: column;
  }
}
</style>

