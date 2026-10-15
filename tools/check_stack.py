#!/usr/bin/env python3
"""Fail the build when the stack budget no longer fits under the globals.

The stack starts at .STACK and grows down toward the end of the
globals (s__HEAP; there is no heap). If the call chain goes deeper than
that gap it writes over globals, which is what broke address generation
in v1.1.0-beta. STACK_WORST is the deepest stack use measured in the
emulator with RAM pre-filled with a canary pattern, across address
generation, signing, PIN and the QR screens. Re-measure it when the
crypto or signing call chains change.

Usage: check_stack.py build/DogeGB.map [worst_bytes] [margin_bytes]
"""
import re
import sys

STACK_WORST = 976   # bytes, measured: fresh-cart address generation
STACK_MARGIN = 128  # bytes kept free on top of the measured depth


def main():
    path = sys.argv[1]
    worst = int(sys.argv[2]) if len(sys.argv) > 2 else STACK_WORST
    margin = int(sys.argv[3]) if len(sys.argv) > 3 else STACK_MARGIN
    text = open(path).read()
    heap = re.search(r"\b0000([0-9A-F]{4})\s+s__HEAP\b", text)
    stack = re.search(r"\b0000([0-9A-F]{4})\s+\.STACK\b", text)
    if not heap or not stack:
        print(f"check_stack: cannot find s__HEAP/.STACK in {path}")
        return 1
    end = int(heap.group(1), 16)
    top = int(stack.group(1), 16)
    room = top - end
    need = worst + margin
    print(f"check_stack: {path}: globals end 0x{end:04X}, stack top 0x{top:04X}, "
          f"room {room} B, worst {worst} B + margin {margin} B = {need} B, "
          f"spare {room - need} B")
    if room < need:
        print("check_stack: FAIL: the stack would run into the globals. "
              "Shrink globals or stack frames, or re-measure the stack depth.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
