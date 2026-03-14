<template>
  <div class="image-history">
    <el-card>
      <template #header>
        <div class="card-header">
          <span class="title">Generation History</span>
          <el-button type="primary" size="small" @click="loadHistory">Refresh</el-button>
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
                  <p><strong>Params:</strong> {{ row.width }} x {{ row.height }}, steps: {{ row.numSteps }}</p>
                  <p v-if="row.errorMessage"><strong>Error:</strong> {{ row.errorMessage }}</p>
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

        <el-table-column label="Actions" width="170">
          <template #default="{ row }">
            <el-button
              type="primary"
              size="small"
              :disabled="row.__detailLoading || row.__binaryLoading"
              @click.stop="handleDownload(row)"
            >
              Download
            </el-button>
            <el-button type="danger" size="small" @click.stop="handleDelete(row)">
              Delete
            </el-button>
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
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { imageApi } from '@/api/image'

const tableRef = ref(null)
const loading = ref(false)
const historyList = ref([])
const currentPage = ref(1)
const pageSize = ref(10)
const total = ref(0)

onMounted(() => {
  loadHistory()
})

const normalizeRow = (item) => ({
  ...item,
  __detailLoaded: Boolean(item?.imageBase64),
  __detailLoading: false,
  __binaryLoading: false,
  __imageObjectUrl: ''
})

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

const loadHistory = async () => {
  loading.value = true
  try {
    const response = await imageApi.getMyImages({
      page: currentPage.value - 1,
      size: pageSize.value
    })
    const rawList = response?.data?.content || []
    revokeAllObjectUrls()
    historyList.value = rawList.map(normalizeRow)
    total.value = response?.data?.totalElements || 0
  } catch (error) {
    ElMessage.error('Failed to load history')
  } finally {
    loading.value = false
  }
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
    ElMessage.error('Failed to load image detail')
  } finally {
    row.__detailLoading = false
  }
}

const ensureBinaryLoaded = async (row) => {
  if (!row?.id || row.__binaryLoading || row.__imageObjectUrl || row?.imageBase64) {
    return
  }

  if (String(row?.status || '').toLowerCase() !== 'success' || !row?.imageUrl) {
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

const handleDelete = async (row) => {
  try {
    await ElMessageBox.confirm('Delete this record?', 'Confirm', {
      confirmButtonText: 'Delete',
      cancelButtonText: 'Cancel',
      type: 'warning'
    })

    await imageApi.deleteImage(row.id)
    ElMessage.success('Deleted successfully')
    loadHistory()
  } catch (error) {
    if (error !== 'cancel') {
      ElMessage.error('Delete failed')
    }
  }
}

const getStatusType = (status) => {
  const statusMap = {
    success: 'success',
    failed: 'danger',
    generating: 'warning',
    queued: 'warning',
    pending: 'info'
  }
  return statusMap[String(status || '').toLowerCase()] || 'info'
}

const formatDate = (dateString) => {
  if (!dateString) return '-'
  const date = new Date(dateString)
  return date.toLocaleString('zh-CN')
}

onBeforeUnmount(() => {
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
}

.title {
  font-size: 18px;
  font-weight: bold;
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

:deep(.el-table__row) {
  cursor: pointer;
}

:deep(.el-table__row .el-button) {
  cursor: pointer;
}

:deep(.el-table__row .el-button:focus-visible) {
  outline: none;
}
</style>

