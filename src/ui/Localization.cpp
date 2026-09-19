#include "ui/Localization.hpp"
#include "core/Block.hpp"
#include "player/Item.hpp"

#include <array>
#include <unordered_map>

namespace {
    using Dictionary = std::unordered_map<std::string_view, std::string>;

    const Dictionary RU = {
        {"main.singleplayer", "Одиночная игра"}, {"main.multiplayer", "Сетевая игра"},
        {"main.settings", "Настройки..."}, {"main.quit", "Выйти из игры"},
        {"worlds.title", "Выбор мира"}, {"worlds.empty", "Пока нет сохранённых миров"},
        {"worlds.play", "Играть"}, {"worlds.delete", "Удалить"}, {"worlds.confirm", "Точно?"},
        {"worlds.create", "Создать мир"}, {"common.back", "Назад"}, {"common.cancel", "Отмена"},
        {"common.done", "Готово"},
        {"worlds.seed", "Сид"},
        {"create.title", "Создание мира"}, {"create.name", "Название мира"},
        {"create.seed", "Сид (необязательно)"}, {"create.mode", "Режим игры"},
        {"create.creative", "Творческий"}, {"create.survival", "Выживание"},
        {"create.required", "Введите название мира"}, {"pause.title", "Меню игры"},
        {"pause.resume", "Вернуться в игру"}, {"pause.save_quit", "Сохранить и выйти"},
        {"settings.title", "Настройки"}, {"settings.controls", "Управление"},
        {"settings.graphics", "Графика"}, {"settings.sound", "Музыка и звуки"},
        {"settings.window", "Размер окна"}, {"settings.ui_scale", "Масштаб UI"},
        {"settings.scale.1", "Стандартный"}, {"settings.scale.2", "Средний"},
        {"settings.scale.3", "Большой"}, {"settings.scale.4", "Огромный"},
        {"settings.render", "Дальность рендера"}, {"settings.fog", "Дальность видимости"},
        {"settings.brightness", "Яркость"},
        {"settings.fps", "Ограничение FPS"}, {"settings.filter", "Фильтрация"},
        {"settings.point", "Пиксельная"}, {"settings.smooth", "Плавная"},
        {"settings.master", "Общая громкость"}, {"settings.effects", "Блоки и шаги"},
        {"settings.ambient", "Окружение"}, {"settings.music", "Музыка"},
        {"settings.language", "Язык"}, {"language.ru", "Русский"}, {"language.en", "English"},
        {"mode.survival", "выживание"}, {"mode.creative", "творческий"},
        {"inventory.creative", "Творческий инвентарь"}, {"inventory.pages", "Колесо: страницы"},
        {"death.title", "Вы умерли"},
    };

    const Dictionary EN = {
        {"main.singleplayer", "Singleplayer"}, {"main.multiplayer", "Multiplayer"},
        {"main.settings", "Options..."}, {"main.quit", "Quit Game"},
        {"worlds.title", "Select World"}, {"worlds.empty", "No saved worlds yet"},
        {"worlds.play", "Play"}, {"worlds.delete", "Delete"}, {"worlds.confirm", "Sure?"},
        {"worlds.create", "Create World"}, {"common.back", "Back"}, {"common.cancel", "Cancel"},
        {"common.done", "Done"},
        {"worlds.seed", "Seed"},
        {"create.title", "Create New World"}, {"create.name", "World Name"},
        {"create.seed", "Seed (optional)"}, {"create.mode", "Game Mode"},
        {"create.creative", "Creative"}, {"create.survival", "Survival"},
        {"create.required", "Enter a world name"}, {"pause.title", "Game Menu"},
        {"pause.resume", "Back to Game"}, {"pause.save_quit", "Save and Quit"},
        {"settings.title", "Options"}, {"settings.controls", "Controls"},
        {"settings.graphics", "Video Settings"}, {"settings.sound", "Music & Sounds"},
        {"settings.window", "Window Size"}, {"settings.ui_scale", "GUI Scale"},
        {"settings.scale.1", "Standard"}, {"settings.scale.2", "Medium"},
        {"settings.scale.3", "Large"}, {"settings.scale.4", "Huge"},
        {"settings.render", "Render Distance"}, {"settings.fog", "Fog Distance"},
        {"settings.brightness", "Brightness"},
        {"settings.fps", "Max Framerate"}, {"settings.filter", "Filtering"},
        {"settings.point", "Pixel Perfect"}, {"settings.smooth", "Smooth"},
        {"settings.master", "Master Volume"}, {"settings.effects", "Blocks & Steps"},
        {"settings.ambient", "Ambient"}, {"settings.music", "Music"},
        {"settings.language", "Language"}, {"language.ru", "Русский"}, {"language.en", "English"},
        {"mode.survival", "survival"}, {"mode.creative", "creative"},
        {"inventory.creative", "Creative Inventory"}, {"inventory.pages", "Wheel: pages"},
        {"death.title", "You died"},
    };

