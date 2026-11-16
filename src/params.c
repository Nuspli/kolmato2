#include "params.h"
#include "search.h"

#ifdef TUNE_SEARCH
#include <strings.h>

// the variables behind the parameter names (params_gen.h turns NAME into tune_NAME), one per line of params.def
#define X(n, d, lo, hi) int tune_##n = tune_init_##n;
#include "params.def"
#undef X

static struct tune_param tune_table[] = {
#define X(n, d, lo, hi) {#n, &tune_##n, tune_init_##n, lo, hi},
#include "params.def"
#undef X
};

int tune_count(void) {
    return (int)(sizeof(tune_table) / sizeof(tune_table[0]));
}

const struct tune_param *tune_at(int index) {
    return &tune_table[index];
}

bool tune_set_option(const char *name, int value) {
    for (int i = 0; i < tune_count(); i++) {
        if (!strcasecmp(name, tune_table[i].name)) {
            if (value < tune_table[i].min) value = tune_table[i].min;
            if (value > tune_table[i].max) value = tune_table[i].max;
            *tune_table[i].value = value;
            init_lmr_table(); // the late move reduction table depends on two of the parameters
            return true;
        }
    }
    return false;
}
#endif
