#include "scripting/LuaScripting.hpp"

#include "items/Item.hpp"
#include "worldgen/Structure.hpp"

#include "raylib.h"

// Lua is built as C++ (see CMakeLists.txt), so its errors unwind as C++
// exceptions and destructors on the way run - its C headers are included
// without extern "C".
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <array>
#include <cstring>
#include <unordered_set>

namespace {

    // A call is stopped once it has run this many instructions (checked
    // every INSTRUCTION_STEP) - an endless loop, not a busy script.
    constexpr int INSTRUCTION_STEP = 10000;
    constexpr int MAX_INSTRUCTION_STEPS = 1000; // 10 million
    constexpr const char* POS_METATABLE = "BlockPos";
    constexpr const char* FACE_NAMES[6] = {"top", "bottom", "north", "south", "east", "west"};

    // Positions: pos(x, y, z), with helpers - defined in Lua, on the
    // metatable every position the engine hands a script shares.
    constexpr const char* PRELUDE = R"lua(
Pos.__index = Pos
Pos.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
Pos.__tostring = function(p) return "(" .. p.x .. ", " .. p.y .. ", " .. p.z .. ")" end
function pos(x, y, z) return setmetatable({x = x, y = y, z = z}, Pos) end
function Pos:offset(dx, dy, dz) return pos(self.x + dx, self.y + dy, self.z + dz) end
function Pos:above(n) return self:offset(0, n or 1, 0) end
function Pos:below(n) return self:offset(0, -(n or 1), 0) end
function Pos:north(n) return self:offset(0, 0, -(n or 1)) end
function Pos:south(n) return self:offset(0, 0, n or 1) end
function Pos:east(n) return self:offset(n or 1, 0, 0) end
function Pos:west(n) return self:offset(-(n or 1), 0, 0) end
function Pos:sides() return { self:north(), self:south(), self:east(), self:west() } end
)lua";

    lua_State* state = nullptr;
    std::function<void(const std::string&)> message_handler;
    std::unordered_set<std::string> reported; // each error once per start()

    // The event call in progress - what `world` acts through.
    BlockApi* current_api = nullptr;
    BlockUse* current_use = nullptr;
    int instruction_steps = 0;

    void say(const std::string& text)
    {
        if (message_handler) message_handler(text);
    }

    // An error: its whole traceback to the log, its first line to the chat -
    // once, however often the script hits it.
    void report_error(const std::string& text)
    {
        if (!reported.insert(text).second) return;
        TraceLog(LOG_WARNING, "script error: %s", text.c_str());
        say("[script] " + text.substr(0, text.find('\n')));
    }

    BlockApi& api(lua_State* L)
    {
        if (!current_api) luaL_error(L, "the world can only be used inside a block's event");
        return *current_api;
    }

    // --- Values between Lua and the engine ---

    BlockPos check_pos(lua_State* L, int index)
    {
        index = lua_absindex(L, index);
        luaL_checktype(L, index, LUA_TTABLE);
        auto coordinate = [&](const char* name) {
            lua_getfield(L, index, name);
            int is_number = 0;
            const lua_Integer value = lua_tointegerx(L, -1, &is_number);
            lua_pop(L, 1);
            if (!is_number) luaL_error(L, "a position needs whole numbers x, y and z");
            return static_cast<int>(value);
        };
        return {coordinate("x"), coordinate("y"), coordinate("z")};
    }

    void push_pos(lua_State* L, BlockPos pos)
    {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, pos.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, pos.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, pos.z);
        lua_setfield(L, -2, "z");
        luaL_setmetatable(L, POS_METATABLE);
    }

    BlockType check_block(lua_State* L, int index)
    {
        const char* name = luaL_checkstring(L, index);
        if (std::strcmp(name, "air") == 0) return BlockType::Air;
        if (const std::optional<BlockType> type = block_type_from_name(name)) return *type;
        luaL_error(L, "no block named '%s'", name);
        return BlockType::Air;
    }

    // A block or an item, by name.
    ItemRef check_item(lua_State* L, int index)
    {
        const char* name = luaL_checkstring(L, index);
        if (const std::optional<BlockType> type = block_type_from_name(name)) return ItemRef(*type);
        if (const std::optional<ItemType> type = item_type_from_name(name)) return ItemRef(*type);
        luaL_error(L, "no block or item named '%s'", name);
        return ItemRef(BlockType::Air);
    }

    int check_count(lua_State* L, int index, int fallback)
    {
        return static_cast<int>(luaL_optinteger(L, index, fallback));
    }

    // --- world.* ---

    int world_get_block(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        lua_pushstring(L, get_block_name(api(L).get_block(pos)).c_str());
        return 1;
    }

    int world_get_property(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const std::optional<int> value = api(L).get_property(pos, luaL_checkstring(L, 2));
        if (value) lua_pushinteger(L, *value);
        else lua_pushnil(L);
        return 1;
    }

    int world_get_light(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        lua_pushinteger(L, api(L).get_light(pos));
        return 1;
    }

    int world_get_sky_light(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        lua_pushinteger(L, api(L).get_sky_light(pos));
        return 1;
    }

    int world_get_block_light(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        lua_pushinteger(L, api(L).get_block_light(pos));
        return 1;
    }

    int world_is_block_near(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const BlockType type = check_block(L, 2);
        const int radius = check_count(L, 3, 1);
        const int height = check_count(L, 4, radius);
        lua_pushboolean(L, api(L).is_block_near(pos, type, radius, height));
        return 1;
    }

    // random(): 0..1; random(a, b): a whole number a..b.
    int world_random(lua_State* L)
    {
        const float unit = api(L).random();
        if (lua_gettop(L) < 2) {
            lua_pushnumber(L, unit);
            return 1;
        }
        const lua_Integer low = luaL_checkinteger(L, 1), high = luaL_checkinteger(L, 2);
        if (high < low) return luaL_error(L, "random(a, b) needs a <= b");
        const lua_Integer span = high - low + 1;
        lua_pushinteger(L, std::min(high, low + static_cast<lua_Integer>(unit * static_cast<float>(span))));
        return 1;
    }

    int world_tick(lua_State* L)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(api(L).game_tick()));
        return 1;
    }

    int world_set_block(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const BlockType type = check_block(L, 2);
        api(L).set_block(pos, type);
        return 0;
    }

    int world_set_property(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const std::string name = luaL_checkstring(L, 2);
        const int value = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) ? 1 : 0) : static_cast<int>(luaL_checkinteger(L, 3));
        api(L).set_property(pos, name, value);
        return 0;
    }

    int world_break_block(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const bool drops = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
        api(L).break_block(pos, drops);
        return 0;
    }

    int world_drop_item(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const ItemRef item = check_item(L, 2);
        const int count = check_count(L, 3, 1);
        if (count > 0) api(L).drop_item(pos, item.stack(count));
        return 0;
    }

    int world_schedule_tick(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const int delay = check_count(L, 2, 1);
        api(L).schedule_tick(pos, delay);
        return 0;
    }

    int world_place_structure(lua_State* L)
    {
        const BlockPos pos = check_pos(L, 1);
        const char* name = luaL_checkstring(L, 2);
        for (const StructureDefinition& structure : get_structures()) {
            if (structure.name != name) continue;
            lua_pushboolean(L, api(L).place_structure(pos, structure));
            return 1;
        }
        return luaL_error(L, "no structure named '%s'", name);
    }

    constexpr luaL_Reg WORLD_FUNCTIONS[] = {
        {"get_block", world_get_block},
        {"get_property", world_get_property},
        {"get_light", world_get_light},
        {"get_sky_light", world_get_sky_light},
        {"get_block_light", world_get_block_light},
        {"is_block_near", world_is_block_near},
        {"random", world_random},
        {"tick", world_tick},
        {"set_block", world_set_block},
        {"set_property", world_set_property},
        {"break_block", world_break_block},
        {"drop_item", world_drop_item},
        {"schedule_tick", world_schedule_tick},
        {"place_structure", world_place_structure},
        {nullptr, nullptr},
    };

    // use:take(n) - takes n (1) from what the player holds.
    int use_take(lua_State* L)
    {
        if (!current_use) return luaL_error(L, "use:take() only works during on_use");
        api(L).take_held(*current_use, check_count(L, 2, 1));
        return 0;
    }

    // What on_use gets: { face, creative, held = { name, count } or nil, take }.
    void push_use(lua_State* L, BlockUse& use)
    {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, FACE_NAMES[static_cast<int>(use.face)]);
        lua_setfield(L, -2, "face");
        lua_pushboolean(L, use.creative);
        lua_setfield(L, -2, "creative");
        if (!use.held.empty()) {
            lua_createtable(L, 0, 2);
            const std::string& name = use.held.holds_item() ? get_item_name(use.held.tool) : get_block_name(use.held.block);
            lua_pushstring(L, name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushinteger(L, use.held.count);
            lua_setfield(L, -2, "count");
            lua_setfield(L, -2, "held");
        }
        lua_pushcfunction(L, use_take);
        lua_setfield(L, -2, "take");
    }

    // print(...) - to the log and the chat.
    int script_print(lua_State* L)
    {
        std::string line;
        for (int i = 1; i <= lua_gettop(L); ++i) {
            if (i > 1) line += "  ";
            line += luaL_tolstring(L, i, nullptr);
            lua_pop(L, 1);
        }
        TraceLog(LOG_INFO, "script: %s", line.c_str());
        say("[script] " + line);
        return 0;
    }

    int traceback(lua_State* L)
    {
        const char* message = lua_tostring(L, 1);
        luaL_traceback(L, L, message ? message : "(an error that isn't text)", 1);
        return 1;
    }

    void instruction_hook(lua_State* L, lua_Debug*)
    {
        if (++instruction_steps > MAX_INSTRUCTION_STEPS) luaL_error(L, "ran too long (an endless loop?) - stopped");
    }

    // Runs the function on top of the stack (under its `args` arguments)
    // for one result, protected; an error reported as "<script>: <event>".
    // True if it ran - its result is then on top of the stack.
    bool protected_call(lua_State* L, int args, const std::string& script, const char* event)
    {
        const int function_index = lua_gettop(L) - args;
        lua_pushcfunction(L, traceback);
        lua_insert(L, function_index);
        const int saved_steps = instruction_steps; // a nested call (one script's event setting off another's)
        instruction_steps = 0;
        const int status = lua_pcall(L, args, 1, function_index);
        instruction_steps = saved_steps;
        lua_remove(L, function_index); // the traceback handler
        if (status == LUA_OK) return true;
        const char* message = lua_tostring(L, -1);
        report_error(script + (event ? std::string(": ") + event : std::string()) + ": " + (message ? message : "?"));
        lua_pop(L, 1);
        return false;
    }

    // A block behavior from a script's table of event functions.
    class ScriptBehavior : public BlockBehavior {
    public:
        enum Event { CanPlace, CanStay, OnPlaced, OnBroken, OnUse, OnRandomTick, OnScheduledTick, OnNeighborChanged, EVENT_COUNT };
        static constexpr const char* EVENT_NAMES[EVENT_COUNT] = {"can_place",      "can_stay",       "on_placed",         "on_broken",
                                                                 "on_use",         "on_random_tick", "on_scheduled_tick", "on_neighbor_changed"};

        ScriptBehavior(std::string name, int table_ref, const std::array<bool, EVENT_COUNT>& has)
            : name(std::move(name)), table_ref(table_ref), has(has) {}

        bool can_place(BlockApi& api, BlockPos pos) override { return call(CanPlace, api, pos, nullptr, nullptr, true); }
        bool can_stay(BlockApi& api, BlockPos pos) override { return call(CanStay, api, pos, nullptr, nullptr, true); }
        void on_placed(BlockApi& api, BlockPos pos) override { call(OnPlaced, api, pos, nullptr, nullptr, false); }
        void on_broken(BlockApi& api, BlockPos pos) override { call(OnBroken, api, pos, nullptr, nullptr, false); }
        bool on_use(BlockApi& api, BlockPos pos, BlockUse& use) override { return call(OnUse, api, pos, nullptr, &use, false); }
        void on_random_tick(BlockApi& api, BlockPos pos) override { call(OnRandomTick, api, pos, nullptr, nullptr, false); }
        void on_scheduled_tick(BlockApi& api, BlockPos pos) override { call(OnScheduledTick, api, pos, nullptr, nullptr, false); }
        void on_neighbor_changed(BlockApi& api, BlockPos pos, BlockPos from) override
        {
            call(OnNeighborChanged, api, pos, &from, nullptr, false);
        }

    private:
        // The event with its arguments; its result as true/false -
        // `fallback` when it returns nothing, fails, or isn't there.
        bool call(Event event, BlockApi& block_api, BlockPos pos, const BlockPos* from, BlockUse* use, bool fallback)
        {
            if (!has[event] || !state) return fallback;
            lua_State* L = state;
            const int top = lua_gettop(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, table_ref);
            lua_getfield(L, -1, EVENT_NAMES[event]);
            lua_remove(L, -2);
            push_pos(L, pos);
            int args = 1;
            if (from) {
                push_pos(L, *from);
                ++args;
            }
            if (use) {
                push_use(L, *use);
                ++args;
            }
            BlockApi* outer_api = current_api;
            BlockUse* outer_use = current_use;
            current_api = &block_api;
            current_use = use;
            bool result = fallback;
            if (protected_call(L, args, name, EVENT_NAMES[event]) && !lua_isnil(L, -1)) result = lua_toboolean(L, -1);
            current_api = outer_api;
            current_use = outer_use;
            lua_settop(L, top);
            return result;
        }

        std::string name;
        int table_ref;
        std::array<bool, EVENT_COUNT> has;
    };

    // A script name: letters, digits, _ - and / for a subfolder, never "..".
    bool valid_script_name(const std::string& name)
    {
        if (name.empty() || name.find("..") != std::string::npos || name.front() == '/') return false;
        for (char c : name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '-';
            if (!ok) return false;
        }
        return true;
    }
}

