/*
 * Host-side stand-in for the PS5 payload SDK's ps5/kernel.h.
 *
 * The real header pulls in a lot of FreeBSD/PS5 specific machinery that
 * cannot be built on a Linux host. The firmware dispatch logic in kdlsym.c
 * only needs three things from it, so the host test provides exactly those
 * and nothing else.
 */
#ifndef HOSTTEST_MOCK_PS5_KERNEL_H
#define HOSTTEST_MOCK_PS5_KERNEL_H

#include <stdint.h>
#include <sys/types.h>

extern const intptr_t KERNEL_ADDRESS_TEXT_BASE;

uint32_t kernel_get_fw_version(void);

#endif /* HOSTTEST_MOCK_PS5_KERNEL_H */
