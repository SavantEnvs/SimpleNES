// Fuzz target: an iNES ROM file, as `SimpleNES <rom-path>` loads and runs it.
// Cartridge::loadFromFile parses the header and PRG/CHR banks, Mapper::createMapper picks the mapper
// from header bytes 6/7, then the ROM's own 6502 code drives CPU, PPU, APU, MainBus (incl. OAM DMA via
// MainBus::getPagePtr), PictureBus and the mapper for three frames.
// Bytes past the PRG/CHR banks are never read by the loader; the last three input bytes are the
// controller state for frames 0..2 (bit i = NES button i), so the input fully determines the run.
#include <cstddef>
#include <cstdint>

#include "headless_emulator.h"
#include "scratch_file.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    mayhem::ScratchFile rom(data, size, "simplenes-rom-");
    if (!rom.ok)
        return 0;
    mayhem::HeadlessEmulator emu;
    if (!emu.load(rom.path))
        return 0;
    for (size_t frame = 0; frame < 3; ++frame)
    {
        mayhem::g_key_mask = size > frame ? data[size - 1 - frame] : 0;
        emu.runFrame();
    }
    mayhem::g_key_mask = 0;
    return 0;
}
