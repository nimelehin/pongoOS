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
uint64_t gOpuntiaosVbase = 0x40000000;
uint64_t gOpuntiaosPbase = 0x0;
uint64_t gLastLoadedElfVaddr = 0x0;

uint64_t gCOPYOpuntiaosVbase = 0xc30000000;
uint64_t gCOPYOpuntiaosPbase = 0x0;

void* gOpuntiaDevtreeBase = NULL;
uint64_t gOpuntiaDevtreeSize = 0x0;

void* gOpuntiaRamdiskVbase = (void*)0xc20000000;
void* gOpuntiaRamdiskPbase = NULL;
uint64_t gOpuntiaRamdiskSize = 0x0;

struct elfseg_data {
    uint64_t vbase;
    uint64_t size;
    char data[1];
};
typedef struct elfseg_data elfseg_data_t;

#define VMM_LV0_ENTITY_COUNT (512)
#define VMM_LV1_ENTITY_COUNT (512)
#define VMM_LV2_ENTITY_COUNT (512)
#define VMM_LV3_ENTITY_COUNT (512)
#define VMM_PAGE_SIZE (get_page_size())

#define PAGE_START(vaddr) ((vaddr & ~(uintptr_t)get_page_mask())
#define FRAME(addr) (addr / VMM_PAGE_SIZE)

#define PTABLE_LV_TOP (3)
#define PTABLE_LV0_VADDR_OFFSET (12)
#define PTABLE_LV1_VADDR_OFFSET (21)
#define PTABLE_LV2_VADDR_OFFSET (30)
#define PTABLE_LV3_VADDR_OFFSET (39)

#define VM_VADDR_OFFSET_AT_LEVEL(vaddr, off, ent) ((vaddr >> off) % ent)

static void dump_table(uint64_t virt)
{
    extern uint64_t* ttbr0;
    uint64_t* pdir = (uint64_t*)ttbr0;
    iprintf("Pdir itself %p\n", ttbr0);
    uint64_t lv2desc = pdir[VM_VADDR_OFFSET_AT_LEVEL(virt, PTABLE_LV2_VADDR_OFFSET, VMM_LV2_ENTITY_COUNT)];
    iprintf("Debug PDIR at virt %llx, lv2 info: %llx\n", virt, lv2desc);
    if ((lv2desc & 1) == 0 || (lv2desc & 0b11) == 0b01) {
        return;
    }

    pdir = (uint64_t*)(((lv2desc >> 12) << 12) & 0xffffffffffff);
    uint64_t lv1desc = pdir[VM_VADDR_OFFSET_AT_LEVEL(virt, PTABLE_LV1_VADDR_OFFSET, VMM_LV1_ENTITY_COUNT)];
    iprintf("Debug Ptable, lv1 at virt %llx, lv1 info: %llx\n", virt, lv1desc);

    if ((lv1desc & 1) == 0 || (lv1desc & 0b11) == 0b01) {
        return;
    }

    pdir = (uint64_t*)(((lv1desc >> 12) << 12) & 0xffffffffffff);
    uint64_t lv0desc = pdir[VM_VADDR_OFFSET_AT_LEVEL(virt, PTABLE_LV0_VADDR_OFFSET, VMM_LV0_ENTITY_COUNT)];
    iprintf("Debug Ptable, lv0 at virt %llx, lv1 info: %llx\n", virt, lv0desc);
}

void pongo_load_devtree()
{
    if (!loader_xfer_recv_count) {
        iprintf("No devtree is transmitted\n");
        return;
    }

    gOpuntiaDevtreeBase = malloc(loader_xfer_recv_count);
    if (!gOpuntiaDevtreeBase)
        panic("couldn't reserve heap for opuntia devtree");

    gOpuntiaDevtreeSize = loader_xfer_recv_count;
    memcpy(gOpuntiaDevtreeBase, loader_xfer_recv_data, loader_xfer_recv_count);
    iprintf("Load OpuntiaOS Paddr Base at %llx\n", gOpuntiaosPbase);

    loader_xfer_recv_count = 0;
}

