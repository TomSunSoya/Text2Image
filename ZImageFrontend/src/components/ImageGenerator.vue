<template>
  <div class="image-generator">
    <el-card class="generator-card">
      <template #header>
        <div class="card-header">
          <span class="title">AI Image Generator</span>
          <el-tag :type="healthStatus === 'healthy' ? 'success' : 'danger'">
            {{ healthStatus === 'healthy' ? 'Service Healthy' : 'Service Unhealthy' }}
          </el-tag>
        </div>
      </template>

      <el-form ref="formRef" :model="form" :rules="rules" label-width="120px">
        <el-form-item label="Prompt" prop="prompt">
          <el-input
            v-model="form.prompt"
            type="textarea"
            :rows="4"
            placeholder="Describe the image to generate"
            maxlength="1000"
            show-word-limit
          />
        </el-form-item>

        <el-form-item label="Negative Prompt">
          <el-input
            v-model="form.negativePrompt"
            type="textarea"
            :rows="2"
            placeholder="Describe what should not appear"
            maxlength="500"
            show-word-limit
          />
        </el-form-item>

        <el-row :gutter="20">
          <el-col :span="8">
            <el-form-item label="Steps" prop="numSteps">
              <el-input-number
                v-model="form.numSteps"
                :min="1"
                :max="50"
                controls-position="right"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>

          <el-col :span="8">
            <el-form-item label="Width" prop="width">
              <el-input-number
                v-model="form.width"
                :min="512"
                :max="2048"
                :step="64"
                controls-position="right"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>

          <el-col :span="8">
            <el-form-item label="Height" prop="height">
              <el-input-number
                v-model="form.height"
                :min="512"
                :max="2048"
                :step="64"
                controls-position="right"
                style="width: 100%"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item label="Seed">
          <el-input-number
            v-model="form.seed"
            :min="0"
            controls-position="right"
            placeholder="Empty for random"
            style="width: 200px"
          />
          <el-button style="margin-left: 10px" size="small" @click="form.seed = null">
            Random
          </el-button>
        </el-form-item>

        <el-form-item>
          <el-button type="primary" :loading="loading" :disabled="loading" size="large" @click="handleGenerate">
            {{ loading ? 'Generating...' : 'Generate Image' }}
          </el-button>
          <el-button size="large" @click="handleReset">Reset</el-button>
        </el-form-item>
      </el-form>

      <el-divider v-if="currentImage" />

      <div v-if="loading" class="loading-container">
        <el-progress :percentage="progressPercentage" :stroke-width="20" striped striped-flow />
        <p class="loading-text">{{ loadingStatusText }}</p>
      </div>

      <div v-if="currentImage" class="result-container" :class="{ 'is-loading': loading }">
        <div class="image-wrapper">
          <el-image
            v-if="imageDataUrl"
            :src="imageDataUrl"
            fit="contain"
            :preview-src-list="[imageDataUrl]"
            preview-teleported
            :z-index="3000"
            class="generated-image"
          >
            <template #error>
              <div class="image-error">
                <el-icon><Picture /></el-icon>
                <span>Image load failed</span>
              </div>
            </template>
          </el-image>
          <div v-else-if="imageBinaryLoading" class="image-error">
            <el-icon><Picture /></el-icon>
            <span>Loading protected image preview...</span>
          </div>
          <div v-else class="image-error">
            <el-icon><Picture /></el-icon>
            <span>No image data yet. Current status: {{ currentImage.status || 'unknown' }}</span>
          </div>
        </div>

        <div class="image-info">
          <el-descriptions :column="2" border>
            <el-descriptions-item label="Status">
              <el-tag :type="statusTagType">
                {{ currentImage.status || '-' }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="Generation Time">
              {{ currentImage.generationTime ? `${currentImage.generationTime.toFixed(2)}s` : '-' }}
            </el-descriptions-item>
            <el-descriptions-item label="Request ID">
              {{ currentImage.requestId || '-' }}
            </el-descriptions-item>
            <el-descriptions-item label="Retry">
              {{ `${currentImage.retryCount || 0} / ${currentImage.maxRetries || 0}` }}
            </el-descriptions-item>
            <el-descriptions-item label="Size">
              {{ currentImage.width || form.width }} x {{ currentImage.height || form.height }}
            </el-descriptions-item>
            <el-descriptions-item label="Steps">
              {{ currentImage.numSteps || form.numSteps }}
            </el-descriptions-item>
            <el-descriptions-item label="Prompt" :span="2">
              {{ currentImage.prompt || form.prompt }}
            </el-descriptions-item>
            <el-descriptions-item v-if="currentImage.failureCode" label="Failure Code" :span="2">
              {{ currentImage.failureCode }}
            </el-descriptions-item>
            <el-descriptions-item v-if="currentImage.errorMessage" label="Error" :span="2">
              {{ currentImage.errorMessage }}
            </el-descriptions-item>
          </el-descriptions>

          <div class="action-buttons">
            <el-button
              v-if="currentTaskCanCancel"
              type="warning"
              plain
              :loading="cancelling"
              @click="handleCancelCurrentTask"
            >
              {{ cancelling ? 'Cancelling...' : 'Cancel Task' }}
            </el-button>
            <el-button type="primary" :disabled="!canDownloadCurrentImage || imageBinaryLoading" @click="handleDownload">
              <el-icon><Download /></el-icon>
              Download
            </el-button>
            <el-button :disabled="!(currentImage.prompt || form.prompt)" @click="handleCopyPrompt">
              <el-icon><CopyDocument /></el-icon>
              Copy Prompt
            </el-button>
          </div>
        </div>
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Picture, Download, CopyDocument } from '@element-plus/icons-vue'
import { imageApi } from '@/api/image'
import { useImageStore } from '@/stores/image'
import { subscribeTaskEvents } from '@/utils/taskSocket'
import {
  canCancelImageTask,
  canDownloadImageTask,
  getImageStatusTagType,
  isImageTerminalStatus,
  normalizeImageStatus
} from '@/utils/imageTask'

