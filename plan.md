# ZImage Backend 优化计划

> **进度（2026-05-17）：** P1 + P2 全部完成。最终单测 190 个用例全过。详见文末 [进度日志](#进度日志-2026-05-17)。

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

**PR4 — presigned URL 缓存** ✅ 已完成（已提交，commit hash 待补）

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
