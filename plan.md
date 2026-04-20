# ZImage Backend 优化计划

## 1. MinIO 客户端连接复用

**问题：** `MinioClient` 每次调用 `putObject`/`getObject`/`presignGetUrl`/`deleteObject`/`ensureBucketExists` 都通过 `createClient(config_)` 重新创建 `ClientBundle`（含 BaseUrl 解析、Provider 构造、Client 实例化）。在 `presignListImages` 等列表场景下 N 张图触发 N 次连接创建。

**涉及文件：**
- `Backend/src/services/minio_client.cpp`
- `Backend/include/services/minio_client.h`

**方案：**
- 将 `ClientBundle` 作为 `MinioClient` 的成员变量，构造时初始化一次
- 可用 `std::unique_ptr<ClientBundle>` 延迟初始化，或直接在构造函数中创建
- 所有方法改为使用 `bundle_->client` 而非每次 `createClient`

---

## 2. 拆分 image_service.cpp

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

## 3. 统一错误处理模式 — Repo 层迁移到 std::expected

**问题：** 当前存在三种错误模式并存：
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

---

## 4. C++23 std::ranges 替换手写循环

**问题：** 多处手写 for 循环做 transform/filter 操作，可用 `std::ranges` 表达更简洁。

**涉及文件及具体位置：**

| 文件 | 位置 | 当前写法 | ranges 替换 |
|------|------|---------|------------|
| `image_controller.cpp:28-31` | `toListJson` | for 循环 push_back `item.toJson()` | `std::views::transform` |
| `redis_client.cpp:73-77` | `rebuildTaskQueue` | for 循环 push_back `std::to_string(taskId)` | `std::views::transform` |
| `image_service.cpp:614-635` | `presignListImages` | for 循环逐个 presign | `std::ranges::for_each` + filter |
| `image_service.cpp:236-239` | `toLower` | `std::transform` with lambda | `std::ranges::transform` |

---

## 5. 残留 std::to_string → std::format 统一

**问题：** 项目大部分地方已用 `std::format`，但仍有十余处使用 `std::to_string`，风格不一致。

**涉及文件：**
- `Backend/src/services/redis_client.cpp` — `std::to_string(taskId)` 出现多次
- `Backend/src/services/minio_client.cpp:25` — `std::to_string(response.status_code)`
- `Backend/src/database/ImageRepo.cpp` — 可能有零散使用

**方案：** 全局搜索 `std::to_string` 替换为 `std::format("{}", value)`。对于 Redis 拼接场景，`std::format` 可读性更好。

---

## 6. 扩大 std::string_view 使用范围

**问题：** 多处函数参数用 `const std::string&` 但只读不存储，可以改 `std::string_view` 减少不必要的拷贝和临时对象构造。

**涉及文件及候选函数：**
- `image_service.cpp` — `normalizeStatus()`, `toLower()`, `getStringField()`
- `ImageRepo` — `findByRequestIdAndUserId()`, `findByUserIdAndStatus()` 等查询方法的 string 参数
- `redis_client.cpp` — `leaseKey()` 内部拼接

**注意：** 如果函数内部需要将参数传给接受 `const std::string&` 的第三方 API（如 mysqlx bind），则不适合改为 `string_view`，需逐个判断。

---

## 7. Controller 样板代码消除

**问题：** `image_controller.cpp` 每个 handler 都重复：创建 resp → setContentType → resolveUserId → new ImageService → 调方法 → 错误/成功处理 → callback。

**涉及文件：**
- `Backend/src/controllers/image_controller.cpp`
- `Backend/include/controllers/image_controller.h`

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

---

## 8. Redis 引入 Cache-Aside 缓存层

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

**PR1 — `CacheClient` 基础设施**

*内容：* 新增 `CacheClient` 抽象（`get` / `setex` / `del` / `bumpVersion`）+ `cache` 配置节 + Redis 可用性降级逻辑。

*必做测试场景：*
| 场景 | 断言 |
|---|---|
| `setex / get` 往返 | 写入后能读出同值；过期后读不到 |
| `del` 单 key | 删后 `get` 返回 `nullopt` |
| `bumpVersion` 语义 | 连续调用返回递增整数；并发调用无丢失（`INCR` 原子性） |
| **Redis 不可用降级**（核心） | `isAvailable()==false` 时：`get` 返回 `nullopt`，`setex / del / bumpVersion` **不抛异常**、静默失败 |

---

**PR2 — `getById` 元数据缓存 + invalidation**

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

**PR3 — 列表缓存 + `list_ver` 版本号失效**

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

**PR4 — presigned URL 缓存**

*内容：* 在 `ImageService::getById` 和 `presignListImages` 中调用 `storage.presignUrl(storage_key)` 的位置加一层缓存；key 按 `storage_key` 哈希，TTL 取 MinIO presigned URL TTL 的 80%。

*必做测试场景（对应 review 第 6 条）：*
| 场景 | 断言 |
|---|---|
| **按 `storage_key` 索引** | 两次 `presignUrl(同一 storage_key)` 第二次不调用 MinIO SDK（mock/spy `ImageStorage::presignUrl`） |
| **TTL 严格小于 MinIO URL TTL**（核心） | 启动时或 `CacheClient` 构造时断言 `cache_ttl < minio_presign_ttl`；配置错误应 **启动失败**而非返回过期链接 |
| **失效** | `deleteById` 后对应 `zimage:img:url:<hash>` 被删除 |
| **Redis 挂时降级** | 直接走 `storage.presignUrl()`，不抛异常 |

---

**PR5 — 监控指标**

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

| 优先级 | 任务 | 理由 |
|--------|------|------|
| P0 | 1. MinIO 连接复用 | 真实性能问题，列表页 N 次连接创建 |
| P0 | 2. 拆分 image_service.cpp | 面试高频追问点，展示架构能力 |
| P0 | 8. Redis Cache-Aside 缓存层 | 补齐 Redis 核心用法，面试必问点，工程收益 + 展示价值双高 |
| P1 | 3. 统一错误处理 | 一致性问题，面试容易被问 |
| P1 | 4. std::ranges 替换循环 | 最直观的 C++23 升级展示 |
| P2 | 5. std::to_string → std::format | 风格统一，改动小 |
| P2 | 6. std::string_view | 性能微优化，需逐个判断兼容性 |
| P2 | 7. Controller 样板消除 | 代码整洁度，非阻塞 |
