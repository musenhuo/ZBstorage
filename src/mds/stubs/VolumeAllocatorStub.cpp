#include "../allocator/VolumeAllocator.h"
#include <utility>

#ifdef ZB_MDS_VOLUME_MANAGER_STUB

VolumeAllocator::VolumeAllocator(std::shared_ptr<IVolumeRegistry> registry)
    : registry_(std::move(registry)) {}

bool VolumeAllocator::allocate_for_inode(const std::shared_ptr<Inode>&) {
    return false;
}

bool VolumeAllocator::free_blocks_for_inode(const std::shared_ptr<Inode>&) {
    return false;
}

#endif // ZB_MDS_VOLUME_MANAGER_STUB
