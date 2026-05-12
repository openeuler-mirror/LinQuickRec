# Docker 閮ㄧ讲鎸囧崡

## 鏋舵瀯鎬昏

```
                          鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?
                          鈹?                linquickrec-net                   鈹?
                          鈹?                (Docker bridge)                    鈹?
                          鈹?                                                   鈹?
 Client :8080 鈹€鈹€鈻?鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?                                              鈹?
                  鈹?  Proxy    鈹傗攢鈹€鈹€鈹€ Discovery (8100)                          鈹?
                  鈹? (gateway) 鈹?                                              鈹?
                  鈹斺攢鈹€鈹€鈹€鈹€鈹攢鈹€鈹€鈹€鈹€鈹€鈹?                                              鈹?
                        鈹?discover downstream instances                        鈹?
                        鈻?                                                     鈹?
         鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹尖攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?                                      鈹?
         鈻?             鈻?             鈻?                                      鈹?
  Feature (x1)    Recall (xN)    Precalc (xN)                                  鈹?
  :8001             :8002           :8003                                      鈹?
  [pending]         + vLLM                                                     鈹?
                    :8000          KVWorker                                    鈹?
         鈹?             鈹?           :31502                                    鈹?
         鈹?             鈻?                                                     鈹?
         鈹?        KVWorker                                                    鈹?
         鈹?        :31501                                                      鈹?
         鈹斺攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?                                      鈹?
                        鈻?                                                     鈹?
                 RankMaster (xN)                                               鈹?
                 :8004                                                         鈹?
                        鈹?                                                     鈹?
                        鈻?                                                     鈹?
                 RankSub (xN)  鈼勨攢鈹€鈹€鈹€ KVWorker :31502                           鈹?
                 :8005                                                         鈹?
                          鈹?                                                   鈹?
                          鈹?                                                   鈹?
                  Discovery Server :8100                                       鈹?
                          鈹斺攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?
```

## 鐩綍缁撴瀯

```
deploy/docker/
鈹溾攢鈹€ docker-compose.yml      # 涓荤紪鎺掓枃浠讹紙7 涓湇鍔★級
鈹溾攢鈹€ .env                    # 鐜鍙橀噺閰嶇疆锛堝搴?K8s ConfigMap锛?
鈹溾攢鈹€ recall/
鈹?  鈹溾攢鈹€ Dockerfile          # Recall 鏈嶅姟闀滃儚锛堝惈 vLLM + Qwen3-0.6B锛?
鈹?  鈹斺攢鈹€ entrypoint.sh       # 鍚姩 vLLM 鈫?绛夊緟灏辩华 鈫?鍚姩 Recall 鏈嶅姟
鈹溾攢鈹€ precalc/
鈹?  鈹溾攢鈹€ Dockerfile          # Precalc 鏈嶅姟闀滃儚
鈹?  鈹斺攢鈹€ entrypoint.sh       # 鍚姩 Precalc 鏈嶅姟
鈹溾攢鈹€ rank-master/
鈹?  鈹溾攢鈹€ Dockerfile          # RankMaster 鏈嶅姟闀滃儚
鈹?  鈹斺攢鈹€ entrypoint.sh       # 绛夊緟 RankSub 灏辩华 鈫?鍚姩 RankMaster 鏈嶅姟
鈹溾攢鈹€ rank-sub/
鈹?  鈹溾攢鈹€ Dockerfile          # RankSub 鏈嶅姟闀滃儚
鈹?  鈹斺攢鈹€ entrypoint.sh       # 鍚姩 RankSub 鏈嶅姟
鈹溾攢鈹€ feature/
鈹?  鈹溾攢鈹€ Dockerfile          # Feature 鏈嶅姟闀滃儚锛坢ock锛?
鈹?  鈹斺攢鈹€ entrypoint.sh       # 鍚姩 Feature 鏈嶅姟
鈹斺攢鈹€ proxy/
    鈹溾攢鈹€ Dockerfile          # Proxy 缃戝叧闀滃儚
    鈹斺攢鈹€ entrypoint.sh       # 鍚姩 Gateway 浠ｇ悊鏈嶅姟
```

