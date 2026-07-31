# UK data market — report + sample feed

A UK mobile-data-market brief plus a machine-readable **sample** for your report engine.

## Files
- `UK_Data_Market_Brief.pdf` — the readable 2-page brief (pricing, handset ex-factory margins, market/marketing).
- `plans.csv`, `handsets.csv`, `market.csv` — three tidy tables to feed the report engine.
- `uk_data_market_sample.json` — all three tables + meta/assumptions in one JSON.
- `gen_sample.py` — one deterministic generator that emits the CSVs + JSON.
- `data_dictionary.md` — column-by-column schema + the modelling assumptions.

## Run
```
python3 gen_sample.py          # write the 3 CSVs + JSON, print a summary
python3 gen_sample.py --check  # verify invariants, print json checksum, exit 0/1
```
No dependencies. Same input → byte-identical output on any machine (json sha256 `a792719cdd4e03a6`).

## Honesty
Every row has a `basis`: `measured` (a reported figure, see `source`) or `modelled`
(computed from a stated assumption — not a quoted number). The ex-factory column and the
two marketing rows are **modelled** and flagged as such. Modelling constants
(`FX_USD_GBP`, `EXFACTORY_MULT`, `VAT_RATE`) are at the top of `gen_sample.py` — change one
and the whole dataset re-prices.

This is a **sample**. Send me your report-engine's expected schema (column names / format)
and your own marketing figures, and I'll match the layout exactly and widen the dataset.
