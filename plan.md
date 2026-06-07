# ZImage Backend 优化计划

> **进度（2026-05-17）：** P1 + P2 全部完成。最终单测 190 个用例全过。详见文末 [进度日志](#进度日志-2026-05-17)。
>
> **进度（2026-05-20）：** 新增 [P3 — 上线前差距清单](#p3--上线前差距清单2026-05-20)，按"阻断上线 / 建议补强 / 可选优化"三级整理后端、部署、运营层面的待办项。后端核心代码层面 P2 已闭环，剩余工作主要在部署形态、运营能力与跨层补强。
>
> **进度（2026-05-20）：** CODEX-R1 / P2 遗留 1 已完成：`GenerationClient` 已迁移到 `HttpResult::toExpectedBody()`，新增 `mapHttpError()`，并移除旧 `HttpResult::ok()` / `error` 字符串字段。Backend 单测 196 个用例全过。
>
> **进度（2026-05-20）：** CODEX-A2 已完成：新增 Redis token bucket 限流、用户活跃任务配额、匿名 IP 登录/注册限流与 `RATE_LIMIT_*` 配置。Backend 单测 208/208 通过，Redis 限流集成测试 3/3 通过。
>
> **进度（2026-05-20）：** CODEX-A3 已完成：ModelService healthcheck 会识别 `unhealthy`，`/health` 暴露 `active_kind` 与卡死阈值，TaskEngine 在模型 loading / unhealthy / busy 满载时回滚任务到 `queued` 并退避。Backend 单测 216/216 通过，Backend 集成测试 2/2 通过，ModelService 入口编译检查和 compose 配置校验通过。
>
> **进度（2026-05-21）：** CODEX-A1 已完成：生产 compose 前置 Traefik + Let's Encrypt HTTP-01，公网只发布 80/443，frontend/backend/model-service 以及 MySQL/Redis/MinIO 的基础端口映射在 prod overlay 中 reset；新增 `/api/health` 便于 HTTPS 反代验收。Backend 构建、全部 CTest、prod compose 配置校验和 diff 检查通过。
>
> **进度（2026-05-21）：** CODEX-A5 已完成：MySQL 启用 ROW binlog，prod `ops` profile 新增 `mysql-backup` 与 `mc-mirror`，提供定时 dump、binlog 归档、S3 兼容对象镜像、MySQL 恢复脚本和备份恢复 runbook。默认 prod compose、ops profile compose、shell 脚本语法检查和 diff 检查通过；真实备份/恢复演练需部署环境执行。
>
> **进度（2026-05-21）：** CODEX-B5 已完成：生产环境 `ENV=production` 下 Backend 与 ModelService 均拒绝 wildcard / localhost / 127.0.0.1 / [::1] / 0.0.0.0 CORS origins，`.env.production.example` 与 README 已同步生产域名配置说明。Backend 构建、全部 CTest、ModelService 编译/格式检查、compose 配置校验和 fail-fast 负例通过。
>
> **进度（2026-05-21）：** CODEX-A4 已完成：`ModelService/model_service.py` 拆成 6 行入口 + `model_service_app/` 模块（api/pipelines/state/storage/schemas/config），新增 20 个 pytest 用例覆盖 `/generate`、`/health`、状态机、错误映射和临时文件访问；CI 的 `model-service-smoke` 已改为安装依赖、编译入口并运行 pytest。`cd ModelService && python -m pytest tests -v` 20/20 通过。
>
> **进度（2026-05-21）：** CODEX-B2 已完成：Backend 暴露 Prometheus 文本 `/metrics`，覆盖请求延迟、任务状态迁移、worker queue depth、DB gauge、ModelService 出站调用耗时；ModelService 暴露 `/metrics`，覆盖健康状态、生成耗时、活跃任务和 GPU 显存；prod `monitoring` profile 新增 Prometheus + Grafana 和预置 dashboard。Backend 构建 / UnitTests、ModelService pytest 21/21、compose monitoring config 校验通过。
>
> **进度（2026-05-21）：** CODEX-B1 已完成：Backend 改为 15 分钟 access token + 7 天 refresh token，refresh token `jti` 存 Redis `zimage:refresh:<jti>` 并在 `/api/auth/refresh` rotate、`/api/auth/logout` revoke；前端 axios 401 自动刷新并用 Promise 锁防刷新风暴。Backend 完整 CTest、B1 定向单测、Frontend build、prod compose 配置校验和 diff 检查通过。
>
> **进度（2026-05-21）：** CODEX-B4 已完成：新增 `/profile`、`/admin`、`/403`、NotFound 和 App 错误边界；Backend 新增 `/api/auth/me` 与 `/api/auth/password`，改密成功会撤销该用户全部 refresh token 并要求重新登录。Backend 完整 CTest、B4 定向单测、Frontend build、prod compose 配置校验和 diff 检查通过。
>
> **进度（2026-05-22）：** CODEX-B3 已完成：生产 compose 改为 Docker Secrets + `*_FILE` secret 引用，Backend 配置加载支持 `DB_PASSWORD_FILE` / `JWT_SECRET_FILE` / `REDIS_PASSWORD_FILE` / `CACHE_PASSWORD_FILE` / `MINIO_SECRET_KEY_FILE`，`.env.production.example` 移除明文 secret 占位，备份/迁移脚本支持 secret 文件，新增 `k8s/secrets.yaml.example`。Backend 构建、完整 CTest、ModelService pytest、Frontend build、prod compose 配置校验、shell 语法检查、secret 明文搜索、clang-format dry-run 和 diff 检查通过。
>
> **进度（2026-06-07）：** P3-C 运营优化已落地：新增 `docs/openapi.yaml` + Swagger 预览说明、Backend 结构化审计日志（auth / image create-delete-cancel-retry / admin cache metrics 访问，带 timestamp / request id / resource id / user id / ip / status 且不记录密码、token、prompt、图片内容）、`ops/load-tests` 的 k6/wrk 压测脚本与 QPS/p95/p99/runbook、`train_lora.py` 从 ModelService runtime 镜像隔离并新增 `Dockerfile.train`、`DB_POOL_SIZE` 接入配置/compose/env/Prometheus capacity gauge/容量公式说明、跨 Backend / ModelService `X-Request-Id` 读写回显与 `/generate` 透传。两个 YAGNI 小尾巴继续保持：`HttpError Kind enum` 暂未触发，`MetricsController` admin guard 等第 2 个 admin 端点再抽。

## 1. MinIO 客户端连接复用 ✅ 已完成 (4cf7f19)

**问题：** `MinioClient` 每次调用 `putObject`/`getObject`/`presignGetUrl`/`deleteObject`/`ensureBucketExists` 都通过 `createClient(config_)` 重新创建 `ClientBundle`（含 BaseUrl 解析、Provider 构造、Client 实例化）。在 `presignListImages` 等列表场景下 N 张图触发 N 次连接创建。

**涉及文件：**
- `Backend/src/services/minio_client.cpp`
- `Backend/include/services/minio_client.h`

**方案：**
- 将 `ClientBundle` 作为 `MinioClient` 的成员变量，构造时初始化一次
- 可用 `std::unique_ptr<ClientBundle>` 延迟初始化，或直接在构造函数中创建
- 所有方法改为使用 `bundle_->client` 而非每次 `createClient`

---

## 2. 拆分 image_service.cpp ✅ 已完成 (98417d7)

**问题：** `image_service.cpp` 约 1000 行，混合了任务引擎（worker 线程池、lease 管理、lease 过期扫描）、远程调用（构建 payload、HTTP 请求、结果解析、图片下载）、参数校验、状态归一化、presign 签名等职责。

**涉及文件：**
- `Backend/src/services/image_service.cpp`
- `Backend/include/services/image_service.h`

**方案：** 按职责拆分为三个模块：

| 新模块 | 职责 | 从 image_service.cpp 迁出的内容 |
|--------|------|-------------------------------|
| `task_engine.h/cpp` | Worker 线程池、任务调度、lease 管理、lease 过期扫描、Redis 入队通知 | `TaskEngineConfig`, `loadTaskEngineConfig`, `workerLoop`, `processClaimedTask`, `startLeaseKeeper`, `leaseExpiryLoop`, `recoverOrphanedTasks`, `ensureWorkersStarted`, `enqueueAndNotify`, `notifyWorkers` |
| `generation_client.h/cpp` | 远程 ModelService 调用、结果解析、图片下载、状态归一化 | `runRemoteGeneration`, `mergeRemoteResult`, `normalizeStatus`, `downloadImageBytes`, `getStringField`, `persistGeneratedImage`, `cleanupOrphanedStoredImage` |
| `image_service.h/cpp`（保留） | 业务编排：参数校验、调用 Repo/TaskEngine/GenerationClient、presign | `validateGenerationParams`, `create`, `listMy`, `getById`, `cancelById`, `retryById`, `deleteById`, `checkHealth`, `presignListImages` |

---

## 3. 统一错误处理模式 — Repo 层迁移到 std::expected ✅ 已完成

**问题：** 原先存在三种错误模式并存：
- Service 层：`std::expected<T, ServiceError>`
- Repo 层：`std::optional<T>` + output param 指针（如 `cancelByIdAndUserId(..., ImageGeneration* updated = nullptr)`）
- HTTP Client：`HttpResult` struct with error string

**涉及文件：**
- `Backend/include/database/ImageRepo.h`
- `Backend/src/database/ImageRepo.cpp`
- `Backend/include/database/UserRepo.h`
- `Backend/src/database/UserRepo.cpp`
- `Backend/include/services/client.h`
- `Backend/src/services/client.cpp`

**方案：**
- 定义 `RepoError`（或复用 `ServiceError`），Repo 层返回值统一改为 `std::expected<T, RepoError>`
- 去掉 output param 指针模式（`cancelByIdAndUserId`, `retryByIdAndUserId` 等）
- `HttpResult` 可保留 struct 形式，但增加 `std::expected` 风格的转换方法
- Service 层调用方从 `if (!result)` 改为 `if (!result.has_value())` 或直接用 monadic 操作 `.and_then()` / `.transform()`

**落地状态（2026-05-17）：**
- 新增独立 `RepoError` / `RepoResult<T>`，并由 `mapRepoError()` 统一映射到 `ServiceError`
- `ImageRepo` / `IImageRepo` / `UserRepo` / `IUserRepo` 已迁移到 `std::expected<T, RepoError>`
- `cancelByIdAndUserId`、`retryByIdAndUserId` 已改为 `expected<optional<ImageGeneration>, RepoError>`,移除 output param
- `AuthService`、`ImageService`、`TaskEngine` 已同步处理 Repo 层错误
- `HttpResult` 保留 struct 形式，并新增 `toExpectedBody()` 转换入口
- 异常 → `RepoError` 的分类逻辑（`classifyErrorMessage` 等）抽到 `database/repo_error.{h,cpp}`，由 `ImageRepo` / `UserRepo` 共享
- `RepoSerializationError` + `repoInvoke<Fn>` 模板抽到独立 `database/repo_invoke.h`，两个 Repo 共用一份实现，避免后续新增异常分类时改两处
- 已补充 RepoError 映射、AuthService Repo 错误注入、HttpResult 转换相关单测

**遗留 / 后续可考虑：**
- `HttpResult::toExpectedBody()` 目前仅基础设施层有测试覆盖，`GenerationClient` 等真实消费方仍在用旧 `ok()` + `error` 字符串风格，待后续按调用点迁移
- `HttpError` 仅含 `status_code` + `message`，网络错误统一用 `status_code == 0` 表达；如果未来需要更精细的错误分类（网络 / 超时 / 解析 / BadStatus），可增加 `Kind` enum

---

## 4. C++23 std::ranges 替换手写循环 ✅ 已完成

**问题：** 多处手写 for 循环做 transform/filter 操作，可用 `std::ranges` 表达更简洁。

**涉及文件及具体位置：**

| 文件 | 位置 | 当前写法 | ranges 替换 | 落地状态 |
|------|------|---------|------------|----------|
| `image_controller.cpp` | `toListJson` | for 循环 push_back `item.toJson()` | `std::views::transform(&ImageGeneration::toJson)` | ✅ item 2 拆分时顺手改 |
| `redis_client.cpp` | `rebuildTaskQueue` | for 循环 push_back `std::to_string(taskId)` | `views::transform | ranges::to<vector<string>>` | ✅ commit `c75d106` |
| `image_service.cpp` | `presignListImagesInPlace` | for 循环逐个 presign | `for (auto& img : images | std::views::filter(...))` | ✅ item 2 拆分时顺手改 |
| `image_service.cpp`(→`generation_client.cpp:37`) | `toLower` | `std::transform` with lambda | `std::ranges::transform` | ✅ item 2 拆分时顺手改 |

**额外覆盖（本次会话）：**
- `image_service.cpp::writeListCache` — sanitize 循环 → `views::transform | ranges::to<vector<json>>`
- `string_utils.cpp::parseBool` — filter+lower 循环 → `views::filter | views::transform | ranges::to<string>`

---

## 5. 残留 std::to_string → std::format 统一 ✅ 已完成

**问题：** 项目大部分地方已用 `std::format`，但仍有十余处使用 `std::to_string`，风格不一致。

**涉及文件：**
- `Backend/src/services/redis_client.cpp` — `std::to_string(taskId)` 出现多次
- `Backend/src/services/minio_client.cpp:25` — `std::to_string(response.status_code)`
- `Backend/src/database/ImageRepo.cpp` — 可能有零散使用

**方案：** 只迁移字符串拼接场景，纯数字转换继续保留 `std::to_string`，避免把简单传参改成更啰嗦的 `std::format("{}", value)`。

**落地状态（2026-05-17）：**
- `MinioClient::describeResponse` 的 HTTP fallback 改为 `std::format("http {}", status_code)`
- `RedisClient::leaseKey` 改为 `std::format("{}{}", lease_key_prefix, taskId)`
- `security::hashPassword` 的 PBKDF2 hash 串改为 `std::format`
- `lpush` / `bumpVersion` / `getVersion` 等纯转换调用保持 `std::to_string`

---

## 6. 扩大 std::string_view 使用范围 ✅ 已完成

**问题：** 多处函数参数用 `const std::string&` 但只读不存储，可以改 `std::string_view` 减少不必要的拷贝和临时对象构造。

**涉及文件及候选函数：**
- `image_service.cpp` — `normalizeStatus()`, `toLower()`, `getStringField()`
- `ImageRepo` — `findByRequestIdAndUserId()`, `findByUserIdAndStatus()` 等查询方法的 string 参数
- `redis_client.cpp` — `leaseKey()` 内部拼接

**注意：** 如果函数内部需要将参数传给接受 `const std::string&` 的第三方 API（如 mysqlx bind），则不适合改为 `string_view`，需逐个判断。

**落地状态（2026-05-17）：**
- `ImageService::listMyByStatus` 的 `status` 改为 `std::string_view`，直接下放给 `normalizeTaskStatus`
- `ImageService::writeToCache` / `writeListCache` 的 `key` 改为 `std::string_view`，透传给 cache `setex`
- `IImageStorage::contentTypeForKey` 及实现/fake 改为 `std::string_view`，只读 `ends_with`
- Repo / MinIO / Drogon / JWT / PBKDF2 边界继续保留 `const std::string&`，避免为了兼容第三方 API 反向构造 `std::string`

---

## 7. Controller 样板代码消除 ✅ 已完成

**问题：** `image_controller.cpp` 每个 handler 都重复：创建 resp → setContentType → resolveUserId → new ImageService → 调方法 → 错误/成功处理 → callback。

**涉及文件：**
- `Backend/src/controllers/image_controller.cpp`
- `Backend/include/controllers/image_controller.h`
- `Backend/include/controllers/handler_utils.h`
- `Backend/src/controllers/handler_utils.cpp`

**方案：** 抽取模板辅助函数：

```cpp
template <typename Fn>
void handleRequest(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   Fn&& handler) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) { callback(resp); return; }

    auto result = handler(*userId);
    if (!result) { fillServiceError(resp, result.error()); }
    else { resp->setStatusCode(drogon::k200OK); resp->setBody(result->toJson().dump()); }
    callback(resp);
}
```

各 handler 简化为 1-3 行调用。

**落地状态（2026-05-17）：**
- 新增 `controllers::runJsonHandler`，统一 JSON 响应创建、parse_error / std exception 捕获和 callback
- 新增 `controllers::runAuthenticatedJson`，复用 `resolveUserId` 做认证短路
- 新增 `controllers::respondFromExpected`，统一 `std::expected<T, ServiceError>` 到 HTTP 响应
- `ImageController` 10 个 handler 与 `AuthController::registerUser/login` 已迁移
- `MetricsController::getCacheMetrics` 保持原样，因为 admin role guard 目前只有一个调用点，暂不为单点抽象 admin guard
- 新增 `test_handler_utils.cpp` 4 个用例覆盖成功、失败、void 成功和 JSON parse_error 路径

---

## 8. Redis 引入 Cache-Aside 缓存层 ✅ 已完成 (PR1–PR5)

**问题：** 当前 Redis 只作为任务队列 + 分布式锁使用，完全没发挥"高速缓存"的本职作用。所有读请求（`getById` / `listMy` / `listMyByStatus` / `getStatusById`）都直接打 MySQL，存在以下热点：

- 前端 WebSocket 挂掉时会降级成**轮询 `GET /status`**，QPS 高且查询内容重复
- `GET /api/images/{id}` 查询已完成任务——**不可变数据**（除删除外），命中率理论上接近 100%
- 列表查询涉及 `COUNT(*)` + `SELECT` 两次 DB 往返，翻页抖动明显
- `ImageService::getById` 和 `presignListImages` **每次调用都重新签名** MinIO URL。presigned URL 本身有 TTL（几分钟到几小时），在 TTL 内重复使用完全安全

> **说明：** `getBinaryById` 返回的是**原始字节流**（从 MinIO 拉下来塞进 HTTP body），不经过 presigned URL 机制，也不适合在 Redis 里缓存图片字节（体积大，违背"缓存小而热"原则），这里不纳入方案。

**涉及文件（新增 + 修改）：**

*新增：*
- `Backend/include/services/cache_client.h` — 统一缓存接口：`get` / `setex` / `del` / `bumpVersion`（见 8.3 方案 B）
- `Backend/src/services/cache_client.cpp` — 基于 `sw::redis::Redis` 的实现，复用 `RedisClient` 的连接池

*修改：*
- `Backend/src/services/redis_client.cpp` — 扩展通用 KV 操作（`get` / `setex` / `del` / `incr`），或让 `CacheClient` 内部持有独立 `sw::redis::Redis` 连接
- `Backend/src/services/image_service.cpp` — `getById` / `listMy` / `listMyByStatus` / `presignListImages` 增加 Cache-Aside 逻辑（`getBinaryById` 返回原始字节流，不进缓存）
- 所有写路径：`create` / `cancelById` / `retryById` / `deleteById` + task worker 状态流转处增加 invalidation 调用
- `Backend/config.json.example` — 新增 `cache` 配置节（TTL、开关）

---

### 8.1 Cache-Aside（旁路缓存）读写流程

**读路径：**
```
1. 查 Redis：cache.get(key)
2. 命中  → 反序列化返回
3. 未命中 → 查 MySQL
4. DB 有值 → cache.setex(key, ttl, json) 回填
5. DB 无值 → cache.setex(key, short_ttl, "__NULL__") 防穿透
```

**写路径（所有修改 DB 的操作之后）：**
```
1. 更新 MySQL（真相源）
2. 失效缓存 —— 分两路（详见 8.3）：
     - 单对象缓存：cache.del("zimage:img:meta:<id>")  / cache.del("zimage:img:url:<hash>")
     - 列表缓存：  cache.bumpVersion("img:list", userId)   ← INCR 版本号，作废该用户所有分页
3. 下一次读会自然 miss → 从 DB 重新加载最新值
```

> **为什么失效而不是更新？** Write-Through 需要保证"DB 写 + 缓存写"原子性，否则并发下会脏数据。Cache-Aside 只删不写，简单可靠，是工业界主流做法。
>
> **为什么列表走版本号而不是 DEL 多个 key？** 见 8.3 三方案对比——避免 SCAN 的性能和并发漏删问题。

---

### 8.2 Key 设计 & TTL 策略

| Key Pattern | 对应接口 | TTL | 理由 |
|---|---|---|---|
| `zimage:img:meta:<id>` | `getById` / `getStatusById` | 300s (已完成) / 5s (进行中) | 已完成任务不变；进行中短 TTL 保证状态及时刷新 |
| `zimage:img:list:<userId>:v<ver>:<page>:<size>` | `listMy` | 60s + 随机 0-10s 抖动 | 列表易变，短 TTL + **随机抖动防雪崩**。`<ver>` 来自版本号 key（见 8.3） |
| `zimage:img:list:<userId>:v<ver>:<status>:<page>:<size>` | `listMyByStatus` | 60s + 抖动 | 同上 |
| `zimage:img:url:<storage_key_hash>` | `getById` / `presignListImages` 的 presigned URL | MinIO presigned TTL 的 80% | **必须短于 presigned URL 本身 TTL**，否则返回过期链接。按 `storage_key` 而非 `image_id` 索引，因为 presigned URL 是对象粒度的 |

**命名规范：** 统一 `zimage:<domain>:<subtype>:<identifier>` 三段式，便于排查和监控。

**不纳入缓存的对象（明确排除）：**
- **用户信息** (`user:<id>`)：JWT 纯本地 HS256 验签（`utils::verifyToken`），`resolveUserId` 路径完全不查 DB，不存在读热点
- **图片二进制字节**：`getBinaryById` 拉取的是完整图片数据，大对象不适合塞 Redis

---

### 8.3 Invalidation 策略（关键 + 有取舍的设计点）

这是 Cache-Aside 最容易出 bug 的地方。单对象失效很简单（`DEL key`），**难点在列表缓存**——同一用户的 `listMy` 会展开成 N 个分页 key（不同 `page`/`size`/`status` 组合），任何一个任务的状态变化都会让所有分页缓存失效。下面给出**三种方案的取舍**，不是"推荐 SCAN + DEL"。

#### 单对象 key（简单）

| 触发点 | 需要失效的 Key |
|---|---|
| `deleteById` | `zimage:img:meta:<id>` + `zimage:img:url:<storage_key_hash>` |
| `cancelById` / `retryById` | `zimage:img:meta:<id>` |
| Worker 状态流转 | `zimage:img:meta:<id>` |

#### 列表 key 失效：三选一

| 方案 | 做法 | 优点 | 代价 | 适用 |
|---|---|---|---|---|
| **A. 仅靠短 TTL** | 不主动失效，60s TTL + 抖动 | 零实现成本，无一致性假设 | 用户提交新任务后最长 60s 才能在列表看到 | 对"列表实时性"要求低的场景 |
| **B. 版本号递增**（推荐） | 每个用户维护 `zimage:img:list_ver:<userId>`，列表 key 拼版本号；写操作 `INCR` 该 key | O(1) 失效，旧 key 自然过期，**无需 SCAN**，线性扩展到集群 | 多一次 Redis RTT，旧缓存占内存直到 TTL 过期 | **生产级推荐** |
| **C. `SCAN MATCH` + `DEL`** | 写操作时遍历并删除 | 立即释放内存 | SCAN 成本随 key 数量线性增长，**大规模下 CPU 压力大**；集群模式下 SCAN 跨 slot 更麻烦；并发 SCAN 可能漏删新写入的 key | 小规模 / 明确知道 key 数量有限的场景 |

> **方案 B 详解：** 列表 key 形如 `zimage:img:list:<userId>:v<ver>:<page>:<size>`。读路径先 `GET zimage:img:list_ver:<userId>`（初始值 0），拼到 key 里再查缓存。写路径只需 `INCR zimage:img:list_ver:<userId>`，下一次读会自动 miss 并用新版本号回填。**本质上是把"删除多个 key" 变成了"作废整个版本"**。
>
> **为什么不推荐 C：** 除了性能问题，SCAN 的游标语义在并发写入下不保证"抓到所有当时存在的 key"，容易留下脏缓存。把这种方案塞进 `CacheClient::delByPattern` 会诱导后续开发者滥用，不是好抽象。

**落地建议：** PR3（列表缓存）采用方案 B。`CacheClient` 暴露 `bumpVersion(namespace, id)`，不要暴露 `delByPattern`。

---

### 8.4 三件套防护

**① 缓存穿透**（查不存在的 id 反复打 DB）：
- DB 未命中时，缓存特殊标记 `"__NULL__"`，TTL 30s
- 读路径检测到该标记直接返回 404，不再查 DB

**② 缓存击穿**（热 key 失效瞬间大量并发打 DB）：
- 在 `ImageService` 进程内用 `std::mutex` + `std::unordered_map<std::string, std::shared_future>` 实现 **singleflight**：同一 key 的并发 miss 只有一个线程查 DB，其他等待结果
- 单机方案足够；如果以后扩成多实例，改用 Redis `SET NX` 分布式锁

**③ 缓存雪崩**（大量 key 同时失效）：
- TTL 加随机抖动（`base_ttl + rand(0, 10s)`）
- Redis 挂掉时自动降级（复用现有 `isAvailable()` 检查），不影响功能

---

### 8.5 一致性取舍

- **最终一致**：缓存 invalidation 是异步副作用，极端情况下（删缓存失败 + 后续读命中旧值）会有秒级不一致
- **补偿**：所有缓存 key 都有 TTL 兜底，最坏情况等 TTL 过期自愈
- **强一致场景不走缓存**：`getBinaryById` 下载图片字节流本身不缓存（只缓存 URL），避免 Redis 存大对象
- **文档化**：在 `CacheClient` 头文件注释中明确标注"eventual consistency, bounded by TTL"

---

### 8.6 实现步骤（建议 PR 拆分 + 必做测试场景）

> **原则：** 缓存层的核心风险是"正确性"——错误的缓存返回错误的数据比没缓存还糟。每个 PR 都必须附带覆盖下表场景的测试，缺失测试的 PR 不合入。测试框架复用现有 Google Test（`UnitTests` / `IntegrationTests` 两个 target）。Redis 依赖可用 testcontainer 或现成的 `docker compose` MySQL+Redis 侧车。

> **测试前置条件（不要忽略）：** 下面 PR2/PR3/PR4 的测试表里多处出现"用 spy/mock 验证 `ImageRepo` / `ImageStorage` 调用次数"。**当前代码做不到**——`ImageService` 的方法内部直接栈构造 `ImageRepo repo;` / `ImageStorage storage;`，没有接口抽象也没有构造注入。两条出路，按 PR 选一条：
>
> - **路径 A（推荐）：** 等 **item 2（拆分 `image_service.cpp`）** 完成，并顺带引入依赖注入（`ImageService` 构造函数接受 `IImageRepo` / `IImageStorage` 接口指针或 `std::function` 工厂），之后 PR2/3/4 才做纯单元测试。这把"可测性"作为 item 2 的隐含交付物，一次性解决。
> - **路径 B（回退）：** 如果暂不做拆分，所有 "验证调用次数" 类断言改为 **integration test**（复用现有 `IntegrationTests` target + MySQL 侧车 + Redis 侧车），通过"第二次查询 DB 的 `query_count` 指标 / 慢查询日志 / 数据库侧 counter"间接验证命中率；或放弃"精确调用次数"断言，改为黑盒断言（返回值正确、TTL 生效、Redis 里有对应 key）。
>
> 无论选哪条，**不能默认"现成可 mock"直接开写**。PR2 动工前必须先明确选择。

---

**PR1 — `CacheClient` 基础设施** ✅ 已完成 (5080b15)

*内容：* 新增 `CacheClient` 抽象（`get` / `setex` / `del` / `bumpVersion`）+ `cache` 配置节 + Redis 可用性降级逻辑。

*必做测试场景：*
| 场景 | 断言 |
|---|---|
| `setex / get` 往返 | 写入后能读出同值；过期后读不到 |
| `del` 单 key | 删后 `get` 返回 `nullopt` |
| `bumpVersion` 语义 | 连续调用返回递增整数；并发调用无丢失（`INCR` 原子性） |
| **Redis 不可用降级**（核心） | `isAvailable()==false` 时：`get` 返回 `nullopt`，`setex / del / bumpVersion` **不抛异常**、静默失败 |

---

**PR2 — `getById` 元数据缓存 + invalidation** ✅ 已完成 (acad82a)

*内容：* `ImageService::getById` 走 Cache-Aside；`deleteById` / `cancelById` / `retryById` + task worker 状态流转处调用 `cache.del("zimage:img:meta:<id>")`。

*必做测试场景（对应 review 第 1、2、4、5 条）：*
| 场景 | 断言 |
|---|---|
| **命中** | 同一 `(userId, id)` 连续两次 `getById`，第二次 **不触发 `ImageRepo::findByIdAndUserId`**（用 spy/mock repo 验证调用计数） |
| **`deleteById` 失效** | 删除后再 `getById` 返回 404，且**重新查 DB**（不返回缓存中的旧对象） |
| **Worker 状态流转失效** | 任务从 `generating` → `success` 后，下一次 `getById` 返回 `success`，不是缓存里的 `generating` |
| **`__NULL__` 防穿透** | `getById(不存在的 id)` 第一次查 DB 写入 `__NULL__`，第二次**不打 DB**且仍返回 404；等 short TTL (30s) 过后自然恢复查询 |
| **Redis 挂时降级** | `RedisClient::isAvailable()==false` 下，`getById` 正常从 DB 返回结果，不抛异常 |

---

**PR3 — 列表缓存 + `list_ver` 版本号失效** ✅ 已完成 (88f2c74)

*内容：* `listMy` / `listMyByStatus` 走 Cache-Aside，key 拼 `list_ver`；写路径（`create` / `cancelById` / `retryById` / `deleteById` / worker 状态流转）调用 `cache.bumpVersion("img:list", userId)`。TTL 加 0–10s 随机抖动。

*必做测试场景（对应 review 第 3 条）：*
| 场景 | 断言 |
|---|---|
| **命中** | 同一 `(userId, page, size)` 连续两次 `listMy`，第二次不触发 `ImageRepo::findByUserId` |
| **版本号递增后旧缓存不再被读到** | 第一次 `listMy(userId, page=0)` → 写入 `list:<uid>:v0:0:10`；随后 `create` 新任务触发 `bumpVersion`；再次 `listMy(userId, page=0)` 应 **miss → 查 DB → 回填到 `list:<uid>:v1:0:10`**。即使 `v0` key 还在 Redis 里也不应被读到 |
| **跨用户隔离** | 用户 A 的 `bumpVersion` **不影响**用户 B 的命中 |
| **TTL 抖动** | 抽样 20 次写入，TTL 落在 `[60, 70]` 区间（防雪崩验证） |
| **Redis 挂时降级** | 同 PR2 |

---

**PR4 — presigned URL 缓存** ✅ 已完成 (f5725f9)

*内容：* 在 `ImageService::getById` 和 `presignListImages` 中调用 `storage.presignUrl(storage_key)` 的位置加一层缓存；key 按 `storage_key` 哈希，TTL 取 MinIO presigned URL TTL 的 80%。

*必做测试场景（对应 review 第 6 条）：*
| 场景 | 断言 |
|---|---|
| **按 `storage_key` 索引** | 两次 `presignUrl(同一 storage_key)` 第二次不调用 MinIO SDK（mock/spy `ImageStorage::presignUrl`） |
| **TTL 严格小于 MinIO URL TTL**（核心） | 启动时或 `CacheClient` 构造时断言 `cache_ttl < minio_presign_ttl`；配置错误应 **启动失败**而非返回过期链接 |
| **失效** | `deleteById` 后对应 `zimage:img:url:<hash>` 被删除 |
| **Redis 挂时降级** | 直接走 `storage.presignUrl()`，不抛异常 |

---

**PR5 — 监控指标** ✅ 已完成（本次提交）

*内容：* `cache_hit_total` / `cache_miss_total` / `cache_degraded_total`（Redis 不可用计数），按 namespace 打标签（`meta` / `list` / `url`）。

*必做测试场景：* 命中 / 未命中 / 降级路径下对应 counter 正确递增。

---

每个 PR 的 commit message 强调"为什么"（穿透/击穿/雪崩/版本号失效的具体解法），面试时每个都是一个独立故事，测试用例本身就是最好的"我考虑到了哪些边界"的证据。

---

### 8.7 面试延伸话题（plan.md 不落但要心里有数）

- **为什么不用 Write-Through / Write-Back？** → 一致性复杂度 vs 收益
- **缓存 vs 物化视图？** → MySQL 查询优化的替代路径
- **Redis 集群化？** → 当前单实例，`CacheClient` 抽象让未来迁移 `RedisCluster` 成本可控
- **二级缓存（本地 LRU + Redis）？** → 超高热数据才需要，当前规模不必

---

## 优先级

| 优先级 | 任务 | 状态 | 理由 |
|--------|------|------|------|
| P0 | 1. MinIO 连接复用 | ✅ | 真实性能问题，列表页 N 次连接创建 |
| P0 | 2. 拆分 image_service.cpp | ✅ | 面试高频追问点，展示架构能力 |
| P0 | 8. Redis Cache-Aside 缓存层 | ✅ | 补齐 Redis 核心用法，面试必问点，工程收益 + 展示价值双高 |
| P1 | 3. 统一错误处理 | ✅ | 一致性问题，面试容易被问 |
| P1 | 4. std::ranges 替换循环 | ✅ | 最直观的 C++23 升级展示 |
| P2 | 5. std::to_string → std::format | ✅ | 风格统一，改动小 |
| P2 | 6. std::string_view | ✅ | 性能微优化，需逐个判断兼容性 |
| P2 | 7. Controller 样板消除 | ✅ | 代码整洁度，非阻塞 |

---

## P2 遗留项（小尾巴）

> **背景：** 主线 P1+P2 已闭环，但 plan.md 不同位置散落着三个显式标注的"后续可考虑"项。集中到这里便于追踪。

### 遗留 1 — `HttpResult::toExpectedBody()` 消费方迁移 ✅ 已完成（CODEX-R1）

**出处：** Item 3 落地状态（line 70）+ 2026-05-17 进度日志 PR-C ⚠ 备注（line 483）

**现状：** 已完成生产消费方迁移。`GenerationClient::generate` / `checkHealth` / 图片下载路径都通过 `HttpResult::toExpectedBody()` 消费 HTTP 响应；底层 `HttpResult` 已移除旧 `ok()` 方法和 `error` 字符串字段。

**涉及文件：**
- `Backend/include/services/i_http_client.h`（`HttpResult` / `HttpError` 定义）
- `Backend/src/services/generation_client.cpp`（主要消费方）
- `Backend/src/services/client.cpp`（其他 HTTP 调用点，如有）
- `Backend/tests/unit/test_generation_client.cpp`（已补网络错误 / 5xx / 4xx / 成功 body 路径）
- `Backend/include/services/http_error_mapper.h`
- `Backend/src/services/http_error_mapper.cpp`
- `Backend/tests/unit/test_http_error_mapper.cpp`

**落地状态（2026-05-20）：**
- `HttpResult::toExpectedBody()` 成为 `GenerationClient` 的真实生产消费入口
- 新增 `mapHttpError(const HttpError&) → ServiceError`，按网络错误 / 5xx / 4xx 集中映射模型服务错误
- `HttpResult` 底层改为 `status_code + body + optional<HttpError> failure`，不再暴露旧 `ok()` / `error` 消费风格
- 单测从 190 增至 196，新增 `HttpErrorMapper` 覆盖与 `GenerationClient` HTTP 错误路径覆盖

**验证：**
- `unit_tests.exe --gtest_filter=GenerationClient*:HttpResult*:HttpErrorMapper*`：22/22 通过
- `unit_tests.exe`：196/196 通过

---

### 遗留 2 — `HttpError` Kind enum 细化

**出处：** Item 3 落地状态 line 71

**现状：** `HttpError` 仅含 `status_code` + `message`，网络错误统一用 `status_code == 0` 表达。

**触发条件（暂未触发）：** 如未来需要区分"网络错误 / 超时 / 解析失败 / BadStatus"等子类型，再加 `Kind` enum。当前调用方无此需求。

**实施提示：** 沿用 `RepoError::Kind` 模式 —— 4-5 种 Kind + classification helper + `std::expected<T, HttpError>` 透传到 Service 层后由 `mapHttpError(HttpError) → ServiceError` 集中映射。

---

### 遗留 3 — `MetricsController` admin guard 抽象

**出处：** Item 7 落地状态 line 169-170

**现状：** Item 7 抽出了 `handler_utils` envelope（`runJsonHandler` / `runAuthenticatedJson` / `respondFromExpected`），但 `MetricsController::getCacheMetrics` 保持原样。

**理由：** admin role guard 目前只有这一个调用点，抽 guard 会早于真实复用需求

**触发条件：** 出现第 2 个 admin 端点时再抽（如 P3-C 的审计日志 endpoint、未来的用户管理 endpoint），避免"单例抽象"

**实施提示：** 在 `handler_utils.h` 新增 `runAdminJson(req, callback, handlerName, body)`，内部先 `resolveUserId` 后查 `UserRepo::isAdmin(userId)`，统一返回 403。

---

## 进度日志 (2026-05-16)

### 本会话期间完成

**PR4 — presigned URL 缓存** ✅ 已提交
- 新增：`image_cache::presignKey` / `derivePresignTtl(minioExpiry * 0.8)` / `kPresignCacheTtlRatio`
- `ImageService::setPresignTtl` 静态 setter，main.cpp 启动时从 MinIO `presign_expiry_seconds` 派生（不引入独立配置字段，避免配置漂移）
- `ImageService::presignWithCache` 私有方法：三道防御（空 key 跳过 / TTL=0 fallback / 空 url 不写 cache）
- `presignListImages` free function 移成 `presignListImagesInPlace` 成员方法（让 cache_ 可访问）
- 失效点：`deleteById` 加 `cache_->del(presignKey(storage_key))`
- 单测：`test_image_service_presign_cache.cpp` 11 个用例（含 derivePresignTtl 边界 / hit/miss/不同 key 隔离 / deleteById 失效 / listMy 走 cache / 空 storage_key / 空 url 不写 cache / Redis 不可用 fallback / TTL=0 禁用）
- **顺手踩的坑（记录给后续 PR）**：
  1. `std::max(...)` 在 Windows 上会被 `<windows.h>` 宏展开 —— 必须写 `(std::max)(...)` 加 paren 防御。项目其它地方都这样做了，新代码沿用。
  2. CMake `GLOB_RECURSE` + `CONFIGURE_DEPENDS` 加新文件后偶尔需要强制 reconfigure 才能让 Visual Studio 重新扫描。

### 本会话期间完成（续）

**PR5 — 监控指标** ✅ 已完成

已写的文件：
- `Backend/include/services/cache_metrics.h` — `Namespace` enum + `CacheMetrics` 类（3 个 atomic counter × 4 namespace）+ `classifyKey` / `namespaceName`
- `Backend/src/services/cache_metrics.cpp` — 实现
- `Backend/include/services/metrics_cache_client.h` — Decorator 声明
- `Backend/src/services/metrics_cache_client.cpp` — Decorator 实现（只在 `get` 路径记录 hit/miss/degraded，其它方法透明转发）
- `Backend/include/controllers/metrics_controller.h` — Drogon HttpController，`ADD_METHOD_TO` 暴露 `GET /api/metrics/cache`
- `Backend/src/controllers/metrics_controller.cpp` — endpoint 实现，503 fallback 当 metrics 未初始化
- `Backend/src/main.cpp` — 创建 `CacheMetrics` 实例，**包装 cacheClient 后再调 `setDefaultCache`**（顺序很重要，否则 controller 拿到 raw cache，metrics 永远 0）
- `Backend/tests/unit/test_cache_metrics.cpp` — 9 个用例，覆盖 key 分类、namespace 隔离、atomic counter 并发递增、JSON snapshot
- `Backend/tests/unit/test_metrics_cache_client.cpp` — 6 个用例，覆盖 decorator hit/miss/degraded、构造防御、非 get 操作透明转发

已修复的 bug（本会话内）：
- main.cpp 装配顺序：之前 `setDefaultCache` 在 wrap 前 → 99% 流量不计数。已挪到 wrap 后。
- `metrics_controller.h`：之前用 `METHOD_ADD("/cache", ...)` → 实际路径变成 `/MetricsController/cache`。已改为 `ADD_METHOD_TO("/api/metrics/cache", ...)`。
- `metrics_controller.cpp` 缺 `#include <nlohmann/json.hpp>` —— `toJson().dump()` 需要完整定义，fwd 不够。
- `classifyKey` 改成 if + return 链（早返回风格，跟其它 helper 一致）。

验证：
- `unit_tests.exe --gtest_filter=CacheMetrics*:*MetricsCacheClient*`：15 个 PR5 单测全过
- `ctest --test-dir out/build/x64-debug -C Debug -R UnitTests --output-on-failure`：UnitTests 全过

### 下一次会话从这里继续

**Step 1：开始 item 3（错误处理统一）**，先做 Repo 层错误模型和迁移边界设计，再决定是否分 PR 推进。

### 关键文件指引（下次会话快速定位）

- 缓存 key/TTL 集中处：`Backend/include/services/image_cache_key.h`
- 缓存装配链：`Backend/src/main.cpp` line ~58–90
- ImageService cache 注入点：`Backend/src/services/image_service.cpp` `defaultCacheClient()` + `setDefaultCache`
- TaskEngine cache 注入点：`Backend/src/services/task_engine.cpp` `Impl::cache` + `invalidateAllForTask`
- 测试 fakes：`Backend/tests/unit/image_service_test_fakes.h`

---

## 进度日志 (2026-05-17)

### 本会话期间完成

**Item 3 — 错误处理统一** ✅ 已完成（一次会话内推进完 PR-A / PR-B / PR-C 三步）

PR-A（ImageRepo 主体迁移）✅ 已 commit `03444d8`：
- 新增 `Backend/include/database/repo_error.h` —— `RepoError { Kind, message }` + `RepoResult<T> = std::expected<T, RepoError>`
- 新增 `Backend/include/services/repo_error_mapper.h` + `repo_error_mapper.cpp` —— `mapRepoError(RepoError) → ServiceError`，5 个 Kind 集中映射
- `IImageRepo` / `ImageRepo` 全部方法迁移到 `RepoResult<T>`；`cancelByIdAndUserId` / `retryByIdAndUserId` 干掉 output param，返回 `expected<optional<ImageGeneration>, RepoError>`
- Service 层"两层判断"消费风格：外层 `if (!result) return mapRepoError(...)`，内层 `if (!*result) return ServiceError{...}`
- `TaskEngine` 用 `logRepoError` 记录但不调用 `mapRepoError`（无 HTTP 边界）
- Fakes 加 `next_error` 故障注入字段，单测覆盖 DbUnavailable→503 等故障路径

PR-B（UserRepo + AuthService）✅ 工作树未 commit：
- 新增 `Backend/include/database/i_user_repo.h`，AuthService 通过 `shared_ptr<IUserRepo>` 注入
- UserRepo 全部方法迁移到 `RepoResult<T>`
- AuthService 加 null repo 检查 + `RejectsNullRepoDependency` 单测
- 新增 `test_auth_service_repo_errors.cpp` — register / login 故障注入用例

PR-C（HttpResult 渐进式改造）✅ 工作树未 commit：
- 新增 `HttpError { status_code, message }` struct
- 新增 `HttpResult::toExpectedBody() → std::expected<std::string, HttpError>` 转换方法
- **保留旧 `HttpResult` struct + `ok()` API**，让 `GenerationClient` 可逐步迁移而不破坏现有调用方
- 新增 `test_http_result.cpp` 3 个用例覆盖 success / network error / bad status 三条路径
- ⚠ 当前 `GenerationClient` 仍用旧 API，`toExpectedBody` 暂无生产消费方——按调用点逐步迁移

健壮性紧固（本会话内修复）：
- `classifyErrorMessage`：把"配置类"错误（"db not initialized" / "database name is empty"）从 `DbUnavailable`(503) 移到 `Internal`(500)，因为重试无效
- `classifyErrorMessage`：ConstraintViolation 关键词从 `"constraint"` / `"foreign key"` 收紧到 `"duplicate entry"` / `"foreign key constraint fails"`，避免误伤"check constraint failed"等
- `cancelByIdAndUserId` / `retryByIdAndUserId`：UPDATE 生效但回读 SELECT 返 0 行（极端并发：被并发 DELETE）→ `throw std::runtime_error` 让 `repoInvoke` 映射为 `Internal`(500)，不再被 Service 误判为 409 conflict
- `classifyErrorMessage` + `makeRepoErrorFromMysqlMessage` / `makeRepoErrorFromExceptionMessage` 从 `ImageRepo.cpp` anonymous namespace 提到独立 `database/repo_error.{h,cpp}`，让 `ImageRepo` / `UserRepo` 共享一份分类逻辑
- `RepoSerializationError` 类型 + `repoInvoke<Fn>` 模板从 `ImageRepo.cpp` / `UserRepo.cpp` 两份重复副本提到 `database/repo_invoke.h`，未来新增异常分类只需改一处

测试增量：171 → 180 用例（+3 HttpResult / +3 RepoError 分类 / +2 AuthService 故障注入 / +1 null repo 拒绝）

### 顺手踩的坑

- `repo_error.cpp` / `repo_invoke.h` 加入后，CMake `GLOB_RECURSE` + `CONFIGURE_DEPENDS` 第一次 build 会触发 reconfigure 但 vcxproj 尚未更新源文件列表，导致链接 LNK2019。**重跑一次 build 即可**——和 PR4 踩过的坑同源
- `repoInvoke` 模板需要看到 `mysqlx::Error` 完整类型才能 catch，所以 `repo_invoke.h` 必须 include `<mysqlx/xdevapi.h>`。**故意不放到 `repo_error.h`**——后者被 `i_image_repo.h` / `i_user_repo.h` 等接口头文件 include，不应间接拉 mysqlx 依赖

### 本会话期间完成（续）

**Item 4 — std::ranges 替换手写循环** ✅ 已完成（commit `c75d106` + item 2 拆分时已顺手处理的若干位置）

复盘 plan 表格里四个位置：
- `image_controller.cpp::toListJson` — 早先 PR 已用 `std::views::transform(&ImageGeneration::toJson)`
- `image_service.cpp::presignListImagesInPlace` — item 2 拆分时已用 `views::filter`
- `image_service.cpp::toLower`（迁到 `generation_client.cpp:37`）— 已用 `std::ranges::transform`
- `redis_client.cpp::rebuildTaskQueue` — **本次会话** `views::transform | ranges::to<vector<string>>`

本次会话额外迁移：
- `image_service.cpp::writeListCache` 的 sanitize 循环 → `views::transform | ranges::to<vector<json>>`，`j["content"] = nlohmann::json(std::move(content))`
- `string_utils.cpp::parseBool` 的 filter+lower 循环 → `views::filter | views::transform | ranges::to<string>`；lambdas 用 `unsigned char` 参数，规避 `std::isspace(char)` / `std::tolower(char)` 对 UTF-8 高位字节的 UB

测试增量：180 → 186 用例（+3 `ImageServiceRangesSanitization` / +3 `RedisClientRanges`）

**易踩的小坑（本次留意到）：**
- `parseBool` 里 view chain 引用按值传入的局部 `value`，必须在函数返回前用 `ranges::to<string>` 物化掉，不能直接把 view 作为返回值
- `nlohmann::json` 隐式支持 `std::vector<json>` → JSON array 的转换，所以 `j["content"] = std::move(content)` 也可，外层 `nlohmann::json(...)` 包装属于显式风格选择
- `RedisClientRanges` 测试现在测的是"transform pattern 本身"的等价性，而不是 `RedisClient::rebuildTaskQueue` 整体行为（后者要 mock `sw::redis::Redis` 才能写）—— 留作回归探针

### 本会话期间完成（续）

**P2 — item 5 / 6 / 7** ✅ 全部完成（commits `65e2523` / `65722af` / `df3776f`）

Item 5 只改拼接场景：`MinioClient::describeResponse`、`RedisClient::leaseKey`、`security::hashPassword`。`lpush` / `bumpVersion` / `getVersion` 等纯转换位点继续保留 `std::to_string`，因为改成 `std::format("{}", x)` 没有可读性收益。

Item 6 只迁移无第三方 API 边界、无反向构造成本的只读参数：`ImageService::listMyByStatus(status)`、`writeToCache(key)`、`writeListCache(key)`、`IImageStorage::contentTypeForKey(storageKey)`。Repo 的 mysqlx bind、MinIO/Drogon/JWT/PBKDF2 透传参数保持 `const std::string&`。

Item 7 新增 `controllers/handler_utils.{h,cpp}`，集中 JSON handler envelope、认证短路和 expected→response 映射；`ImageController` 与 `AuthController` 已迁移。`MetricsController` 保持原样，理由是 admin role guard 目前只有一个调用点，抽 guard 会早于真实复用需求。

测试增量：186 → 190 用例（+4 `HandlerUtils`）。最终 `unit_tests.exe` 190/190 通过。新增文件触发的首次 LNK2019 属于 CMake `GLOB_RECURSE` + vcxproj 刷新已知现象，重跑 build 后通过。

### 下一次会话从这里继续

P2 已闭环。下一步可从新的 P3/部署验证/性能压测中选一条，不再有 P2 残留项。

### 关键文件指引（item 3 / item 4 后新增）

- RepoError / RepoResult 抽象：`Backend/include/database/repo_error.h`
- 错误分类逻辑：`Backend/src/database/repo_error.cpp`（`classifyErrorMessage` + `makeRepoErrorFrom*Message`）
- `repoInvoke` + `RepoSerializationError`：`Backend/include/database/repo_invoke.h`（两个 Repo 共享）
- Repo → Service 错误映射：`Backend/include/services/repo_error_mapper.h` + 实现
- IUserRepo / UserRepo：`Backend/include/database/i_user_repo.h` + `UserRepo.cpp`
- HttpResult expected 转换：`Backend/include/services/i_http_client.h` 的 `toExpectedBody`
- std::ranges 集中观察点：`redis_client.cpp::rebuildTaskQueue` / `image_service.cpp::writeListCache` / `string_utils.cpp::parseBool` / `generation_client.cpp::toLower` / `image_controller.cpp::toListJson` / `image_service.cpp::presignListImagesInPlace`
- Controller handler helper：`Backend/include/controllers/handler_utils.h` / `Backend/src/controllers/handler_utils.cpp`

---

## P3 — 上线前差距清单（2026-05-20）

> **背景：** P2 已闭环，后端核心代码层面没有遗留项。本节按"能否上线"的视角重新审视整套系统，识别出三类差距：阻断上线（必须解决，否则不能面向公网用户）、建议补强（强烈推荐在小流量上线前完成）、可选优化（不影响上线，但运营期会逐步遇到）。
>
> **判定原则：** 是否影响**面向公网真实用户**的可用性、安全性、可恢复性。仅做演示 / 内测可豁免 P3-A 部分项。
>
> **已具备能力（不在 P3 范围）：** 三层架构、JWT 鉴权 + role 字段、Cache-Aside + 指标 endpoint、worker lease + 过期回收、原子任务认领、`RepoError → ServiceError → HTTP` 错误分层、`docker-compose.prod.yml` 资源限制 + 副本 + 日志切割、`.env.production.example` 完整占位符、DB 版本化迁移、CI（前端 build + Backend 单测 + 集成测 + smoke + compose 校验 + runtime 镜像 build）。

### P3-A 阻断上线（必须解决）

#### A1. HTTPS / TLS 终结层缺失 ✅ 已完成（CODEX-A1）

**问题：** `docker-compose.prod.yml` 直接把 nginx 暴露在 `FRONTEND_PORT=80`，全链路明文。生产暴露在公网会同时面临凭证窃听 + Mixed Content + 浏览器拒绝部分 API（如 Service Worker / Clipboard）。

**涉及文件：**
- `docker-compose.prod.yml`
- `ZImageFrontend/nginx.conf` 或新增反向代理服务
- `.env.production.example`

**方案选项：**
- **A.** 前置 `traefik` / `caddy`，自动签发 Let's Encrypt 证书。改动小、零额外配置脚本。**推荐**
- **B.** 用 `nginx-proxy` + `acme-companion` 两个 sidecar 容器
- **C.** 把 TLS 终结放在云厂商 LB（ALB / Cloud Load Balancer），compose 只跑内网 HTTP

**测试：** 部署后用 `curl -vk https://<domain>` 验证证书链；用 SSL Labs 跑一次 A 级及以上评分。

**落地状态（2026-05-21）：**
- `docker-compose.prod.yml` 新增 `traefik:v3`，启用 Docker provider、HTTP→HTTPS 重定向、Let's Encrypt HTTP-01、`le` cert resolver，以及 `${TRAEFIK_ACME_CA_SERVER}` staging/production 切换。
- frontend / backend 通过 Traefik routers 暴露在 `${DOMAIN}`，backend 使用 `Host(...) && PathPrefix('/api')` 并设置更高 priority；WebSocket 继续走 `/api/ws/images`。
- prod overlay 通过 `ports: !reset []` 清掉基础 compose 的直接宿主机端口映射，只保留 Traefik 的 `${HTTP_PORT:-80}` / `${HTTPS_PORT:-443}`。
- `Backend/src/main.cpp` 同时注册 `/health` 与 `/api/health`，满足反代后 `https://${DOMAIN}/api/health` 验收。
- `.env.production.example`、README / README.zh-CN / CLAUDE 文档已补齐 `DOMAIN`、`ACME_EMAIL`、ACME staging、`traefik/acme.json chmod 600`、DNS A 记录和验收命令。

**本地验证：**
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml config --quiet`：通过
- 展开后的 prod compose：只有 `traefik` 保留宿主机 `80/443` 端口发布；backend / frontend / model-service / MySQL / Redis / MinIO 仅保留 compose 内网 `expose`
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests integration_tests task_engine_integration_tests --parallel`：通过
- `ctest --test-dir Backend\out\build\x64-debug -C Debug --output-on-failure`：3/3 通过
- `git diff --check`：通过（仅 Git 的 LF→CRLF 工作区提示）

**仍需实机验收：** `curl -vk https://${DOMAIN}`、`curl https://${DOMAIN}/api/health`、`curl -I http://${DOMAIN}`、SSL Labs ≥ A、`wss://${DOMAIN}/api/ws/images` 需要真实域名、可公网访问的 80/443 和部署机环境。

---

#### A2. 用户限流 / 任务配额缺失 ✅ 已完成（CODEX-A2）

**问题：** `ImageController::create` 没有任何速率/并发限制。ModelService 单次推理秒级、GPU 资源稀缺，任意登录用户可以无限循环 `POST /api/images` 把 GPU 队列打满，DoS 攻击门槛极低。

**涉及文件：**
- `Backend/src/controllers/image_controller.cpp`（限流入口）
- `Backend/src/services/image_service.cpp::create`（业务层并发上限）
- `Backend/include/services/i_cache_client.h`（如果借 Redis 做 token bucket）
- `Backend/config.json.example` + `.env.production.example` + `Backend.cpp::applyEnvOverrides`（新增 `rate_limit` 配置节）

**方案：**
- **每用户并发任务数**（业务层）：`ImageService::create` 前查 `count(*) where user_id=? and status in ('queued','pending','generating')`，超过阈值（如 3）返回 `ServiceError{429, "too_many_active_tasks"}`。
- **每用户 QPS**（接入层）：Redis token bucket，key `zimage:rate:user:<id>`，`INCR` + `EXPIRE`，超额 429。
- **匿名 IP 维度限流**（接入层）：覆盖 `/api/auth/register` / `/api/auth/login`，防爆破。

**测试：**
- 单测：mock Redis，验证 token bucket 边界（漏桶恢复、过期重置）
- 集成测：并发 N 次 `POST /api/images`，确认第 (并发上限+1) 次返回 429
- 错误码：用 `ServiceError` 体系扩 `RateLimited(429)` 一种 Kind，复用现有 HTTP 映射

**注意：** 测试相关 fakes 已有 `next_error` 注入字段（见 `image_service_test_fakes.h`），可复用模式。

**落地状态（2026-05-20）：**
- `ServiceError::tooManyRequests(...)` 统一返回 429，响应体继续使用现有 `code` / `message` 错误信封。
- 新增 `IRateLimiter` / `RedisTokenBucketLimiter`，用 `SCRIPT LOAD` + `EVALSHA` 缓存 Lua token bucket；Redis 不可用时按 `fail_open` 记录日志并放行。
- `ImageService::create` 在参数校验后查询 `countActiveTasksByUserId`，非管理员超过 `rate_limit.max_active_tasks_per_user` 时返回 `too_many_active_tasks`。
- `ImageController::create` 对非管理员使用 `zimage:rate:user:<id>` 做接入层 QPS 限流；`AuthController::registerUser` / `login` 使用 `zimage:rate:ip:<ip>` 做匿名 IP 限流。
- 新增 `rate_limit` 配置节和 `RATE_LIMIT_*` 环境变量覆盖，已同步 `config.json.example` / `.env.example` / `.env.production.example`。

**验证：**
- `unit_tests.exe`：208/208 通过
- `integration_tests.exe --gtest_filter=RateLimiterRedisIntegration.*`：3/3 通过

---

#### A3. ModelService 单点 + 缺 healthcheck ✅ 已完成（CODEX-A3）

**问题：** `MODEL_SERVICE_REPLICAS=1`、模型权重 GB 级、`docker-compose.prod.yml` 没看到 `healthcheck:` 配置。GPU 节点 OOM / 死锁 / pipeline 卡住时 compose 不会自动重启或剔除，请求会一直堆在 Backend worker 上直到 lease 超时。

**涉及文件：**
- `docker-compose.yml`（healthcheck 通用）+ `docker-compose.prod.yml`（生产副本/资源）
- `ModelService/model_service.py::/health`（已有，需要确认在 pipeline 卡住时也能返回 unhealthy）
- `Backend/src/services/task_engine.cpp`（worker 侧需要对 ModelService unhealthy 状态做退避）

**方案：**
- compose 加 `healthcheck` 段，命令 `curl -fsS http://localhost:8081/health || exit 1`，`interval: 30s` / `timeout: 5s` / `retries: 3` / `start_period: 120s`（等模型加载）
- `restart: unless-stopped` 已有，配合 healthcheck 自动重启
- 多副本：`MODEL_SERVICE_REPLICAS=2` + 前面加个 nginx upstream 做轮询（注意 GPU 卡数约束，单卡只能跑 1 个副本）
- Backend 侧消费 `active_kind` 字段做"忙时退避"（已有，验证逻辑覆盖）

**测试：**
- 手动 `docker kill` ModelService 容器，观察是否自动恢复
- pipeline 模拟死锁（在 `model_service.py` 临时插入 `time.sleep(60)`），验证 healthcheck 标 unhealthy
- Backend 在 ModelService 不可用期间的任务应被标 `failed` 或保留在 `queued` 等待恢复，不应 `lease_expires_at` 超时后直接 `timeout`

**落地状态（2026-05-20）：**
- `docker-compose.yml` / `docker-compose.prod.yml` 的 ModelService healthcheck 改为 Python JSON 检查：`unhealthy` 返回非 0，`loading` / `busy` / `healthy` 继续视为进程可用；探测参数为 `interval: 30s` / `timeout: 5s` / `retries: 3` / `start_period: 120s`。
- `ModelService/model_service.py` 新增 `MODEL_SERVICE_BUSY_UNHEALTHY_SECONDS`，`/health` 返回 `active_kind`、`active_seconds`、`active_generations`、`max_concurrent_generations`；生成任务超过阈值未结束时报告 `unhealthy`。
- `GenerationClient::checkHealth()` 保留远端 `busy` / `loading` / `unhealthy` 状态，不再因为 `model_loaded=true` 把 `busy` 折叠成 `healthy`。
- `TaskEngine` 在调用 `/generate` 前通过 `model_health::decideForGenerate()` 做门禁；模型 `loading`、`unhealthy` 或 `busy` 且满载时，已 claim 的任务写回 `queued`、清理 worker lease、记录 `model_service_unavailable`，然后按 5s/30s 退避；达到 10 次门禁失败后标记为 `failed`。

**验证：**
- `unit_tests.exe`：216/216 通过
- `ctest --test-dir Backend\out\build\x64-debug -C Debug -R IntegrationTests --output-on-failure`：2/2 通过
- `python -m compileall ModelService\model_service.py ModelService\main.py ModelService\train_lora.py`：通过
- `docker compose -f docker-compose.yml -f docker-compose.prod.yml config --quiet`：通过

---

#### A4. ModelService 零测试覆盖 ✅ 已完成（CODEX-A4）

**问题：** `ModelService/tests/` 是空目录（git ls-files 确认无任何 `.py` 测试文件），CI 的 `model-service-smoke` 只做 `python -m compileall`（语法检查）。生成路径、参数校验、错误处理、`/health` 状态机改动都没有任何回归保护。

**涉及文件：**
- `ModelService/tests/`（新增）
- `ModelService/requirements.txt`（新增 `pytest`、`pytest-asyncio`、`httpx`）
- `.github/workflows/ci.yml`（`model-service-smoke` job 加 `pytest` 步骤）

**方案（最小可行覆盖）：**
- **API 契约测试**：用 FastAPI `TestClient`，覆盖 `/generate` / `/edit` / `/health` 三个端点的 happy path + 参数缺失 + 不支持的 mime
- **状态机测试**：mock 重型 pipeline，验证 `active_kind` 在 `generate → busy → none` / `edit → busy → none` 路径下的迁移
- **错误处理**：CUDA OOM 模拟（抛 `torch.cuda.OutOfMemoryError`）→ 返回 503 而不是 500，避免 Backend 误判为永久失败重试耗尽次数

**测试前置：** pipeline 加载本身要 mock，否则 CI 跑不动。可以用 `monkeypatch` 替换 `ZImagePipeline.from_pretrained` 返回一个 stub 对象。

**注意：** 这一项的工作量比看起来大——ModelService 全部代码集中在 `model_service.py` 一个文件里，没有职责拆分，模块边界要先理清才能上 mock。可顺手把"模型加载 / 推理执行 / API 编排"拆成三个 module，再补测试。

**落地状态（2026-05-21）：**
- `ModelService/model_service.py` 缩到 6 行薄入口，运行方式仍是 `python model_service.py`。
- 新增 `ModelService/model_service_app/`：
  - `config.py`：env、日志、路径、CORS fail-fast、模型 dtype/device 配置
  - `api.py`：FastAPI app factory、路由、请求校验、错误映射、cleanup lifespan
  - `pipelines.py`：`ZImageModelService`、模型加载、推理封装
  - `state.py`：`active_kind` / busy / loading / unhealthy 状态机
  - `storage.py`：临时文件解析与清理
  - `schemas.py`：Pydantic request/response model
- 新增 `ModelService/tests/` 20 个 pytest 用例，使用 stub pipeline，不加载真实权重。
- `requirements.txt` 新增 `pytest>=8`、`pytest-asyncio`、`httpx`。
- `.github/workflows/ci.yml::model-service-smoke` 改为安装依赖、compileall `model_service.py model_service_app main.py train_lora.py`，再跑 `pytest tests/ -v`。
- README / README.zh-CN / CLAUDE 已更新 ModelService 测试说明。

**验证：**
- `cd ModelService; python -m pytest tests -v`：20/20 通过
- `python -m black --check ModelService\model_service.py ModelService\model_service_app ModelService\tests`：通过
- `python -m ruff format --check ModelService\model_service.py ModelService\model_service_app ModelService\tests`：通过
- `python -m compileall ModelService\model_service.py ModelService\model_service_app ModelService\main.py ModelService\train_lora.py`：通过
- `ModelService/model_service.py`：6 行，满足入口 < 100 行

**说明：** 当前生产 API 只有 `/generate`、`/health`、`/temp/{filename}`，没有 `/edit` 路由；因此 A4 测试覆盖按现有真实契约落地，没有凭空新增 edit 端点。

---

#### A5. MinIO / MySQL 备份策略缺失 ✅ 已完成（CODEX-A5）

**问题：** MinIO 单实例 + MySQL 仅靠 named volume，没有定时备份、binlog 归档、PITR 方案。物理卷损坏或运维误删 = 全量用户数据丢失。

**涉及文件：**
- `docker-compose.prod.yml`（新增备份 sidecar 或 ops profile）
- `scripts/`（新增 `backup-mysql.sh` / `backup-minio.sh`）
- `init-db/01-schema.sql`（不变；用于灾备重建参考）

**方案：**
- **MySQL：** 在 `--profile ops` 下加 `mysql-backup` 服务，定时 `mysqldump --single-transaction --routines` 到挂载卷或 S3。生产环境强烈建议开 `log-bin` + 归档 binlog 到对象存储，支持 PITR。
- **MinIO：** 用 `mc mirror` 同步到异地 MinIO / S3。或上 MinIO 分布式部署（4+ 节点 erasure coding），不再依赖外部备份。
- **恢复演练**：备份只是写入；恢复链路至少要演练一次，写进 runbook。

**测试：** 模拟 MySQL volume 损坏，按 runbook 从最近备份 + binlog 恢复，验证 RPO/RTO 在预期范围内。

**落地状态（2026-05-21）：**
- `docker-compose.yml::mysql` 启用 `--log-bin=mysql-bin`、`--binlog-format=ROW`、`--binlog-expire-logs-seconds=${MYSQL_BINLOG_EXPIRE_SECONDS:-1209600}`。
- `docker-compose.prod.yml` 在 `ops` profile 下新增 `mysql-backup`，每天 `${MYSQL_BACKUP_TIME_UTC:-02:00}` UTC 生成 `mysqldump --single-transaction --routines --triggers --events --source-data=2` 的 gzip 备份，并归档 `mysql-bin.*`。
- `docker-compose.prod.yml` 在 `ops` profile 下新增 `mc-mirror`，每天 `${MINIO_MIRROR_TIME_UTC:-03:00}` UTC 将 MinIO bucket 与 MySQL 备份 volume 镜像到 S3 兼容远端。
- 新增 `scripts/restore-mysql.sh`，支持从指定 `.sql.gz` 恢复，并可按 `--binlog-dir` + `--until` 做 PITR 辅助恢复。
- 新增 `docs/runbook-backup-restore.md`，明确 RPO ≤ 24h、RTO ≤ 2h、每周抽查恢复、每季度完整演练、手动触发备份/镜像命令和恢复步骤。
- `.env.production.example` 新增 `MYSQL_BACKUP_*`、`MINIO_MIRROR_*`、`BACKUP_S3_*`、`MYSQL_BINLOG_EXPIRE_SECONDS` 占位符。

**本地验证：**
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml config --quiet`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml --profile ops config --quiet`：通过
- `bash -n scripts/backup-mysql.sh scripts/mirror-minio.sh scripts/restore-mysql.sh`：通过
- `git diff --check`：通过（仅 Git 的 LF→CRLF 工作区提示）

**仍需实机验收：** 手动触发一次 `mysql-backup`、一次 `mc-mirror`，确认远端 S3 对象存在；在测试库执行一次 `scripts/restore-mysql.sh` 恢复并校验数据一致性；用 `SHOW BINARY LOGS` 确认 binlog 列表。

---

### P3-B 建议补强（小流量上线前完成）

#### B1. JWT 刷新 / 撤销机制 ✅ 已完成（CODEX-B1）

**问题：** `JWT_EXPIRATION_HOURS=24` + 静态 `JWT_SECRET` + 无黑名单。密钥泄露或用户主动登出场景下，token 在过期前一直有效。

**方案：**
- 短期 access token（15 分钟）+ 长期 refresh token（7 天），refresh token 存 DB 可吊销
- 或者维护 Redis 黑名单 `zimage:jwt:revoked:<jti>`，登出 / 强制下线时写入，`jwt_middleware` 验证时多查一次
- 引入 `jti` claim 让单个 token 可识别

**取舍：** 黑名单方案破坏 JWT "无状态" 优势，但实施成本最低。如果未来要做"管理员强制下线某用户"，建议直接走 refresh token 方案。

**落地状态（2026-05-21）：**
- `utils::createToken()` 现在签发短期 access token，包含 `exp`、`type=access`、`jti`、`role`；默认 TTL 为 `JWT_ACCESS_EXPIRATION_MINUTES=15`。
- 新增 `utils::issueRefreshToken()` / `verifyRefreshToken()`，refresh token 包含 `type=refresh` 和 UUID v4 `jti`，默认 TTL 为 `JWT_REFRESH_EXPIRATION_DAYS=7`。
- 新增 `IRefreshTokenStore`、`RedisRefreshTokenStore`、`InMemoryRefreshTokenStore`。生产路径使用 Redis key `zimage:refresh:<jti>` 保存 user id，并用原子 get+del 完成 refresh rotate；测试路径用内存 store 避免把 DB-only 集成测试绑定到 Redis。
- `/api/auth/login` 返回 `{access_token, refresh_token, expires_in: 900, user}`，并保留旧字段 `token` 兼容现有前端。
- 新增 `/api/auth/refresh`：验证 refresh token 后消费旧 `jti`，读取当前用户信息并签发新的 access/refresh pair；已 rotate 的旧 refresh token 再次使用会返回 `401 refresh_token_revoked`。
- 新增 `/api/auth/logout`：撤销提交的 refresh token；access token 不做黑名单，按 15 分钟 TTL 自然过期。
- 前端 `request.js` 在 access token 过期或 API 返回 401 时自动调用 `/api/auth/refresh`，并用全局 `refreshPromise` 合并并发 401，避免刷新风暴；`auth` store 保存 `refreshToken` 并在 logout 时 best-effort revoke。
- `config.json.example`、`.env.example`、`.env.production.example`、`docker-compose.yml` 已从 `JWT_EXPIRATION_HOURS` 迁移到 `JWT_ACCESS_EXPIRATION_MINUTES` + `JWT_REFRESH_EXPIRATION_DAYS`；旧 `JWT_EXPIRATION_HOURS` env 仅作为兼容转换。

**验证：**
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests --parallel`：通过
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests integration_tests task_engine_integration_tests --parallel`：通过
- `ctest --test-dir Backend\out\build\x64-debug -C Debug --output-on-failure`：3/3 通过
- `unit_tests.exe --gtest_filter=JWT.*:AuthRefresh.*`：17/17 通过，覆盖 access 过期、refresh 过期、refresh rotate、logout revoke、旧 refresh 失效
- `unit_tests.exe --gtest_filter=AuthServiceRepoErrors.*:ControllerRateLimit.LoginReturns429WhenIpBucketIsExceeded:ControllerRateLimit.RegisterReturns429WhenIpBucketIsExceeded`：5/5 通过
- `cd ZImageFrontend; npm run build`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml config --quiet`、`--profile ops`、`--profile monitoring`：通过
- `git diff --check`：通过（仅 Git 的 LF→CRLF 工作区提示）

---

#### B2. 可观测性 — 仅有 `/api/metrics/cache` ✅ 已完成（CODEX-B2）

**问题：** 当前只有缓存命中率指标，没有：请求 QPS / 延迟分布、ModelService 推理耗时分布、worker 队列长度、MinIO 写入失败率、MySQL 慢查询、JVM/系统层指标。运营期出问题只能 `docker logs`。

**方案：**
- Backend 接入 `prometheus-cpp` 暴露 `/metrics`（QPS / latency histogram / worker queue depth / lease 过期次数 / DB pool 饱和度）
- ModelService 接 `prometheus-client` 暴露推理时长、显存占用、active_kind 状态
- 部署 Prometheus + Grafana sidecar（或外部托管），加入 `docker-compose.prod.yml`
- 日志聚合走 Loki / ELK，至少把 spdlog 输出格式标准化（JSON 行式 + 固定字段 `trace_id` / `user_id` / `task_id`）

**先做最小集：** Backend 加 `/metrics`，导出 `image_task_total{status=...}` / `image_request_duration_seconds`；其余可分阶段补。

**落地状态（2026-05-21）：**
- Backend 新增 `services/metrics_registry.h/.cpp`，渲染 Prometheus text exposition format；`GET /metrics` 不走 admin guard，`GET /api/metrics/cache` 仍保持 admin-only JSON。
- `handler_utils` 自动记录 JSON controller 请求耗时到 `image_request_duration_seconds{endpoint,status}`。
- `ImageService` / `TaskEngine` 记录 `image_task_total{status}`，并维护近似 `worker_queue_depth`。
- `GenerationClient` 记录 `/health`、`/generate`、`/temp` 的 `model_service_call_duration_seconds{endpoint,status}`。
- `ModelService/model_service_app/metrics.py` 基于 `prometheus-client` 暴露 `model_service_generation_seconds`、`model_service_generation_total`、`model_service_active_generations`、`model_service_health_status`、`model_service_gpu_memory_allocated_bytes`。
- `docker-compose.prod.yml` 新增 `monitoring` profile：Prometheus 抓取 `backend:8080/metrics` 和 `model-service:8081/metrics`；Grafana 预置 `monitoring/dashboards/zimage.json`。
- `Backend/vcpkg.json` 加入 `prometheus-cpp`，`ModelService/requirements.txt` 加入 `prometheus-client`。

**验证：**
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests --parallel`：通过（首次 glob 刷新后重跑通过）
- `unit_tests.exe --gtest_filter=MetricsRegistryTest.*`：3/3 通过
- `ctest --test-dir Backend\out\build\x64-debug -C Debug -R UnitTests --output-on-failure`：通过
- `cd ModelService; python -m pytest tests -v`：21/21 通过
- `python -m compileall ModelService\model_service.py ModelService\model_service_app ModelService\main.py ModelService\train_lora.py`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml --profile monitoring config --quiet`：通过

**仍需实机验收：** 启动 `--profile monitoring` 后访问本机 `PROMETHEUS_PORT` / `GRAFANA_PORT`，确认 dashboard 能看到真实流量曲线；如生产端口不是 8080/8081，需要同步调整 `monitoring/prometheus.yml`。

---

#### B3. Secrets 管理 ✅ 已完成（CODEX-B3）

**问题：** `JWT_SECRET` / `MYSQL_ROOT_PASSWORD` / `MINIO_ROOT_PASSWORD` 全部走 `.env`，文件落盘在主机上。开发机 / CI runner / 主机被入侵 = 密钥全失守。

**方案：**
- Docker Swarm：用 `docker secret`，compose 引用 `secrets:` 段
- K8s：迁到 `Secret` 对象 + Sealed Secrets / External Secrets
- 单机部署：至少把 `.env` 文件权限改成 `chmod 600` + 不要进版本控制（已 gitignore）

**落地状态（2026-05-22）：**
- `Backend.cpp::applyEnvOverrides` 新增文件型 secret 读取：`DB_PASSWORD_FILE`、`JWT_SECRET_FILE`、`REDIS_PASSWORD_FILE`、`CACHE_PASSWORD_FILE`、`MINIO_SECRET_KEY_FILE`。`*_FILE` 优先于明文 env；文件不可读或为空会在启动加载配置时抛出包含变量名和路径的明确错误。
- `docker-compose.prod.yml` 新增 external Docker secrets：`jwt_secret`、`db_password`、`mysql_root_password`、`redis_password`、`minio_password`、`backup_s3_access_key`、`backup_s3_secret_key`、`grafana_admin_password`。
- 生产 overlay 中 Backend、MySQL、Redis、MinIO、db-migrate、mysql-backup、mc-mirror、Grafana 均改为通过 `/run/secrets/*` 或官方 `*_FILE` 变量消费 secret，prod compose 不再渲染明文密码 / JWT secret。
- `.env.production.example` 移除 `JWT_SECRET=`、`DB_PASSWORD=`、`REDIS_PASSWORD=`、`CACHE_PASSWORD=`、`MINIO_ROOT_PASSWORD=`、`BACKUP_S3_*KEY=`、`GRAFANA_ADMIN_PASSWORD=` 等明文字段，改为 `*_FILE=/run/secrets/...` 与 secret name 配置。
- `scripts/run-db-migrations.sh`、`backup-mysql.sh`、`mirror-minio.sh`、`restore-mysql.sh` 支持 `*_FILE` secret 输入，并对缺失/空文件给出明确错误。
- 新增 `k8s/secrets.yaml.example`，记录 K8s / Sealed Secrets 路径的示例和不要提交明文 Secret 的提示。
- README / README.zh-CN / CLAUDE / backup runbook 已同步 Docker Secrets 创建、`.env.production chmod 600` 和生产 `.env` 不落明文 secret 的说明。

**验证：**
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests integration_tests task_engine_integration_tests --parallel`：通过
- `unit_tests.exe --gtest_filter=BackendConfig.*:MysqlConfig.*`：10/10 通过，覆盖 `*_FILE` 优先级、明文 env fallback、缺失 secret 文件、空 secret 文件。
- `ctest --test-dir Backend\out\build\x64-debug -C Debug --output-on-failure`：3/3 通过
- `cd ModelService; python -m pytest tests -v`：21/21 通过
- `cd ZImageFrontend; npm run build`：通过
- `docker compose --env-file .env.example -f docker-compose.yml config --quiet`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml config --quiet`、`--profile ops`、`--profile monitoring`：通过
- `bash -n scripts/backup-mysql.sh scripts/mirror-minio.sh scripts/restore-mysql.sh scripts/run-db-migrations.sh`：通过
- `.env.production.example` 明文 secret 搜索无匹配；渲染后的 prod config 未出现明文 password / secret / key（仅保留非 secret 的 `MINIO_ACCESS_KEY` 用户名）。
- `clang-format --dry-run --Werror Backend/src/Backend.cpp Backend/tests/unit/test_db_config.cpp`：通过
- `git diff --check`：通过（仅 Git 的 LF→CRLF 工作区提示）

---

#### B4. 前端只有 3 个 view ✅ 已完成（CODEX-B4）

**问题：** `src/views/` 仅 `Home.vue` / `Login.vue` / `Register.vue`。需要核实 `Home.vue` 是否承载了所有功能（任务列表、生成表单、详情查看、取消重试、个人设置、错误页 404/500），否则用户体验完整度不够。

**方案：**
- 至少补 `NotFound.vue` + 全局错误边界
- 拆出 `Profile.vue`（用户信息 + 修改密码）
- 如果有 admin 用户，应该有 `Admin.vue`（消费 `/api/metrics/cache`）

**落地状态（2026-05-21）：**
- 新增 `ZImageFrontend/src/views/NotFound.vue`，catch-all 路由显示 404；普通用户访问 `/admin` 会跳转 `/403`。
- 新增 `ZImageFrontend/src/views/Profile.vue`，展示用户名、邮箱、角色、注册时间占位，并提供改密表单。
- 新增 `ZImageFrontend/src/views/Admin.vue`，复用 `CacheMetricsPanel` 展示 admin-only cache metrics。
- `router/index.js` 新增 `/profile`、`/admin`、`/403` 与 catch-all 路由，并用 `requiresAdmin` 守卫保护 `/admin`。
- `Home.vue` 用户菜单新增个人中心和管理面板入口。
- `App.vue` 增加 `onErrorCaptured` 错误边界，使用 `el-result` 展示运行时错误。
- `auth.js` 新增 `getProfile()` / `changePassword()`。
- Backend 新增 `/api/auth/me` GET 与 `/api/auth/password` PUT；改密校验旧密码和新密码长度，成功后通过 `IRefreshTokenStore::revokeUser()` 撤销该用户全部 refresh token，前端随后清理本地 auth 状态并跳转登录。
- `RedisRefreshTokenStore` 新增 user 维度索引 `zimage:refresh:user:<userId>`，支持改密后批量撤销 refresh token。

**验证：**
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests --parallel`：通过
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests integration_tests task_engine_integration_tests --parallel`：通过
- `ctest --test-dir Backend\out\build\x64-debug -C Debug --output-on-failure`：3/3 通过
- `unit_tests.exe --gtest_filter=AuthRefresh.*:AuthProfile.*:JWT.*`：21/21 通过，覆盖 profile、旧密码错误、新密码过短、改密后全部 refresh token 失效、新密码可登录
- `unit_tests.exe --gtest_filter=AuthServiceRepoErrors.*`：3/3 通过
- `cd ZImageFrontend; npm run build`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml config --quiet`、`--profile ops`、`--profile monitoring`：通过
- `git diff --check`：通过（仅 Git 的 LF→CRLF 工作区提示）

---

#### B5. CORS 默认值是开发地址 ✅ 已完成（CODEX-B5）

**问题：** `model_service.py::ALLOW_ORIGINS` 默认 `["http://localhost:3000"]`，部署时必须靠 env override。如果运维忘记设置，前端报跨域错。

**方案：**
- 在 `.env.production.example` 显式列出 `MODEL_SERVICE_ALLOW_ORIGINS=https://your-domain.com`
- 启动时如果 origins 包含 `localhost` 且 `ENV=production`，打 warning（或直接 fail-fast）

**注意：** 实际上前端不直接调 ModelService（架构图 Frontend → Backend → ModelService），所以这条优先级不高，但留个口子防止后续接入新前端时翻车。

**落地状态（2026-05-21）：**
- `ModelService/model_service.py` 读取 `ENV`，生产环境拒绝 `*`、`localhost`、`127.0.0.1`、`[::1]`、`0.0.0.0` origins；非生产保留 `*` warning。
- `Backend.cpp::applyEnvOverrides` 新增 `CORS_ENABLED` / `CORS_ALLOW_ORIGINS` 覆盖，支持逗号分隔 origins。
- `Backend/src/main.cpp` 在启动早期校验生产 CORS，命中 wildcard/local origins 时 fail-fast。
- `docker-compose.yml` 将 `ENV`、Backend CORS env、ModelService CORS env 注入容器；`.env.production.example` 设置 `ENV=production`、`CORS_ALLOW_ORIGINS=https://CHANGE_ME_DOMAIN`、`MODEL_SERVICE_ALLOW_ORIGINS=https://CHANGE_ME_DOMAIN`。
- README / README.zh-CN / CLAUDE 已补生产 CORS 配置说明。

**验证：**
- `cmake --build Backend\out\build\x64-debug --config Debug --target Backend unit_tests integration_tests task_engine_integration_tests --parallel`：通过
- `ctest --test-dir Backend\out\build\x64-debug -C Debug --output-on-failure`：3/3 通过
- `python -m black --check ModelService\model_service.py`：通过
- `python -m ruff format --check ModelService\model_service.py`：通过
- `python -m compileall ModelService\model_service.py ModelService\main.py ModelService\train_lora.py`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml config --quiet`：通过
- `docker compose --env-file .env.production.example -f docker-compose.yml -f docker-compose.prod.yml --profile ops config --quiet`：通过
- 负例：`ENV=production` + `MODEL_SERVICE_ALLOW_ORIGINS=http://localhost:3000` 时 ModelService import 直接报错退出
- 负例：`ENV=production` + Backend 默认 localhost CORS 时 Backend 直接报 `Fatal startup error: CORS allow_origins contains unsafe production origins`

---

### P3-C 可选优化（运营期逐步遇到）

- **OpenAPI / Swagger 文档** ✅ 已完成（2026-06-07）：新增 `docs/openapi.yaml` 覆盖 Backend 公开 API、鉴权、错误 envelope、`X-Request-Id` 响应头；`docs/openapi.md` 提供 Swagger UI 预览命令。
- **审计日志** ✅ 已完成（2026-06-07）：Backend 新增 `utils/audit_log` 与 `controllers::auditRequest`，覆盖 auth register/login/refresh/logout/changePassword、image create/delete/cancel/retry、admin cache metrics 访问；日志仅记录 `timestamp / event / outcome / request_id / client_ip / resource_id / status / user_id`，不记录 token、密码、prompt、base64、图片内容或完整请求体。
- **压测数据** ✅ 已完成（2026-06-07）：新增 `ops/load-tests/k6_backend.js`、`wrk_health.lua`、`wrk_images_list.lua` 与 runbook；默认压健康/读取路径，任务创建需 `ENABLE_CREATE=true` 显式开启。
- **`train_lora.py` 镜像隔离** ✅ 已完成（2026-06-07）：`ModelService/Dockerfile` runtime 只复制推理入口与 `requirements-runtime.txt`，不再包含 `train_lora.py`；新增 `ModelService/Dockerfile.train` 作为训练专用镜像。
- **数据库连接池调优** ✅ 已完成（2026-06-07）：`DB_POOL_SIZE` 接入 Backend 配置覆盖、compose、示例 env 与 `MysqlConfig::pool_size`；启动时按 `BACKEND_THREADS + TASK_ENGINE_WORKERS + 1` 估算单副本 session 需求并告警，`/metrics` 暴露 `db_pool_configured_connections`。当前 DBManager 是 thread-local session 设计，不强行引入借还式连接池。
- **请求 trace ID** ✅ 已完成（2026-06-07）：Backend `utils/request_id` + PreRouting/PreSending advice 读取或生成 `X-Request-Id` 并回显，`/generate` 出站透传；ModelService `trace.py` 纯 ASGI 中间件读取/生成并回显，日志统一带 `[request_id]`。注意：业务幂等键 `request_id`（请求体）与跨服务 trace `X-Request-Id`（请求头）是两个不同概念，仅在 `/generate` 路径上取值一致。

---

### 落地优先级建议

如果资源有限，按以下顺序推进：

1. **第一阶段（阻断 → 可上线）：** A1 HTTPS + A2 限流 + A3 ModelService healthcheck → 可以小范围内测
2. **第二阶段（备份 + 测试）：** A4 ModelService 测试 + A5 备份 → 数据可恢复，回归有保护
3. **第三阶段（可观测）：** B2 监控 + B1 JWT 刷新 + B3 secrets → 可以放心面向真实用户
4. **第四阶段（运营完善）：** B4 前端补全 + B5 CORS + C 系列优化

预估第一 + 第二阶段大概 1-2 周专注工作量，看是否接入 K8s / 云资源可能再加 3-5 天。

---

### 下一次会话从这里继续

P3 已成清单；CODEX-R1 / P2 遗留 1、CODEX-A1（HTTPS / TLS 终结层）、CODEX-A2（限流 / 任务配额）、CODEX-A3（ModelService healthcheck + Backend 退避）、CODEX-A4（ModelService 测试）、CODEX-A5（备份 / 灾难恢复）、CODEX-B1（JWT 刷新 / 撤销）、CODEX-B2（Prometheus / Grafana）、CODEX-B3（Secrets）、CODEX-B4（前端补全）、CODEX-B5（生产 CORS fail-fast）已落地。上线阻断项和建议补强项已全部落地，之后视需要补 C 系列运营优化。
