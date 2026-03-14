# ZImage Workspace（总览）

## 1. 项目范围
本 README 对应当前主链路：
- `ZImageFrontend/`：前端（Vue 3 + Vite + Element Plus）
- `Backend/`：后端（C++20 + Drogon + MySQL）
- `PythonProject/`：模型服务（FastAPI + Diffusers）

不在本文档范围内：
- `PythonProject1/`
- `ZImageBackend/`

## 2. 架构与调用链
整体是三层结构：

1. 前端 `ZImageFrontend` 调用 `/api/*`
2. 后端 `Backend` 提供鉴权、历史记录、任务状态，并转发模型生成请求
3. Python 服务 `PythonProject` 执行模型推理并返回结果

默认端口约定：
- 前端开发服务：`3000`
- 后端 API：`8080`
- Python 模型服务：`8081`

主流程（生成图片）：

1. 前端 `POST /api/images`
2. 后端入库状态为 `queued`，进入异步队列
3. 后端 worker 调 Python `POST /generate`
4. Python 返回 `image_url`（后端可转成 `imageBase64`）
5. 后端更新 MySQL 状态（`success/failed`）
6. 前端轮询 `GET /api/images/{id}/status` 展示结果

## 3. 核心能力

### 3.1 认证与用户
- 注册：`POST /api/auth/register`
- 登录：`POST /api/auth/login`
- 登录后使用 Bearer Token 访问图片接口

### 3.2 图片生成与历史
- 创建任务：`POST /api/images`
- 我的历史：`GET /api/images/my-list`
- 按状态筛选：`GET /api/images/my-list/status/{status}`
- 查询详情：`GET /api/images/{id}`
- 查询状态：`GET /api/images/{id}/status`
- 删除记录：`DELETE /api/images/{id}`

历史记录已使用 MySQL 持久化，非内存临时存储。

### 3.3 健康检查
- 后端自身：`GET /health`
- 后端代理模型健康：`GET /api/images/health`
- Python 模型健康：`GET /health`（运行在 8081）

## 4. 目录说明
- `ZImageFrontend/src/`：页面、组件、路由、Pinia、API 封装
- `Backend/src/controllers/`：HTTP 接口层
- `Backend/src/services/`：业务逻辑、异步队列、外部调用
- `Backend/src/database/`：MySQL 访问与仓储
- `PythonProject/model_service.py`：FastAPI 模型服务入口
- `PythonProject/start_model_service.py`：模型服务启动脚本

## 5. 快速启动（开发）
建议按顺序启动：

```powershell
# 1) 启动 Python 模型服务（默认 8081）
cd C:\Users\pc1\PycharmProjects\PythonProject
python start_model_service.py
```

```powershell
# 2) 构建并启动 C++ 后端（8080）
cd C:\Users\pc1\PycharmProjects\Backend
cmake --preset x64-debug
cmake --build out\build\x64-debug --config Debug
.\out\build\x64-debug\Debug\Backend.exe
```

```powershell
# 3) 启动前端（3000）
cd C:\Users\pc1\PycharmProjects\ZImageFrontend
npm install
npm run dev
```

## 6. 配置说明

### 6.1 Backend
- 配置文件：`Backend/config.json`
- 支持环境变量覆盖（示例）：
  - `BACKEND_PORT`
  - `DB_HOST` `DB_PORT` `DB_USERNAME` `DB_PASSWORD` `DB_NAME`
  - `JWT_SECRET`
  - `PYTHON_SERVICE_URL` `PYTHON_SERVICE_TIMEOUT_SECONDS`

### 6.2 PythonProject
- 关键环境变量：
  - `MODEL_SERVICE_PORT`（默认 8081）
  - `MODEL_PATH`（本地模型路径）
  - `MODEL_SERVICE_ALLOW_ORIGINS`

## 7. 当前状态（摘要）
- 前后端与模型服务链路已打通。
- 图片历史已持久化到 MySQL。
- 前端已支持异步轮询与状态展示。
- 适合继续做生产化增强（任务持久队列、对象存储、可观测性、限流等）。

## 8. 安全提示
- 不要在仓库提交真实密钥/密码。
- `config.json` 中的敏感字段建议仅作本地开发占位，生产环境务必使用环境变量。