void pongo_load_ramdisk()
{
    if (!loader_xfer_recv_count) {
        iprintf("No ramdisk is transmitted\n");
        return;
    }

    // Load it after 512mb mark which is a ramsize for opuntiaOS.
    uint64_t pa = alloc_phys(512 << 20);
    gOpuntiaRamdiskPbase = (void*)ROUND_CEIL(alloc_phys(loader_xfer_recv_count + (1 << 20)), 1 << 20);
    gOpuntiaRamdiskSize = loader_xfer_recv_count;
    free_phys(pa, 512 << 20);

    size_t size_to_map = ROUND_CEIL(gOpuntiaRamdiskSize, 16 << 10);
    extern void map_range_noflush_rwx(uint64_t va, uint64_t pa, uint64_t size, uint64_t sh, uint64_t attridx, bool overwrite);
    map_range_noflush_rwx((uint64_t)gOpuntiaRamdiskVbase, (uint64_t)gOpuntiaRamdiskPbase, size_to_map, 3, 1, true);
    flush_tlb();

    memcpy(gOpuntiaRamdiskVbase, loader_xfer_recv_data, loader_xfer_recv_count);

    iprintf("Load OpuntiaOS Ramdisk Paddr Base at %p\n", gOpuntiaRamdiskPbase);
    loader_xfer_recv_count = 0;
}

void pongo_load_elfseg_into_ram()
{
    if (!loader_xfer_recv_count) {
        iprintf("No segment is transmitted\n");
        return;
    }

    if (!gOpuntiaosPbase) {
        // Allocating ~64mb, which are aligned at 2mb mark.
        // It is used for kernel, arguments and PMM.
        gOpuntiaosPbase = ROUND_CEIL(alloc_phys(67 << 20), 2 << 20);
        gCOPYOpuntiaosPbase = ROUND_CEIL(alloc_phys(67 << 20), 2 << 20);
        iprintf("Load OpuntiaOS Paddr Base at %llx\n", gOpuntiaosPbase);

        // TODO: Mapping everything with RWX perms, need to be fixed.
        extern void map_range_noflush_rwx(uint64_t va, uint64_t pa, uint64_t size, uint64_t sh, uint64_t attridx, bool overwrite);
        map_range_noflush_rwx(gOpuntiaosVbase, gOpuntiaosPbase, 4 << 20, 3, 1, true);
        flush_tlb();

        map_range_noflush_rwx(gCOPYOpuntiaosVbase, gCOPYOpuntiaosPbase, 4 << 20, 3, 1, true);
        flush_tlb();

        memset((void*)gOpuntiaosVbase, 0, 4 << 20);
        memset((void*)gCOPYOpuntiaosVbase, 0, 4 << 20);

        dump_table(gOpuntiaosVbase);
        dump_table(0xfb0000000ULL);
    }

    size_t datalen = loader_xfer_recv_count - 16;
    elfseg_data_t* elfseg = (elfseg_data_t*)loader_xfer_recv_data;

    // elfseg->opuntiaosVbase could be unaligned, fixing this
    uint64_t vaddr = elfseg->vbase & (~0x3fffULL);
    uint64_t mapsize = ROUND_CEIL(elfseg->size + (elfseg->vbase - vaddr), 0x4000);
    iprintf("Load OpuntiaOS Elf Segment: %llx %zx\n", elfseg->vbase, datalen);

    memcpy((void*)elfseg->vbase, elfseg->data, datalen);
    memcpy((void*)(elfseg->vbase - gOpuntiaosVbase + gCOPYOpuntiaosVbase), elfseg->data, datalen);

    loader_xfer_recv_count = 0;
    gOpuntiaHasLoaded = 1;
    gLastLoadedElfVaddr = max(gLastLoadedElfVaddr, vaddr + mapsize);
}

void pongo_dump_info()
{
    uint64_t pstd = ROUND_CEIL(alloc_phys(16 << 20), 4 << 20);
    uint64_t vstd = 0xe20000000;

    iprintf("Debug Map %llx %llx %llx\n", vstd, pstd, 4ull << 20);

    extern void map_range_noflush_rwx(uint64_t va, uint64_t pa, uint64_t size, uint64_t sh, uint64_t attridx, bool overwrite);
    map_range_noflush_rwx(vstd, pstd, 4 << 20, 3, 1, true);
    flush_tlb();

    dump_table(vstd);
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
    command_register("devtreeo", "load opuntiaos devtree", pongo_load_devtree);
    command_register("ramdisko", "load opuntiaos devtree", pongo_load_ramdisk);
    command_register("dumpinfoo", "dump info for opuntia", pongo_dump_info);
}
