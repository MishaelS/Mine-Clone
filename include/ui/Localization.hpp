#pragma once

#include <string>
#include <string_view>
#include <cstdint>

enum class BlockType : uint8_t;
enum class ItemType : uint8_t;

namespace ui {

// Central menu localization. Screens deal only in stable keys; adding a
// third language therefore does not require changing their layout code.
void set_language(const std::string& language_code);
const std::string& language();
const std::string& tr(std::string_view key);
// Display text only; persistent block/item IDs remain language-independent.
std::string block_display_name(BlockType type);
std::string item_display_name(ItemType type);

}
