<template>
  <div class="profile-page">
    <section class="profile-shell">
      <header class="profile-header">
        <div>
          <h1>个人中心</h1>
          <p>{{ profile?.nickname || profile?.username || authStore.nickname }}</p>
        </div>
        <el-button :icon="ArrowLeft" @click="router.push('/')">返回首页</el-button>
      </header>

      <el-row :gutter="20">
        <el-col :xs="24" :md="10">
          <el-card class="profile-card" v-loading="loading">
            <template #header>
              <span>账号信息</span>
            </template>
            <el-descriptions :column="1" border>
              <el-descriptions-item label="用户名">
                {{ profile?.username || '-' }}
              </el-descriptions-item>
              <el-descriptions-item label="邮箱">
                {{ profile?.email || '-' }}
              </el-descriptions-item>
              <el-descriptions-item label="角色">
                <el-tag :type="profile?.role === 'admin' ? 'danger' : 'info'">
                  {{ profile?.role || 'user' }}
                </el-tag>
              </el-descriptions-item>
              <el-descriptions-item label="注册时间">
                {{ profile?.createdAt || profile?.created_at || '-' }}
              </el-descriptions-item>
            </el-descriptions>
          </el-card>
        </el-col>

        <el-col :xs="24" :md="14">
          <el-card class="profile-card">
            <template #header>
              <span>修改密码</span>
            </template>
            <el-form
              ref="passwordFormRef"
              :model="passwordForm"
              :rules="passwordRules"
              label-width="96px"
              @submit.prevent
            >
              <el-form-item label="旧密码" prop="oldPassword">
                <el-input
                  v-model="passwordForm.oldPassword"
                  type="password"
                  show-password
                  autocomplete="current-password"
                />
              </el-form-item>
              <el-form-item label="新密码" prop="newPassword">
                <el-input
                  v-model="passwordForm.newPassword"
                  type="password"
                  show-password
                  autocomplete="new-password"
                />
              </el-form-item>
              <el-form-item label="确认密码" prop="confirmPassword">
                <el-input
                  v-model="passwordForm.confirmPassword"
                  type="password"
                  show-password
                  autocomplete="new-password"
                />
              </el-form-item>
              <el-form-item>
                <el-button type="primary" :loading="saving" @click="submitPassword">
                  保存修改
                </el-button>
              </el-form-item>
            </el-form>
          </el-card>
        </el-col>
      </el-row>
    </section>
  </div>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue';
import { useRouter } from 'vue-router';
import { ElMessage, ElMessageBox } from 'element-plus';
import { ArrowLeft } from '@element-plus/icons-vue';
import { authApi } from '@/api/auth';
import { useAuthStore } from '@/stores/auth';

const router = useRouter();
const authStore = useAuthStore();
const loading = ref(false);
const saving = ref(false);
const profile = ref(null);
const passwordFormRef = ref(null);

const passwordForm = reactive({
  oldPassword: '',
  newPassword: '',
  confirmPassword: '',
});

const passwordRules = {
  oldPassword: [{ required: true, message: '请输入旧密码', trigger: 'blur' }],
  newPassword: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 6, message: '新密码至少 6 位', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认新密码', trigger: 'blur' },
    {
      validator: (_rule, value, callback) => {
        if (value !== passwordForm.newPassword) {
          callback(new Error('两次输入的新密码不一致'));
          return;
        }
        callback();
      },
      trigger: 'blur',
    },
  ],
};

const loadProfile = async () => {
  loading.value = true;
  try {
    const response = await authApi.getProfile();
    profile.value = response.data || {};
  } catch (error) {
    ElMessage.error(error.response?.data?.error?.message || error.message || '获取用户信息失败');
  } finally {
    loading.value = false;
  }
};

const submitPassword = async () => {
  await passwordFormRef.value?.validate();
  saving.value = true;
  try {
    await authApi.changePassword({
      old_password: passwordForm.oldPassword,
      new_password: passwordForm.newPassword,
    });
    await ElMessageBox.alert('密码已修改，请重新登录。', '修改成功', {
      confirmButtonText: '确定',
      type: 'success',
    });
    await authStore.logout(false);
  } catch (error) {
    ElMessage.error(error.response?.data?.error?.message || error.message || '修改失败');
  } finally {
    saving.value = false;
  }
};

onMounted(loadProfile);
</script>

<style scoped>
.profile-page {
  min-height: 100vh;
  background: #f5f7fb;
  padding: 32px 20px;
}

.profile-shell {
  max-width: 1120px;
  margin: 0 auto;
}

.profile-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.profile-header h1 {
  margin: 0;
  color: #1f2937;
  font-size: 28px;
}

.profile-header p {
  margin: 6px 0 0;
  color: #6b7280;
}

.profile-card {
  border-radius: 8px;
}

@media (max-width: 768px) {
  .profile-header {
    align-items: flex-start;
    gap: 12px;
    flex-direction: column;
  }
}
</style>
