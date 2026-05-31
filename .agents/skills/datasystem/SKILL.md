---
name: datasystem
description: >
  本项目使用元戎 Datasystem（dscli / KVClient）作为分布式 KV 缓存。Precalc 写入
  （Set）、RankSub 读取（Get）都通过它。客户端与 worker 之间走宿主机共享内存通信，
  这点和普通 BRPC 微服务完全不同，部署时极易踩坑。只要涉及 KVWorker / KVClient /
  元戎 / datasystem / dscli 的部署、报错（尤其 "hash master get failed"、
  "CreateMetadataToMaster failed"、"KVClient Set/Get failed"）、K8s/Docker 编排、
  或新增一个要读写 KV 的服务，都应参考本 skill。
---

## 核心心智模型：为什么 datasystem 和别的服务不一样

普通 BRPC 服务之间是纯网络（TCP）通信，谁能 ping 通谁就能调用。**Datasystem 不是**——
它的客户端（KVClient）和 worker 之间，对象数据走的是**宿主机共享内存**（POSIX shm，
落在 `/dev/shm`），只有元数据/控制面走网络。这是为了零拷贝、低时延。

直接后果：**KVClient 所在的 Pod 必须和它要通信的 worker 在同一个物理节点上，并且能访问
同一块 `/dev/shm`。** 跨节点时共享内存映射不到，客户端找不到负责该 key 的 worker，就会报
`hash master get failed / CreateMetadataToMaster failed`。

worker 启动参数里的 `--oc_shm_transfer_threshold_kb 0` 表示"所有对象都走共享内存"
（阈值为 0），所以这条共享内存通道是强制的，不能绕过。

## 三个必须同时满足的条件

要让一个服务成功读写 KV，它的 Pod 必须满足下面全部三点，缺一不可：

1. **共享同一块共享内存** —— 挂载宿主机的 `/dev/shm`（hostPath），而不是容器默认那块
   64MB 的小 tmpfs。worker 默认申请 `--shared_memory_size_mb 2048`。
2. **共享主机 IPC 命名空间** —— `hostIPC: true`，否则即使挂了 `/dev/shm`，IPC 资源
   （信号量等）也隔离在各自命名空间里，attach 不到 worker 的段。
3. **同节点要有 worker** —— KVClient 用 `PREFERRED_SAME_NODE` 亲和策略，期望本节点就有
   一个 worker。注意它的语义是"优先本地、无本地则兜底远端"，所以缺本地 worker **不会在
   选择阶段直接报错**；但因为 worker 带了 `--oc_shm_transfer_threshold_kb 0`（对象传输
   强制走共享内存），而共享内存只在同节点有效，兜底到远端 worker 时这条强制通道用不上，
   会走不通或退化。所以 **kv-worker 要用 DaemonSet**（每节点一个），保证客户端落在哪都有
   一个能用共享内存的同节点 worker，而不是去赌远端兜底的行为。

## 各项设置逐条说明

下面这些是把一个 KV 客户端服务（如 precalc / rank-sub）跑通所需的 K8s 设置：

```yaml
spec:
  template:
    spec:
      hostIPC: true                 # ① 共享主机 IPC 命名空间，才能 attach worker 的 shm 段
      containers:
      - name: <svc>
        securityContext:
          privileged: true          # ② datasystem 需要访问宿主机 shm、做 CPU 亲和(taskset) 等特权操作
        env:
        - name: HOST_ID             # ③ 节点名，供 PREFERRED_SAME_NODE 亲和匹配；
          valueFrom:                #    客户端 sdOpts.hostIdEnvName="HOST_ID"，必须和 worker 用同一个 env 名
            fieldRef:
              fieldPath: spec.nodeName
        volumeMounts:
        - name: shm
          mountPath: /dev/shm       # ④ 把宿主机 /dev/shm 挂进容器，与同节点 worker 共用同一块共享内存
      volumes:
      - name: shm
        hostPath:
          path: /dev/shm
```

worker（kv-worker）侧的关键设置：

```yaml
kind: DaemonSet                      # ⑤ 每个节点一个 worker，匹配客户端 PREFERRED_SAME_NODE
spec:
  template:
    spec:
      hostIPC: true                  # 同 ①
      containers:
      - name: kv-worker
        env:
        - name: POD_IP
          valueFrom:
            fieldRef:
              fieldPath: status.podIP
        - name: worker_address       # ⑥ worker 注册到 etcd 的可达地址；K8s 里用 Pod IP
          value: "$(POD_IP):31501"   #    注意：$(VAR) 只能引用同一 env 列表里、且在它之前定义的变量，
                                     #    所以 POD_IP 必须排在 worker_address 之前
        - name: HOST_ID              # 同 ③，worker 和客户端必须用相同的 HOST_ID 语义（都=节点名）
          valueFrom:
            fieldRef:
              fieldPath: spec.nodeName
        securityContext:
          privileged: true           # 同 ②
        volumeMounts:
        - name: shm
          mountPath: /dev/shm        # 同 ④
      volumes:
      - name: shm
        hostPath:
          path: /dev/shm
```

