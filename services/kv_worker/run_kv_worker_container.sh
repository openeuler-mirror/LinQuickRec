# Please set huge page in host firstly

worker_address="X.X.X.X:Y" # 宿主机IP:端口，不能写127.0.0.1
etcd_address="X.X.X.X:2379" # etcd 所在宿主机IP:2379
enable_urma=0                # 是否启用URMA，0=不启用，1=启用

docker run -d \
  --name kv_worker \
  --restart=always \
  --privileged \
  --ipc=host \
  --net=host \
  -v /dev/shm:/dev/shm \
  -e worker_address=${worker_address} \
  -e etcd_address=${etcd_address} \
  -e enable_urma=${enable_urma} \
  kv_worker:latest
