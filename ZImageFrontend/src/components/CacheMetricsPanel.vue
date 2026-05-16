<template>
  <div class="cache-metrics-panel">
    <el-card class="metrics-card" v-loading="loading && !metrics">
      <template #header>
        <div class="card-header">
          <div>
            <span class="title">缓存指标</span>
            <p class="subtitle">数据自后端启动以来累计，每 10 秒自动刷新。</p>
          </div>
          <div class="header-actions">
            <span class="last-refresh">最后刷新：{{ lastRefreshLabel }}</span>
            <el-button type="primary" plain :icon="Refresh" :loading="loading" @click="loadMetrics">
              刷新
            </el-button>
          </div>
        </div>
      </template>

      <el-alert
        v-if="error"
        class="error-alert"
        type="error"
        :title="error"
        show-icon
        :closable="false"
      />

      <el-empty v-if="!metrics && !loading && !error" description="暂无缓存指标数据" />

      <template v-else-if="metrics">
        <section class="summary-grid">
          <div class="summary-tile">
            <span class="summary-label">总读取数</span>
            <strong>{{ formatNumber(summary.totalReads) }}</strong>
            <small>get 请求累计</small>
          </div>
          <div class="summary-tile hit">
            <span class="summary-label">命中数</span>
            <strong>{{ formatNumber(summary.hits) }}</strong>
            <small>命中率 {{ formatPercent(summary.hitRate) }}</small>
          </div>
          <div class="summary-tile miss">
            <span class="summary-label">未命中数</span>
            <strong>{{ formatNumber(summary.misses) }}</strong>
            <small>缓存可用但无 key</small>
          </div>
          <div class="summary-tile degraded">
            <span class="summary-label">降级数</span>
            <strong>{{ formatNumber(summary.degraded) }}</strong>
            <small>降级率 {{ formatPercent(summary.degradedRate) }}</small>
          </div>
        </section>

        <el-divider />

        <el-table :data="namespaceRows" stripe style="width: 100%">
          <el-table-column label="Namespace" min-width="140">
            <template #default="{ row }">
              <div class="namespace-cell">
                <span class="namespace-dot" :class="row.name" />
                <span>{{ row.label }}</span>
                <el-tag size="small" effect="plain">{{ row.name }}</el-tag>
              </div>
            </template>
          </el-table-column>
          <el-table-column label="Hits" prop="hits" width="120" align="right">
            <template #default="{ row }">{{ formatNumber(row.hits) }}</template>
          </el-table-column>
          <el-table-column label="Misses" prop="misses" width="120" align="right">
            <template #default="{ row }">{{ formatNumber(row.misses) }}</template>
          </el-table-column>
          <el-table-column label="Degraded" prop="degraded" width="130" align="right">
            <template #default="{ row }">{{ formatNumber(row.degraded) }}</template>
          </el-table-column>
          <el-table-column label="Total" prop="total" width="120" align="right">
            <template #default="{ row }">{{ formatNumber(row.total) }}</template>
          </el-table-column>
          <el-table-column label="Hit Rate" width="130" align="right">
            <template #default="{ row }">{{ formatPercent(row.hitRate) }}</template>
          </el-table-column>
          <el-table-column label="Degraded Rate" width="150" align="right">
            <template #default="{ row }">{{ formatPercent(row.degradedRate) }}</template>
          </el-table-column>
        </el-table>
      </template>
    </el-card>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { Refresh } from '@element-plus/icons-vue';
import { metricsApi } from '@/api/metrics';

const REFRESH_INTERVAL_MS = 10000;
const numberFormatter = new Intl.NumberFormat('zh-CN');

const namespaceOrder = ['meta', 'list', 'url', 'other'];
const namespaceLabels = {
  meta: '图片元数据',
  list: '列表分页',
  url: '预签名 URL',
  other: '其他',
};

const metrics = ref(null);
const loading = ref(false);
const error = ref('');
const lastRefreshAt = ref(null);
let refreshTimer = 0;

