#pragma once

#include "world/BlockBehavior.hpp"

#include <functional>
#include <memory>
#include <string>

// Block behaviors written in Lua 5.4 - assets/scripts/<name>.lua, named by
// a block file's "script" (content/BlockFile.hpp). A script returns a table
// of event functions, the same events a C++ BlockBehavior has:
//
//   return {
//       on_random_tick = function(pos)
//           if not world.is_block_near(pos, "water", 4, 1) then
//               world.set_property(pos, "moist", 0)
//           end
//       end,
//       on_use = function(pos, use) ... return true end,
//   }
//
// and acts on the world only through the global `world` table - the
// engine's BlockApi, block and item names instead of ids. The whole API is
// listed in assets/scripts/README.md.
//
// Scripts are sandboxed: no files, no OS, no loading native code; a call
// that runs too long is stopped. An error stops only that call - it is
// logged and reported (see set_message_handler()) once per distinct
// message. Main thread only, like every block behavior.
namespace scripting {

    // assets/scripts/
    std::string directory();

    // (Re)starts Lua with the `world` API - dropping every script loaded
    // before. content::register_behaviors() calls it, so reloading every
    // behavior reloads the scripts too.
    void start();
    void stop();

    // The behavior assets/scripts/<name>.lua gives a block - nullptr (the
    // reason reported) if it's missing, fails to run or doesn't return a
    // table.
    std::shared_ptr<BlockBehavior> load_block_script(const std::string& name);

    // Where script errors and print() go besides the log - the game's chat.
    void set_message_handler(std::function<void(const std::string&)> handler);

} // namespace scripting
