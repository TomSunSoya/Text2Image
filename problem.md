# ZImage 待改进问题清单

## 1. 安全问题 (高优先级)

### 1.1 config.json 明文密码已提交到 git
- **位置**: `Backend/config.json:12`
- **问题**: `"password": "Zw08253533"` 直接暴露在仓库中，即使 README 有提醒，历史 commit 中密码已经泄露
- **修复**: 轮换数据库密码，将 config.json 加入 .gitignore，改用环境变量注入敏感配置

### 1.2 ModelService 路径穿越风险
- **位置**: `ModelService/model_service.py:301-307`
- **问题**: `/temp/{filename}` 端点直接用 `os.path.join(TEMP_DIR, filename)` 拼接用户输入，攻击者可用 `../../etc/passwd` 读取任意文件
- **修复**: 校验 filename 不包含 `..` 或路径分隔符，或使用 `pathlib` 验证解析后的路径仍在 TEMP_DIR 内

### 1.3 无速率限制
- **位置**: 全局
- **问题**: 认证接口 (login/register) 和图像生成接口都没有限流，容易被暴力破解或资源耗尽攻击
- **修复**: 后端添加 IP/用户维度的速率限制中间件

---

## 2. 性能瓶颈 (中高优先级)

### 2.1 ModelService generation_lock 串行化所有推理
- **位置**: `ModelService/model_service.py:184`
- **问题**: 使用 `threading.Lock()` 保护推理过程，即使 GPU 有余量也只能同时生成一张图
- **修复**: 改用 `threading.Semaphore` 允许受控并发，或根据 GPU 显存动态调整并发数

### 2.2 Backend 全局数据库互斥锁
- **位置**: `Backend/src/database/ImageRepo.cpp:27-31`
- **问题**: 单个 `std::mutex` 保护所有 DB 操作，连接池的并发优势被完全消解
- **修复**: 改为 session-per-request 模式，让每个线程独立从连接池获取 session

### 2.3 buildRequestId 用毫秒时间戳，存在碰撞风险
- **位置**: `Backend/src/services/image_service.cpp:62-67`
- **问题**: 同一毫秒内的两个请求会得到相同的 request_id
- **修复**: 改用 UUID 生成唯一标识

---

## 3. 代码质量问题

### 3.1 ModelService 图片保存了两次但只用了一次
- **位置**: `ModelService/model_service.py:214-217`
- **问题**: 先 `image.save(filepath)` 存文件，又 `image.save(buffered, format="PNG")` 存到 BytesIO，但 `buffered` 从未被使用
- **修复**: 删除无用的 BytesIO 写入

### 3.2 FastAPI startup 事件使用了已弃用的 API
- **位置**: `ModelService/model_service.py:329`
- **问题**: `@app.on_event("startup")` 已被 FastAPI 弃用
- **修复**: 迁移到 `lifespan` context manager 模式

### 3.3 前端中英文混杂
- **位置**: `ZImageFrontend/src/stores/auth.js` (中文) vs `ZImageFrontend/src/components/ImageGenerator.vue` (英文)
- **问题**: 用户可见文案中文英文混用，体验不统一
- **修复**: 统一语言，最好引入 vue-i18n 做国际化

### 3.4 Logger 使用 f-string 而非惰性格式化
- **位置**: `ModelService/model_service.py` 全文
- **问题**: 使用 `logger.info(f"...")` 而非 `logger.info("...", arg)`，即使日志级别不满足也会执行字符串格式化，浪费性能
- **修复**: 改为 `logger.info("Loading model from %s", LOCAL_MODEL_PATH)` 风格

### 3.5 localtime_s 是 Windows 专用
- **位置**: `Backend/src/database/ImageRepo.cpp:59`
- **问题**: `localtime_s` 是 MSVC 扩展，Linux/macOS 上编译不过 (对应函数为 `localtime_r`)
- **修复**: 使用条件编译或 C++20 `<chrono>` 的跨平台格式化

---

## 4. 功能缺失

### 4.1 无 WebSocket/SSE 实时推送
- **现状**: 前端每 2 秒轮询一次任务状态 (`ImageGenerator.vue:204`)
- **问题**: 轮询效率低、延迟高、增加服务器负载
- **建议**: 引入 SSE (Server-Sent Events) 替代轮询，后端推送任务状态变更

### 4.2 无图片画廊/网格视图
- **现状**: 历史记录只有表格列表形式
- **问题**: 缺少视觉化浏览体验，不直观
- **建议**: 增加网格/瀑布流画廊模式，支持切换列表/画廊视图

### 4.3 无 Prompt 模板/预设
- **现状**: 用户每次都要从零输入 prompt
- **问题**: 降低使用效率和新用户上手体验
- **建议**: 提供预设模板库，支持用户收藏常用 prompt

### 4.4 无用户设置页
- **现状**: 登录后没有个人中心
- **问题**: 无法修改密码、查看账户信息、调整偏好设置
- **建议**: 增加用户设置页面

### 4.5 前端 Token 过期检测不完整
- **位置**: `ZImageFrontend/src/stores/auth.js:79-98`
- **问题**: `checkAuth()` 只检查 token 是否存在，不解析 JWT 检查是否过期，导致持有过期 token 的用户直到收到 401 才被踢出
- **建议**: 在 `checkAuth()` 中解析 JWT payload 检查 `exp` 字段

### 4.6 无响应式/移动端适配
- **位置**: `ZImageFrontend/src/components/` 中的 CSS
- **问题**: 样式只考虑了桌面端，移动端体验可能很差
- **建议**: 增加媒体查询和响应式布局

---

## 5. 工程基础设施

### 5.1 无 Docker/docker-compose
- **问题**: 三个服务 + MySQL 需要手动逐一启动和配置，新人上手成本高
- **建议**: 编写 docker-compose.yml，一键拉起全部服务

### 5.2 无 CI/CD 流水线
- **问题**: 没有自动化测试、构建、部署
- **建议**: 配置 GitHub Actions，至少覆盖 lint + 单元测试 + 构建检查

### 5.3 无数据库 migration 工具
- **问题**: 建表/改表全靠手动 SQL，多人协作和环境同步容易出错
- **建议**: 引入 migration 工具 (如 Flyway 或自定义 SQL 版本管理)

### 5.4 前端未使用 TypeScript
- **问题**: Vue 3 对 TypeScript 支持非常好，纯 JS 缺少编译期类型检查，容易引入运行时类型错误
- **建议**: 逐步迁移到 TypeScript

### 5.5 无 .env 环境管理
- **位置**: `ZImageFrontend/src/utils/request.js`
- **问题**: 前端 API 基础地址通过 Vite proxy 硬编码，不同环境难以切换
- **建议**: 使用 Vite 的 `.env` / `.env.production` 机制管理环境变量

---

## 建议优先执行顺序

1. **修复安全问题** — 轮换数据库密码、修复路径穿越、敏感配置排除出 git
2. **解除性能瓶颈** — DB mutex 改为 per-session、generation lock 改为 Semaphore、request ID 改为 UUID
3. **加 Docker Compose** — 一键启动整个开发环境
4. **前端升级 TypeScript + i18n**
5. **引入 SSE 替代轮询**
6. **增加 Prompt 模板和图片画廊**
