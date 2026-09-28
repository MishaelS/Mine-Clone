#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

// A structure described in a file of its own, assets/structures/<name>.json -
// a tree, a ruin, a well: blocks laid out around an origin, in one or more
// variants, and where the world generator puts it. What the model editor's
// "Structures" tab edits and the game loads at startup (see
// Load_structures() in worldgen/Structure.hpp).
//
// Blocks are named, not numbered - only needs the JSON reader, so the editor
// uses it without the game's block registry.
namespace structure_file {

    // StructureReplaceRule, in its own order: what a block of it may be
    // placed over.
    constexpr const char* REPLACE_IDS[] = {"air", "air_or_foliage", "any"};
    constexpr int REPLACE_COUNT = 3;

    // Biome, in its own order - the keys under "chance".
    constexpr const char* BIOME_IDS[] = {"plains", "forest", "desert", "hills", "ocean", "sea"};
    constexpr int BIOME_COUNT = 6;

    // One block, relative to the origin: the cell right above the block the
    // structure stands on.
    struct Block {
        int x = 0, y = 0, z = 0;
        std::string block;  // by name ("oak_log")
        int replace = 0;    // index into REPLACE_IDS
        // Where it can't go (something it may not replace is in the way),
        // the whole structure isn't placed - a tree's trunk.
        bool required = false;
    };

    // One shape of it - the generator picks one per structure it places.
    struct Variant {
        std::vector<Block> blocks; // at most one per cell
    };

    struct StructureFile {
        std::string name; // stable id: the file's name
        std::vector<Variant> variants;

        // World generation: stands on one of these blocks (by name), with air
        // above; tried once in every 4x4 column cell with this chance in the
        // cell's biome (0 - never there).
        std::vector<std::string> placed_on;
        std::array<float, BIOME_COUNT> chance{};
        bool tree_density = false; // the chance scaled by the world's tree amount (a custom world's setting)
        // A block (a sapling) that grows into it, placed with its origin
        // where that block was - "" none.
        std::string grows_from;
    };

    // The cells a structure's variants reach, relative to its origin.
    struct Bounds {
        int min_x = 0, min_y = 0, min_z = 0;
        int max_x = 0, max_y = 0, max_z = 0;
        bool empty = true;
    };
    Bounds bounds(const StructureFile& structure);
    Bounds bounds(const Variant& variant);

    // The block at a cell of `variant`, or nullptr.
    Block* block_at(Variant& variant, int x, int y, int z);
    const Block* block_at(const Variant& variant, int x, int y, int z);

    // A name a structure file can have: a-z, 0-9 and _.
    bool valid_name(const std::string& name);

    // assets/structures/
    std::string directory();

    std::optional<StructureFile> load(const std::string& path);
    bool save(const StructureFile& structure, const std::string& path);
    // Every *.json in directory(), by name. Unreadable files are skipped
    // with a warning.
    std::vector<StructureFile> load_all();

} // namespace structure_file
