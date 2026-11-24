// Headless SimpleNES: the component classes wired exactly as sn::Emulator (src/Emulator.cpp) wires
// them, minus the sf::RenderWindow, the wall-clock loop and AudioPlayer::start() (no audio device).
// Shared by the fuzz_rom harness and the selftest oracle.
#pragma once
#include <SFML/Graphics.hpp>
#include <memory>
#include <string>
#include <vector>

#include "APU/APU.h"
#include "APU/Constants.h"
#include "AudioPlayer.h"
#include "CPU.h"
#include "Cartridge.h"
#include "Controller.h"
#include "MainBus.h"
#include "Mapper.h"
#include "PPU.h"
#include "PictureBus.h"
#include "VirtualScreen.h"

namespace mayhem
{
// Keyboard state seen by sf::Keyboard::isKeyPressed (keyboard_stub.cpp): bit i = key i pressed.
// The harness binds button i of both controllers to key i, so bit i = NES button i.
extern unsigned g_key_mask;

// Member order matches sn::Emulator (include/Emulator.h).
struct HeadlessEmulator
{
    sn::CPU                     cpu;
    sn::AudioPlayer             audio;
    sn::PictureBus              pictureBus;
    sn::PPU                     ppu;
    sn::APU                     apu;
    sn::Cartridge               cartridge;
    std::unique_ptr<sn::Mapper> mapper;
    sn::Controller              controller1, controller2;
    sn::MainBus                 bus;
    sn::VirtualScreen           screen;

    HeadlessEmulator()
      : cpu(bus)
      , audio(static_cast<int>(1.0 / sn::apu_clock_period_s.count()))
      , ppu(pictureBus, screen)
      , apu(audio, cpu.createIRQHandler(), [&](sn::Address addr) { return DMCDMA(addr); })
      , bus(ppu, apu, controller1, controller2, [&](sn::Byte b) { OAMDMA(b); })
    {
        ppu.setInterruptCallback([&]() { cpu.nmiInterrupt(); });
        std::vector<sf::Keyboard::Key> keys;
        for (int i = 0; i < 8; ++i)
            keys.push_back(static_cast<sf::Keyboard::Key>(i));
        controller1.setKeyBindings(keys);
        controller2.setKeyBindings(keys);
    }

    // Emulator::OAMDMA / Emulator::DMCDMA
    void OAMDMA(sn::Byte page)
    {
        cpu.skipOAMDMACycles();
        auto page_ptr = bus.getPagePtr(page);
        if (page_ptr != nullptr)
            ppu.doDMA(page_ptr);
    }
    sn::Byte DMCDMA(sn::Address addr)
    {
        cpu.skipDMCDMACycles();
        return bus.read(addr);
    }

    // Emulator::run up to the window loop.
    bool load(const std::string& rom_path)
    {
        if (!cartridge.loadFromFile(rom_path))
            return false;
        mapper = sn::Mapper::createMapper(static_cast<sn::Mapper::Type>(cartridge.getMapper()),
                                          cartridge,
                                          cpu.createIRQHandler(),
                                          [&]() { pictureBus.updateMirroring(); });
        if (!mapper)
            return false;
        if (!bus.setMapper(mapper.get()) || !pictureBus.setMapper(mapper.get()))
            return false;
        cpu.reset();
        ppu.reset();
        screen.create(sn::ScanlineVisibleDots, sn::VisibleScanlines, 1.f, sf::Color::White);
        return true;
    }

    // One NES frame of the emulator loop body (Emulator::run, "Around one frame").
    void runFrame()
    {
        for (int i = 0; i < 29781; ++i)
        {
            ppu.step();
            ppu.step();
            ppu.step();
            cpu.step();
            apu.step();
        }
    }
};
}