const POLL_TIMEOUT_MS = 8 * 60 * 1000
const TASK_POLL_INTERVAL_MS = 2500

const imageStore = useImageStore()
const formRef = ref(null)
const loading = ref(false)
const cancelling = ref(false)
const progress = ref(0)
const progressPercentage = computed(() => Math.floor(progress.value))
const healthStatus = ref('unknown')
const currentImage = ref(null)
const imageBinaryLoading = ref(false)
const binaryImageUrl = ref('')
let activeBinaryToken = 0
let activeTaskToken = 0
let socketUnsubscribe = null
let progressTimer = 0
let taskPollTimer = 0
let taskTimeoutId = 0
let taskResolve = null
let taskReject = null

const statusTagType = computed(() => {
  return getImageStatusTagType(currentImage.value?.status)
})

const loadingStatusText = computed(() => {
  const status = normalizeImageStatus(currentImage.value?.status)

  if (status === 'queued' || status === 'pending') {
    return 'Task queued, waiting for server push...'
  }

  if (status === 'generating') {
    return 'Image is generating, waiting for worker update...'
  }

  return 'Waiting for task update from server...'
})

const currentTaskCanCancel = computed(() => canCancelImageTask(currentImage.value?.status))

const canDownloadCurrentImage = computed(() => {
  if (!canDownloadImageTask(currentImage.value?.status)) {
    return false
  }

  return Boolean(imageDataUrl.value || currentImage.value?.imageUrl)
})

const form = reactive({
  prompt: '',
  negativePrompt: '',
  numSteps: 8,
  width: 768,
  height: 768,
  seed: null
})

const rules = {
  prompt: [
    { required: true, message: 'Please input prompt', trigger: 'blur' },
    { min: 3, message: 'Prompt must be at least 3 chars', trigger: 'blur' }
  ],
  numSteps: [
    { required: true, message: 'Please input steps', trigger: 'blur' }
  ],
  width: [
    { required: true, message: 'Please input width', trigger: 'blur' }
  ],
  height: [
    { required: true, message: 'Please input height', trigger: 'blur' }
  ]
}

