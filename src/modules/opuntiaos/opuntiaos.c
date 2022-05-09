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

#define LAUNCH_SERVER_PATH "/System/launch_server"

extern dt_node_t* gDeviceTree;
extern uint64_t gIOBase;

extern uint64_t gOpuntiaosVbase;
extern uint64_t gOpuntiaosPbase;
extern uint64_t gLastLoadedElfVaddr;

extern uint64_t gCOPYOpuntiaosVbase;
extern uint64_t gCOPYOpuntiaosPbase;

extern void* gOpuntiaDevtreeBase;
extern uint64_t gOpuntiaDevtreeSize;

extern void* gOpuntiaRamdiskVbase;
extern void* gOpuntiaRamdiskPbase;
extern uint64_t gOpuntiaRamdiskSize;

void* gOpunKernel = NULL;
uint64_t gOpunKernelSize = 0;
void* gOpunBootArgs = 0;

static devtree_header_t* devtree_header;
static devtree_entry_t* devtree_body;
static char* devtree_name_section;

const char* devtree_name_of_entry(devtree_entry_t* en)
{
    if (&devtree_body[0] <= en && en <= &devtree_body[devtree_header->entries_count]) {
        return &devtree_name_section[en->rel_name_offset];
    }
    return NULL;
}

devtree_entry_t* devtree_find_device(const char* name)
{
    if (!devtree_body) {
        return NULL;
    }

    for (int i = 0; i < devtree_header->entries_count; i++) {
        const char* curdev_name = devtree_name_of_entry(&devtree_body[i]);
        if (curdev_name && strcmp(curdev_name, name) == 0) {
            return &devtree_body[i];
        }
    }
    return NULL;
}

static void devtree_fillup_ramdisk()
{
    if (!gOpuntiaDevtreeBase) {
        panic("No devtree loaded");
        return;
    }

    devtree_header = (devtree_header_t*)gOpuntiaDevtreeBase;
    devtree_body = (devtree_entry_t*)&devtree_header[1];
    devtree_name_section = ((char*)devtree_header + devtree_header->name_list_offset);

    devtree_entry_t* rddev = devtree_find_device("ramdisk");
    if (!rddev) {
        panic("No ramdisk entry");
        return;
    }

    rddev->region_base = (uint64_t)gOpuntiaRamdiskPbase;
    rddev->region_size = gOpuntiaRamdiskSize;

    devtree_entry_t* aplfb = devtree_find_device("aplfb");
    if (!aplfb) {
        panic("No aplfb entry");
        return;
    }

    uint64_t rowpixels = gBootArgs->Video.v_rowBytes >> 2;
    uint64_t height = gBootArgs->Video.v_height;
    uint64_t fbsize = height * rowpixels * 4;
    aplfb->region_base = (uint64_t)gBootArgs->Video.v_baseAddr;
    aplfb->region_size = fbsize;
}

static void* alloc_after_kernel(size_t size)
{
    // It is already mmaped by a block of 2mb
    void* res = (void*)gLastLoadedElfVaddr;
    gLastLoadedElfVaddr = ROUND_CEIL(gLastLoadedElfVaddr + size, 256);

    if ((gLastLoadedElfVaddr + (32 << 20)) < gLastLoadedElfVaddr) {
        iprintf("OOM during boot\n");
        for (;;) { }
    }
    return res;
}

void opuntia_prep_boot()
{
    // pongoOS is used as bootloader for opuntiaOS and should have similar
    // functionality to other bootloaders. At this stage, there is
    // a common entry point for qemu-virt and apl devs, so we have to use
    // mmu here to emulate loading at 1gb mark.
    gEntryPoint = (void*)gOpuntiaosVbase;

    // TODO: Remove this, just a double-check that gOpuntiaosVbase is not used here.
    if (memcmp((void*)gOpuntiaosVbase, (void*)gCOPYOpuntiaosVbase, 4 << 20)) {
        iprintf("Diff in loaded data, hmm...\n");
        for (;;) { }
    }

    void* devtree = NULL;
    if (gOpuntiaRamdiskPbase) {
        devtree_fillup_ramdisk();
    }

    if (gOpuntiaDevtreeSize) {
        devtree = alloc_after_kernel(gOpuntiaDevtreeSize);
        memcpy(devtree, gOpuntiaDevtreeBase, gOpuntiaDevtreeSize);
    }

    memory_map_t* memmap_entry = alloc_after_kernel(sizeof(memory_map_t));
    // Thinking that RAM which is avail starts from the address where kernel
    // is loaded.
    memmap_entry[0].startHi = (gOpuntiaosPbase >> 32);
    memmap_entry[0].startLo = (gOpuntiaosPbase & 0xffffffff);
    memmap_entry[0].sizeHi = 0x0;
    // TODO: get this data based on device. For now 512mb
    memmap_entry[0].sizeLo = (512 << 20);
    memmap_entry[0].type = 0x1; // Free

    opun_boot_args_t* args = alloc_after_kernel(sizeof(opun_boot_args_t));
    args->vaddr = gOpuntiaosVbase;
    args->paddr = gOpuntiaosPbase;
    args->memory_map = (void*)memmap_entry;
    args->memory_map_size = 1;
    args->devtree = devtree;
    memcpy(args->init_process, LAUNCH_SERVER_PATH, sizeof(LAUNCH_SERVER_PATH));

    args->fb_boot_desc.vaddr = (uint64_t)gFramebuffer;
    args->fb_boot_desc.paddr = gBootArgs->Video.v_baseAddr;
    args->fb_boot_desc.width = gBootArgs->Video.v_width;
    args->fb_boot_desc.height = gBootArgs->Video.v_height;
    args->fb_boot_desc.pixels_per_row = (gBootArgs->Video.v_rowBytes >> 2);

    args->kernel_size = gLastLoadedElfVaddr - gOpuntiaosVbase;

    gOpunBootArgs = args;
    iprintf("Booting OpuntiaOS: %p(%p) %d -- %lx\n", gEntryPoint, gOpunBootArgs, is_16k(), gBootArgs->Video.v_baseAddr);
}

volatile void jump_to_image(uint64_t args, uint64_t original_image);
void opuntia_boot()
{
    disable_interrupts();
    hexdump((void*)gEntryPoint, 0x80);
    jump_to_image((uint64_t)gEntryPoint, (uint64_t)gOpunBootArgs);
}