const toNumber = (value) => {
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : 0;
};

const rate = (numerator, denominator) => (denominator > 0 ? numerator / denominator : null);

const namespaceRows = computed(() => {
  const namespaces = metrics.value?.namespaces || {};
  return namespaceOrder.map((name) => {
    const counters = namespaces[name] || {};
    const hits = toNumber(counters.hits);
    const misses = toNumber(counters.misses);
    const degraded = toNumber(counters.degraded);
    const total = hits + misses + degraded;
    return {
      name,
      label: namespaceLabels[name],
      hits,
      misses,
      degraded,
      total,
      hitRate: rate(hits, hits + misses),
      degradedRate: rate(degraded, total),
    };
  });
});

const summary = computed(() => {
  const totals = namespaceRows.value.reduce(
    (acc, row) => ({
      hits: acc.hits + row.hits,
      misses: acc.misses + row.misses,
      degraded: acc.degraded + row.degraded,
      totalReads: acc.totalReads + row.total,
    }),
    { hits: 0, misses: 0, degraded: 0, totalReads: 0 }
  );

  return {
    ...totals,
    hitRate: rate(totals.hits, totals.hits + totals.misses),
    degradedRate: rate(totals.degraded, totals.totalReads),
  };
});

const lastRefreshLabel = computed(() => {
  return lastRefreshAt.value ? lastRefreshAt.value.toLocaleString('zh-CN') : '-';
});

const formatNumber = (value) => numberFormatter.format(value);

const formatPercent = (value) => {
  if (value === null || value === undefined) {
    return '-';
  }
  return `${(value * 100).toFixed(1)}%`;
};

const loadMetrics = async () => {
  if (loading.value) {
    return;
  }

  loading.value = true;
  try {
    const response = await metricsApi.getCacheMetrics();
    metrics.value = response?.data || null;
    lastRefreshAt.value = new Date();
    error.value = '';
  } catch (err) {
    error.value = err?.message || '缓存指标加载失败';
  } finally {
    loading.value = false;
  }
};

onMounted(() => {
  loadMetrics();
  refreshTimer = window.setInterval(loadMetrics, REFRESH_INTERVAL_MS);
});

onBeforeUnmount(() => {
  if (refreshTimer) {
    window.clearInterval(refreshTimer);
    refreshTimer = 0;
  }
});
</script>

<style scoped>
.cache-metrics-panel {
  padding: 20px;
  max-width: 1400px;
  margin: 0 auto;
}

.metrics-card {
  box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
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
  color: #6b7280;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

.last-refresh {
  font-size: 13px;
  color: #6b7280;
}

.error-alert {
  margin-bottom: 16px;
}

.summary-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 16px;
}

.summary-tile {
  min-height: 118px;
  padding: 18px;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  background: #f9fafb;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

.summary-label {
  font-size: 13px;
  color: #6b7280;
}

.summary-tile strong {
  margin: 10px 0 6px;
  font-size: 32px;
  line-height: 1;
  color: #111827;
}

.summary-tile small {
  color: #6b7280;
}

.summary-tile.hit {
  border-color: #bbf7d0;
  background: #f0fdf4;
}

.summary-tile.miss {
  border-color: #fed7aa;
  background: #fff7ed;
}

.summary-tile.degraded {
  border-color: #fecaca;
  background: #fef2f2;
}

.namespace-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.namespace-dot {
  width: 10px;
  height: 10px;
  border-radius: 999px;
  background: #6b7280;
}

.namespace-dot.meta {
  background: #3b82f6;
}

.namespace-dot.list {
  background: #22c55e;
}

.namespace-dot.url {
  background: #f59e0b;
}

.namespace-dot.other {
  background: #64748b;
}

@media (max-width: 1024px) {
  .summary-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 768px) {
  .card-header,
  .header-actions {
    flex-direction: column;
    align-items: stretch;
  }

  .summary-grid {
    grid-template-columns: 1fr;
  }
}
</style>