const imageDataUrl = computed(() => {
  const rawBase64 = currentImage.value?.imageBase64
  if (rawBase64 && typeof rawBase64 === 'string') {
    const trimmed = rawBase64.replace(/\s/g, '')
    if (trimmed) {
      return `data:image/png;base64,${trimmed}`
    }
  }

  if (binaryImageUrl.value) {
    return binaryImageUrl.value
  }

  return ''
})

const revokeBinaryImageUrl = () => {
  if (binaryImageUrl.value && binaryImageUrl.value.startsWith('blob:')) {
    URL.revokeObjectURL(binaryImageUrl.value)
  }
  binaryImageUrl.value = ''
}

const clearTaskTimeout = () => {
  if (taskTimeoutId) {
    window.clearTimeout(taskTimeoutId)
    taskTimeoutId = 0
  }
}

const clearTaskPolling = () => {
  if (taskPollTimer) {
    window.clearTimeout(taskPollTimer)
    taskPollTimer = 0
  }
}

const clearTaskWaiter = ({ rejectPending = true } = {}) => {
  clearTaskTimeout()
  clearTaskPolling()
  const reject = rejectPending ? taskReject : null
  taskResolve = null
  taskReject = null
  if (reject) {
    reject(new Error('superseded'))
  }
}

const resolveTaskWaiter = (task) => {
  if (!taskResolve) {
    return
  }

  const resolve = taskResolve
  clearTaskWaiter({ rejectPending: false })
  resolve(task)
}

const rejectTaskWaiter = (error) => {
  if (!taskReject) {
    return
  }

  const reject = taskReject
  clearTaskWaiter({ rejectPending: false })
  reject(error)
}

const stopProgressAnimation = () => {
  if (progressTimer) {
    window.clearInterval(progressTimer)
    progressTimer = 0
  }
}

const syncProgressFromStatus = (status) => {
  const normalized = normalizeImageStatus(status)
  if (normalized === 'queued' || normalized === 'pending') {
    progress.value = Math.max(progress.value, 15)
    return
  }

  if (normalized === 'generating') {
    progress.value = Math.max(progress.value, 70)
    return
  }

  if (isImageTerminalStatus(normalized)) {
    progress.value = 100
  }
}

const startProgressAnimation = () => {
  stopProgressAnimation()

  progressTimer = window.setInterval(() => {
    if (!loading.value) {
      return
    }

    const status = normalizeImageStatus(currentImage.value?.status)
    const cap = status === 'generating' ? 92 : 45
    progress.value = Math.min(cap, progress.value + 1)
  }, 800)
}

const resetBinaryPreview = () => {
  activeBinaryToken += 1
  imageBinaryLoading.value = false
  revokeBinaryImageUrl()
}

const loadBinaryPreview = async (image) => {
  if (!image?.id || !image?.imageUrl || image?.imageBase64 || normalizeImageStatus(image?.status) !== 'success') {
    return
  }

  const binaryToken = ++activeBinaryToken
  imageBinaryLoading.value = true

  try {
    const response = await imageApi.getImageBinary(image.id)
    if (binaryToken !== activeBinaryToken) {
      return
    }

    const blob = response?.data
    if (!(blob instanceof Blob)) {
      throw new Error('invalid image binary response')
    }

    revokeBinaryImageUrl()
    binaryImageUrl.value = URL.createObjectURL(blob)
  } finally {
    if (binaryToken === activeBinaryToken) {
      imageBinaryLoading.value = false
    }
  }
}

const applyTaskUpdate = (task) => {
  currentImage.value = {
    ...(currentImage.value || {}),
    ...(task || {})
  }
  imageStore.setCurrentImage(currentImage.value)
  syncProgressFromStatus(currentImage.value?.status)
  return currentImage.value
}

const checkHealth = async () => {
  try {
    const response = await imageApi.checkHealth()
    const rawStatus = normalizeImageStatus(response?.data?.status)
    healthStatus.value = ['healthy', 'ok', 'success'].includes(rawStatus) ? 'healthy' : 'unhealthy'
  } catch (error) {
    healthStatus.value = 'unhealthy'
  }
}

