# mov r32, imm32

x86-64 `b8+r imm32` writes the low 32-bit destination and clears
the upper half of the 64-bit parent register. the immediate is unsigned.

this implementation decodes the 5-byte instruction, or the 6-byte
form with a REX prefix. REX.B selects r8d through r15d. REX.W
continues to select the existing 10-byte mov r64, imm64 form.

the IR lowers the zero-extended immediate as a 64-bit move. the
existing ARM64 materializer writes the whole guest register, so a
second 32-bit store path is unnecessary.

the scalar interpreter uses the same zero-extended immediate.
invalid or truncated encodings do not modify decode output.
the IR validator rejects an immediate outside the 32-bit range.

issue: https://github.com/Deadbytes101/DBLYATEM/issues/155
QEMU is not physical ARM64 hardware and does not prove Winlator parity.
