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

#endif // OPUNBOOT