/*
 * Host-side test for the firmware dispatch logic in kdlsym.c.
 *
 * The payload cannot be executed on this machine: it is a FreeBSD/x86-64 PIE
 * that links against PS5 SPRX libraries and talks to the console's kernel.
 * What *can* be tested offline is the part this project is responsible for,
 * namely the mapping from a detected firmware version to the correct offset
 * table. Getting that wrong produces a payload that builds cleanly, deploys
 * cleanly, and then patches the kernel at completely wrong addresses.
 *
 * This test compiles the real kdlsym.c against a mock of the three SDK
 * symbols it needs, then drives kernel_get_fw_version() to every supported
 * version and checks that the lookup lands on the right table.
 *
 * Checks performed:
 *   1. every supported version routes to its own table, for all 14 kernel
 *      symbols and all 21 gadgets
 *   2. pinned golden offsets, so a table that is edited or swapped is caught
 *      even though the routing itself is unchanged
 *   3. versions the payload does not support resolve to 0, which is what
 *      main.c tests to decide whether to bail out
 *   4. out of range indices resolve to 0
 *   5. which versions are indistinguishable from each other
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kdlsym.h"

/* ---- mock of the PS5 payload SDK ---- */

static uint32_t g_mock_fw_version = 0;

const intptr_t KERNEL_ADDRESS_TEXT_BASE = 0xFFFF800000000000LL;

uint32_t kernel_get_fw_version(void)
{
  return g_mock_fw_version;
}

/* kdlsym.c uses kread32() from kdldmap(); nothing else in kdlsym.c touches
 * the kernel, so a stub is enough here. */
uint32_t kread32(uint64_t kaddr)
{
  (void)kaddr;
  return 0;
}

/* ---- the tables under test ---- */

#include "../include/offsets/12_00.h"
#include "../include/offsets/12_02.h"
#include "../include/offsets/12_20.h"
#include "../include/offsets/12_40.h"
#include "../include/offsets/12_60.h"
#include "../include/offsets/12_70.h"
#include "../include/offsets/13_00.h"
#include "../include/offsets/13_20.h"
#include "../include/offsets/13_40.h"
#include "../include/offsets/13_42.h"
#include "../include/offsets/13_60.h"

typedef struct {
  uint32_t          version;
  const char       *name;
  const uint64_t   *syms;
  const uint64_t   *gadgets;
} fw_case_t;

static const fw_case_t g_cases[] = {
  { 0x12000000, "12.00", g_sym_map_1200,    g_gadget_map_1200    },
  { 0x12020000, "12.02", g_sym_map_1202,    g_gadget_map_1202    },
  { 0x12200000, "12.20", g_sym_map_1220,    g_gadget_map_1220    },
  { 0x12400000, "12.40", g_sym_map_1240,    g_gadget_map_1240    },
  { 0x12600000, "12.60", g_sym_map_1260,    g_gadget_map_1260    },
  { 0x12700000, "12.70", g_sym_map_1270,    g_gadget_map_1270    },
  { 0x13000000, "13.00", g_sym_map_1300,    g_gadget_map_1300    },
  { 0x13200000, "13.20", g_sym_map_1320,    g_gadget_map_1320    },
  { 0x13400000, "13.40", g_sym_map_1340,    g_gadget_map_1340    },
  { 0x13420000, "13.42", g_sym_map_1342,    g_gadget_map_1342    },
  { 0x13600000, "13.60", g_sym_map_1360,    g_gadget_map_1360    },
};

#define NUM_CASES ((int)(sizeof(g_cases) / sizeof(g_cases[0])))

/*
 * Pinned offsets, independent of the header files. If a table is edited or
 * replaced with another version's data these values will not match, which is
 * exactly the situation that has to be caught.
 */
typedef struct {
  const char *name;
  uint64_t    dmpml4i;
  uint64_t    sys_getuid;
  uint64_t    hv_mp_startup_call;
  uint64_t    mp_startup_init_vector;
  int         hw_verified;
} golden_t;

/* hw_verified: the offsets for this version have been confirmed by running
 * the payload on a console running it. The others are pinned so that an
 * edited or swapped table is still caught, but they have not been run. */