逐条对照：

| 编号 | 设置 | 作用 | 不设会怎样 |
|------|------|------|-----------|
| ① | `hostIPC: true` | 共享主机 IPC 命名空间 | 客户端 attach 不到 worker 的共享内存段 |
| ② | `privileged: true` | 允许访问宿主机 shm / CPU 亲和等 | datasystem 初始化/共享内存操作被拒 |
| ③ | `HOST_ID = spec.nodeName` | 节点亲和匹配的依据 | `PREFERRED_SAME_NODE` 无法识别本节点 worker |
| ④ | `/dev/shm` hostPath | 客户端与 worker 共用同一块共享内存 | 用容器内 64MB tmpfs，看不到 worker 的段 |
| ⑤ | kv-worker 用 DaemonSet | 每节点都有同节点 worker | 客户端落到没有同节点 worker 的节点 → 只能兜底远端，shm 强制通道用不上 → 走不通/退化 |
| ⑥ | `worker_address=$(POD_IP):31501` | worker 注册可达地址到 etcd | 注册成不可达地址，元数据路由失败 |

> 两个动作各管一件事，别混淆：①④（挂 `/dev/shm` + `hostIPC`）让客户端**能用共享内存**这条
> 传输方式——这是原始 `hash master get failed` 的主因（客户端根本没挂 shm，连本地 worker
> 都用不了）；⑤（DaemonSet）保证客户端所在节点**有一个同节点 worker** 可以配合那条 shm 路。
> 只做 DaemonSet 不挂 shm 修不好；只挂 shm 不保证同节点 worker，则要依赖不可靠的远端兜底。
> 两个一起才对。
>
> 补充：`PREFERRED_SAME_NODE` 是"优先本地、无本地兜底远端"，**缺本地 worker 不会在选择阶段
> 直接失败**；DaemonSet 的意义是让 shm 快路径始终可用，而非"否则选不到 worker"。远端兜底在
> `oc_shm_transfer_threshold_kb=0` 下到底直接失败还是退化走网络，属版本相关行为，建议实测确认。

## 哪些服务需要这套配置？

判断标准很简单：**看源码里是否直接用了 `datasystem::KVClient`**，而不是看文档里写"依赖 KVWorker"。

- **需要**：precalc（`KVClient.Set`）、rank-sub（`KVClient.Get`）——它们 include
  `<datasystem/kv_client.h>`，并用 `datasystem::ServiceDiscovery` + `PREFERRED_SAME_NODE`。
- **不需要**：rank-master——它用的是项目自己的 `common::ServiceDiscovery`（BRPC 服务发现）
  去发现 rank-sub，并不直接连 KVWorker。容易被文档误导，务必以源码为准。

新增一个要读写 KV 的服务时，照着上面 ①~④ 给它的 Pod 加配置，并确认它设置了 `HOST_ID`。

## Docker Compose 下的等价物

单机 compose 之所以不用这么麻烦，是因为：所有容器 `ipc: host` + 都挂 `/dev/shm`，
且天然在同一台机器上，所以共享内存和同节点这两个条件自动满足。compose 里 worker 地址
用稳定 DNS（`kv-worker:31501`），K8s 里因为没有稳定单实例 DNS（DaemonSet 多实例）才改用 Pod IP。

## 排查 "hash master get failed" 的清单

按可能性从高到低：

1. 客户端 Pod 没挂 `/dev/shm` 或没开 `hostIPC` → 补上 ①④（最常见主因）。
2. 客户端所在节点没有同节点 worker → kv-worker 改 DaemonSet（⑤），或用亲和把客户端和 worker 绑到同节点。
3. `kubectl get pods -o wide` 确认客户端和某个 kv-worker 在同一 NODE 上（shm 仅同节点有效，跨节点不行）。
4. worker 注册地址不对（检查 `worker_address`，K8s 下应是 Pod IP）。
5. etcd 不可达或 worker 还没注册完成（启动时序，看 worker 日志是否已 ready）。

验证成功标志：precalc 日志出现 `KVClient Set success`，rank-sub 出现 `KVClient Get` 成功。

## 注意事项

- DaemonSet 会在所有可调度节点（含 control-plane / GPU 节点）起 worker，测试集群一般 OK；
  如需排除某些节点，用 `nodeSelector` / `tolerations` 控制。
- `privileged` + `hostIPC` + hostPath `/dev/shm` 属于高权限配置，确认集群的 PodSecurity 策略放行。
- 把 kv-worker 从 Deployment 改成 DaemonSet 时，**必须先 `kubectl delete deployment kv-worker`**，
  否则新旧两套会并存。