    // Names used by tooltips and crafting results. One row keeps RU/EN
    // together; the key is the stable content ID from blocks.json/Item.cpp.
    const std::unordered_map<std::string_view, std::array<std::string, 2>> CONTENT_NAMES = {
        {"block.air", {"Воздух", "Air"}},
        {"block.grass", {"Дёрн", "Grass Block"}},
        {"block.short_grass", {"Низкая трава", "Short Grass"}},
        {"block.dirt", {"Земля", "Dirt"}},
        {"block.foliage", {"Дубовые листья", "Oak Leaves"}},
        {"block.oak_log", {"Дубовое бревно", "Oak Log"}},
        {"block.oak_planks", {"Дубовые доски", "Oak Planks"}},
        {"block.sand", {"Песок", "Sand"}},
        {"block.gravel", {"Гравий", "Gravel"}},
        {"block.clay", {"Глина", "Clay"}},
        {"block.stone", {"Камень", "Stone"}},
        {"block.cobblestone", {"Булыжник", "Cobblestone"}},
        {"block.coal_ore", {"Угольная руда", "Coal Ore"}},
        {"block.iron_ore", {"Железная руда", "Iron Ore"}},
        {"block.gold_ore", {"Золотая руда", "Gold Ore"}},
        {"block.diamond_ore", {"Алмазная руда", "Diamond Ore"}},
        {"block.redstone_ore", {"Редстоуновая руда", "Redstone Ore"}},
        {"block.bedrock", {"Бедрок", "Bedrock"}},
        {"block.water", {"Вода", "Water"}},
        {"block.workbench", {"Верстак", "Crafting Table"}},
        {"block.glass", {"Стекло", "Glass"}},
        {"block.ice", {"Лёд", "Ice"}},
        {"block.double_stone_slab", {"Двойная каменная плита", "Double Stone Slab"}},
        {"block.bricks", {"Кирпичи", "Bricks"}},
        {"block.tnt", {"Динамит", "TNT"}},
        {"block.iron_block", {"Железный блок", "Block of Iron"}},
        {"block.gold_block", {"Золотой блок", "Block of Gold"}},
        {"block.diamond_block", {"Алмазный блок", "Block of Diamond"}},
        {"block.chest", {"Сундук", "Chest"}},
        {"block.bookshelf", {"Книжная полка", "Bookshelf"}},
        {"block.mossy_cobblestone", {"Замшелый булыжник", "Mossy Cobblestone"}},
        {"block.obsidian", {"Обсидиан", "Obsidian"}},
        {"block.sponge", {"Губка", "Sponge"}},
        {"block.white_wool", {"Белая шерсть", "White Wool"}},
        {"block.mob_spawner", {"Рассадник монстров", "Monster Spawner"}},
        {"block.snow_block", {"Снежный блок", "Snow Block"}},
        {"block.snowy_grass", {"Заснеженный дёрн", "Snowy Grass Block"}},
        {"block.cactus", {"Кактус", "Cactus"}},
        {"block.note_block", {"Нотный блок", "Note Block"}},
        {"block.jukebox", {"Проигрыватель", "Jukebox"}},
        {"block.furnace", {"Печь", "Furnace"}},
        {"block.lit_furnace", {"Горящая печь", "Lit Furnace"}},
        {"block.dispenser", {"Раздатчик", "Dispenser"}},
        {"block.netherrack", {"Незерак", "Netherrack"}},
        {"block.soul_sand", {"Песок душ", "Soul Sand"}},
        {"block.glowstone", {"Светокамень", "Glowstone"}},
        {"block.piston", {"Поршень", "Piston"}},
        {"block.sticky_piston", {"Липкий поршень", "Sticky Piston"}},
        {"block.spruce_log", {"Еловое бревно", "Spruce Log"}},
        {"block.birch_log", {"Берёзовое бревно", "Birch Log"}},
        {"block.pumpkin", {"Тыква", "Pumpkin"}},
        {"block.jack_o_lantern", {"Светильник Джека", "Jack o'Lantern"}},
        {"block.spruce_foliage", {"Еловые листья", "Spruce Leaves"}},
        {"block.birch_foliage", {"Берёзовые листья", "Birch Leaves"}},
        {"block.lapis_block", {"Лазуритовый блок", "Block of Lapis Lazuli"}},
        {"block.lapis_ore", {"Лазуритовая руда", "Lapis Lazuli Ore"}},
        {"block.sandstone", {"Песчаник", "Sandstone"}},
        {"block.black_wool", {"Чёрная шерсть", "Black Wool"}},
        {"block.gray_wool", {"Серая шерсть", "Gray Wool"}},
        {"block.red_wool", {"Красная шерсть", "Red Wool"}},
        {"block.pink_wool", {"Розовая шерсть", "Pink Wool"}},
        {"block.green_wool", {"Зелёная шерсть", "Green Wool"}},
        {"block.lime_wool", {"Лаймовая шерсть", "Lime Wool"}},
        {"block.brown_wool", {"Коричневая шерсть", "Brown Wool"}},
        {"block.yellow_wool", {"Жёлтая шерсть", "Yellow Wool"}},
        {"block.blue_wool", {"Синяя шерсть", "Blue Wool"}},
        {"block.light_blue_wool", {"Голубая шерсть", "Light Blue Wool"}},
        {"block.purple_wool", {"Фиолетовая шерсть", "Purple Wool"}},
        {"block.magenta_wool", {"Пурпурная шерсть", "Magenta Wool"}},
        {"block.cyan_wool", {"Бирюзовая шерсть", "Cyan Wool"}},
        {"block.orange_wool", {"Оранжевая шерсть", "Orange Wool"}},
        {"block.light_gray_wool", {"Светло-серая шерсть", "Light Gray Wool"}},
        {"block.lava", {"Лава", "Lava"}},
        {"block.oak_stairs", {"Дубовая лестница", "Oak Stairs"}},
        {"block.oak_trapdoor", {"Дубовый люк", "Oak Trapdoor"}},
        {"block.oak_door_lower", {"Дубовая дверь", "Oak Door"}},
        {"block.oak_door_upper", {"Дубовая дверь", "Oak Door"}},
        {"block.iron_door_lower", {"Железная дверь", "Iron Door"}},
        {"block.iron_door_upper", {"Железная дверь", "Iron Door"}},
        {"block.bed_head", {"Кровать", "Bed"}},
        {"block.bed_foot", {"Кровать", "Bed"}},
        {"block.cake", {"Торт", "Cake"}},
        {"item.none", {"Нет предмета", "No Item"}},
        {"item.wooden_sword", {"Деревянный меч", "Wooden Sword"}},
        {"item.stone_sword", {"Каменный меч", "Stone Sword"}},
        {"item.iron_sword", {"Железный меч", "Iron Sword"}},
        {"item.diamond_sword", {"Алмазный меч", "Diamond Sword"}},
        {"item.gold_sword", {"Золотой меч", "Golden Sword"}},
        {"item.wooden_shovel", {"Деревянная лопата", "Wooden Shovel"}},
        {"item.stone_shovel", {"Каменная лопата", "Stone Shovel"}},
        {"item.iron_shovel", {"Железная лопата", "Iron Shovel"}},
        {"item.diamond_shovel", {"Алмазная лопата", "Diamond Shovel"}},
        {"item.gold_shovel", {"Золотая лопата", "Golden Shovel"}},
        {"item.wooden_pickaxe", {"Деревянная кирка", "Wooden Pickaxe"}},
        {"item.stone_pickaxe", {"Каменная кирка", "Stone Pickaxe"}},
        {"item.iron_pickaxe", {"Железная кирка", "Iron Pickaxe"}},
        {"item.diamond_pickaxe", {"Алмазная кирка", "Diamond Pickaxe"}},
        {"item.gold_pickaxe", {"Золотая кирка", "Golden Pickaxe"}},
        {"item.wooden_axe", {"Деревянный топор", "Wooden Axe"}},
        {"item.stone_axe", {"Каменный топор", "Stone Axe"}},
        {"item.iron_axe", {"Железный топор", "Iron Axe"}},
        {"item.diamond_axe", {"Алмазный топор", "Diamond Axe"}},
        {"item.gold_axe", {"Золотой топор", "Golden Axe"}},
        {"item.wooden_hoe", {"Деревянная мотыга", "Wooden Hoe"}},
        {"item.stone_hoe", {"Каменная мотыга", "Stone Hoe"}},
        {"item.iron_hoe", {"Железная мотыга", "Iron Hoe"}},
        {"item.diamond_hoe", {"Алмазная мотыга", "Diamond Hoe"}},
        {"item.gold_hoe", {"Золотая мотыга", "Golden Hoe"}},
        {"item.stick", {"Палка", "Stick"}},
        {"item.coal", {"Уголь", "Coal"}},
        {"item.iron_ingot", {"Железный слиток", "Iron Ingot"}},
        {"item.gold_ingot", {"Золотой слиток", "Golden Ingot"}},
        {"item.diamond", {"Алмаз", "Diamond"}},
        {"item.redstone_dust", {"Редстоун", "Redstone Dust"}},
        {"item.sapling", {"Саженец", "Sapling"}},
        {"item.apple", {"Яблоко", "Apple"}},
        {"item.wheat_seeds", {"Семена пшеницы", "Wheat Seeds"}},
        {"item.golden_apple", {"Золотое яблоко", "Golden Apple"}},
        {"item.soup", {"Суп", "Soup"}},
        {"item.raw_porkchop", {"Сырая свинина", "Raw Porkchop"}},
        {"item.cooked_porkchop", {"Жареная свинина", "Cooked Porkchop"}},
        {"item.raw_fish", {"Сырая рыба", "Raw Fish"}},
        {"item.cooked_fish", {"Жареная рыба", "Cooked Fish"}},
        {"item.bread", {"Хлеб", "Bread"}},
        {"item.cookie", {"Печенье", "Cookie"}},
        {"item.egg", {"Яйцо", "Egg"}},
        {"item.milk_bucket", {"Ведро молока", "Bucket of Milk"}},
    };