const waitForTaskCompletion = (taskToken) => new Promise((resolve, reject) => {
  taskResolve = resolve
  taskReject = reject
  clearTaskTimeout()
  taskTimeoutId = window.setTimeout(() => {
    if (taskToken !== activeTaskToken) {
      return
    }

    rejectTaskWaiter(new Error('generation timeout, please check history later'))
  }, POLL_TIMEOUT_MS)
})

const scheduleTaskStatusPoll = (taskId, taskToken) => {
  clearTaskPolling()

  taskPollTimer = window.setTimeout(async () => {
    if (taskToken !== activeTaskToken || !loading.value || currentImage.value?.id !== taskId) {
      return
    }

    try {
      const response = await imageApi.getImageStatus(taskId)
      if (taskToken !== activeTaskToken || currentImage.value?.id !== taskId) {
        return
      }

      const latest = applyTaskUpdate(response?.data || {})
      if (isImageTerminalStatus(latest.status)) {
        stopProgressAnimation()
        resolveTaskWaiter(latest)
        return
      }
    } catch (error) {
      console.error('Task status poll failed:', error)
    }

    if (taskToken === activeTaskToken && loading.value && currentImage.value?.id === taskId) {
      scheduleTaskStatusPoll(taskId, taskToken)
    }
  }, TASK_POLL_INTERVAL_MS)
}

const handleTaskSocketEvent = (event) => {
  if (event?.type !== 'image.task.updated') {
    return
  }

  const task = event.task || {}
  if (!task.id) {
    return
  }

  if (currentImage.value?.id !== task.id) {
    return
  }

  const latest = applyTaskUpdate(task)
  const status = normalizeImageStatus(latest.status)
  if (!isImageTerminalStatus(status)) {
    return
  }

  stopProgressAnimation()
  resolveTaskWaiter(latest)
}

const handleGenerate = async () => {
  const taskToken = ++activeTaskToken

  try {
    await formRef.value.validate()
    loading.value = true
    cancelling.value = false
    progress.value = 10
    currentImage.value = null
    imageStore.setCurrentImage(null)
    resetBinaryPreview()
    clearTaskWaiter()
    startProgressAnimation()

    const submitResponse = await imageApi.generateImage(form)
    const queuedTask = submitResponse?.data || {}
    const imageId = queuedTask.id

    if (!imageId) {
      throw new Error('missing queued task id from backend')
    }

    applyTaskUpdate({
      ...queuedTask,
      prompt: form.prompt,
      negativePrompt: form.negativePrompt,
      numSteps: form.numSteps,
      width: form.width,
      height: form.height,
      seed: form.seed
    })

    const initialStatus = normalizeImageStatus(currentImage.value?.status)
    const completionPromise = isImageTerminalStatus(initialStatus)
      ? Promise.resolve(currentImage.value)
      : waitForTaskCompletion(taskToken)

    if (!isImageTerminalStatus(initialStatus)) {
      scheduleTaskStatusPoll(imageId, taskToken)
    }

    const finalImage = await completionPromise
    if (taskToken !== activeTaskToken) {
      return
    }

    stopProgressAnimation()
    progress.value = 100
    imageStore.setCurrentImage(finalImage)
    currentImage.value = finalImage

    const finalStatus = normalizeImageStatus(finalImage.status)
    if (finalStatus === 'success') {
      imageStore.addToHistory(finalImage)
      ElMessage.success('Image generated successfully')
      return
    }

    if (finalStatus === 'cancelled') {
      ElMessage.warning('Task cancelled')
      return
    }

    if (finalStatus === 'timeout') {
      ElMessage.error(finalImage.errorMessage || 'Image generation timed out')
      return
    }

    throw new Error(finalImage.errorMessage || 'image generation failed')
  } catch (error) {
    if (taskToken !== activeTaskToken) {
      return
    }

    if (!loading.value) {
      return
    }

    clearTaskWaiter()
    stopProgressAnimation()
    progress.value = 0
    ElMessage.error('Generation failed: ' + (error?.message || 'unknown error'))
  } finally {
    if (taskToken === activeTaskToken) {
      loading.value = false
      cancelling.value = false
    }
  }
}

