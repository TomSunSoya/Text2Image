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
        <p class="loading-text">Task queued/running, polling status...</p>
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
            <el-descriptions-item label="Size">
              {{ currentImage.width || form.width }} x {{ currentImage.height || form.height }}
            </el-descriptions-item>
            <el-descriptions-item label="Steps">
              {{ currentImage.numSteps || form.numSteps }}
            </el-descriptions-item>
            <el-descriptions-item label="Prompt" :span="2">
              {{ currentImage.prompt || form.prompt }}
            </el-descriptions-item>
            <el-descriptions-item v-if="currentImage.errorMessage" label="Error" :span="2">
              {{ currentImage.errorMessage }}
            </el-descriptions-item>
          </el-descriptions>

          <div class="action-buttons">
            <el-button type="primary" :disabled="!imageDataUrl" @click="handleDownload">
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

const POLL_INTERVAL_MS = 2000
const POLL_TIMEOUT_MS = 8 * 60 * 1000

const imageStore = useImageStore()
const formRef = ref(null)
const loading = ref(false)
const progress = ref(0)
const progressPercentage = computed(() => Math.floor(progress.value))
const healthStatus = ref('unknown')
const currentImage = ref(null)
const imageBinaryLoading = ref(false)
const binaryImageUrl = ref('')
let activePollToken = 0
let activeBinaryToken = 0

const statusTagType = computed(() => {
  const status = String(currentImage.value?.status || '').toLowerCase()
  if (status === 'success') return 'success'
  if (status === 'failed') return 'danger'
  if (status === 'generating' || status === 'queued') return 'warning'
  return 'info'
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

const wait = (ms) => new Promise(resolve => setTimeout(resolve, ms))
const normalizeStatus = (status) => String(status || '').toLowerCase()

const revokeBinaryImageUrl = () => {
  if (binaryImageUrl.value && binaryImageUrl.value.startsWith('blob:')) {
    URL.revokeObjectURL(binaryImageUrl.value)
  }
  binaryImageUrl.value = ''
}

const resetBinaryPreview = () => {
  activeBinaryToken += 1
  imageBinaryLoading.value = false
  revokeBinaryImageUrl()
}

const loadBinaryPreview = async (image) => {
  if (!image?.id || !image?.imageUrl || image?.imageBase64 || normalizeStatus(image?.status) !== 'success') {
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

const checkHealth = async () => {
  try {
    const response = await imageApi.checkHealth()
    const rawStatus = normalizeStatus(response?.data?.status)
    healthStatus.value = ['healthy', 'ok', 'success'].includes(rawStatus) ? 'healthy' : 'unhealthy'
  } catch (error) {
    healthStatus.value = 'unhealthy'
  }
}

const pollImageTask = async (imageId, pollToken) => {
  const start = Date.now()

  while (true) {
    if (pollToken !== activePollToken) {
      throw new Error('poll cancelled')
    }

    const elapsed = Date.now() - start
    if (elapsed > POLL_TIMEOUT_MS) {
      throw new Error('generation timeout, please check history later')
    }

    const response = await imageApi.getImageStatus(imageId)
    const latest = {
      ...(currentImage.value || {}),
      ...(response?.data || {}),
      id: imageId,
      prompt: form.prompt,
      negativePrompt: form.negativePrompt,
      numSteps: form.numSteps,
      width: form.width,
      height: form.height,
      seed: form.seed
    }
    currentImage.value = latest

    const status = normalizeStatus(latest.status)
    if (status === 'success') {
      progress.value = 100
      return latest
    }

    if (status === 'failed') {
      throw new Error(latest.errorMessage || 'image generation failed')
    }

    const ratio = Math.min(elapsed / POLL_TIMEOUT_MS, 1)
    progress.value = Math.max(5, Math.min(95, Math.floor(ratio * 90) + 5))

    await wait(POLL_INTERVAL_MS)
  }
}

const handleGenerate = async () => {
  const pollToken = ++activePollToken

  try {
    await formRef.value.validate()
    loading.value = true
    progress.value = 5
    currentImage.value = null
    resetBinaryPreview()

    const submitResponse = await imageApi.generateImage(form)
    const queuedTask = submitResponse?.data || {}
    const imageId = queuedTask.id

    if (!imageId) {
      throw new Error('missing queued task id from backend')
    }

    currentImage.value = {
      ...queuedTask,
      prompt: form.prompt,
      negativePrompt: form.negativePrompt,
      numSteps: form.numSteps,
      width: form.width,
      height: form.height,
      seed: form.seed
    }

    const finalImage = await pollImageTask(imageId, pollToken)
    imageStore.setCurrentImage(finalImage)
    imageStore.addToHistory(finalImage)
    ElMessage.success('Image generated successfully')
  } catch (error) {
    if (pollToken !== activePollToken) {
      return
    }

    progress.value = 0
    ElMessage.error('Generation failed: ' + (error?.message || 'unknown error'))
  } finally {
    if (pollToken === activePollToken) {
      loading.value = false
    }
  }
}

const handleReset = () => {
  activePollToken += 1
  loading.value = false
  progress.value = 0
  currentImage.value = null
  resetBinaryPreview()
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

    if (!next.id || !next.imageUrl || next.imageBase64 || normalizeStatus(next.status) !== 'success') {
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
  activePollToken += 1
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
