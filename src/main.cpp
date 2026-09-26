#include "app/Application.hpp"

int main()
{
    // The application owns the window, the menus and the main loop; the
    // game itself (GameEngine) runs inside it once a world is opened from
    // the startup menu.
    Application app(1280, 720, "MineToo");
    app.run();
    return 0;
}
