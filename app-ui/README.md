# AI2ORBIT / NetSwitch — app shell UI

Single self-contained `index.html` (no dependencies, both brand images embedded as
data URIs). Covers the three UI pieces requested:

1. **Startup / splash** — AI2ORBIT logo + the Finnish product sign (Avainlippu
   "Tehty Suomessa"). **No company name** on this screen, by request.
2. **Long-task loading indicator** — for tasks that take a while (an IP traceroute
   can run 4–30 s). Pulsing logo + spinner + rolling status text
   ("Processing network data…", "Resolving hops…", …) + a live elapsed-seconds
   counter, so the screen is never silent while work is in progress.
3. **Copyright / About view** — the Avainlippu sign attached, plus the text
   `© 2026 AI2Orbit Co. — NetSwitch`, "Tehty Suomessa · Made in Finland", and the
   Avainlippu® registered-mark line.

Open `index.html` in any browser — the splash plays, then the console appears;
"Run IP traceroute" demonstrates the loader; "About" opens the copyright view.

Notes:
- The two images are exactly the assets supplied (AI2ORBIT wordmark; Avainlippu mark).
- Product name is `NetSwitch` (the `[Productname]` placeholder in the spec) — one
  constant at the top of the generator, trivial to change.
- The traceroute here is a timed demo; the production build calls the native
  traceroute and streams real hops into the same view/loader.
