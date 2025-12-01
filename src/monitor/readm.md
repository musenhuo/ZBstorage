// 没用
/bin/prometheus --config.file=/mnt/md0/Projects/ZBStorage/src/monitor/prometheus.yml --storage.tsdb.path=/prometheus

//似乎没用
sudo systemctl restart prometheus

sudo docker ps
sudo docker restart <容器名或ID>
//更新配置
sudo cp /mnt/md0/Projects/ZBStorage/src/monitor/prometheus.yml /etc/prometheus/prometheus.yml