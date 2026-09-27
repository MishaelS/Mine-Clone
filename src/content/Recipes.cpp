#include "content/Content.hpp"

// Crafting recipes (Minecraft Beta 1.7.3). Shaped patterns read like the
// crafting grid itself: one string per row, a space is an empty cell, each
// letter is defined in the key list after it. See content/Content.hpp.

namespace content {

    namespace {
        using B = BlockType;
        using I = ItemType;

        // One tool of each kind for a given head material `m`, all with a
        // stick handle - the whole Wood/Stone/Iron/Gold/Diamond tool set
        // differs only in that one ingredient.
        void tool_set(ItemRef m, I pickaxe, I shovel, I axe, I hoe, I sword) {
            shaped(pickaxe, {"MMM", " S ", " S "}, {{'M', m}, {'S', I::Stick}});
            shaped(shovel , {"M"  , "S"  , "S"  }, {{'M', m}, {'S', I::Stick}});
            shaped(axe    , {"MM" , "MS" , " S" }, {{'M', m}, {'S', I::Stick}});
            shaped(hoe    , {"MM" , " S" , " S" }, {{'M', m}, {'S', I::Stick}});
            shaped(sword  , {"M"  , "M"  , "S"  }, {{'M', m}, {'S', I::Stick}});
        }

        // Nine of `item` <-> one storage block, both ways.
        void storage_block(B block, I item) {
            shaped(block, {"XXX", "XXX", "XXX"}, {{'X', item}});
            shapeless(item, 9, {block});
        }
    }

    void register_recipes() {
        // Basics
        shapeless(B::OakPlanks , 4, {B::OakLog});
        shapeless(B::OakPlanks , 4, {B::SpruceLog});
        shapeless(B::OakPlanks , 4, {B::BirchLog});
        shaped(I::Stick        , 4, {"P"  , "P" }        ,  {{'P', B::OakPlanks  }});
        shaped(B::Workbench    ,    {"PP" , "PP"}        ,  {{'P', B::OakPlanks  }});
        shaped(B::Torch        , 4, {"C"  , "S" }        ,  {{'C', I::Coal       }, {'S', I::Stick}});
        shaped(B::Furnace      ,    {"CCC", "C C", "CCC"},  {{'C', B::Cobblestone}});
        shaped(B::Chest        ,    {"PPP", "P P", "PPP"},  {{'P', B::OakPlanks  }});

        // Tools
        tool_set(B::OakPlanks  , I::WoodenPickaxe , I::WoodenShovel , I::WoodenAxe , I::WoodenHoe , I::WoodenSword );
        tool_set(B::Cobblestone, I::StonePickaxe  , I::StoneShovel  , I::StoneAxe  , I::StoneHoe  , I::StoneSword  );
        tool_set(I::IronIngot  , I::IronPickaxe   , I::IronShovel   , I::IronAxe   , I::IronHoe   , I::IronSword   );
        tool_set(I::GoldIngot  , I::GoldPickaxe   , I::GoldShovel   , I::GoldAxe   , I::GoldHoe   , I::GoldSword   );
        tool_set(I::Diamond    , I::DiamondPickaxe, I::DiamondShovel, I::DiamondAxe, I::DiamondHoe, I::DiamondSword);

        // Building
        shaped(B::OakSlab      , 6, {"PPP"              },  {{'P', B::OakPlanks}});
        shaped(B::OakStairs    , 4, {"P  ", "PP ", "PPP"},  {{'P', B::OakPlanks}});
        shaped(B::OakDoorLower ,    {"PP" , "PP" , "PP" },  {{'P', B::OakPlanks}});
        shaped(B::IronDoorLower,    {"II" , "II" , "II" },  {{'I', I::IronIngot}});
        shaped(B::BedHead      ,    {"WWW", "PPP"       },  {{'W', B::WhiteWool}, {'P', B::OakPlanks}});
        shaped(B::Sandstone    ,    {"SS" , "SS"        },  {{'S', B::Sand     }});

        // Food
        shaped(I::GoldenApple  ,    {"GGG", "GAG", "GGG"},  {{'G', B::GoldBlock}, {'A', I::Apple}});
        shaped(I::Bread        ,    {"WWW"}              ,  {{'W', I::Wheat}});
        shaped(I::Bucket       ,    {"I I", " I "}       ,  {{'I', I::IronIngot}});

        // Storage blocks
        storage_block(B::IronBlock   , I::IronIngot);
        storage_block(B::GoldBlock   , I::GoldIngot);
        storage_block(B::DiamondBlock, I::Diamond  );
    }

} // namespace content
