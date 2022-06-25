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

extern uint32_t* gFramebuffer;

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

void* alloc_after_kernel(size_t size)
{
    // It is already mmaped by a block of 2mb
    void* res = (void*)gLastLoadedElfVaddr;
    gLastLoadedElfVaddr = ROUND_CEIL(gLastLoadedElfVaddr + size, 256);

    if ((gLastLoadedElfVaddr + (32 << 20)) < gLastLoadedElfVaddr) {
        panic("OOM during boot");
    }
    return res;
}

static void devtree_fillup(void* devtree)
{
    if (!devtree) {
        panic("No devtree loaded");
        return;
    }

    devtree_header = (devtree_header_t*)devtree;
    devtree_body = (devtree_entry_t*)&devtree_header[1];
    devtree_name_section = ((char*)devtree_header + devtree_header->name_list_offset);

    devtree_entry_t* rddev = devtree_find_device("ramdisk");
    if (!rddev) {
        panic("No ramdisk entry");
        return;
    }
    rddev->region_base = (uint64_t)gOpuntiaRamdiskPbase;
    rddev->region_size = gOpuntiaRamdiskSize;

    devtree_entry_t* ramdev = devtree_find_device("ram");
    if (!ramdev) {
        panic("No ram entry");
        return;
    }
    ramdev->region_base = gOpuntiaosPbase;
    ramdev->region_size = 368 << 20;

    devtree_entry_t* simplefb = devtree_find_device("simplefb");
    if (!simplefb) {
        panic("No simplefb entry");
        return;
    }
    uint64_t rowpixels = gBootArgs->Video.v_rowBytes >> 2;
    uint64_t height = gBootArgs->Video.v_height;
    uint64_t fbsize = height * rowpixels * 4;
    simplefb->region_base = (uint64_t)gBootArgs->Video.v_baseAddr;
    simplefb->region_size = fbsize;
    simplefb->aux1 = gBootArgs->Video.v_width;
    simplefb->aux2 = gBootArgs->Video.v_height;
    simplefb->aux3 = (gBootArgs->Video.v_rowBytes >> 2);
    simplefb->aux4 = (uint64_t)gFramebuffer;
}

static void devtree_fillup_rawimage()
{
    devtree_header = NULL;
    for (uint64_t vaddr = gOpuntiaosVbase; gOpuntiaosVbase < gLastLoadedElfVaddr; vaddr++) {
        if (gOpuntiaosVbase > gLastLoadedElfVaddr - DEVTREE_HEADER_SIGNATURE_LEN) {
            panic("No devtree in raw image");
        }

        if (memcmp((void*)vaddr, DEVTREE_HEADER_SIGNATURE, DEVTREE_HEADER_SIGNATURE_LEN) == 0) {
            devtree_header = (devtree_header_t*)vaddr;
            break;
        }
    }

    devtree_fillup(devtree_header);
}

// Loading with a new boot method.
void opuntia_load_rawimage()
{
    gEntryPoint = (void*)gOpuntiaosPbase;
    devtree_fillup_rawimage();
}

void opuntia_prep_boot()
{
    // pongoOS is used as bootloader for opuntiaOS and should have similar
    // functionality to other bootloaders. At this stage, there is
    // a common entry point for qemu-virt and apl devs, so we have to use
    // mmu here to emulate loading at 1gb mark.
    opuntia_load_rawimage();
    iprintf("Booting rawimage OpuntiaOS: %p(None) %d -- none\n", gEntryPoint, is_16k());
}

volatile void jump_to_image(uint64_t args, uint64_t original_image);
void opuntia_boot()
{
    disable_interrupts();
    hexdump((void*)gEntryPoint, 0x80);
    jump_to_image((uint64_t)gEntryPoint, (uint64_t)gOpunBootArgs);
}
