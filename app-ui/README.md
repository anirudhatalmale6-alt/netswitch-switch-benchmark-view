# AI2ORBIT / AINetworkSwitch — app shell UI

Single self-contained `index.html` (no dependencies, key-flag mark embedded as a
white-background data URI). **White theme.** Product: **AINetworkSwitch** —
automated IP routing and rerouting.

## What's in it

1. **Startup / splash** — the **AI2ORBIT text sign on white** + the Finnish product
   sign (Avainlippu key-flag, small). **No company name** on this screen, by request.
2. **Long-task loading indicator** — for tasks that take a while (an IP traceroute
   can run 4–30 s): AI2ORBIT sign + spinner + rolling status text
   ("Processing network data…", "Resolving hops…", …) + a live elapsed-seconds
   counter. The screen is never silent while work is in progress.
3. **Call quality** — monitored continuously and shown to stay live **through every
   reroute / network change on the go**. Hit "Reroute" and the meter dips to
   "Switching route… — call held", then recovers on the new path.
4. **Copyright — two variants:**
   - **App / Windows** → a full **Copyright view**: AI2ORBIT sign + key-flag +
     `© 2026 AI2Orbit Co. — AINetworkSwitch` + "Made in Finland" + the Avainlippu®
     registered-mark line. (Open with "About".)
   - **Browser** → a slim **footer line**: `Copyright 2026 AI2Orbit Co.` + the
     Keylogo (key-flag). Always visible at the bottom of the console.

## Run

Open `index.html` in any browser — splash plays, then the console appears.
"Run IP traceroute" demonstrates the loader; "Reroute" demonstrates call-quality
hold; "About" opens the App/Windows copyright view; the browser footer is the
browser copyright variant.

Notes:
- AI2ORBIT is drawn as a **text sign** (per "Text sign: AI2ORBIT, white background"),
  not the yellow screenshot — cleaner and truly white.
- The key-flag is the supplied Avainlippu mark, background recolored to white so it
  sits cleanly on the light theme.
- The traceroute/reroute here are timed demos; the production build calls the native
  traceroute and the real least-cost rerouter and streams into the same views.
