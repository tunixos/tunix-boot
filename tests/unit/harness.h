#ifndef TUNIX_BOOT_TEST_HARNESS_H
#define TUNIX_BOOT_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>

static int test_failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            test_failures++;                                                  \
        }                                                                     \
    } while (0)

#define TEST_MAIN(body)                                                       \
    int main(void) {                                                          \
        body                                                                  \
        return test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;              \
    }

#endif
