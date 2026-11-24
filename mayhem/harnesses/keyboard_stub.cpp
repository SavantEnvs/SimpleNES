// Mock of the keyboard device: sf::Keyboard::isKeyPressed queries X11 (and aborts with no DISPLAY).
// Defined in the executable, so Controller.cpp's calls bind here instead of libsfml-window.
#include <SFML/Window/Keyboard.hpp>

namespace mayhem
{
unsigned g_key_mask = 0;
}

bool sf::Keyboard::isKeyPressed(sf::Keyboard::Key key)
{
    return key >= 0 && key < 8 && ((mayhem::g_key_mask >> key) & 1u);
}
