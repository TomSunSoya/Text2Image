<template>
  <div class="result-page">
    <el-result
      :icon="isForbidden ? 'warning' : 'info'"
      :title="isForbidden ? '403' : '404'"
      :sub-title="isForbidden ? '没有权限访问该页面' : '页面不存在'"
    >
      <template #extra>
        <el-button type="primary" @click="router.push('/')">返回首页</el-button>
      </template>
    </el-result>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';

const route = useRoute();
const router = useRouter();
const isForbidden = computed(() => route.path === '/403');
</script>

<style scoped>
.result-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #f5f7fb;
}
</style>
