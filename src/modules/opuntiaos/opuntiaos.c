/*
 * Copyright (C) 2020-2022 The opuntiaOS Project Authors.
 *  + Contributed by Nikita Melekhin <nimelehin@gmail.com>
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "opunboot.h"
#include <pongo.h>
#include <stdbool.h>

#define ROUND_CEIL(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUND_FLOOR(a, b) ((a) & ~((b)-1))

extern dt_node_t* gDeviceTree;
extern uint64_t gIOBase;

extern uint64_t gOpuntiaosVbase;
extern uint64_t gOpuntiaosPbase;
extern uint64_t gLastLoadedElfVaddr;
void* gOpunKernel = NULL;
uint64_t gOpunKernelSize = 0;
void* gOpunBootArgs = 0;

static void* alloc_after_kenrel(size_t size)
{
    void* res = (void*)gLastLoadedElfVaddr;

    // Just remapping everything. Expensive, but easy :)
    uint64_t startvaddr = (uint64_t)res;
    uint64_t vaddr = startvaddr & (~0x3fffULL);
    uint64_t paddr = vaddr - gOpuntiaosVbase + gOpuntiaosPbase;
    uint64_t mapsize = ROUND_CEIL(size + (startvaddr - vaddr), 0x4000);
    extern void map_range_noflush_rwx(uint64_t va, uint64_t pa, uint64_t size, uint64_t sh, uint64_t attridx, bool overwrite);
    map_range_noflush_rwx(vaddr, paddr, mapsize, 3, 1, true);
    flush_tlb();

    gLastLoadedElfVaddr = ROUND_CEIL(gLastLoadedElfVaddr + size, 256);
    return res;
}

static void map_space_for_pmm()
{
    // TODO: Calc the real size.
    uint64_t vaddr = ROUND_FLOOR(gLastLoadedElfVaddr, 0x4000);
    uint64_t paddr = vaddr - gOpuntiaosVbase + gOpuntiaosPbase;
    uint64_t mapsize = 0x40000;

    extern void map_range_noflush_rwx(uint64_t va, uint64_t pa, uint64_t size, uint64_t sh, uint64_t attridx, bool overwrite);
    map_range_noflush_rwx(vaddr, paddr, mapsize, 3, 1, true);
    flush_tlb();
}

void opuntia_prep_boot()
{
    // pongoOS is used as bootloader for opuntiaOS and should have similar
    // functionality to other bootloaders thus mmu should be inited.
    // gOpunKernel = (void*)(((uint64_t)gOpunKernel) - kCacheableView + 0x800000000);
    gEntryPoint = (void*)gOpuntiaosVbase;

    memory_map_t* memmap_entry = alloc_after_kenrel(sizeof(memory_map_t));
    // Thinking that RAM which is avail starts from the address where kernel
    // is loaded.
    memmap_entry[0].startHi = (gOpuntiaosPbase >> 32);
    memmap_entry[0].startLo = (gOpuntiaosPbase & 0xffffffff);
    memmap_entry[0].sizeHi = 0x0;
    // TODO: get this data based on device. For now 512mb
    memmap_entry[0].sizeLo = (512 << 20);
    memmap_entry[0].type = 0x1; // Free

    opun_boot_args_t* args = alloc_after_kenrel(sizeof(opun_boot_args_t));
    args->vaddr = gOpuntiaosVbase;
    args->paddr = gOpuntiaosPbase;
    args->memory_map = (void*)memmap_entry;
    args->memory_map_size = 1;
    args->devtree = NULL;

    args->fb_boot_desc.vaddr = (uint64_t)gFramebuffer;
    args->fb_boot_desc.paddr = gBootArgs->Video.v_baseAddr;
    args->fb_boot_desc.width = gBootArgs->Video.v_width;
    args->fb_boot_desc.height = gBootArgs->Video.v_height;
    args->fb_boot_desc.pixels_per_row = (gBootArgs->Video.v_rowBytes >> 2);

    args->kernel_size = gLastLoadedElfVaddr - gOpuntiaosVbase;

    gOpunBootArgs = args;
    map_space_for_pmm();
    iprintf("Booting OpuntiaOS: %p(%p)\n", gEntryPoint, gOpunBootArgs);
}

volatile void jump_to_image(uint64_t args, uint64_t original_image);
void opuntia_boot()
{
    disable_interrupts();
    hexdump((void*)gEntryPoint, 0x80);
    jump_to_image((uint64_t)gEntryPoint, (uint64_t)gOpunBootArgs);
}
