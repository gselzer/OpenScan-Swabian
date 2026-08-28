#!/usr/bin/env python3
"""Print a raw Swabian Time Tagger tag dump in a human-readable form.

Reads the file format produced by the vendor SDK's `Dump` measurement
(driver/include/measurements/Dump.h): a sequence of 16-byte (128-bit)
records with no file header, one per event. This is NOT the same format
as `FileWriter`'s .ttbin files, which are compressed and framed
differently.

Record layout (little-endian), from the `Tag` struct in TimeTagger.h:
    offset 0   1 byte   type            (0=TimeTag, 1=Error,
                                          2=OverflowBegin, 3=OverflowEnd,
                                          4=MissedEvents)
    offset 1   1 byte   reserved        (unused)
    offset 2   2 bytes  missed_events   (uint16, meaningful only for
                                          MissedEvents records)
    offset 4   4 bytes  channel         (int32; sign encodes edge --
                                          positive = rising, negative =
                                          falling)
    offset 8   8 bytes  time            (int64, picoseconds)
"""

import argparse
import struct
import sys

RECORD_SIZE = 16
RECORD_FORMAT = "<BBHiq"  # type, reserved, missed_events, channel, time

TAG_TYPE_NAMES = {
    0: "TimeTag",
    1: "Error",
    2: "OverflowBegin",
    3: "OverflowEnd",
    4: "MissedEvents",
}


def format_record(index, tag_type, reserved, missed_events, channel, time_ps):
    type_name = TAG_TYPE_NAMES.get(tag_type, f"Unknown({tag_type})")
    line = f"[{index:8d}] t={time_ps:>18d} ps  {type_name:<13s}"

    if tag_type == 0:  # TimeTag
        line += f"  channel={channel:+d}"
    elif tag_type == 4:  # MissedEvents
        line += f"  channel={channel:+d}  missed={missed_events}"
    elif tag_type in (1, 2, 3):  # Error, OverflowBegin, OverflowEnd
        pass  # channel/missed_events are not meaningful for these
    else:
        line += f"  channel={channel:+d}  missed={missed_events}"

    if reserved != 0:
        line += f"  (reserved={reserved}, expected 0)"

    return line


def dump_tags(path, max_tags, out):
    with open(path, "rb") as f:
        data = f.read()

    extra_bytes = len(data) % RECORD_SIZE
    if extra_bytes:
        print(
            f"warning: file size {len(data)} is not a multiple of "
            f"{RECORD_SIZE} bytes; ignoring trailing {extra_bytes} byte(s) "
            "(truncated or not a raw tag dump?)",
            file=sys.stderr,
        )

    total_records = len(data) // RECORD_SIZE
    printed = 0
    for index in range(total_records):
        if max_tags is not None and printed >= max_tags:
            print(f"... stopped after {max_tags} tag(s); "
                  f"{total_records - index} more in file", file=out)
            break

        offset = index * RECORD_SIZE
        record = data[offset:offset + RECORD_SIZE]
        tag_type, reserved, missed_events, channel, time_ps = \
            struct.unpack(RECORD_FORMAT, record)
        print(format_record(index, tag_type, reserved, missed_events,
                             channel, time_ps), file=out)
        printed += 1

    print(f"\n{printed} tag(s) printed, {total_records} total in file",
          file=out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", help="raw tag dump file (from Dump())")
    parser.add_argument(
        "-n", "--max-tags", type=int, default=None,
        help="stop after printing this many tags (default: print all)",
    )
    args = parser.parse_args()

    dump_tags(args.file, args.max_tags, sys.stdout)


if __name__ == "__main__":
    main()
