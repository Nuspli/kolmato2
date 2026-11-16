#ifndef PARAMS_H
#define PARAMS_H

// the tunable parameters (params.def). a normal build: constants (-DNAME=value overrides them). -DTUNE_SEARCH: variables that are UCI options.
#include <stdbool.h>
#include "params_gen.h"

// params_gen.h is made from params.def by tools/gen_params.py; a missing or changed parameter stops the build here
#define X(n, d, lo, hi) _Static_assert(n##_GEN == d, "params_gen.h is stale: run python3 tools/gen_params.py");
#include "params.def"
#undef X

#ifdef TUNE_SEARCH
#define X(n, d, lo, hi) extern int tune_##n;
#include "params.def"
#undef X

struct tune_param {
    const char *name;
    int *value;
    int def, min, max;
};
int tune_count(void);
const struct tune_param *tune_at(int index);
bool tune_set_option(const char *name, int value); // true when name is a tuning parameter. only call it while no search is running
#endif

#endif
