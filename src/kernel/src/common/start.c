#include <chipset.h>
#include <constants.h>
#include <dtb.h>
#include <hardwareinfo.h>
#include <interrupt.h>
#include <klibc.h>
#include <mem_alloc.h>
#include <stdint.h>
#include <test.h>
#include <vm2.h>

extern size_t __DTB_START[];

/// Entrypoint for the C part of the kernel.
/// This function is called by the assembly located in [startup.s].
/// The MMU has already been initialized here but only the first MiB of the kernel has been mapped.
void start(uint32_t * p_bootargs, struct DTHeader * dtb) {
    if (dtb == NULL) {
        dtb = (struct DTHeader *)
            __DTB_START;  // DTB not passed by bootloader, we are in QEMU. Use Embedded DTB.
    }

    // Before this point, all code has to be hardware independent.
    // After this point, code can request the hardware info struct to find out what
    // Code should be ran.
    init_hardwareinfo(dtb);

    // Initialize the chipset and enable uart
    init_chipset();

    struct DTProp * mem_prop = dtb_get_property(dtb, "/memory", "reg");
    assert(mem_prop != NULL);  // Failed to find memory node in the DTB

    struct DTProp2UInt memory_prop = dtb_wrap_2uint_prop(
        dtb, mem_prop);  // First element is address and second element is size of available memory
    assert(memory_prop.first == 0);  // There can be multiple memory declarations in the DTB, but
                                     // we're assuming there's only 1, and it starts at address 0

    if (memory_prop.second == 0) {
        memory_prop.second = Gibibyte;
        INFO("DTB reports 0 RAM, setting size to 1 GB");
    }


    INFO("Detected memory size: 0x%x Bytes", memory_prop.second);
    INFO("Started chipset specific handlers");

    // just cosmetic (and for debugging)
    print_hardwareinfo();

    // start proper virtual and physical memory management.
    // Even though we already enabled the mmu in startup.s to
    // create a higher half kernel. The pagetable created there
    // was temporary and has to be replaced here.
    // This will actually map the whole kernel in memory and initialize the physicalMemoryManager.
    INFO("Initializing the physical and virtual memory managers.");
    vm2_start(memory_prop.second);

    INFO("Setting up interrupt vector tables");
    // Set up the exception handlers.
    init_vector_table();

    INFO("Setting up heap");
    // After this point kmalloc and kfree can be used for dynamic memory management.
    init_heap();

    // Splash screen
    splash();

    // Turn on interrupts
    enable_interrupt(BOTH);

    // Call the chipset again to do any initialization after enabling interrupts and the heap.
    chipset.late_init();


#ifndef ENABLE_TESTS
//    argparse_process(p_bootargs);
//
// TODO: Start init process
#else
    test_main();
    // If we return, the tests failed.
    SemihostingCall(OSSpecific);
#endif


    // TODO:
    //  * Mount vfs
    //  * Load initramfs into tmpfs
    //  * execute userland init program

    asm volatile("cpsie i");

    INFO("End of boot sequence.\n");
    SLEEP;
}
