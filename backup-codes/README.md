# 6GGW / NetSwitch — account backup codes (`ggw_backupcodes`)

500 one-time backup codes to verify the user when the normal factor isn't
available — the same idea as GitHub / Google 2FA recovery codes, wired into the
`.switchc` config under `<security>`.

## The security shape (why it's built this way)

- The user is shown the **500 plaintext codes once**, to print or save.
- The `.switchc` config stores **only salted SHA-256 hashes** — never the codes.
  So if someone gets the config file, they still can't log in: a hash can't be
  turned back into a code.
- Each code is **single-use**. Verifying a code flips its `used="0"` to
  `used="1"` (burned), so a code can never be replayed.

Putting the raw 500 codes into the config would mean anyone who can read the
file owns the account. That's why the codes and the config are two separate
things: codes to the user, hashes to the file.

## Use

```
# generate 500 codes  ->  a user sheet (plaintext) + a config block (hashes)
ggw_backupcodes gen --count 500 --sheet backup-codes.txt --config backup-codes.switchc.xml

# verify a code the user typed (accepts once, then burns it)
ggw_backupcodes verify --config backup-codes.switchc.xml --code WEVG0-9T22K
```

`gen` writes two files:

- `backup-codes.txt` — the 500 plaintext codes, for the user to keep. Show once.
- `backup-codes.switchc.xml` — the `<backup-codes>` block. Paste it into the
  `<security>` section of the `.switchc` (see `switch-config/example.switchc`).

`verify` prints one of:

```
OK — code accepted (backup code #12), now consumed.
REJECTED — that code was already used (#12).
REJECTED — code not recognised.
```

## Code format

Each code is 10 symbols, grouped `XXXXX-XXXXX`, e.g. `WEVG0-9T22K`. The alphabet
is Crockford base32 — no `I L O U`, so no look-alikes when a user reads a code
off paper. 32 symbols and 256 ÷ 32 = 8 exactly, so `byte % 32` is perfectly
uniform: no bias. Ten symbols = **50 bits** of entropy per code.

## What's real / verified

- **CSPRNG** — codes and salt come from the OS crypto RNG: `/dev/urandom`
  (Linux) / `BCryptGenRandom` (Windows). Never `std::rand`.
- **SHA-256** — self-contained FIPS 180-4 implementation, no external crypto
  library. Sanity-checked against the known empty-string digest
  `e3b0c442…b855`.
- **Cross-platform** — verified on Linux (native) and Windows (`.exe`, run under
  wine). A code generated on Windows verifies on Linux and vice-versa (identical
  hashing both sides).
- **Burn / replay** — tested end to end: accept → same code rejected as used →
  a fresh code accepted → garbage rejected.

## Files in this folder

| File | What |
|------|------|
| `ggw_backupcodes.cpp` | the tool (single file, C++17, no dependencies) |
| `ggw_backupcodes.exe` | prebuilt Windows binary |
| `example-backup-codes.txt` | a real generated 500-code user sheet (example) |
| `example-backup-codes.switchc.xml` | the matching 500-hash config block (example) |

## Build

```
# Linux
g++ -std=c++17 -O2 ggw_backupcodes.cpp -o ggw_backupcodes

# Windows (mingw; MSVC works too) — links the system crypto RNG
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_backupcodes.cpp -o ggw_backupcodes.exe -static -lbcrypt
```
