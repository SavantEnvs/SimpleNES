// Known-answer functional test for SimpleNES (upstream ships no test suite).
// Built WITHOUT sanitizers by build.sh; run by mayhem/test.sh. Each check prints
// "PASS <name>" or "FAIL <name>"; test.sh counts them.
//  * cpu_*: a hand-assembled NROM program (below) runs on the headless emulator; the test reads RAM
//    back through MainBus and checks values the 6502 code computed (ASL/ADC/STA abs,X loop, a
//    Fibonacci loop with zp,X addressing, controller strobe+serial read, OAM DMA via $4014).
//  * keys_*: sn::parseControllerConf on the repo's keybindings.conf and on a crafted file.
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "headless_emulator.h"
#include "scratch_file.h"

namespace sn
{
void parseControllerConf(std::string filepath, std::vector<sf::Keyboard::Key>& p1, std::vector<sf::Keyboard::Key>& p2);
}

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    (ok ? g_pass : g_fail)++;
}

static std::vector<uint8_t> buildRom()
{
    // 6502 program at $8000 (one 16 KB PRG bank, mirrored at $C000).
    const uint8_t prog[] = {
        0x78, 0xD8, 0xA2, 0xFF, 0x9A,       // SEI; CLD; LDX #$FF; TXS
        0xA2, 0x00,                         // LDX #0
        0x8A, 0x0A, 0x18, 0x69, 0x11,       // loop: TXA; ASL; CLC; ADC #$11
        0x9D, 0x00, 0x02, 0xE8, 0xD0, 0xF5, //   STA $0200,X; INX; BNE loop
        0xA9, 0x00, 0x8D, 0x03, 0x20,       // LDA #0; STA $2003  (OAMADDR)
        0xA9, 0x02, 0x8D, 0x14, 0x40,       // LDA #2; STA $4014  (OAM DMA from page 2)
        0xA9, 0x01, 0x85, 0x10, 0x85, 0x11, // LDA #1; STA $10; STA $11
        0xA2, 0x00,                         // LDX #0
        0xB5, 0x10, 0x18, 0x75, 0x11,       // fib: LDA $10,X; CLC; ADC $11,X
        0x95, 0x12, 0xE8, 0xE0, 0x0E, 0xD0, 0xF4, //   STA $12,X; INX; CPX #14; BNE fib
        0xA9, 0x01, 0x8D, 0x16, 0x40,       // LDA #1; STA $4016 (strobe on)
        0xA9, 0x00, 0x8D, 0x16, 0x40,       // LDA #0; STA $4016 (latch buttons)
        0xA2, 0x00,                         // LDX #0
        0xAD, 0x16, 0x40, 0x29, 0x01,       // joy: LDA $4016; AND #1
        0x95, 0x20, 0xE8, 0xE0, 0x08, 0xD0, 0xF4, //   STA $20,X; INX; CPX #8; BNE joy
        0xA9, 0xA5, 0x85, 0x30,             // LDA #$A5; STA $30 (done marker)
        0x4C, 0x4C, 0x80,                   // done: JMP done  ($804C)
        0x40,                               // $804F: RTI (NMI/IRQ handler)
    };
    std::vector<uint8_t> rom = { 'N', 'E', 'S', 0x1A, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    std::vector<uint8_t> prg(0x4000, 0xEA);
    std::copy(std::begin(prog), std::end(prog), prg.begin());
    const uint8_t vectors[] = { 0x4F, 0x80, 0x00, 0x80, 0x4F, 0x80 }; // NMI, RESET, IRQ
    std::copy(std::begin(vectors), std::end(vectors), prg.begin() + 0x3FFA);
    rom.insert(rom.end(), prg.begin(), prg.end());
    rom.insert(rom.end(), 0x2000, 0); // CHR bank
    return rom;
}

static void cpuTests()
{
    std::vector<uint8_t> rom = buildRom();
    mayhem::ScratchFile  file(rom.data(), rom.size(), "simplenes-selftest-");
    mayhem::HeadlessEmulator emu;
    bool loaded = file.ok && emu.load(file.path);
    check("cpu_rom_loads", loaded);
    if (!loaded)
        return;
    mayhem::g_key_mask = 0xA5; // buttons A, Select, Down, Right... bit i = button i
    emu.runFrame();
    emu.runFrame();
    mayhem::g_key_mask = 0;

    check("cpu_program_finished", emu.bus.read(0x30) == 0xA5);

    bool loop_ok = true;
    for (int x = 0; x < 256; ++x)
        loop_ok &= emu.bus.read(0x0200 + x) == uint8_t(2 * x + 0x11);
    check("cpu_asl_adc_abs_x_loop", loop_ok);

    uint8_t f[16] = { 1, 1 };
    for (int i = 0; i < 14; ++i)
        f[i + 2] = uint8_t(f[i] + f[i + 1]);
    bool fib_ok = true;
    for (int i = 0; i < 16; ++i)
        fib_ok &= emu.bus.read(0x10 + i) == f[i];
    check("cpu_fibonacci_zp_x", fib_ok);

    bool joy_ok = true;
    for (int i = 0; i < 8; ++i)
        joy_ok &= emu.bus.read(0x20 + i) == ((0xA5 >> i) & 1);
    check("controller_serial_read", joy_ok);

    bool oam_ok = true;
    for (int i = 0; i < 256; ++i)
    {
        emu.bus.write(0x2003, uint8_t(i));
        oam_ok &= emu.bus.read(0x2004) == uint8_t(2 * i + 0x11);
    }
    check("ppu_oam_dma", oam_ok);
}

static void keyTests(const char* repo_conf)
{
    using K = sf::Keyboard;
    std::vector<sf::Keyboard::Key> p1(8, K::Unknown), p2(8, K::Unknown);
    sn::parseControllerConf(repo_conf, p1, p2);
    check("keys_repo_conf_player1",
          p1 == std::vector<sf::Keyboard::Key> { K::J, K::K, K::RShift, K::Return, K::W, K::S, K::A, K::D });
    check("keys_repo_conf_player2",
          p2 == std::vector<sf::Keyboard::Key> { K::Numpad5, K::Numpad6, K::Numpad8, K::Numpad9, K::Up, K::Down,
                                                 K::Left, K::Right });

    const std::string conf = "# comment\n\n  [Player2]  \n Start =  F12 \nBogus = A\nA = NoSuchKey\n"
                             "[Player1]\nB=Tilde\nRight = Num7\ngarbage line\n";
    mayhem::ScratchFile file(reinterpret_cast<const uint8_t*>(conf.data()), conf.size(), "simplenes-keys-");
    std::vector<sf::Keyboard::Key> q1(8, K::Unknown), q2(8, K::Unknown);
    sn::parseControllerConf(file.path, q1, q2);
    std::vector<sf::Keyboard::Key> e1(8, K::Unknown), e2(8, K::Unknown);
    e1[1] = K::Tilde;
    e1[7] = K::Num7;
    e2[3] = K::F12;
    check("keys_crafted_conf", file.ok && q1 == e1 && q2 == e2);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <keybindings.conf>\n", argv[0]);
        return 2;
    }
    cpuTests();
    keyTests(argv[1]);
    std::printf("SUMMARY passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
