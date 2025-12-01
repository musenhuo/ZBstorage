#pragma once
#include "IIOGateway.h"
#include "srm/storage_manager/StorageResource.h"

class LocalStorageGateway final : public IIOGateway {
public:
    double processIO(const IORequest& req) override;
    void processIOBatch(const std::vector<IORequest>& reqs) override;
};