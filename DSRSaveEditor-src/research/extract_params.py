#!/usr/bin/env python3
"""Reads the decoded DSR parameter tables (GameParam.parambnd.dcx -> BND3 -> *.param) and writes
research/param_items.csv.  Usage: python3 -I research/extract_params.py <dir with the *.param files> <Paramdex DS1R dir>
Item names come from Paramdex's DS1R/Names, field layouts from DS1R/Defs (both soulsmods/Paramdex)."""
import csv, os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
params, pdex = sys.argv[1].rstrip("/") + "/", sys.argv[2].rstrip("/") + "/"
os.environ["PARAMDEX_DS1R_DEFS"] = pdex + "Defs/"
sys.path.insert(0, here)
import paramlib as P

def names(f):
    n = {}
    for l in open(pdex + "Names/%s.txt" % f, encoding="utf-8", errors="ignore"):
        m = re.match(r"\s*(\d+)\s+(.*)", l)
        if m: n[int(m[1])] = m[2].strip()
    return n

out = []
for cat, f in (("goods", "EquipParamGoods"), ("weapon", "EquipParamWeapon"), ("armor", "EquipParamProtector"), ("ring", "EquipParamAccessory")):
    rows, _, _, _ = P.rows(params + f + ".param", f)
    nm = names(f)
    for i, v in sorted(rows.items()):
        out.append(dict(category=cat, id=i, name=nm.get(i, ""), sortId=v["sortId"],
                        maxNum=v.get("maxNum", ""), durabilityMax=v.get("durabilityMax", ""),
                        goodsType=v.get("goodsType", ""), weaponCategory=v.get("weaponCategory", "")))
with open(os.path.join(here, "param_items.csv"), "w", newline="") as fh:
    w = csv.DictWriter(fh, list(out[0].keys())); w.writeheader(); w.writerows(out)
print(len(out), "rows")
