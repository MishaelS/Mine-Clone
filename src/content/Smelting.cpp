#include "content/Content.hpp"

// Furnace recipes and fuels (Minecraft Beta 1.7.3 rules). Every recipe
// takes one input and smelting_cook_ticks() (10 seconds) to make its
// output; fuel burn time is in game ticks, 20 per second.

namespace content {

namespace {
    using B = BlockType;
    using I = ItemType;
}

void register_smelting()
{
    smelt(B::IronOre    , I::IronIngot     );
    smelt(B::GoldOre    , I::GoldIngot     );
    smelt(B::DiamondOre , I::Diamond       );
    smelt(B::Sand       , B::Glass         );
    smelt(B::Cobblestone, B::Stone         );
    smelt(I::RawPorkchop, I::CookedPorkchop);
    smelt(I::RawFish    , I::CookedFish    );
    // Vanilla gives charcoal - this build has no separate charcoal item,
    // and it works the same as coal.
    smelt(B::OakLog     , I::Coal);
    smelt(B::SpruceLog  , I::Coal);
    smelt(B::BirchLog   , I::Coal);

    fuel(I::Coal        , 1600); // 8 items
    fuel(B::OakLog      , 300 ); // 1.5 items
    fuel(B::SpruceLog   , 300 );
    fuel(B::BirchLog    , 300 );
    fuel(B::OakPlanks   , 300 );
    fuel(B::Workbench   , 300 );
    fuel(B::Chest       , 300 );
    fuel(B::Bookshelf   , 300 );
    fuel(B::NoteBlock   , 300 );
    fuel(B::Jukebox     , 300 );
    fuel(B::OakStairs   , 300 );
    fuel(B::OakTrapdoor , 300 );
    fuel(B::OakSlab     , 150 );
    fuel(I::Stick       , 100 );
    fuel(I::Sapling     , 100 );
    fuel(B::OakSapling  , 100 );
}

} // namespace content
