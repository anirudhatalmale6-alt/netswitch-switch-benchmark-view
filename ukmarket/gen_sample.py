#!/usr/bin/env python3
"""
gen_sample.py  -  UK data-market sample dataset generator (6GGW report engine feed)

One deterministic source of truth for three tidy tables a report engine can ingest:

    plans.csv     - UK SIM-only data tariffs (pricing, price-per-GB)
    handsets.csv  - handset cost -> ex-factory -> RRP margin waterfall
    market.csv    - market context (ARPU, coverage, share, marketing benchmarks)

It also emits a combined uk_data_market_sample.json (all three tables + meta) and
prints a short console summary.

HONESTY LABELS (every row carries a `basis` field):
    measured  - taken from a public/reported figure (see `source`)
    modelled  - computed here from a stated assumption (NOT a real quoted number)

Modelling assumptions (all explicit, all editable in the constants below):
    FX_USD_GBP     USD -> GBP conversion used for BOM/ex-factory figures
    EXFACTORY_MULT ex-factory price modelled as BOM x this (assembly+test+mfr margin)
    VAT_RATE       UK VAT stripped from RRP to get ex-VAT trade value

Run:
    python3 gen_sample.py            # writes the 3 CSVs + JSON, prints summary
    python3 gen_sample.py --check    # regenerate in memory, verify totals, exit 0/1

No third-party deps. Deterministic: same input -> byte-identical output on any machine.
"""

import csv, json, sys, io, hashlib

# ---- stated modelling constants ------------------------------------------------
FX_USD_GBP      = 0.79     # 1 USD = 0.79 GBP  (GBP/USD ~ 1.27)
EXFACTORY_MULT  = 1.30     # ex-factory ~ BOM x 1.30 (assembly, test, mfr overhead+margin)
VAT_RATE        = 0.20     # UK VAT

# ---- 1) PLANS: UK SIM-only tariffs (measured, public list prices mid-2026) -----
# data_gb: numeric GB, or "unlimited". price is GBP/month. per_gb computed below.
PLANS = [
    # operator,      type,   host,       plan,               data_gb,   price, months, roam_eu
    ("Three",        "MNO",  "Three",    "Unlimited SIM",    "unlimited", 22.00, 24, "yes"),
    ("Three",        "MNO",  "Three",    "10GB SIM",         10,           6.00,  1, "yes"),
    ("EE",           "MNO",  "EE",       "Unlimited SIM",    "unlimited", 40.00, 24, "yes"),
    ("O2",           "MNO",  "O2",       "Entry SIM",         3,           5.80,  1, "yes"),
    ("Vodafone",     "MNO",  "Vodafone", "Unlimited SIM",    "unlimited", 26.00, 24, "yes"),
    ("Smarty",       "MVNO", "Three",    "3GB rolling",       3,           3.90,  1, "yes"),
    ("Smarty",       "MVNO", "Three",    "~30GB rolling",    30,           8.00,  1, "yes"),
    ("Smarty",       "MVNO", "Three",    "Unlimited",        "unlimited", 12.00, 12, "yes"),
    ("iD Mobile",    "MVNO", "Three",    "120GB",           120,          12.00,  1, "yes"),
    ("iD Mobile",    "MVNO", "Three",    "Unlimited",        "unlimited", 16.00,  1, "yes"),
    ("Lebara",       "MVNO", "Vodafone", "5GB",               5,           5.00,  1, "yes"),
    ("Voxi",         "MVNO", "Vodafone", "Unlimited",        "unlimited", 15.00,  1, "yes"),
    ("Tesco Mobile", "MVNO", "O2",       "5GB",               5,           7.50, 12, "yes"),
    ("ASDA Mobile",  "MVNO", "Vodafone", "3GB",               3,           5.00,  1, "yes"),
]
PLANS_SOURCE = "operator price lists / UK comparison sites, mid-2026"

# ---- 2) HANDSETS: BOM -> ex-factory -> RRP margin waterfall ---------------------
# bom_usd measured (teardown estimates). rrp_gbp measured (UK launch RRP, inc VAT).
# ex-factory is MODELLED (bom x EXFACTORY_MULT). Row basis reflects the mix.
HANDSETS = [
    # model,                brand,     year, bom_usd, rrp_gbp_incvat, basis,     source
    ("iPhone 15 Pro Max",   "Apple",   2023,  558.0, 1199.0, "measured", "TechInsights/IHS-style teardown est. 2025"),
    ("Galaxy S23 Ultra",    "Samsung", 2023,  469.0, 1249.0, "measured", "teardown est.; 'sells ~154% over mfg cost'"),
    ("Flagship (generic)",  "-",       2026,  520.0, 1150.0, "modelled", "modelled tier midpoint"),
    ("Mid-range (generic)", "-",       2026,  230.0,  399.0, "modelled", "modelled tier midpoint"),
    ("Budget (generic)",    "-",       2026,  105.0,  159.0, "modelled", "modelled tier midpoint"),
]

