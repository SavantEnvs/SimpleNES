// Fuzz target: the keybindings file, as `SimpleNES -C <path>` (main.cpp) passes it to
// sn::parseControllerConf (src/KeybindingsParser.cpp), with main.cpp's default bindings.
#include <SFML/Window/Keyboard.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "scratch_file.h"

namespace sn
{
// Declared the same way main.cpp declares it (no header ships it).
void parseControllerConf(std::string filepath, std::vector<sf::Keyboard::Key>& p1, std::vector<sf::Keyboard::Key>& p2);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    mayhem::ScratchFile conf(data, size, "simplenes-conf-");
    if (!conf.ok)
        return 0;
    std::vector<sf::Keyboard::Key> p1 { sf::Keyboard::J, sf::Keyboard::K, sf::Keyboard::RShift, sf::Keyboard::Return,
                                        sf::Keyboard::W, sf::Keyboard::S, sf::Keyboard::A,      sf::Keyboard::D },
      p2 { sf::Keyboard::Numpad5, sf::Keyboard::Numpad6, sf::Keyboard::Numpad8, sf::Keyboard::Numpad9,
           sf::Keyboard::Up,      sf::Keyboard::Down,    sf::Keyboard::Left,    sf::Keyboard::Right };
    sn::parseControllerConf(conf.path, p1, p2);
    return 0;
}
