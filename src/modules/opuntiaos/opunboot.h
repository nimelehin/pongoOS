#ifndef OPUNBOOT
#define OPUNBOOT

#include <stddef.h>
#include <stdint.h>

struct memory_map {
    uint32_t startLo;
    uint32_t startHi;
    uint32_t sizeLo;
    uint32_t sizeHi;
    uint32_t type;
    uint32_t acpi_3_0;
};
typedef struct memory_map memory_map_t;

struct opun_fb_boot_desc {
    uintptr_t vaddr;
    uintptr_t paddr;
    size_t width;
    size_t height;
    size_t pixels_per_row;
};
typedef struct opun_fb_boot_desc opun_fb_boot_desc_t;

struct opun_boot_args {
    size_t paddr;
    size_t vaddr;
    void* memory_map;
    size_t memory_map_size;
    size_t kernel_size;
    void* devtree;
    opun_fb_boot_desc_t fb_boot_desc;
    char cmd_args[32];
    char init_process[32];
};
typedef struct opun_boot_args opun_boot_args_t;

#define PACKED __attribute__((packed))

struct PACKED devtree_header {
    char signature[8];
    uint32_t revision;
    uint32_t flags;
    uint32_t entries_count;
    uint32_t name_list_offset;
};
typedef struct devtree_header devtree_header_t;

#define DEVTREE_ENTRY_FLAGS_MMIO = (1 << 0)
#define DEVTREE_ENTRY_TYPE_IO (0)
#define DEVTREE_ENTRY_TYPE_FB (1)
#define DEVTREE_ENTRY_TYPE_UART (2)
#define DEVTREE_ENTRY_TYPE_RAM (3)
#define DEVTREE_ENTRY_TYPE_STORAGE (4)
#define DEVTREE_ENTRY_TYPE_BUS_CONTROLLER (5)

struct PACKED devtree_entry {
    uint32_t type;
    uint32_t flags;
    uint64_t region_base;
    uint64_t region_size;
    uint32_t irq_lane;
    uint32_t irq_flags;
    uint32_t irq_priority;
    uint32_t rel_name_offset;
    uint64_t aux1;
    uint64_t aux2;
};
typedef struct devtree_entry devtree_entry_t;

#endif // OPUNBOOT