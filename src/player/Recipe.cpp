#include "player/Recipe.hpp"
#include "content/Content.hpp"
#include "player/Item.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
    struct Recipe {
        ItemStack output;

        bool shapeless = false;
        int grid_size = 2; // 2 or 3 (square) - the smallest grid this recipe fits in

        // Shaped only: pattern_rows x pattern_cols, row-major, std::nullopt = empty cell.
        int pattern_rows = 0;
        int pattern_cols = 0;
        std::vector<std::optional<ItemRef>> pattern;

        // Shapeless only: one entry per required unit (an ingredient needed
        // x3 appears 3 times) - order doesn't matter, matching is by
        // multiset.
        std::vector<ItemRef> shapeless_ingredients;
    };

    std::vector<Recipe> recipes;

    std::string describe(ItemRef what)
    {
        return what.is_item() ? get_item_name(what.item) : get_block_name(what.block);
    }
}

namespace content {

void shaped(ItemRef output, int count, std::initializer_list<const char*> pattern, std::initializer_list<Key> keys)
{
    std::vector<std::string> rows(pattern.begin(), pattern.end());
    auto row_empty = [](const std::string& row) {
        return row.find_first_not_of(' ') == std::string::npos;
    };
    // Trim fully empty rows/columns around the pattern, so it matches
    // wherever it's placed in the grid (Minecraft's own behavior).
    while (!rows.empty() && row_empty(rows.back())) rows.pop_back();
    while (!rows.empty() && row_empty(rows.front())) rows.erase(rows.begin());
    size_t first_col = std::string::npos;
    size_t last_col = 0;
    for (const std::string& row : rows) {
        size_t first = row.find_first_not_of(' ');
        if (first == std::string::npos) continue;
        first_col = std::min(first_col, first);
        last_col = std::max(last_col, row.find_last_not_of(' '));
    }
    if (rows.empty()) {
        throw std::runtime_error("shaped recipe for '" + describe(output) + "': empty pattern");
    }

    Recipe recipe;
    recipe.output = output.stack(std::max(1, count));
    recipe.pattern_rows = static_cast<int>(rows.size());
    recipe.pattern_cols = static_cast<int>(last_col - first_col + 1);
    recipe.grid_size = recipe.pattern_rows > 2 || recipe.pattern_cols > 2 ? 3 : 2;
    if (recipe.pattern_rows > 3 || recipe.pattern_cols > 3) {
        throw std::runtime_error("shaped recipe for '" + describe(output) + "': pattern larger than 3x3");
    }
    for (const std::string& row : rows) {
        for (size_t col = first_col; col <= last_col; ++col) {
            char symbol = col < row.size() ? row[col] : ' ';
            if (symbol == ' ') {
                recipe.pattern.push_back(std::nullopt);
                continue;
            }
            const Key* key = std::find_if(keys.begin(), keys.end(),
                                          [symbol](const Key& k) { return k.symbol == symbol; });
            if (key == keys.end()) {
                throw std::runtime_error("shaped recipe for '" + describe(output) + "': no key for '" +
                                         std::string(1, symbol) + "'");
            }
            recipe.pattern.push_back(key->what);
        }
    }
    recipes.push_back(std::move(recipe));
}

void shaped(ItemRef output, std::initializer_list<const char*> pattern, std::initializer_list<Key> keys)
{
    shaped(output, 1, pattern, keys);
}

void shapeless(ItemRef output, int count, std::initializer_list<ItemRef> ingredients)
{
    if (ingredients.size() == 0 || ingredients.size() > 9) {
        throw std::runtime_error("shapeless recipe for '" + describe(output) + "': needs 1-9 ingredients");
    }
    Recipe recipe;
    recipe.output = output.stack(std::max(1, count));
    recipe.shapeless = true;
    recipe.grid_size = ingredients.size() > 4 ? 3 : 2;
    recipe.shapeless_ingredients.assign(ingredients.begin(), ingredients.end());
    recipes.push_back(std::move(recipe));
}

} // namespace content

void Load_recipes()
{
    recipes.clear();
    content::register_recipes();
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
                for (const ItemRef& ingredient : recipe.shapeless_ingredients) {
                    bool found = false;
                    for (size_t i = 0; i < occupied.size(); ++i) {
                        if (used[i] || !ingredient.matches(*occupied[i])) continue;
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
                                if (!ingredient->matches(cell)) fits = false;
                            } else if (!cell.empty()) {
                                fits = false;
                            }
                        }
                    }
                    if (fits) matched = true;
                }
            }
        }

        // A freshly crafted tool starts at full durability - see
        // ItemRef::stack().
        if (matched) return recipe.output;
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
