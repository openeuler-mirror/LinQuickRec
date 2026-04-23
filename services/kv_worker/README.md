# kv_worker

## 文件说明

| 文件 | 说明 |
|------|------|
| `Dockerfile` | 容器镜像定义，基于 `brpc_base:latest`，安装 `openyuanrong_datasystem*.whl`，并将启动脚本和工作脚本复制到 `/workerspace` |
| `start_datasystem.sh` | Datasystem Worker 启动脚本，通过环境变量 `worker_address`、`etcd_address`、`enable_urma` 配置 Worker 参数并启动 |
| `start_etcd.sh` | 本地 etcd 节点启动脚本（用于开发/测试环境） |
| `run_kv_worker_container.sh` | 旧版实验用容器启动脚本（含冗余操作，仅供参考） |

## Dockerfile 使用

### 构建镜像

```bash
# 在 kv_worker 目录下执行，确保 openyuanrong_datasystem*.whl 在同一目录
docker build -t kv_worker:latest .
```

### 正确启动容器

镜像默认 CMD 已包含启动 datasystem worker、运行 example.py、保持容器不退出，因此无需额外指定 command：

```bash
docker run -d \
  --name kv_worker \
  --restart=always \
  --privileged \
  --ipc=host \
  --net=host \
  -v /dev/shm:/dev/shm \
  -e worker_address="X.X.X.X:Y" \
  -e etcd_address="X.X.X.X:2379" \
  -e enable_urma=0 \
  kv_worker:latest
```

**注意：**
- `worker_address` 必须填写宿主机 IP，不能写 `127.0.0.1`
- 启动前需在宿主机设置好 huge page
- 如需启动多个 worker 实例，修改 `--name` 和 `worker_address` 端口即可