static const golden_t g_golden[] = {
  { "12.00", 0x3B6D254, 0x357530, 0xC9E0D0, 0x3187F8, 0 },
  { "12.02", 0x3B6D254, 0x357530, 0xC9E0D0, 0x3187F8, 0 },
  { "12.20", 0x3B6D254, 0x3573B0, 0xC9DF50, 0x318678, 1 },
  { "12.40", 0x3B6D254, 0x3573B0, 0xC9DF50, 0x318678, 0 },
  { "12.60", 0x3B6D254, 0x3573B0, 0xC9DFB0, 0x318678, 0 },
  { "12.70", 0x3B6D254, 0x3573B0, 0xC9DFB0, 0x318678, 0 },
  { "13.00", 0x3B25294, 0x2A8B20, 0xC0A110, 0x269238, 0 },
  { "13.20", 0x3B25294, 0x2A8B20, 0xC0A310, 0x26A798, 1 },
  { "13.40", 0x3B29294, 0x2A8B80, 0xC0A620, 0x268D08, 0 },
  { "13.42", 0x3B29294, 0x2A8B80, 0xC0A620, 0x2687D8, 0 },
  { "13.60", 0x3B49294, 0x2A8D60, 0xC16EE0, 0x269FB8, 1 },
};

#define NUM_GOLDEN ((int)(sizeof(g_golden) / sizeof(g_golden[0])))

static int g_failures = 0;

/* ---- tests ---- */

static void test_routing(void)
{
  printf("1. version -> table routing\n");

  for (int i = 0; i < NUM_CASES; i++) {
    const fw_case_t *c = &g_cases[i];
    int bad_sym = 0, bad_gadget = 0;

    g_mock_fw_version = c->version;

    for (int s = 0; s < KERNEL_SYM_MAX; s++) {
      if (rawoff((ksym_t)s) != c->syms[s]) {
        if (!bad_sym)
          printf("     %s: first mismatching symbol %d: got 0x%llX want 0x%llX\n",
                 c->name, s, (unsigned long long)rawoff((ksym_t)s),
                 (unsigned long long)c->syms[s]);
        bad_sym++;
      }
      /* kdlsym() is the raw offset plus the kernel text base */
      if (kdlsym((ksym_t)s) != (uint64_t)KERNEL_ADDRESS_TEXT_BASE + c->syms[s]) {
        bad_sym++;
      }
    }

    for (int g = 0; g < KERNEL_GADGET_MAX; g++) {
      if (kgadget((kgadget_t)g) !=
          (uint64_t)KERNEL_ADDRESS_TEXT_BASE + c->gadgets[g]) {
        if (!bad_gadget)
          printf("     %s: first mismatching gadget %d\n", c->name, g);
        bad_gadget++;
      }
    }

    if (bad_sym || bad_gadget) {
      printf("   FAIL %s: %d symbol and %d gadget mismatches\n",
             c->name, bad_sym, bad_gadget);
      g_failures++;
    } else {
      printf("   ok   %s: all %d symbols and %d gadgets route correctly\n",
             c->name, (int)KERNEL_SYM_MAX, (int)KERNEL_GADGET_MAX);
    }
  }
  printf("\n");
}

static void test_golden(void)
{
  printf("2. pinned offsets (ground truth)\n");

  for (int i = 0; i < NUM_GOLDEN; i++) {
    const golden_t *g = &g_golden[i];
    int bad = 0;

    for (int j = 0; j < NUM_CASES; j++) {
      if (strcmp(g->name, g_cases[j].name) != 0)
        continue;

      g_mock_fw_version = g_cases[j].version;

      if (rawoff(KERNEL_SYM_DMPML4I) != g->dmpml4i) {
        printf("   FAIL %s DMPML4I: got 0x%llX want 0x%llX\n", g->name,
               (unsigned long long)rawoff(KERNEL_SYM_DMPML4I),
               (unsigned long long)g->dmpml4i);
        bad++;
      }
      if (rawoff(KERNEL_SYM_SYS_GETUID) != g->sys_getuid) {
        printf("   FAIL %s SYS_GETUID: got 0x%llX want 0x%llX\n", g->name,
               (unsigned long long)rawoff(KERNEL_SYM_SYS_GETUID),
               (unsigned long long)g->sys_getuid);
        bad++;
      }
      if (rawoff(KERNEL_SYM_HV_MP_STARTUP_CALL) != g->hv_mp_startup_call) {
        printf("   FAIL %s HV_MP_STARTUP_CALL: got 0x%llX want 0x%llX\n",
               g->name,
               (unsigned long long)rawoff(KERNEL_SYM_HV_MP_STARTUP_CALL),
               (unsigned long long)g->hv_mp_startup_call);
        bad++;
      }
      if (rawoff(KERNEL_SYM_MP_STARTUP_INIT_VECTOR) !=
          g->mp_startup_init_vector) {
        printf("   FAIL %s MP_STARTUP_INIT_VECTOR: got 0x%llX want 0x%llX\n",
               g->name,
               (unsigned long long)rawoff(KERNEL_SYM_MP_STARTUP_INIT_VECTOR),
               (unsigned long long)g->mp_startup_init_vector);
        bad++;
      }

      /* main.c bails out when this resolves to 0, so it must not */
      if (kdlsym(KERNEL_SYM_HV_MP_STARTUP_CALL) == 0) {
        printf("   FAIL %s: main.c would treat this firmware as unsupported\n",
               g->name);
        bad++;
      }
      break;
    }

    if (!bad)
      printf("   ok   %s: all 4 pinned offsets match, firmware is supported%s\n",
             g->name, g->hw_verified ? " [hardware verified]" : "");
    else
      g_failures++;
  }
  printf("\n");
}

