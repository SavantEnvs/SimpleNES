#!/usr/bin/env python3
"""Regenerate mayhem/fuzz_rom/testsuite: one small iNES ROM per supported mapper (0,1,2,3,4,7,11,66).
The 6502 program waits for vblank, loads the palette and nametable through $2006/$2007, fills
page 2 and OAM-DMAs it, turns on NMI + rendering, then loops: OAM DMA, controller strobe/read,
APU writes, PRG/CHR bank-register writes ($8000-$FFFF, serial writes for SxROM), and a DMC sample.
The last 3 bytes after the banks are the harness's per-frame controller state."""
import os, sys

def assemble(mapper):
    code, labels, fix = [], {}, []
    def b(*xs): code.extend(xs)
    def lab(n): labels[n] = 0x8000 + len(code)
    def br(op, n): b(op, 0); fix.append(('rel', len(code) - 1, n))
    def jmp(n): b(0x4C, 0, 0); fix.append(('abs', len(code) - 2, n))
    lab('reset'); b(0x78, 0xD8, 0xA2, 0xFF, 0x9A)            # SEI CLD LDX#FF TXS
    b(0xA9, 0x00, 0x8D, 0x00, 0x20, 0x8D, 0x01, 0x20)          # PPUCTRL=PPUMASK=0
    lab('vb'); b(0x2C, 0x02, 0x20); br(0x10, 'vb')            # BIT $2002; BPL vb
    b(0xA9, 0x3F, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20)  # PPUADDR=$3F00
    b(0xA2, 0x00); lab('pal'); b(0x8A, 0x8D, 0x07, 0x20, 0xE8, 0xE0, 0x20); br(0xD0, 'pal')
    b(0xA9, 0x20, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20)  # PPUADDR=$2000
    b(0xA2, 0x00); lab('nt'); b(0x8A, 0x8D, 0x07, 0x20, 0xE8); br(0xD0, 'nt')
    b(0xA2, 0x00); lab('oam'); b(0x8A, 0x9D, 0x00, 0x02, 0xE8); br(0xD0, 'oam')
    b(0xA9, 0x00, 0x8D, 0x05, 0x20, 0x8D, 0x05, 0x20)          # scroll 0,0
    b(0xA9, 0x80, 0x8D, 0x00, 0x20, 0xA9, 0x1E, 0x8D, 0x01, 0x20)  # NMI on, rendering on
    b(0xA9, 0x58); lab('main')
    b(0xA9, 0x02, 0x8D, 0x14, 0x40)                            # OAM DMA page 2
    b(0xA9, 0x01, 0x8D, 0x16, 0x40, 0xA9, 0x00, 0x8D, 0x16, 0x40, 0xAD, 0x16, 0x40, 0xAD, 0x17, 0x40)
    b(0xA9, 0x1F, 0x8D, 0x15, 0x40)                            # APU enable all
    b(0xA9, 0xBF, 0x8D, 0x00, 0x40, 0xA9, 0x40, 0x8D, 0x02, 0x40, 0xA9, 0x08, 0x8D, 0x03, 0x40)  # pulse1
    b(0xA9, 0x81, 0x8D, 0x08, 0x40, 0xA9, 0x30, 0x8D, 0x0A, 0x40, 0xA9, 0x08, 0x8D, 0x0B, 0x40)  # triangle
    b(0xA9, 0x3F, 0x8D, 0x0C, 0x40, 0xA9, 0x05, 0x8D, 0x0E, 0x40, 0xA9, 0x08, 0x8D, 0x0F, 0x40)  # noise
    b(0xA9, 0x0F, 0x8D, 0x10, 0x40, 0xA9, 0x00, 0x8D, 0x12, 0x40, 0xA9, 0x01, 0x8D, 0x13, 0x40)  # DMC
    b(0xE6, 0x00, 0xA5, 0x00)                                  # INC $00; LDA $00
    # Bank values stay inside the 4 PRG / 2 CHR banks: the mappers do not bound-check them.
    if mapper == 1:   # SxROM: 5 serial writes of a 0..3 PRG bank number
        b(0x29, 0x03)                                          # AND #$03
        for _ in range(5): b(0x8D, 0x00, 0xE0, 0x4A)           # STA $E000; LSR
    elif mapper == 4: # MMC3: bank select/data, mirroring, IRQ latch/reload/enable
        b(0x29, 0x07, 0x8D, 0x00, 0x80, 0xA5, 0x00, 0x29, 0x07, 0x8D, 0x01, 0x80, 0x8D, 0x00, 0xA0,
          0xA9, 0x08, 0x8D, 0x00, 0xC0, 0x8D, 0x01, 0xC0, 0x8D, 0x01, 0xE0, 0x58)
    elif mapper == 2: # UxROM: 16 KB PRG bank
        b(0x29, 0x03, 0x8D, 0x00, 0x80)
    elif mapper == 3: # CNROM: 8 KB CHR bank
        b(0x29, 0x01, 0x8D, 0x00, 0x80)
    else:             # AxROM (32 KB PRG + mirroring bit), ColorDreams/GxROM (PRG/CHR nibbles)
        b(0x29, 0x11, 0x8D, 0x00, 0x80)
    jmp('main')
    lab('nmi'); b(0xE6, 0x01, 0x40)                            # INC $01; RTI
    lab('irq'); b(0x8D, 0x00, 0xE0, 0x40)                      # MMC3 IRQ ack; RTI
    for kind, at, n in fix:
        t = labels[n]
        if kind == 'rel':
            off = t - (0x8000 + at + 1); assert -128 <= off < 128; code[at] = off & 0xFF
        else:
            code[at], code[at + 1] = t & 0xFF, t >> 8
    return code, labels

def rom(mapper, prg_banks=4, chr_banks=2):
    code, labels = assemble(mapper)
    prg = bytearray([0xEA]) * (0x4000 * prg_banks)
    prg[:len(code)] = bytes(code)
    for base in range(0x4000 * prg_banks - 0x4000, -1, -0x4000):   # every 16 KB bank ends in the vectors
        if base: prg[base:base + len(code)] = bytes(code)
        v = base + 0x3FFA
        for i, n in enumerate(('nmi', 'reset', 'irq')):
            prg[v + 2 * i] = labels[n] & 0xFF; prg[v + 2 * i + 1] = labels[n] >> 8
    chr_ = bytes((i * 7) & 0xFF for i in range(0x2000 * chr_banks))
    hdr = bytes([0x4E, 0x45, 0x53, 0x1A, prg_banks, chr_banks, (mapper & 0xF) << 4 | 1, mapper & 0xF0]) + bytes(8)
    return hdr + bytes(prg) + chr_ + bytes([0x09, 0x81, 0xFF])

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'fuzz_rom', 'testsuite')
os.makedirs(out, exist_ok=True)
# AxROM is CHR-RAM only (MapperAxROM never reads CHR-ROM). GxROM (66) and ColorDreams (11) are not
# shipped: their prgbank member is never initialized, so every such ROM faults at the reset-vector
# fetch (an upstream bug the fuzzer finds by flipping header byte 6/7).
for m in (0, 1, 2, 3, 4):
    open(os.path.join(out, f'mapper{m:03d}.nes'), 'wb').write(rom(m))
open(os.path.join(out, 'mapper007.nes'), 'wb').write(rom(7, chr_banks=0))
open(os.path.join(out, 'mapper000-chrram.nes'), 'wb').write(rom(0, prg_banks=1, chr_banks=0))
