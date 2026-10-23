#include "dblyat/x64_decode.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t sub_rsp_40[] = {0x48u, 0x83u, 0xecu, 0x28u};
    const uint8_t short_input[] = {0x48u, 0x83u, 0xecu};
    const uint8_t memory_form[] = {0x48u, 0x83u, 0x2cu, 0x24u, 0x28u};
    db_x64_instruction out;
    db_x64_instruction before;
    db_x64_decode_status status;
    (void)memset(&out, 0xa5, sizeof(out));
    before = out;
    status = db_x64_decode_one(sub_rsp_40, sizeof(sub_rsp_40), &out);
    if (status != DB_X64_DECODE_OK) {
        (void)printf("SUB RSP IMM8: RED status=%s\n", db_x64_decode_status_name(status));
        return 1;
    }
    if ((unsigned)out.kind != 15u || out.length != 4u ||
        out.destination_register != 4u || out.immediate != 40u) {
        (void)puts("SUB RSP IMM8: bad decoded contract");
        return 2;
    }
    out = before;
    if (db_x64_decode_one(short_input, sizeof(short_input), &out) != DB_X64_DECODE_TRUNCATED ||
        memcmp(&out, &before, sizeof(out)) != 0) {
        return 3;
    }
    out = before;
    if (db_x64_decode_one(memory_form, sizeof(memory_form), &out) != DB_X64_DECODE_UNSUPPORTED_ADDRESSING ||
        memcmp(&out, &before, sizeof(out)) != 0) {
        return 4;
    }
    (void)puts("SUB RSP IMM8 DECODE: PASS");
    return 0;
}
