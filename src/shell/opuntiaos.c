/*
 * Copyright (C) 2020-2022 The opuntiaOS Project Authors.
 *  + Contributed by Nikita Melekhin <nimelehin@gmail.com>
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pongo.h>

extern volatile char gBootFlag;

int gOpuntiaHasLoaded = 0;

#ifndef max
#define max(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#endif /* max */

#ifndef min
#define min(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
#endif /* min */

#define ROUND_CEIL(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUND_FLOOR(a, b) ((a) & ~((b)-1))

// TODO: Hardcoded values for now.
uint64_t gOpuntiaosVbase = 0xc10000000;
uint64_t gOpuntiaosPbase = 0x0;
uint64_t gLastLoadedElfVaddr = 0x0;

struct elfseg_data {
    uint64_t vbase;
    uint64_t size;
    char data[1];
};
typedef struct elfseg_data elfseg_data_t;

void pongo_load_elfseg_into_ram()
{
    if (!loader_xfer_recv_count) {
        iprintf("No segment is transmitted\n");
        return;
    }

    if (!gOpuntiaosPbase) {
        // Allocating ~64mb, which are aligned at 1mb mark.
        // It is used for kernel, arguments and PMM.
        gOpuntiaosPbase = ROUND_CEIL(alloc_phys(65 << 20), 1 << 20);
        iprintf("Load OpuntiaOS Paddr Base at %llx\n", gOpuntiaosPbase);
    }

    size_t datalen = loader_xfer_recv_count - 8;
    elfseg_data_t* elfseg = (elfseg_data_t*)loader_xfer_recv_data;

    // elfseg->opuntiaosVbase could be unaligned, fixing this
    uint64_t vaddr = elfseg->vbase & (~0x3fffULL);
    uint64_t paddr = vaddr - gOpuntiaosVbase + gOpuntiaosPbase;
    uint64_t mapsize = ROUND_CEIL(elfseg->size + (elfseg->vbase - vaddr), 0x4000);
    iprintf("Load OpuntiaOS Elf Segment: %llx -> %llx %llx\n", vaddr, paddr, mapsize);

    // TODO: Mapping everything with RWX perms, need to be fixed.
    extern void map_range_noflush_rwx(uint64_t va, uint64_t pa, uint64_t size, uint64_t sh, uint64_t attridx, bool overwrite);
    map_range_noflush_rwx(vaddr, paddr, mapsize, 3, 1, true);
    flush_tlb();
    memcpy((void*)elfseg->vbase, elfseg->data, datalen);

    loader_xfer_recv_count = 0;
    gOpuntiaHasLoaded = 1;
    gLastLoadedElfVaddr = max(gLastLoadedElfVaddr, vaddr + mapsize);
}

void pongo_dump_info()
{
    uint64_t ubase = dt_get_u32_prop("uart0", "reg");
    ubase += gIOBase;

    iprintf("uart base:\n");
    iprintf("   global io base: %llx\n", gIOBase);
    iprintf("   uart offset: %llx\n", ubase);

    iprintf("screen info:\n");
    iprintf("   w: %lu h: %lu rp: %lu\n", gBootArgs->Video.v_width, gBootArgs->Video.v_height, gBootArgs->Video.v_rowBytes >> 2);
}

void pongo_boot_opuntia()
{
    if (!gOpuntiaHasLoaded) {
        iprintf("Can't load opuntiaOS : elf file is not loaded\n");
    }
    iprintf("Loading opuntiaOS for aarch64\n");

    gBootFlag = BOOT_FLAG_OPUNTIA;
    task_yield();
}

void opuntiaos_commands_register()
{
    command_register("booto", "boot oneos image", pongo_boot_opuntia);
    command_register("elfsego", "load elf seg", pongo_load_elfseg_into_ram);
    command_register("dumpinfoo", "dump info for opuntia", pongo_dump_info);
}