## 鍓嶇疆鏉′欢

| 渚濊禆 | 璇存槑 |
|------|------|
| Docker + Compose v2 | `docker compose` 鍛戒护鍙敤 |
| `linquickrec/base:latest` | 鍩虹闀滃儚锛岄渶鎻愬墠鏋勫缓鎴栧鍏ワ紙鍖呭惈 brpc銆乸rotobuf銆乬RPC銆乤bseil 绛変緷璧栵級 |
| NVIDIA GPU + nvidia-container-toolkit | Recall 鏈嶅姟杩愯 vLLM 闇€瑕?GPU |

## 蹇€熷紑濮?

### 1. 涓€閿惎鍔?

鎵€鏈夋湇鍔″潎鍦ㄥ鍣ㄥ唴缂栬瘧锛屾棤闇€瀹夸富鏈哄畨瑁?brpc 鎴栭缂栬瘧浠讳綍浜岃繘鍒讹細

```bash
cd deploy/docker
docker compose up --build -d
```

### 2. 楠岃瘉

```bash
# 鏌ョ湅鎵€鏈夊鍣ㄧ姸鎬?
docker compose ps

# 妫€鏌?proxy 缃戝叧鏄惁灏辩华
curl http://localhost:8080  # 鎴栨牴鎹疄闄呮帴鍙ｆ祴璇?

# 鏌ョ湅 discovery 娉ㄥ唽鐨勬湇鍔?
docker compose logs discovery-server
```

## docker-compose 鏈嶅姟娓呭崟

