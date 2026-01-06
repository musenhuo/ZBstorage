grafana:
http://222.20.95.30:3000/
account:
admin
password:mysecurepassword
prometheus:
http://222.20.95.30:9090/

./msg/RPC/mds_rpc_server \
  --mds_port=8010 \
  --mds_data_dir=/mnt/md0/Projects/tmp_zb/mds \
  --mds_create_new=true \
  --node_alloc_policy=prefer_real

./srm/srm_main --srm_port=9100 --mds_addr=127.0.0.1:8010 --virtual_node_count=0

./storagenode/real_node/real_node_server \
  --port=9010 \
  --skip_mount=true \
  --base_path=/mnt/md0/Projects/tmp_zb/node_data \
  --srm_addr=127.0.0.1:9100 \
  --advertise_ip=127.0.0.1

sudo ./client/fuse/zb_fuse_client \
  --mount_point=/mnt/md0/Projects/tmp_zb/mp_zb \
  --mds_addr=127.0.0.1:8010 \
  --srm_addr=127.0.0.1:9100 \
  --node_id=node-1 \
  --allow_other \
  --foreground

  

