# Data dictionary — UK data market sample

Three tidy tables, one row per record, UTF-8. Every row carries a `basis` field
(`measured` = public/reported figure, see `source`; `modelled` = computed here from a
stated assumption — not a quoted number). Modelling constants live at the top of
`gen_sample.py`: `FX_USD_GBP=0.79`, `EXFACTORY_MULT=1.30`, `VAT_RATE=0.20`.

## plans.csv — SIM-only tariffs
| column | type | notes |
|---|---|---|
| operator | text | brand |
| type | enum | MNO / MVNO |
| host_network | text | physical network the SIM runs on |
| plan_name | text | tariff label |
| data_gb | number \| "unlimited" | monthly allowance |
| price_gbp_month | number | headline £/month |
| contract_months | int | 1 = 30-day rolling |
| price_per_gb_gbp | number | price ÷ data_gb; blank for unlimited |
| roaming_eu | enum | yes / no |
| basis | enum | measured |
| source | text | provenance |

## handsets.csv — cost → ex-factory → RRP waterfall
| column | type | notes |
|---|---|---|
| model, brand, launch_year | text/int | handset id |
| bom_usd | number | bill-of-materials, teardown estimate (measured) |
| bom_gbp | number | bom_usd × FX_USD_GBP |
| est_exfactory_usd/gbp | number | **modelled** = BOM × EXFACTORY_MULT (assembly+test+mfr margin) |
| uk_rrp_gbp_incvat | number | public UK RRP incl. VAT (measured) |
| rrp_ex_vat_gbp | number | RRP ÷ (1+VAT_RATE) — trade value |
| gross_margin_pct | number | (rrp_ex_vat − bom_gbp) ÷ rrp_ex_vat × 100 |
| markup_x_over_bom | number | rrp_ex_vat ÷ bom_gbp |
| basis | enum | measured / modelled |

Note: BOM excludes assembly, test, software, IP/royalties, logistics, warranty and
marketing — so `gross_margin_pct` is the pool those are paid from, **not** net profit.

## market.csv — context metrics
| column | type | notes |
|---|---|---|
| metric | text | what is measured |
| value | number | figure |
| unit | text | unit of value |
| period | text | year/quarter |
| segment | text | postpaid / prepaid / MVNO / all |
| basis | enum | measured / modelled |
| source | text | provenance |

The two marketing rows (`Marketing spend of revenue`, `Cost per acquisition`) are
**modelled placeholders** — replace with your own figures and they flow through unchanged.
