#include "../../fs/volume/VolumeManager.h"

// Lightweight stub used by mds_server unit tests to avoid linking the full
// storage subsystem. Production builds should link against the real
// VolumeManager implementation instead of this file.

#ifdef ZB_MDS_VOLUME_MANAGER_STUB

void VolumeManager::register_volume(std::shared_ptr<Volume>,
                                    std::shared_ptr<IIOGateway>) {}

bool VolumeManager::set_volume_gateway(const std::string&,
                                       std::shared_ptr<IIOGateway>) { return false; }

void VolumeManager::set_default_gateway(std::shared_ptr<IIOGateway>) {}

ssize_t VolumeManager::write_file(const std::shared_ptr<Inode>&,
                                  size_t,
                                  const char*,
                                  size_t) { return -1; }

ssize_t VolumeManager::read_file(const std::shared_ptr<Inode>&,
                                 size_t,
                                 char*,
                                 size_t) { return -1; }

bool VolumeManager::release_inode_blocks(const std::shared_ptr<Inode>&) { return false; }

#endif // ZB_MDS_VOLUME_MANAGER_STUB