static void test_unsupported(void)
{
  static const uint32_t unsupported[] = {
    0x10000000, /* 10.00 */
    0x11000000, /* 11.00 */
    0x12010000, /* 12.01, no table */
    0x13010000, /* 13.01, no table */
    0x14000000, /* 14.00, no table */
    0x99990000, /* out of range */
  };

  printf("3. unsupported firmware resolves to 0\n");

  for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
    g_mock_fw_version = unsupported[i];

    if (kdlsym(KERNEL_SYM_HV_MP_STARTUP_CALL) == 0 &&
        rawoff(KERNEL_SYM_HV_MP_STARTUP_CALL) == 0 &&
        kgadget(KERNEL_GADGET_IRETQ) == 0) {
      printf("   ok   0x%08X: resolves to 0, main.c would bail out\n",
             unsupported[i]);
    } else {
      printf("   FAIL 0x%08X: did not resolve to 0\n", unsupported[i]);
      g_failures++;
    }
  }
  printf("\n");
}

static void test_bounds(void)
{
  printf("4. out of range indices resolve to 0\n");

  g_mock_fw_version = 0x13600000;

  if (kdlsym(KERNEL_SYM_MAX) == 0 && rawoff(KERNEL_SYM_MAX) == 0) {
    printf("   ok   symbol index KERNEL_SYM_MAX resolves to 0\n");
  } else {
    printf("   FAIL symbol index KERNEL_SYM_MAX did not resolve to 0\n");
    g_failures++;
  }

  if (kgadget(KERNEL_GADGET_MAX) == 0) {
    printf("   ok   gadget index KERNEL_GADGET_MAX resolves to 0\n");
  } else {
    printf("   FAIL gadget index KERNEL_GADGET_MAX did not resolve to 0\n");
    g_failures++;
  }
  printf("\n");
}

static void test_version_masking(void)
{
  printf("5. version masking (low bits must be ignored)\n");

  /* the switch runs on fw & 0xffff0000, so a version with extra low bits
   * set must still route to the same table */
  g_mock_fw_version = 0x13600000 | 0x1234;
  uint64_t masked = rawoff(KERNEL_SYM_HV_MP_STARTUP_CALL);

  g_mock_fw_version = 0x13600000;
  uint64_t plain = rawoff(KERNEL_SYM_HV_MP_STARTUP_CALL);

  if (masked == plain && masked != 0) {
    printf("   ok   0x13601234 routes the same as 0x13600000\n");
  } else {
    printf("   FAIL masking: 0x%llX vs 0x%llX\n",
           (unsigned long long)masked, (unsigned long long)plain);
    g_failures++;
  }
  printf("\n");
}

static void test_distinguishable(void)
{
  printf("6. which versions are indistinguishable from each other\n");

  for (int i = 0; i < NUM_CASES; i++) {
    for (int j = i + 1; j < NUM_CASES; j++) {
      if (memcmp(g_cases[i].syms, g_cases[j].syms,
                 KERNEL_SYM_MAX * sizeof(uint64_t)) == 0 &&
          memcmp(g_cases[i].gadgets, g_cases[j].gadgets,
                 KERNEL_GADGET_MAX * sizeof(uint64_t)) == 0) {
        /* note which of the pair has been confirmed on hardware */
        const char *verified = "neither";
        for (int k = 0; k < NUM_GOLDEN; k++) {
          if (strcmp(g_golden[k].name, g_cases[i].name) == 0 && g_golden[k].hw_verified)
            verified = g_cases[i].name;
          if (strcmp(g_golden[k].name, g_cases[j].name) == 0 && g_golden[k].hw_verified)
            verified = g_cases[j].name;
        }
        printf("   note %s and %s have identical offsets"
               " (a misroute between them is undetectable; %s is hardware"
               " verified)\n",
               g_cases[i].name, g_cases[j].name, verified);
      }
    }
  }
  printf("\n");
}

int main(void)
{
  printf("Byepervisor Reborn - firmware dispatch test\n");
  printf("=============================================\n\n");

  test_routing();
  test_golden();
  test_unsupported();
  test_bounds();
  test_version_masking();
  test_distinguishable();

  if (g_failures) {
    printf("RESULT: %d check(s) FAILED\n", g_failures);
    return 1;
  }

  printf("RESULT: all checks passed\n");
  return 0;
}