namespace scripting {

    std::string directory()
    {
        return std::string(ASSETS_PATH) + "scripts/";
    }

    void set_message_handler(std::function<void(const std::string&)> handler)
    {
        message_handler = std::move(handler);
    }

    void stop()
    {
        if (state) lua_close(state);
        state = nullptr;
        current_api = nullptr;
        current_use = nullptr;
    }

    void start()
    {
        stop();
        reported.clear();
        state = luaL_newstate();
        lua_State* L = state;

        // Only what can't reach outside the game: no io, os, package, debug.
        luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
        luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
        luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
        luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
        luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
        lua_pop(L, 5);
        for (const char* unsafe : {"dofile", "loadfile", "load"}) {
            lua_pushnil(L);
            lua_setglobal(L, unsafe);
        }
        lua_pushcfunction(L, script_print);
        lua_setglobal(L, "print");

        luaL_newmetatable(L, POS_METATABLE);
        lua_setglobal(L, "Pos");
        if (luaL_dostring(L, PRELUDE) != LUA_OK) {
            report_error(std::string("prelude: ") + lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        luaL_newlib(L, WORLD_FUNCTIONS);
        lua_setglobal(L, "world");
        lua_sethook(L, instruction_hook, LUA_MASKCOUNT, INSTRUCTION_STEP);
    }

    std::shared_ptr<BlockBehavior> load_block_script(const std::string& name)
    {
        if (!state) start();
        lua_State* L = state;
        if (!valid_script_name(name)) {
            report_error("bad script name '" + name + "' (letters, digits, _ and / only)");
            return nullptr;
        }
        const std::string path = directory() + name + ".lua";
        if (luaL_loadfilex(L, path.c_str(), "t") != LUA_OK) {
            const char* message = lua_tostring(L, -1);
            report_error(message ? message : ("can't load " + path));
            lua_pop(L, 1);
            return nullptr;
        }
        current_api = nullptr; // the world isn't there while it loads
        if (!protected_call(L, 0, name, nullptr)) return nullptr;
        if (!lua_istable(L, -1)) {
            report_error(name + ": the script must return a table of events");
            lua_pop(L, 1);
            return nullptr;
        }

        // Which events it has - and a warning for a name that looks like a
        // misspelled one.
        std::array<bool, ScriptBehavior::EVENT_COUNT> has{};
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const std::string key = lua_tostring(L, -2);
                bool known = false;
                for (int e = 0; e < ScriptBehavior::EVENT_COUNT; ++e) {
                    if (key != ScriptBehavior::EVENT_NAMES[e]) continue;
                    known = true;
                    has[static_cast<size_t>(e)] = lua_isfunction(L, -1);
                }
                if (!known && (key.rfind("on_", 0) == 0 || key.rfind("can_", 0) == 0)) {
                    report_error(name + ": no event called '" + key + "'");
                }
            }
            lua_pop(L, 1);
        }
        const int table_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        return std::make_shared<ScriptBehavior>(name, table_ref, has);
    }

} // namespace scripting
