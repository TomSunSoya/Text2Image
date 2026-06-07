<template>
  <el-result
    v-if="runtimeError"
    icon="error"
    title="页面加载失败"
    :sub-title="runtimeError"
    class="app-error"
  >
    <template #extra>
      <el-button type="primary" @click="resetError">重新加载</el-button>
    </template>
  </el-result>
  <router-view v-else />
</template>

<script setup>
import { ref, onErrorCaptured } from 'vue';

const runtimeError = ref('');

const resetError = () => {
  runtimeError.value = '';
  window.location.reload();
};

onErrorCaptured((error) => {
  runtimeError.value = error?.message || '未知错误';
  return false;
});
</script>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family:
    'SF Pro Display',
    -apple-system,
    BlinkMacSystemFont,
    'Segoe UI',
    Roboto,
    sans-serif;
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
}

#app {
  min-height: 100vh;
}

.app-error {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
}

/* 自定义滚动条 */
::-webkit-scrollbar {
  width: 8px;
  height: 8px;
}

::-webkit-scrollbar-track {
  background: #f1f1f1;
  border-radius: 4px;
}

::-webkit-scrollbar-thumb {
  background: #888;
  border-radius: 4px;
}

::-webkit-scrollbar-thumb:hover {
  background: #555;
}
</style>