    std::string display_name(const std::string& key, const std::string& id, bool english)
    {
        auto found = CONTENT_NAMES.find(key);
        if (found != CONTENT_NAMES.end()) return found->second[english ? 1 : 0];
        // A future content entry without a translation still has a readable
        // name. Its persistent ID is never modified or written back.
        std::string fallback = id;
        bool word_start = true;
        for (char& c : fallback) {
            if (c == '_') { c = ' '; word_start = true; }
            else { if (word_start && c >= 'a' && c <= 'z') c -= 'a' - 'A'; word_start = false; }
        }
        return fallback;
    }

    std::string current_language = "ru";
    const std::string missing = "?";
}

namespace ui {
    void set_language(const std::string& language_code) {
        current_language = language_code == "en" ? "en" : "ru";
    }

    const std::string& language() { return current_language; }

    std::string block_display_name(BlockType type) {
        const std::string& id = get_block_name(type);
        return display_name("block." + id, id, current_language == "en");
    }

    std::string item_display_name(ItemType type) {
        const std::string id = type == ItemType::None ? "none" : get_item_name(type);
        return display_name("item." + id, id, current_language == "en");
    }

    const std::string& tr(std::string_view key) {
        const Dictionary& dictionary = current_language == "en" ? EN : RU;
        auto found = dictionary.find(key);
        if (found != dictionary.end()) return found->second;
        auto content = CONTENT_NAMES.find(key);
        return content == CONTENT_NAMES.end() ? missing : content->second[current_language == "en" ? 1 : 0];
    }

}
