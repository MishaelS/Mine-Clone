#include "content/Content.hpp"

// What breaking a block yields (Minecraft Beta 1.7.3 rules). A block with
// no rule here simply drops itself, hand or any tool - so only blocks that
// need a tool, drop something else, or drop nothing are listed.

namespace content {

    namespace {
        using B = BlockType;
        using I = ItemType;
        using T = ToolKind;
    }

    void register_drops() {
        // Stone-type blocks: nothing without a pickaxe.
        when_broken(B::Stone            ).needs(T::Pickaxe, ToolTier::Wood   ).drop(B::Cobblestone       );
        when_broken(B::Cobblestone      ).needs(T::Pickaxe, ToolTier::Wood   ).drop(B::Cobblestone       );
        when_broken(B::MossyCobblestone ).needs(T::Pickaxe, ToolTier::Wood   ).drop(B::MossyCobblestone  );
        when_broken(B::Sandstone        ).needs(T::Pickaxe, ToolTier::Wood   ).drop(B::Sandstone         );
        when_broken(B::DoubleStoneSlab  ).needs(T::Pickaxe, ToolTier::Wood   ).drop(B::DoubleStoneSlab, 2);
        when_broken(B::Obsidian         ).needs(T::Pickaxe, ToolTier::Diamond).drop(B::Obsidian          );

        // Ores
        when_broken(B::CoalOre          ).needs(T::Pickaxe, ToolTier::Wood ).drop(I::Coal              );
        when_broken(B::IronOre          ).needs(T::Pickaxe, ToolTier::Stone).drop(B::IronOre           );
        when_broken(B::GoldOre          ).needs(T::Pickaxe, ToolTier::Iron ).drop(B::GoldOre           );
        when_broken(B::DiamondOre       ).needs(T::Pickaxe, ToolTier::Iron ).drop(I::Diamond           );
        when_broken(B::RedstoneOre      ).needs(T::Pickaxe, ToolTier::Iron ).drop(I::RedstoneDust, 4, 5);
        when_broken(B::LapisOre         ).needs(T::Pickaxe, ToolTier::Stone);       // TODO: lapis_lazuli x4-9 once the item exists

        // Something other than itself
        when_broken(B::Grass            ).drop(B::Dirt);
        when_broken(B::SnowyGrass       ).drop(B::Dirt);
        when_broken(B::Gravel           ).drop(B::Gravel).chance(0.9f);             // TODO: flint (10%) once the item exists
        when_broken(B::Clay             ).drop_nothing();                           // TODO: clay_ball x4 once the item exists
        when_broken(B::SnowBlock        ).drop_nothing();                           // TODO: snowball x4 once the item exists
        when_broken(B::Glowstone        ).needs(T::Pickaxe     );                   // TODO: glowstone_dust x2-4 once the item exists
        when_broken(B::ShortGrass       ).drop(I::WheatSeeds   ).chance(0.125f);
        when_broken(B::LitFurnace       ).drop(B::Furnace      );
        when_broken(B::LitRedstoneTorch ).drop(B::RedstoneTorch);

        // Leaves: occasional sapling/stick/apple, never the leaves themselves.
        when_broken(B::Foliage          ).drop(B::OakSapling).chance(0.05f).drop(I::Stick).chance(0.025f).drop(I::Apple).chance(0.005f);
        when_broken(B::SpruceFoliage    ).drop(I::Sapling   ).chance(0.05f).drop(I::Stick).chance(0.025f);
        when_broken(B::BirchFoliage     ).drop(I::Sapling   ).chance(0.05f).drop(I::Stick).chance(0.025f);

        // Two-cell blocks (their files' "pair"): either half gives back the
        // one placeable item.
        for (BlockType type : all_block_types()) {
            const BlockProperties& properties = get_block_properties(type);
            if (!properties.is_item && properties.partner != BlockType::Air) when_broken(type).drop(properties.partner);
        }

        // Nothing at all
        when_broken(B::Glass            ).drop_nothing();
        when_broken(B::Ice              ).drop_nothing();
        when_broken(B::Bookshelf        ).drop_nothing();
        when_broken(B::Cake             ).drop_nothing();
        when_broken(B::Bedrock          ).drop_nothing();
        when_broken(B::Water            ).drop_nothing();
        when_broken(B::Lava             ).drop_nothing();
    }

} // namespace content
