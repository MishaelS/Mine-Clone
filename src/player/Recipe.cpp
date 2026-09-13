#include "player/Recipe.hpp"
#include "player/Item.hpp"
#include "core/Json.hpp"

#include "raylib.h"

#include <optional>

namespace {
    struct Ingredient {
        bool is_item = false;
        BlockType block = BlockType::Air;
        ItemType item = ItemType::None;
    };

    struct Recipe {
        bool output_is_item = false;
        BlockType output_block = BlockType::Air;
        ItemType output_item = ItemType::None;
        int output_count = 1;

        bool shapeless = false;
        int grid_size = 2; // 2 or 3 (square) - the smallest grid this recipe fits in

        // Shaped only: pattern_rows x pattern_cols, row-major, std::nullopt = empty cell.
        int pattern_rows = 0;
        int pattern_cols = 0;
        std::vector<std::optional<Ingredient>> pattern;

        // Shapeless only: one entry per required unit (an ingredient needed
        // x3 appears 3 times) - order doesn't matter, matching is by
        // multiset.
        std::vector<Ingredient> shapeless_ingredients;
    };

    std::vector<Recipe> recipes;

    std::optional<Ingredient> parse_ingredient(const std::string& name)
    {
        if (name.empty()) return std::nullopt;
        if (std::optional<BlockType> block = block_type_from_name(name)) {
            return Ingredient{false, *block, ItemType::None};
        }
        if (std::optional<ItemType> item = item_type_from_name(name)) {
            return Ingredient{true, BlockType::Air, *item};
        }
        return std::nullopt;
    }

    bool ingredient_matches(const Ingredient& ingredient, const ItemStack& stack)
    {
        if (stack.empty()) return false;
        if (ingredient.is_item) return stack.holds_item() && stack.tool == ingredient.item;
        return !stack.holds_item() && stack.block == ingredient.block;
    }
}

void Load_recipes()
{
    recipes.clear();

    char* fileText = LoadFileText(ASSETS_PATH "recipes.json");
    if (fileText == nullptr) {
        throw std::runtime_error("Could not load " ASSETS_PATH "recipes.json");
    }
    Json root = Json::parse(fileText);
    UnloadFileText(fileText);

    for (const Json& entry : root["recipes"].as_array()) {
        const Json& output = entry["output"];
        std::optional<Ingredient> output_ingredient = parse_ingredient(output["name"].as_string());
        if (!output_ingredient) continue; // output not something this build has yet - skip whole recipe

        Recipe recipe;
        recipe.output_is_item = output_ingredient->is_item;
        recipe.output_block = output_ingredient->block;
        recipe.output_item = output_ingredient->item;
        recipe.output_count = std::max(1, static_cast<int>(output["count"].as_number(1)));
        recipe.shapeless = entry["shape"].as_string() == "shapeless";
        recipe.grid_size = entry["grid_size"].as_string().substr(0, 1) == "3" ? 3 : 2;

        bool all_resolved = true;
        if (recipe.shapeless) {
            for (const Json& ing : entry["ingredients"].as_array()) {
                std::optional<Ingredient> parsed = parse_ingredient(ing["name"].as_string());
                if (!parsed) { all_resolved = false; break; }
                int count = std::max(1, static_cast<int>(ing["count"].as_number(1)));
                for (int i = 0; i < count; ++i) recipe.shapeless_ingredients.push_back(*parsed);
            }
        } else {
            const std::vector<Json>& rows = entry["pattern"].as_array();
            recipe.pattern_rows = static_cast<int>(rows.size());
            recipe.pattern_cols = recipe.pattern_rows > 0 ? static_cast<int>(rows[0].as_array().size()) : 0;
            for (const Json& row : rows) {
                for (const Json& cell : row.as_array()) {
                    if (cell.get_type() != Json::Type::String) {
                        recipe.pattern.push_back(std::nullopt);
                        continue;
                    }
                    std::optional<Ingredient> parsed = parse_ingredient(cell.as_string());
                    if (!parsed) { all_resolved = false; break; }
                    recipe.pattern.push_back(parsed);
                }
                if (!all_resolved) break;
            }
        }
        if (!all_resolved) continue;

        recipes.push_back(std::move(recipe));
    }
}

std::optional<ItemStack> match_recipe(const std::vector<ItemStack>& grid, int rows, int cols)
{
    for (const Recipe& recipe : recipes) {
        if (recipe.grid_size > rows || recipe.grid_size > cols) continue;

        bool matched = false;
        if (recipe.shapeless) {
            std::vector<const ItemStack*> occupied;
            for (const ItemStack& cell : grid) {
                if (!cell.empty()) occupied.push_back(&cell);
            }
            if (occupied.size() == recipe.shapeless_ingredients.size()) {
                std::vector<bool> used(occupied.size(), false);
                matched = true;
                for (const Ingredient& ingredient : recipe.shapeless_ingredients) {
                    bool found = false;
                    for (size_t i = 0; i < occupied.size(); ++i) {
                        if (used[i] || !ingredient_matches(ingredient, *occupied[i])) continue;
                        used[i] = true;
                        found = true;
                        break;
                    }
                    if (!found) { matched = false; break; }
                }
            }
        } else {
            for (int offset_row = 0; offset_row <= rows - recipe.pattern_rows && !matched; ++offset_row) {
                for (int offset_col = 0; offset_col <= cols - recipe.pattern_cols && !matched; ++offset_col) {
                    bool fits = true;
                    for (int r = 0; r < rows && fits; ++r) {
                        for (int c = 0; c < cols && fits; ++c) {
                            const ItemStack& cell = grid[r * cols + c];
                            bool inside = r >= offset_row && r < offset_row + recipe.pattern_rows &&
                                          c >= offset_col && c < offset_col + recipe.pattern_cols;
                            if (!inside) {
                                if (!cell.empty()) fits = false;
                                continue;
                            }
                            const auto& ingredient = recipe.pattern[
                                (r - offset_row) * recipe.pattern_cols + (c - offset_col)];
                            if (ingredient) {
                                if (!ingredient_matches(*ingredient, cell)) fits = false;
                            } else if (!cell.empty()) {
                                fits = false;
                            }
                        }
                    }
                    if (fits) matched = true;
                }
            }
        }

        if (matched) {
            ItemStack result;
            if (recipe.output_is_item) {
                result.tool = recipe.output_item;
                // A freshly crafted tool starts at full durability - left
                // at ItemStack's own default (0) it would read as already
                // broken, and shatter on the very first hit.
                const ItemProperties& properties = get_item_properties(recipe.output_item);
                if (properties.category == ItemCategory::Tool) result.durability = properties.max_durability;
            } else {
                result.block = recipe.output_block;
            }
            result.count = recipe.output_count;
            return result;
        }
    }
    return std::nullopt;
}

void consume_recipe_ingredients(std::vector<ItemStack>& grid, int rows, int cols)
{
    // Every occupied cell in a matched grid is necessarily one of the
    // recipe's own ingredients (an unmatched leftover cell would have
    // failed match_recipe() above already, shaped or shapeless alike) - so
    // crafting one output just costs exactly 1 unit out of every occupied
    // cell, no need to re-derive which cells "belong" to the recipe.
    if (!match_recipe(grid, rows, cols)) return;
    for (ItemStack& cell : grid) {
        if (cell.empty()) continue;
        if (--cell.count <= 0) cell.clear();
    }
}
