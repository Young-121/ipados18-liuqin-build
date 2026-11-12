#!/usr/bin/env python3
"""Write configs/liuqin-memory-sm8475.json: liuqin DRAM, reserved ranges and allocatable RAM.

DRAM is the /memory of the Xiaomi Pad 6 Pro 8 GB unit as the stock ABL handed it to a
development loader (iPadOS-liuqin NOTES.md 6.2, five banks with 2/2 cells). The 12 GB
variant adds a bank; pass --dram to describe another unit.

Reserved is the union of
  * the reserved-memory of the pinned mainline liuqin tree (the SM8450 base, the SM8475
    deletions and the liuqin overrides, yzddmr6/linux-sm8450-liuqin
    fe81794b5e1b8a3f11a165f3acbfdb8fa671dbb3; --fetch caches it in reference/mainline),
  * the stock UEFI reservations of the tablet's own uefiplat.cfg (V14.0.9.0.TMYCNXM), and
  * the vendor runtime reservations of the same SM8475 BSP captured on pong
    (configs/pong-memory-sm8475.json).
A union only over-reserves. The kernel side intersects every allocatable range with the
UEFI EfiConventionalMemory of the boot, so a range that is reserved at runtime but absent
here is still excluded; a range listed here and free at runtime is merely not used.
Replace the mainline input with this tablet's own /sys/firmware/fdt when one is captured.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# A local cache (reference/ is ignored): sm8475-xiaomi-liuqin.dts is GPL-2.0-only and is
# not committed here; --fetch downloads the pinned files.
MAINLINE = ROOT / "reference" / "mainline"
MAINLINE_COMMIT = "fe81794b5e1b8a3f11a165f3acbfdb8fa671dbb3"
MAINLINE_URL = ("https://raw.githubusercontent.com/yzddmr6/linux-sm8450-liuqin/"
                f"{MAINLINE_COMMIT}/arch/arm64/boot/dts/qcom/")
MAINLINE_FILES = ("sm8450.dtsi", "sm8475.dtsi", "sm8475-xiaomi-liuqin.dts")


def fetch_mainline() -> None:
    import urllib.request
    MAINLINE.mkdir(parents=True, exist_ok=True)
    for name in MAINLINE_FILES:
        with urllib.request.urlopen(MAINLINE_URL + name, timeout=60) as r:
            (MAINLINE / name).write_bytes(r.read())
        print(f"fetched {name} at {MAINLINE_COMMIT[:8]}")

DRAM_8GB = [
    (0x80000000, 0x6A000000),
    (0xF1400000, 0x0EC00000),
    (0x800000000, 0x39700000),
    (0x839D00000, 0x01E00000),
    (0x840000000, 0x140000000),
]

# [MemoryMap] of the stock uefiplat.cfg (uefi_a, FFS DDE58710-41CD-4306-DBFB-3FA90BB1D2DD)
# of V14.0.9.0.TMYCNXM, the build on the tablet being ported: every Reserv and RtData
# region, the stock FD window (liuqinPkg's FD sits inside it) and the ABL splash. Left out:
# the Conv regions (XBL RAMDUMP, DBI Dump, DXE Heap), the BsData working memory of the
# stock UEFI (CPU Vectors, MMU PageTables, UEFI Stack, SEC/Sched Heap, FV Region, UEFI RESV),
# which liuqinPkg replaces with its own, and "Kernel" 0xA8000000 +0x10000000, the load
# window of the stock LinuxLoader. HYP, PIL, Demura, UEFI FD and Display Reserved are the
# same in OS3.0.7.0.VMYCNXM (UniXNU-liuqin docs/vendor-inputs.md).
UEFIPLAT = [
    ("uefiplat:HYP", 0x80000000, 0x00600000),
    ("uefiplat:DT BLOB", 0x80600000, 0x00040000),
    ("uefiplat:AOP", 0x80800000, 0x000A0000),
    ("uefiplat:SMEM", 0x80900000, 0x00200000),
    ("uefiplat:CUCP", 0x80B00000, 0x00100000),
    ("uefiplat:PIL Reserved", 0x85200000, 0x1DC00000),
    ("uefiplat:Display Demura", 0xA1400000, 0x02B00000),
    ("uefiplat:UEFI FD", 0xA7000000, 0x00400000),
    ("uefiplat:UEFI FD Reserved", 0xA7400000, 0x00200000),
    ("uefiplat:Info Blk", 0xA7601000, 0x00001000),
    ("uefiplat:Log Buffer", 0xA7605000, 0x00008000),
    ("uefiplat:Display Reserved", 0xB8000000, 0x02B00000),
    ("uefiplat:OEM VM", 0xBB000000, 0x05000000),
    ("uefiplat:MTE Reserved", 0xC0000000, 0x20000000),
    ("uefiplat:VM PIL", 0xE0000000, 0x05700000),
    ("uefiplat:TZ STATS", 0xE8800000, 0x01000000),
    ("uefiplat:TZApps Reserved", 0xEA000000, 0x07400000),
    ("uefiplat:HYP Reserved", 0x830000000, 0x10000000),
]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def reserved_nodes(text: str) -> dict[str, tuple[str, int, int]]:
    """label -> (name, base, size) for every reg in a reserved-memory block."""
    out: dict[str, tuple[str, int, int]] = {}
    for block in re.finditer(r"reserved-memory\s*\{", text):
        depth, i = 1, block.end()
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        body = text[block.end():i - 1]
        for node in re.finditer(r"(?:(\w+)\s*:\s*)?([\w,.-]+@[0-9a-fA-F]+)\s*\{(.*?)\};", body, re.S):
            label, name, props = node.groups()
            reg = re.search(r"\breg\s*=\s*<([^>]*)>", props)
            if not reg:
                continue
            cells = [int(c, 0) for c in reg.group(1).split()]
            for j in range(0, len(cells) - 3, 4):
                base = (cells[j] << 32) | cells[j + 1]
                size = (cells[j + 2] << 32) | cells[j + 3]
                out[label or f"{name}#{j}"] = (name, base, size)
    return out


def mainline_reserved() -> list[tuple[str, int, int]]:
    nodes: dict[str, tuple[str, int, int]] = {}
    for name in ("sm8450.dtsi", "sm8475.dtsi", "sm8475-xiaomi-liuqin.dts"):
        text = strip_comments((MAINLINE / name).read_text(encoding="utf-8"))
        for label in re.findall(r"/delete-node/\s*&(\w+)\s*;", text):
            nodes.pop(label, None)
        nodes.update(reserved_nodes(text))
    return [(f"mainline:{n}", b, s) for n, b, s in nodes.values()]


def pong_reserved() -> list[tuple[str, int, int]]:
    data = json.loads((ROOT / "configs" / "pong-memory-sm8475.json").read_text(encoding="utf-8"))
    return [(f"pong:{r['name']}", int(r["base"], 0), int(r["size"], 0)) for r in data["reserved"]]


def subtract(banks, holes):
    free = list(banks)
    for _, hb, hs in holes:
        he = hb + hs
        nxt = []
        for fb, fs in free:
            fe = fb + fs
            if he <= fb or hb >= fe:
                nxt.append((fb, fs))
                continue
            if hb > fb:
                nxt.append((fb, hb - fb))
            if he < fe:
                nxt.append((he, fe - he))
        free = nxt
    return sorted((b, s) for b, s in free if s >= 0x1000)


def dts_block(dram, reserved) -> str:
    """The memory node and reserved-memory of dts/liuqin.dts: every distinct reserved range in
    (base, size) order, no-map; an exact duplicate keeps the first source's name."""
    def cells(v):
        return f"{v >> 32:#x} {v & 0xFFFFFFFF:#x}"
    # devicetree.generate() requires exactly one contiguous DRAM range: keep the
    # largest bank as /memory and reserve the rest so XNU never allocates from them.
    dram = sorted(dram, key=lambda bs: bs[1])
    single = [dram[-1]]
    rest = dram[:-1]
    lines = ["    /* 8 GB unit, stock ABL /memory. Single contiguous DRAM range for the XNU ADT. */",
             '    memory@840000000 { device_type = "memory"; reg = <'
             + " ".join(f"{cells(b)} {cells(s)}" for b, s in single) + ">; };",
             "    reserved-memory {",
             "        #address-cells = <2>;",
             "        #size-cells = <2>;",
             "        ranges;"]
    for i, (b, s) in enumerate(rest):
        lines.append(f"        dram-bank-{i}@{b:x} {{ reg = <{cells(b)} {cells(s)}>; no-map; }}; /* dram bank {i} */")
    kept: dict[tuple[int, int], str] = {}
    for name, base, size in sorted(reserved, key=lambda r: (r[1], r[2])):
        kept.setdefault((base, size), name)
    kept = [(n, b, s) for (b, s), n in kept.items()]
    base = len(rest)
    for i, (name, base_addr, size) in enumerate(kept):
        lines.append(f"        region{base + i}@{base_addr:x} {{ reg = <{cells(base_addr)} {cells(size)}>; no-map; }}; /* {name} */")
    lines.append("    };")
    return "\n".join(lines) + "\n"


