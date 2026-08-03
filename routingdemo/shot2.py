from playwright.sync_api import sync_playwright
import pathlib, time
url = pathlib.Path("index.html").resolve().as_uri()
with sync_playwright() as p:
    b = p.chromium.launch(args=["--use-gl=swiftshader"])
    pg = b.new_page(viewport={"width":1280,"height":720})
    errs=[]; pg.on("console", lambda m: errs.append(m.text) if m.type=="error" else None)
    pg.on("pageerror", lambda e: errs.append("PAGEERROR: "+str(e)))
    pg.goto(url); time.sleep(1.5)
    has_synth = pg.evaluate("!!window.speechSynthesis")
    # test-call button click (should not throw)
    pg.click("#bCall"); time.sleep(0.3)
    # switch to Manual, pick Path C (index 2)
    pg.click("#modeSeg button[data-mode='manual']"); time.sleep(0.3)
    btns = pg.query_selector_all("#pathPick button")
    npaths = len(btns)
    btns[2].click(); time.sleep(0.8)
    route_manual = pg.eval_on_selector("#active","e=>e.textContent").split('Active route')[-1][:40]
    # switch to Static
    pg.click("#modeSeg button[data-mode='static']"); time.sleep(0.8)
    route_static = pg.eval_on_selector("#active","e=>e.textContent").split('Active route')[-1][:40]
    pg.screenshot(path="demo_manual.png")
    # back to adaptive, enable auto-announce, fail link
    pg.click("#modeSeg button[data-mode='adaptive']")
    pg.check("#autoAnn"); pg.click("#bFail"); time.sleep(1.2)
    pg.screenshot(path="demo_v2.png")
    print("speechSynthesis present:", has_synth)
    print("manual paths offered:", npaths)
    print("manual route (Path C):", route_manual.strip())
    print("static route:", route_static.strip())
    print("console/page errors:", errs[:6])
    b.close()