const handleCancelCurrentTask = async () => {
  if (!currentTaskCanCancel.value || cancelling.value || !currentImage.value?.id) {
    return
  }

  cancelling.value = true

  try {
    const response = await imageApi.cancelImage(currentImage.value.id)
    stopProgressAnimation()
    loading.value = false
    progress.value = 100
    const latest = applyTaskUpdate(response?.data || {})
    resolveTaskWaiter(latest)
    ElMessage.warning('Task cancelled')
  } catch (error) {
    console.error('Cancel task failed:', error)
  } finally {
    cancelling.value = false
  }
}

const handleReset = () => {
  activeTaskToken += 1
  loading.value = false
  cancelling.value = false
  progress.value = 0
  currentImage.value = null
  clearTaskWaiter()
  stopProgressAnimation()
  resetBinaryPreview()
  imageStore.setCurrentImage(null)
  formRef.value?.resetFields()
}

const handleDownload = async () => {
  try {
    if (!currentImage.value) {
      ElMessage.warning('No image data available yet')
      return
    }

    if (!imageDataUrl.value && currentImage.value.imageUrl) {
      await loadBinaryPreview(currentImage.value)
    }

    if (!imageDataUrl.value) {
      ElMessage.warning('No image data available yet')
      return
    }

    const link = document.createElement('a')
    link.href = imageDataUrl.value
    link.download = `generated_${currentImage.value.requestId || Date.now()}.png`
    link.click()
    ElMessage.success('Download started')
  } catch (error) {
    ElMessage.error(error?.message || 'Download failed')
  }
}

const handleCopyPrompt = () => {
  const prompt = currentImage.value?.prompt || form.prompt
  if (!prompt) {
    return
  }

  navigator.clipboard.writeText(prompt)
  ElMessage.success('Prompt copied')
}

onMounted(() => {
  checkHealth()
  socketUnsubscribe = subscribeTaskEvents(handleTaskSocketEvent)
})

watch(
  () => ({
    id: currentImage.value?.id,
    status: currentImage.value?.status,
    imageUrl: currentImage.value?.imageUrl,
    imageBase64: currentImage.value?.imageBase64
  }),
  async (next, previous) => {
    if (next.id !== previous?.id || next.imageBase64) {
      resetBinaryPreview()
    }

    if (!next.id || !next.imageUrl || next.imageBase64 || normalizeImageStatus(next.status) !== 'success') {
      return
    }

    try {
      await loadBinaryPreview(currentImage.value)
    } catch (error) {
      console.error('Failed to load protected image preview:', error)
    }
  }
)

onBeforeUnmount(() => {
  activeTaskToken += 1
  clearTaskWaiter()
  stopProgressAnimation()
  socketUnsubscribe?.()
  socketUnsubscribe = null
  resetBinaryPreview()
})
</script>

<style scoped>
.image-generator {
  padding: 20px;
  max-width: 1200px;
  margin: 0 auto;
}

.generator-card {
  box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.title {
  font-size: 20px;
  font-weight: bold;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}

.loading-container {
  text-align: center;
  padding: 40px;
}

.loading-text {
  margin-top: 20px;
  color: #666;
  font-size: 16px;
}

.result-container {
  margin-top: 20px;
}

.result-container.is-loading {
  opacity: 0.9;
}

.image-wrapper {
  display: flex;
  justify-content: center;
  margin-bottom: 20px;
  background: #f5f7fa;
  padding: 20px;
  border-radius: 8px;
}

.generated-image {
  max-width: 100%;
  max-height: 600px;
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.image-error {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 200px;
  color: #909399;
  gap: 8px;
}

.image-info {
  margin-top: 20px;
}

.action-buttons {
  margin-top: 20px;
  display: flex;
  gap: 10px;
  justify-content: center;
}

:deep(.el-input-number) {
  width: 100%;
}
</style>
