#include "dblyat/pe_structs.h"

#include <stddef.h>
#include <unistd.h>

_Static_assert(sizeof(image_dos_header) == 0x40u,
               "DOS header must be exactly 64 bytes");
_Static_assert(offsetof(image_dos_header, e_magic) == 0x00u,
               "e_magic offset mismatch");
_Static_assert(offsetof(image_dos_header, e_lfarlc) == 0x18u,
               "e_lfarlc offset mismatch");
_Static_assert(offsetof(image_dos_header, e_res) == 0x1cu,
               "e_res offset mismatch");
_Static_assert(offsetof(image_dos_header, e_oemid) == 0x24u,
               "e_oemid offset mismatch");
_Static_assert(offsetof(image_dos_header, e_res2) == 0x28u,
               "e_res2 offset mismatch");
_Static_assert(offsetof(image_dos_header, e_lfanew) == 0x3cu,
               "e_lfanew must be at offset 0x3c");

int main(void)
{
    (void)write(STDOUT_FILENO, "PE STRUCT TEST: PASS\n", 21u);
    return 0;
}
