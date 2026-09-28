#include "world/BlockBehavior.hpp"

#include <array>

BlockPos BlockPos::neighbor(BlockFace face) const
{
    const FaceOffset step = block_face_offset(face);
    return offset(step.dx, step.dy, step.dz);
}

namespace {
    std::array<std::vector<std::shared_ptr<BlockBehavior>>, MAX_BLOCK_TYPES> behaviors_by_type;
}

namespace block_behaviors {

    void add(BlockType type, std::shared_ptr<BlockBehavior> behavior)
    {
        if (behavior) behaviors_by_type[static_cast<size_t>(type)].push_back(std::move(behavior));
    }

    const std::vector<std::shared_ptr<BlockBehavior>>& of(BlockType type)
    {
        return behaviors_by_type[static_cast<size_t>(type)];
    }

    void clear()
    {
        for (auto& behaviors : behaviors_by_type) behaviors.clear();
    }

} // namespace block_behaviors
