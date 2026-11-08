/* Minimal Cortex-M startup for the QEMU mps2-an505 harness: vector table, .data/.bss init,
 * semihosting (rdimon) init, C++ init array, main, exit. */
#include <stdint.h>
#include <stdlib.h>
extern uint32_t __etext, __data_start__, __data_end__, __bss_start__, __bss_end__, __StackTop, __StackLimit;
extern void initialise_monitor_handles(void);
extern void __libc_init_array(void);
extern int main(void);

void Reset_Handler(void)
{
    uint32_t *s = &__etext, *d = &__data_start__;
    *(volatile uint32_t *)0xE000ED88u |= 0xFu << 20;      /* CPACR: CP10/CP11 full access (FPU), as the Pico SDK runtime does */
    __asm volatile ("dsb\n isb");
    while (d < &__data_end__) *d++ = *s++;
    for (d = &__bss_start__; d < &__bss_end__; ) *d++ = 0;
    /* paint the stack so main can report the high-water mark */
    for (d = &__StackLimit; d < (uint32_t *)((uintptr_t)__builtin_frame_address(0) - 256); ) *d++ = 0xA5A5A5A5u;
    initialise_monitor_handles();
    __libc_init_array();
    exit(main());
}
static void Default_Handler(void) { for (;;) __asm volatile ("bkpt #0"); }
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
__attribute__((section(".isr_vector"), used))
const void *const g_vectors[16] = {
    &__StackTop, Reset_Handler, Default_Handler, HardFault_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0, Default_Handler, SysTick_Handler
};
/* -nostartfiles: no crti/crtn, so newlib's init/fini array walkers need these */
void _init(void) {}
void _fini(void) {}