def write_dts(path: Path, block: str) -> None:
    text = path.read_text(encoding="utf-8")
    start = text.index("    /* 8 GB unit, stock ABL /memory. */")
    end = text.index("\n    };\n", text.index("    reserved-memory {", start)) + len("\n    };\n")
    path.write_text(text[:start] + block + text[end:], encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dram", help="comma separated base:size pairs, default the measured 8 GB unit")
    ap.add_argument("--out", type=Path, default=ROOT / "configs" / "liuqin-memory-sm8475.json")
    ap.add_argument("--dts", type=Path, default=ROOT / "dts" / "liuqin.dts",
                    help="rewrite the memory and reserved-memory of this DTS (empty to skip)")
    ap.add_argument("--fetch", action="store_true", help="download the pinned mainline files into reference/mainline")
    args = ap.parse_args()
    if args.fetch:
        fetch_mainline()
    missing = [n for n in MAINLINE_FILES if not (MAINLINE / n).is_file()]
    if missing:
        raise SystemExit(f"{MAINLINE} lacks {missing}: run with --fetch")
    dram = DRAM_8GB
    if args.dram:
        dram = [tuple(int(v, 0) for v in pair.split(":")) for pair in args.dram.split(",")]
    reserved = sorted(mainline_reserved() + UEFIPLAT + pong_reserved(), key=lambda r: (r[1], r[2]))
    allocatable = subtract(sorted(dram), reserved)
    doc = {
        "format": "pong-sm8475-dram-v1",
        "board": "liuqin",
        "physical_bytes": sum(s for _, s in dram),
        "source": "DRAM: stock ABL /memory of the 8 GB unit (iPadOS-liuqin NOTES 6.2); reserved: union of "
                  "mainline liuqin fe81794b, stock uefiplat.cfg V14.0.9.0.TMYCNXM and the pong SM8475 capture",
        "dram": [{"base": hex(b), "size": hex(s)} for b, s in sorted(dram)],
        "reserved": [{"name": n, "base": hex(b), "size": hex(s)} for n, b, s in reserved],
        "allocatable": [{"base": hex(b), "size": hex(s)} for b, s in allocatable],
        "policy": "Intersect with current UEFI EfiConventionalMemory and claim each range; exclude firmware GOP "
                  "at runtime; never reclaim BootServices/RuntimeServices or reserved descriptors",
    }
    args.out.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")
    if args.dts:
        write_dts(args.dts, dts_block(dram, reserved))
    total = sum(s for _, s in allocatable)
    print(f"{args.out.name}: {len(dram)} banks {doc['physical_bytes'] / 2**30:.2f} GiB, "
          f"{len(reserved)} reserved, {len(allocatable)} allocatable ranges {total / 2**30:.2f} GiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