| 鏈嶅姟 | 瀹瑰櫒鍚?| 绔彛 | 闀滃儚 | 璇存槑 |
|------|--------|------|------|------|
| discovery-server | discovery-server | 8100 | linquickrec-discovery | 鏈嶅姟娉ㄥ唽涓庡彂鐜颁腑蹇?|
| feature-service | feature-service | 8001 | linquickrec-feature | 鐗瑰緛鏈嶅姟锛坢ock锛?|
| recall-service | recall-service | 8002 | linquickrec-recall | 鍙洖鏈嶅姟锛堝惈 vLLM锛岄渶 GPU锛?|
| precalc-service | precalc-service | 8003 | linquickrec-precalc | 棰勮绠楁湇鍔?|
| rank-sub-service | 鈥?(鍔ㄦ€? | 8005 | linquickrec-rank-sub | 鎺掑簭瀛愭湇鍔★紝榛樿 3 鍓湰 |
| rank-master-service | rank-master-service | 8004 | linquickrec-rank-master | 鎺掑簭涓绘湇鍔?|
| proxy-service | proxy-service | 8080 | linquickrec-proxy | 绯荤粺鍏ュ彛缃戝叧 |

### 鍚姩椤哄簭

```
discovery-server (healthcheck 閫氳繃)
        鈹?
        鈹溾攢鈹€ feature-service    鈹€鈹?
        鈹溾攢鈹€ recall-service     鈹€鈹?骞惰鍚姩
        鈹溾攢鈹€ precalc-service    鈹€鈹?
        鈹斺攢鈹€ rank-sub-service   鈹€鈹?
                鈹?
        rank-master-service (绛夊緟 rank-sub TCP 灏辩华)
                鈹?
        proxy-service (渚濊禆鎵€鏈変笂娓告湇鍔?
```

## 閰嶇疆绠＄悊

鎵€鏈夐厤缃」闆嗕腑鍦?`.env` 鏂囦欢涓紝淇敼鍚庨噸鏂?`docker compose up -d` 鍗冲彲鐢熸晥銆?

### 鍏抽敭閰嶇疆椤?

```bash
# ---- 澶栭儴渚濊禆 ----
KVWORKER_HOST=141.61.84.245     # KVWorker 鍦板潃
KVWORKER_PORT=31502             # KVWorker 绔彛
ETCD_ADDRESS=141.61.84.245:2379 # ETCD 鍦板潃

# ---- vLLM 妯″瀷 ----
MODEL_NAME=/app/models/Qwen3-0.6B/  # 瀹瑰櫒鍐呮ā鍨嬭矾寰?
VLLM_TIMEOUT_MS=100000              # vLLM 璇锋眰瓒呮椂

# ---- Rank 鎵╁睍 ----
RANK_SUB_REPLICAS=3                  # rank-sub 鍓湰鏁?
SUB_WORKER_COUNT=3                   # rank-master 杩炴帴鐨?worker 鏁帮紙闇€涓庡壇鏈暟涓€鑷达級
SUB_WORKER_ADDRESSES=rank-sub-service:8005  # Docker 鍐呴儴 DNS 鑷姩瑙ｆ瀽鎵€鏈夊壇鏈?

# ---- 缃戝叧 ----
PROXY_PORT=8080                      # 瀵瑰鏆撮湶鐨勪唬鐞嗙鍙?
```

### 鎵╁睍 RankSub 鍓湰

浠ユ墿灞曞埌 5 涓负渚嬶紝淇敼 `.env`锛?

```bash
RANK_SUB_REPLICAS=5
SUB_WORKER_COUNT=5
```

鐒跺悗閲嶆柊鍚姩锛?

```bash
docker compose up -d
```

> `rank-sub-service` 鐨?Docker 鍐呴儴 DNS 浼氳嚜鍔ㄨВ鏋愬埌鎵€鏈夊壇鏈?IP锛宍SUB_WORKER_ADDRESSES` 鏃犻渶鏀逛负閫楀彿鍒嗛殧鍒楄〃銆?

## 甯哥敤鎿嶄綔

```bash
# ---- 鍚仠 ----
docker compose up --build -d          # 鏋勫缓骞跺惎鍔ㄥ叏閮?
docker compose up -d                  # 鍚姩锛堜笉閲嶆柊鏋勫缓锛?
docker compose down                   # 鍋滄骞剁Щ闄ゅ鍣?
docker compose restart proxy-service  # 閲嶅惎鍗曚釜鏈嶅姟

# ---- 鍗曠嫭鏋勫缓/鍚姩 ----
docker compose build recall-service   # 鍙瀯寤?recall
docker compose up -d recall-service   # 鍙惎鍔?recall

# ---- 鏌ョ湅鏃ュ織 ----
docker compose logs -f                        # 鍏ㄩ儴鏃ュ織锛堝疄鏃惰窡韪級
docker compose logs -f proxy-service          # 鍗曚釜鏈嶅姟鏃ュ織
docker compose logs --tail=50 recall-service  # 鏈€杩?50 琛?

# ---- 鎵╃缉瀹?----
docker compose up -d --scale rank-sub-service=5  # 涓存椂鎸囧畾鍓湰鏁?

# ---- 鐘舵€佹鏌?----
docker compose ps                     # 瀹瑰櫒鐘舵€?
docker compose top                    # 瀹瑰櫒鍐呰繘绋?
```

姣忎釜鐩綍鍖呭惈涓€涓?`Dockerfile` 鍜屼竴涓?`entrypoint.sh`銆?

## 鍩虹闀滃儚

鎵€鏈夋湇鍔″熀浜?`linquickrec/base:latest`锛屽寘鍚?brpc銆乸rotobuf銆乤bseil-cpp銆乬flags銆乴eveldb銆乺apidjson 绛変緷璧栥€?

## Dockerfile 璇存槑

### Discovery锛坉iscovery/Dockerfile锛?

缂栬瘧 `discovery_server`锛岀洃鍚?8100 绔彛锛屾彁渚涙湇鍔℃敞鍐?鍙戠幇/蹇冭烦 RPC銆?

### Proxy锛坧roxy/Dockerfile锛?

缂栬瘧 `proxy_server` 鍜?`discovery_client`銆傞€氳繃 sidecar 妯″紡鍚?discovery-server 娉ㄥ唽鑷韩锛屽苟閫氳繃 discovery 鍔ㄦ€佸彂鐜颁笅娓稿疄渚嬨€?

### Recall锛坮ecall/Dockerfile锛?

Recall 鏈嶅姟涓?vLLM 鍚屽鍣ㄩ儴缃诧紝棰濆瀹夎 vLLM wheel 鍖呭拰妯″瀷鏂囦欢锛圦wen3-0.6B锛夛細

1. 瀹夎 PyTorch + vLLM
2. 澶嶅埗 vLLM 鍚姩鑴氭湰锛坄start_vllm_back.sh`銆乣start_vllm.sh`锛?
3. 澶嶅埗妯″瀷鏂囦欢锛坄Qwen3-0.6B/`銆乣Qwen3-8B/`锛?
4. 缂栬瘧 recall_server
5. 鏆撮湶绔彛 8002锛坆rpc锛夊拰 8000锛坴LLM锛?

### Precalc锛坧recalc/Dockerfile锛?

缂栬瘧 `precalc_server`锛岀洃鍚?8003 绔彛锛屽皢鐢ㄦ埛鐗瑰緛棰勮绠楃粨鏋滃啓鍏?KVWorker銆?

### RankMaster锛坮ank-master/Dockerfile锛?

缂栬瘧 `rank_master_server`锛岀洃鍚?8004 绔彛锛屽皢鍊欓€夊晢鍝佸垎鍙戠粰澶氫釜 RankSub 骞惰鎵撳垎鍚庡綊骞剁粨鏋溿€?

### RankSub锛坮ank-sub/Dockerfile锛?

缂栬瘧 `rank_sub_server`锛岀洃鍚?8005 绔彛锛屼粠 KVWorker 璇诲彇鐗瑰緛 tensor 骞跺鍒嗛厤鍒扮殑 SKU 鎵撳垎銆?

### Feature锛坒eature/Dockerfile锛?

1. 澶嶅埗 proto銆乧ommon銆丗eatureService 婧愮爜
2. CMake 缂栬瘧
3. 鏆撮湶绔彛 8001

## EntryPoint 璇存槑

姣忎釜 entrypoint.sh 閫氳繃鐜鍙橀噺閰嶇疆鏈嶅姟鍙傛暟锛岀幆澧冨彉閲忔湁榛樿鍊硷紝涔熷彲閫氳繃 `.env` 鎴?`environment` 娉ㄥ叆瑕嗙洊銆?

### recall/entrypoint.sh

```bash
# 鍚姩娴佺▼锛?
1. 鍚庡彴鍚姩 vLLM锛坰tart_vllm_back.sh锛?
2. 杞 http://127.0.0.1:8000/health 绛夊緟 vLLM 灏辩华锛堟渶闀?120 绉掞級
3. 鍚姩 recall_server
4. 鍚姩 discovery_client 娉ㄥ唽鏈嶅姟
```

| 鐜鍙橀噺 | 榛樿鍊?| 璇存槑 |
|---------|--------|------|
| `SERVER_PORT` | 8002 | brpc 鐩戝惉绔彛 |
| `VLLM_BASE_URL` | http://127.0.0.1:8000 | vLLM 鍦板潃 |
| `VLLM_ENDPOINT` | /v1/chat/completions | vLLM 鎺ㄧ悊鎺ュ彛璺緞 |
| `MODEL_NAME` | /app/models/Qwen3-0.6B/ | 妯″瀷璺緞 |
| `VLLM_TIMEOUT_MS` | 100000 | vLLM 璇锋眰瓒呮椂 |
| `SKU_COUNT` | 100 | SKU 鏁伴噺 |
| `VLLM_PORT` | 8000 | vLLM 鍋ュ悍妫€鏌ョ鍙?|
| `VLLM_STARTUP_TIMEOUT` | 120 | vLLM 鍚姩绛夊緟绉掓暟 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 鏈嶅姟鍙戠幇鍦板潃 |

### precalc/entrypoint.sh

```bash
# 鍚姩娴佺▼锛?
1. 鍚姩 precalc_server
2. 鍚姩 discovery_client 娉ㄥ唽鏈嶅姟
```

| 鐜鍙橀噺 | 榛樿鍊?| 璇存槑 |
|---------|--------|------|
| `SERVER_PORT` | 8003 | brpc 鐩戝惉绔彛 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 鍦板潃 |
| `KVWORKER_PORT` | 31502 | KVWorker 绔彛 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 鍦板潃 |
| `TTL_SECONDS` | 5 | KV 缂撳瓨 TTL |
| `PRECALC_RESULT_SIZE_MB` | 8.5 | 棰勮绠楃粨鏋滃ぇ灏?(MB) |
| `PAYLOAD_SIZE_KB` | 100 | Payload 澶у皬 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 鏈嶅姟鍙戠幇鍦板潃 |

### rank-master/entrypoint.sh

```bash
# 鍚姩娴佺▼锛?
1. 绛夊緟 RankSub 鏈嶅姟 TCP 绔彛灏辩华锛堟渶闀?120 绉掞紝瓒呮椂涔熶細缁х画鍚姩锛?
2. 鍚姩 rank_master_server
3. 鍚姩 discovery_client 娉ㄥ唽鏈嶅姟
```

| 鐜鍙橀噺 | 榛樿鍊?| 璇存槑 |
|---------|--------|------|
| `SERVER_PORT` | 8004 | brpc 鐩戝惉绔彛 |
| `SUB_WORKER_COUNT` | 3 | RankSub 宸ヤ綔绾跨▼鏁?|
| `SUB_WORKER_ADDRESSES` | rank-sub-service:8005 | RankSub 鏈嶅姟鍦板潃 |
| `TOP_K` | 100 | 杩斿洖 Top-K 缁撴灉 |
| `RANK_SUB_HOST` | rank-sub-service | RankSub 涓绘満鍚?|
| `RANK_SUB_PORT` | 8005 | RankSub 绔彛 |
| `RANK_SUB_STARTUP_TIMEOUT` | 120 | 绛夊緟 RankSub 灏辩华绉掓暟 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 鏈嶅姟鍙戠幇鍦板潃 |

### rank-sub/entrypoint.sh

```bash
# 鍚姩娴佺▼锛?
1. 鍚姩 rank_sub_server
2. 鍚姩 discovery_client 娉ㄥ唽鏈嶅姟
```

| 鐜鍙橀噺 | 榛樿鍊?| 璇存槑 |
|---------|--------|------|
| `SERVER_PORT` | 8005 | brpc 鐩戝惉绔彛 |
| `KVWORKER_HOST` | 141.61.84.245 | KVWorker 鍦板潃 |
| `KVWORKER_PORT` | 31502 | KVWorker 绔彛 |
| `ETCD_ADDRESS` | 141.61.84.245:2379 | etcd 鍦板潃 |
| `SCORING_DELAY_MS` | 100 | 鎵撳垎寤惰繜 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 鏈嶅姟鍙戠幇鍦板潃 |

### feature/entrypoint.sh

```bash
# 鍚姩娴佺▼锛?
1. 鍚姩 feature_server锛坢ock锛?
2. 鍚姩 discovery_client 娉ㄥ唽鏈嶅姟
```

| 鐜鍙橀噺 | 榛樿鍊?| 璇存槑 |
|---------|--------|------|
| `SERVER_PORT` | 8001 | brpc 鐩戝惉绔彛 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 鏈嶅姟鍙戠幇鍦板潃 |

### proxy/entrypoint.sh

```bash
# 鍚姩娴佺▼锛?
1. 鍚姩 proxy_server锛岄厤缃墍鏈変笅娓告湇鍔″湴鍧€
2. 鍚姩 discovery_client 娉ㄥ唽鏈嶅姟
```

| 鐜鍙橀噺 | 榛樿鍊?| 璇存槑 |
|---------|--------|------|
| `SERVER_PORT` | 8080 | brpc 鐩戝惉绔彛 |
| `FEATURE_SERVICE_ADDR` | feature-service:8001 | Feature 鏈嶅姟鍦板潃 |
| `RECALL_SERVICE_ADDR` | recall-service:8002 | Recall 鏈嶅姟鍦板潃 |
| `PRECALC_SERVICE_ADDR` | precalc-service:8003 | Precalc 鏈嶅姟鍦板潃 |
| `RANK_SERVICE_ADDR` | rank-master-service:8004 | RankMaster 鏈嶅姟鍦板潃 |
| `DISCOVERY_ADDR` | discovery-server:8100 | 鏈嶅姟鍙戠幇鍦板潃 |

## 鎵嬪姩鏋勫缓鍗曚釜闀滃儚

濡傛灉涓嶄娇鐢?docker-compose锛屼篃鍙互鍗曠嫭鏋勫缓鍜岃繍琛岋細

鍦ㄩ」鐩牴鐩綍涓嬫墽琛岋細

```bash
# 鏋勫缓 Recall锛堥渶瑕?GPU 鏋勫缓鏈哄櫒锛屾枃浠惰矾寰勯渶鍖归厤锛?
docker build -f deploy/docker/recall/Dockerfile -t linquickrec/recall:latest .

# 鏋勫缓 Precalc
docker build -f deploy/docker/precalc/Dockerfile -t linquickrec/precalc:latest .

# 鏋勫缓 RankMaster
docker build -f deploy/docker/rank-master/Dockerfile -t linquickrec/rank-master:latest .

# 鏋勫缓 RankSub
docker build -f deploy/docker/rank-sub/Dockerfile -t linquickrec/rank-sub:latest .

# 鏋勫缓 Feature
docker build -f deploy/docker/feature/Dockerfile -t linquickrec/feature:latest .

# 鏋勫缓 Proxy
docker build -f deploy/docker/proxy/Dockerfile -t linquickrec/proxy:latest .

# 鏋勫缓 Discovery
docker build -f deploy/docker/discovery/Dockerfile -t linquickrec/discovery:latest .
```

## 鏈湴杩愯

### 鏈嶅姟鍙戠幇涓績

```bash
docker run -d --name discovery \
    -p 8100:8100 \
    linquickrec/discovery:latest
```

### Proxy锛堜緷璧?discovery-server锛?

```bash
docker run -d --name proxy \
    -p 8080:8080 \
    -e DISCOVERY_ADDR=host.docker.internal:8100 \
    linquickrec/proxy:latest
```

### Precalc

```bash
docker run -d --name precalc \
    -p 8003:8003 \
    linquickrec/precalc:latest
```

### RankSub

```bash
docker run -d --name rank-sub \
    -p 8005:8005 \
    linquickrec/rank-sub:latest
```

### RankMaster锛堥渶 RankSub 宸茶繍琛岋級

```bash
docker run -d --name rank-master \
    -p 8004:8004 \
    -e RANK_SUB_HOST=host.docker.internal \
    -e SUB_WORKER_ADDRESSES=host.docker.internal:8005 \
    linquickrec/rank-master:latest
```

### Recall锛堥渶 GPU锛?

```bash
docker run -d --gpus all --name recall \
    -p 8002:8002 -p 8000:8000 \
    linquickrec/recall:latest
```

## 绔埌绔紨绀?

`examples/` 鐩綍鎻愪緵瀹屾暣鐨?docker-compose 缂栨帓锛屽弬瑙?[services/discovery/examples/README.md](../../services/discovery/examples/README.md)銆?

