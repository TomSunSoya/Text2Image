import { defineStore } from 'pinia'
import { ref } from 'vue'

export const useImageStore = defineStore('image', () => {
  const currentImage = ref(null)
  const imageHistory = ref([])
  const isGenerating = ref(false)

  function setCurrentImage(image) {
    currentImage.value = image
  }

  function addToHistory(image) {
    imageHistory.value.unshift(image)
    // 限制历史记录数量
    if (imageHistory.value.length > 50) {
      imageHistory.value = imageHistory.value.slice(0, 50)
    }
  }

  function setGenerating(status) {
    isGenerating.value = status
  }

  function clearHistory() {
    imageHistory.value = []
  }

  function removeFromHistory(id) {
    const index = imageHistory.value.findIndex(img => img.id === id)
    if (index > -1) {
      imageHistory.value.splice(index, 1)
    }
  }

  return {
    currentImage,
    imageHistory,
    isGenerating,
    setCurrentImage,
    addToHistory,
    setGenerating,
    clearHistory,
    removeFromHistory
  }
})