# ---- 3) MARKET: context metrics ------------------------------------------------
# marketing rows are MODELLED benchmarks pending the client's own figures.
MARKET = [
    # metric,                        value,  unit,        period, segment, basis,     source
    ("Postpaid ARPU",                15.73, "GBP/month",  "2023", "postpaid", "measured", "Ofcom/Statista"),
    ("Prepaid ARPU",                  5.18, "GBP/month",  "2023", "prepaid",  "measured", "Ofcom/Statista"),
    ("VMO2 total connections",       45.7,  "million",    "2024", "all",      "measured", "Virgin Media O2"),
    ("5G SA population coverage",    83.0,  "%",          "2025", "all",      "measured", "Ofcom Connected Nations 2025"),
    ("Mobile data usage growth",     18.0,  "% YoY",      "2025", "all",      "measured", "Ofcom"),
    ("MVNO consumer share (Sky+Tesco)",15.0,"% (>)",      "2025", "MVNO",     "measured", "market reports"),
    ("Marketing spend of revenue",   12.0,  "% (est)",    "2026", "all",      "modelled", "placeholder - awaiting client figure"),
    ("Cost per acquisition (SIM)",   18.0,  "GBP (est)",  "2026", "all",      "modelled", "placeholder - awaiting client figure"),
]


def plan_rows():
    out = []
    for op, typ, host, plan, gb, price, months, roam in PLANS:
        if isinstance(gb, (int, float)):
            per_gb = round(price / gb, 2)
            gb_out = gb
        else:
            per_gb = ""            # unlimited: per-GB undefined
            gb_out = "unlimited"
        out.append({
            "operator": op, "type": typ, "host_network": host, "plan_name": plan,
            "data_gb": gb_out, "price_gbp_month": round(price, 2),
            "contract_months": months, "price_per_gb_gbp": per_gb,
            "roaming_eu": roam, "basis": "measured", "source": PLANS_SOURCE,
        })
    return out


def handset_rows():
    out = []
    for model, brand, year, bom_usd, rrp_incvat, basis, source in HANDSETS:
        bom_gbp   = round(bom_usd * FX_USD_GBP, 2)
        exf_usd   = round(bom_usd * EXFACTORY_MULT, 2)
        exf_gbp   = round(exf_usd * FX_USD_GBP, 2)
        rrp_exvat = round(rrp_incvat / (1 + VAT_RATE), 2)
        # gross margin of the trade (ex-VAT) value over raw BOM
        gross_margin_pct = round((rrp_exvat - bom_gbp) / rrp_exvat * 100, 1)
        markup_x         = round(rrp_exvat / bom_gbp, 2)          # RRP-exVAT vs BOM
        out.append({
            "model": model, "brand": brand, "launch_year": year,
            "bom_usd": round(bom_usd, 2), "bom_gbp": bom_gbp,
            "est_exfactory_usd": exf_usd, "est_exfactory_gbp": exf_gbp,
            "uk_rrp_gbp_incvat": round(rrp_incvat, 2), "rrp_ex_vat_gbp": rrp_exvat,
            "gross_margin_pct": gross_margin_pct, "markup_x_over_bom": markup_x,
            "basis": basis, "source": source,
        })
    return out


def market_rows():
    return [{
        "metric": m, "value": v, "unit": u, "period": p,
        "segment": seg, "basis": b, "source": s,
    } for (m, v, u, p, seg, b, s) in MARKET]


def write_csv(path, rows):
    if not rows:
        return
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


def build():
    return {
        "meta": {
            "title": "UK Mobile Data Market - sample dataset",
            "generated_by": "gen_sample.py (6GGW report engine feed)",
            "assumptions": {
                "fx_usd_gbp": FX_USD_GBP,
                "exfactory_mult": EXFACTORY_MULT,
                "vat_rate": VAT_RATE,
            },
            "basis_legend": {
                "measured": "public/reported figure (see source)",
                "modelled": "computed from stated assumption - not a quoted number",
            },
        },
        "plans": plan_rows(),
        "handsets": handset_rows(),
        "market": market_rows(),
    }


def json_bytes(data):
    return json.dumps(data, indent=2, sort_keys=False).encode()


def main():
    data = build()
    if "--check" in sys.argv:
        # verify invariants without touching disk
        assert len(data["plans"]) == len(PLANS)
        assert len(data["handsets"]) == len(HANDSETS)
        assert len(data["market"]) == len(MARKET)
        for h in data["handsets"]:
            assert h["markup_x_over_bom"] > 1.0, h["model"]
            assert 0 < h["gross_margin_pct"] < 100, h["model"]
        digest = hashlib.sha256(json_bytes(data)).hexdigest()[:16]
        print("check OK  rows: plans=%d handsets=%d market=%d  json_sha256=%s"
              % (len(data["plans"]), len(data["handsets"]), len(data["market"]), digest))
        return 0

    write_csv("plans.csv", data["plans"])
    write_csv("handsets.csv", data["handsets"])
    write_csv("market.csv", data["market"])
    with open("uk_data_market_sample.json", "wb") as f:
        f.write(json_bytes(data))

    digest = hashlib.sha256(json_bytes(data)).hexdigest()[:16]
    print("wrote plans.csv (%d) handsets.csv (%d) market.csv (%d) + uk_data_market_sample.json"
          % (len(data["plans"]), len(data["handsets"]), len(data["market"])))
    print("json_sha256=%s  (same input -> same checksum on any machine)" % digest)
    print("\nHandset margin waterfall (ex-VAT trade value vs BOM):")
    for h in data["handsets"]:
        print("  %-20s BOM GBP%-7.2f  ex-factory~GBP%-7.2f  RRP-exVAT GBP%-7.2f  margin %4.1f%%  markup %.2fx  [%s]"
              % (h["model"], h["bom_gbp"], h["est_exfactory_gbp"], h["rrp_ex_vat_gbp"],
                 h["gross_margin_pct"], h["markup_x_over_bom"], h["basis"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
