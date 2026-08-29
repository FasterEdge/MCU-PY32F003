// data_base.c — BaseData 实现（PY32F003 (Cortex-M0+) 版）
// logo / info
#include "fe_data.h"
#include <string.h>

static const char *LOGO =
    "  __ _        _\n"
    " / _| |_ _  _(_)__ _\n"
    "|  _|  _| || | / _` |\n"
    "|_|  \\__|\\_, |_\\__,_|\n"
    "        |__/\n";

fe_output_t data_base_dispatch(void *inst, const char *act, const char *args) {
    (void)inst; (void)args;
    if (strcmp(act, "logo") == 0) {
        return fe_ok(act, LOGO);
    }
    if (strcmp(act, "info") == 0) {
        return fe_ok(act,
            "{\"name\":\"BaseData\",\"firmware\":\"FasterEdge-MCU 1.0.20260829\","
            "\"chip\":\"PY32F003\",\"sdk\":\"Keil MDK\"}");
    }
    return fe_err(act, "unsupported command");
}