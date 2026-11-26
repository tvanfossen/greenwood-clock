## @file gen_tz_table.py
## @brief Generate the clock's IANA timezone table (components/location/src/tz_table.c) from
## the host's tzdata: every canonical zone in zone1970.tab, with the POSIX TZ rule that ends
## its TZif file (what the clock's C library runs on), sorted by name, plus UTC.
## @version 1
"""Usage: gen_tz_table.py <zoneinfo dir> <out .c>"""

import sys
from pathlib import Path


## @brief The POSIX rule at the end of TZif file @p path (between its last two newlines).
## @utility
## @version 1
## @return The rule.
def posix_rule(path):
    data = path.read_bytes().rstrip(b"\n")
    return data[data.rindex(b"\n") + 1 :].decode()


## @brief The canonical zone names listed in @p tab (zone1970.tab), plus UTC.
## @utility
## @version 1
## @return Sorted names.
def zone_names(tab):
    names = {"UTC"}
    for line in tab.read_text().splitlines():
        if line and not line.startswith("#"):
            names.add(line.split("\t")[2])
    return sorted(names)


## @brief Write the table.
## @utility
## @version 1
def main():
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    rows = [(n, posix_rule(root / n)) for n in zone_names(root / "zone1970.tab")]
    lines = [
        "/**",
        " * @file tz_table.c",
        " * @version 1",
        " * @brief The IANA timezones and their POSIX rules (see tz.h), generated from tzdata by",
        " *        tools/tz/gen_tz_table.py (`inv tz-table`): do not edit.",
        " */",
        '#include "tz.h"',
        "",
        "const tz_zone_t TZ_ZONES[] = {",
    ]
    lines += [f'    {{ "{n}", "{r}" }},' for n, r in rows]
    lines += ["};", "", f"const int TZ_ZONE_COUNT = {len(rows)};", ""]
    out.write_text("\n".join(lines))
    print(f"{out}: {len(rows)} zones")


if __name__ == "__main__":
    main()
