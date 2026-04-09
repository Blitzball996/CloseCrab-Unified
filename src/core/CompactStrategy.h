#pragma once

#include "Message.h"
#include "../api/APIClient.h"
#include <string>
#include <vector>
#include <cstdint>

namespace closecrab {

// CompactMetadata is defined in Message.h

// Abstract base class for all compaction strategies
class CompactStrategy {
public:
    virtual ~CompactStrategy() = default;

    // Human-readable strategy name
    virtual std::string name() const = 0;

    // Determine whether this strategy should fire
    virtual bool shouldTrigger(const std::vector<Message>& messages,
                               int estimatedTokens,
                               int maxContextTokens) const = 0;

    // Perform compaction in-place and return metadata
    virtual CompactMetadata compact(std::vector<Message>& messages,
                                    APIClient* apiClient,
                                    int maxContextTokens) = 0;
};

} // namespace closecrab
