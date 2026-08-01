#!/usr/bin/env python3
"""
build_cabs.py — "All Cabs" business / premium-cabin pricing report generator.
Aviation (ICAO) + Maritime (IMO/SOLAS) premium cabin pricing, FMA/AI2ORBIT report-engine style.
Same architecture as build_report.py: datasets -> matplotlib charts -> long HTML -> PDF.
Every row tagged basis = measured | modelled.
"""
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import csv, html
import numpy as np

AIR_C="#4a90c2"; SEA_COL="#2a9d8f"; ORANGE="#ef8a3c"; BLUE="#2f6ea5"; MEAS="#4a90c2"; MODEL="#ef8a3c"
plt.rcParams.update({"font.size":8,"axes.titlesize":10,"axes.titleweight":"bold"})
FX_NOTE="USD throughout; fares are return/round-trip long-haul; ferry cabins are from-price per cabin/night."

# ---- AVIATION: long-haul business class return fares (USD) ----
# route, carriers, econ_return(approx,modelled), biz_published(measured), biz_consolidator(measured/mixed)
AIR=[
 ("New York — London","BA / Virgin / AA",700,3500,1900,"measured"),
 ("New York — Dubai","Emirates / Qatar",950,4500,2485,"measured"),
 ("New York — Singapore","Singapore Airlines",1150,4650,2565,"measured"),
 ("London — New York","BA / Virgin",620,4000,2100,"modelled"),
 ("London — Singapore","SIA / BA",900,4200,2400,"modelled"),
 ("London — Dubai","Emirates",520,2600,1500,"modelled"),
 ("US long-haul (average)","all carriers",1000,5300,3000,"measured"),
]
# aviation premium economics
AVE=[
 ("Premium share of seat capacity",6.0,"%","2025","measured","Residual Research"),
 ("Premium share of passenger revenue",30.0,"%","2025","measured","industry"),
 ("Delta premium share of ticket revenue",50.0,"% (>)","Q4 2025","measured","Delta"),
 ("Premium economy vs economy fare",2.0,"× (~)","2025","measured","American Airlines"),
 ("Prem-econ revenue per sq ft vs economy",33.0,"% more","2025","measured","Lufthansa"),
 ("Business published vs economy (this data)",5.0,"× (~)","2026","modelled","derived from fare table"),
]
# ---- MARITIME: Baltic premium cabin ladder (Helsinki–Stockholm), USD from-price ----
# cabin, operator, occ, from_usd, basis, note
SEA=[
 ("Standard inside","Tallink Silja / Viking",4,46,"measured","cheapest berth, no window"),
 ("Standard sea-view","Tallink Silja / Viking",4,90,"modelled","outside cabin midpoint"),
 ("Commodore","Tallink Silja",2,303,"measured","double bed, breakfast, aqua zone"),
 ("Deluxe","Tallink Silja",3,409,"measured","upper-deck, breakfast, aqua zone"),
 ("Suite / Executive Suite","Tallink Silja",2,700,"modelled","premium suite midpoint"),
 ("Top suite (peak)","Tallink Silja",2,2338,"measured","observed high-end cabin price"),
 ("Cabin average (all classes)","route mean",0,194,"measured","reported route average"),
]

def air_calc(econ,pub,con):
    return round(pub/econ,2), round(con/econ,2), round((pub-con)/pub*100,1)

# ---------------- charts ----------------
def hbar(ax,labels,vals,colors,xlabel,fmt="{:.0f}"):
    y=list(range(len(labels))); ax.barh(y,vals,color=colors,edgecolor="white",height=0.72)
    ax.set_yticks(y); ax.set_yticklabels(labels,fontsize=7); ax.invert_yaxis()
    ax.set_xlabel(xlabel,fontsize=8); ax.grid(axis="x",color="#e2e6ea",lw=.7); ax.set_axisbelow(True)
    for s in ("top","right"): ax.spines[s].set_visible(False)
    mx=max(vals) if vals else 1
    for i,v in zip(y,vals): ax.text(v+mx*0.01,i,fmt.format(v),va="center",fontsize=6.4,color="#333")

def make_charts():
    # air fares: published vs consolidator grouped
    rows=[r for r in AIR if r[0]!="US long-haul (average)"]
    fig,ax=plt.subplots(figsize=(9.4,3.4)); yy=np.arange(len(rows)); h=0.38
    ax.barh(yy-h/2,[r[3] for r in rows],h,color=BLUE,label="Business (published)",edgecolor="white")
    ax.barh(yy+h/2,[r[4] for r in rows],h,color="#8bb0d6",label="Business (consolidator)",edgecolor="white")
    ax.set_yticks(yy); ax.set_yticklabels([r[0] for r in rows],fontsize=7); ax.invert_yaxis()
    ax.set_xlabel("USD return"); ax.grid(axis="x",color="#e2e6ea",lw=.7); ax.set_axisbelow(True)
    for s in ("top","right"): ax.spines[s].set_visible(False)
    ax.legend(fontsize=8,frameon=False); ax.set_title("Long-haul business class fares by route (USD return)",fontsize=11)
    fig.tight_layout(); fig.savefig("chart_air.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # aviation premium economics
    fig,ax=plt.subplots(figsize=(9.4,2.4))
    labs=[a[0] for a in AVE]; vals=[a[1] for a in AVE]; cols=[MEAS if a[4]=="measured" else MODEL for a in AVE]
    hbar(ax,labs,vals,cols,"value (% or ×)","{:.0f}")
    ax.set_title("Aviation premium-cabin economics",fontsize=11)
    fig.tight_layout(); fig.savefig("chart_airecon.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # ferry cabin ladder
    rows=[s for s in SEA if s[0]!="Cabin average (all classes)"]
    fig,ax=plt.subplots(figsize=(9.4,3.0))
    cols=[SEA_C(s) for s in rows]
    hbar(ax,[s[0] for s in rows],[s[3] for s in rows],cols,"USD from-price (per cabin/night)","${:.0f}")
    ax.set_title("Maritime premium cabin ladder — Helsinki–Stockholm",fontsize=11)
    fig.tight_layout(); fig.savefig("chart_sea.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # cross-mode premium multiple
    air_mult=[(r[0],air_calc(r[2],r[3],r[4])[0]) for r in AIR if r[0]!="US long-haul (average)"]
    base_inside=46.0
    sea_mult=[("Ferry Commodore",303/base_inside),("Ferry Deluxe",409/base_inside),("Ferry Suite~",700/base_inside)]
    labs=[a[0] for a in air_mult]+[s[0] for s in sea_mult]
    vals=[a[1] for a in air_mult]+[s[1] for s in sea_mult]
    cols=[AIR_C]*len(air_mult)+[SEA_COL]*len(sea_mult)
    fig,ax=plt.subplots(figsize=(9.4,3.2)); hbar(ax,labs,vals,cols,"× over base cabin/economy","{:.1f}x")
    ax.set_title("Premium multiple over base fare — air vs sea",fontsize=11)
    fig.legend(handles=[Patch(color=AIR_C,label="Aviation (business/economy)"),Patch(color=SEA_COL,label="Maritime (premium/standard-inside)")],
               loc="lower center",ncol=2,fontsize=8,frameon=False,bbox_to_anchor=(0.5,-0.02))
    fig.tight_layout(rect=[0,0.05,1,1]); fig.savefig("chart_cross.png",dpi=150,bbox_inches="tight"); plt.close(fig)

AIR_C="#4a90c2"
def SEA_C(s): return MEAS if s[4]=="measured" else MODEL

# ---------------- html ----------------
E=html.escape; PAGES=[]
def page(b): PAGES.append(b)
def tbl(headers,rows,aligns=None):
    aligns=aligns or ["l"]*len(headers)
    th="".join(f'<th class="{"l" if a=="l" else ""}">{h}</th>' for h,a in zip(headers,aligns))
    body=""
    for r in rows:
        tds=""
        for cell,a in zip(r,aligns):
            cls={"l":"","c":"c","r":"r","m":"c m","mr":"r m","ml":"m"}.get(a,"")
            tds+=f'<td class="{cls}">{cell}</td>'
        body+=f"<tr>{tds}</tr>"
    return f'<table><tr>{th}</tr>{body}</table>'
def foot(n): return f'<div class="foot">AI2ORBIT PROJECT — All Cabs: Business &amp; Premium Cabin Pricing — page {n}</div>'

def build():
    make_charts()
    # cover
    page(f'''<div class="cover"><div class="cv-band"></div>
      <div class="cv-mid"><div class="cv-kicker">AI2ORBIT PROJECT · REPORT ENGINE</div>
      <h1 class="cv-title">All Cabs</h1>
      <div class="cv-title2">Business &amp; Premium Cabin Pricing</div>
      <div class="cv-sub">Aviation (ICAO) and Maritime (IMO / SOLAS) premium-cabin fares,<br>economics and cross-mode comparison — with a machine-readable sample feed</div>
      <div class="cv-rule"></div>
      <div class="cv-meta">Prepared for Sami Leino · 2026-08-01 · {len(AIR)} air routes · {len(SEA)} cabin classes · {len(AVE)} economics metrics</div>
      </div><div class="cv-band b"></div></div>''')
    # contents
    toc=["Executive Summary","Methodology &amp; Honesty Labels","What IMO / SOLAS / ICAO Mean Here",
         "Aviation — Business Class Fares","Aviation — Premium Economics","Maritime — Premium Cabin Ladder",
         "Cross-Mode Premium Multiple","Structural Context &amp; Demand","Data Dictionary","Sources, Method &amp; Sample Feed"]
    page('<h1>Contents</h1><ol class="toc">'+"".join(f'<li><span>{i+1:02d}</span>{t}</li>' for i,t in enumerate(toc))+'</ol>'+foot(2))
    # exec
    page(f'''<h1>Executive Summary</h1>
      <p class="p">This report prices the "business/premium cabin" tier across the two long-haul transport modes named in the brief: aviation (governed internationally by <b>ICAO</b>) and maritime passenger shipping (governed by <b>IMO</b> and its <b>SOLAS</b> safety convention). It answers a simple question — what does the front of the cabin cost, and how much more is it than the back?</p>
      <p class="p"><b>Aviation.</b> Long-haul business class returns cluster around <b>$3,500–$4,650 published</b> on the key routes (NYC–London/Dubai/Singapore), with the US long-haul average near $5,300. Specialist "consolidator" fares undercut published prices by roughly 40–45% ($1,900–$2,565 on the same routes). Business published sits at roughly <b>5× economy</b>; via consolidator, ~3×.</p>
      <p class="p"><b>Maritime.</b> On a representative Baltic route (Helsinki–Stockholm, Tallink Silja / Viking Line) the premium cabin ladder runs from a <b>Commodore at $303</b> and <b>Deluxe at $409</b> up to peak suites near <b>$2,338</b>, against a standard inside berth from ~$46 and a route cabin average of ~$194. The premium multiple over the cheapest berth is far higher than aviation's, because the base "cabin" is a shared no-window berth.</p>
      <p class="p"><b>Why it matters.</b> Premium is where the money is: front cabins are ~6% of airline seat capacity but ~30% of passenger revenue, and Delta's premium ticket revenue crossed <b>above 50%</b> of the total in Q4 2025. Every figure below is tagged measured or modelled.</p>'''+foot(3))
    # methodology
    page(f'''<h1>Methodology &amp; Honesty Labels</h1>
      <ul class="chk"><li><b>measured</b> — a public / reported fare or figure (see source).</li>
      <li><b>modelled</b> — an estimate or interpolation from a stated basis (shown in <span class="m">orange</span>), never a quote.</li></ul>
      <p class="p">Fares are volatile: airline and ferry prices move by date, season, demand and booking channel. This report uses representative <i>from</i>-prices and reported ranges, not a live quote. Economy fares used to derive the business multiple are approximate and flagged modelled where not directly sourced. {FX_NOTE}</p>
      <div class="note">A live, always-current version would plug into fare APIs (GDS/airline NDC for air; operator booking feeds for sea). This report is the static, honest sample of that pipeline — the same engine that produced the UK Data Market report.</div>'''+foot(4))
    # acronyms
    page('<h1>What IMO / SOLAS / ICAO Mean Here</h1>'
         +tbl(["Body / convention","Scope","Relevance to pricing"],
              [["ICAO","UN agency for civil aviation standards","defines the aviation system; airlines set the business/economy fare ladder within it"],
               ["IMO","UN agency for maritime shipping","defines the maritime system; ferry/cruise operators set cabin-class pricing within it"],
               ["SOLAS","IMO Safety of Life at Sea convention","governs safety, capacity and life-saving — it constrains how many cabins/berths a ship may sell, not their price"],
               ["\"All Cabs\"","all cabin classes, both modes","the premium (business) tier vs the base tier, priced side by side"]],["l","l","l"])
         +'<p class="p">So "business class pricing for IMO/SOLAS/ICAO All Cabs" is read here as: the price of the premium cabin tier across aviation and maritime, with SOLAS noted as the safety/capacity frame that bounds supply.</p>'+foot(5))
    # aviation fares
    arows=[]
    for (rt,car,ec,pub,con,b) in AIR:
        pm,cm,disc=air_calc(ec,pub,con)
        arows.append([E(rt),E(car),f"{ec:,}",f"{pub:,}",f"{con:,}",f"{pm:.1f}×",f"{cm:.1f}×",("meas." if b=="measured" else "model.")])
    page('<h1>Aviation — Business Class Fares</h1>'
         '<div class="lead">Long-haul return fares, USD. Economy is an approximate anchor (flagged); business published and consolidator are reported. Multiples are business ÷ economy.</div>'
         +tbl(["Route","Carriers","Econ $","Biz publ. $","Biz consol. $","× publ.","× consol.","Basis"],arows,
              ["l","l","r","r","r","r","r","c"])
         +'<img class="chart" src="chart_air.png">'+foot(6))
    # aviation economics
    aerows=[[E(m),f"{v:g}",E(u),pe,("meas." if b=="measured" else "model.")] for (m,v,u,pe,b,s) in AVE]
    page('<h1>Aviation — Premium Economics</h1>'
         '<p class="p">Premium cabins are a small share of seats but a large share of money, and the mix is shifting forward. This is the strategic backdrop to business-class pricing power.</p>'
         +tbl(["Metric","Value","Unit","Period","Basis"],aerows,["l","r","l","c","c"])
         +'<img class="chart" src="chart_airecon.png">'+foot(7))
    # maritime
    srows=[]
    for (cb,op,occ,fr,b,nt) in SEA:
        mult=("—" if fr==0 or cb.startswith("Standard inside") else f"{fr/46:.1f}×")
        srows.append([f'<span class="{"m" if b=="modelled" else ""}">{E(cb)}</span>',E(op),(occ if occ else "—"),f"{fr:,}",mult,("meas." if b=="measured" else "model."),E(nt)])
    page('<h1>Maritime — Premium Cabin Ladder</h1>'
         '<div class="lead">Representative Baltic overnight route (Helsinki–Stockholm), Tallink Silja / Viking Line. From-prices per cabin/night, USD. Multiple is over the cheapest inside berth (~$46).</div>'
         +tbl(["Cabin class","Operator","Occ.","From $","× inside","Basis","Note"],srows,["l","l","c","r","r","c","l"])
         +'<img class="chart" src="chart_sea.png">'+foot(8))
    # cross-mode
    page('<h1>Cross-Mode Premium Multiple</h1>'
         '<p class="p">How much more the premium tier costs than the base, side by side. Aviation business runs ~3–5× economy; maritime premium cabins run much higher over the cheapest berth — but that base is a shared, windowless berth, so the two ratios are not like-for-like.</p>'
         '<img class="chart" src="chart_cross.png">'
         '<ul class="chk"><li>Aviation: business ≈ 5× economy published, ≈ 3× via consolidator.</li>'
         '<li>Maritime: Commodore ≈ 6.6×, Deluxe ≈ 8.9×, suites 15×+ over the cheapest inside berth.</li>'
         '<li>Like-for-like (private outside cabin vs premium) the maritime multiple is much smaller — noted, not overstated.</li></ul>'+foot(9))
    # structure
    page('<h1>Structural Context &amp; Demand</h1>'
         '<p class="p">Premium demand is structurally rising. Airbus projects premium travel driving growth through 2045; US carriers are re-configuring cabins because ~6% of premium capacity drives ~30% of revenue. Delta crossed the point in 2025 where premium ticket revenue exceeded main-cabin revenue.</p>'
         '<p class="p">In maritime, the Baltic overnight ferries are effectively floating premium-leisure products: the Commodore/Deluxe/Suite ladder is the "business class" of the sea, and price dispersion within one sailing (from ~$46 to ~$2,338) is wider than a single long-haul flight. SOLAS caps how much premium inventory a hull can carry, which supports premium pricing when demand peaks (summer, holidays).</p>'
         '<div class="note">For an operator or reseller, the takeaway mirrors the UK data report: the premium tier is where margin concentrates, and dynamic, channel-aware pricing (published vs consolidator; peak vs off-peak cabins) is the lever.</div>'+foot(10))
    # data dictionary
    page('<h1>Data Dictionary</h1>'
         '<h2>air (aviation fares)</h2>'+tbl(["Column","Meaning"],[["route/carriers","O&D pair; example carriers"],["econ_return_usd","approx economy return (anchor, often modelled)"],["biz_published_usd","reported published business return"],["biz_consolidator_usd","specialist/consolidator business return"],["mult_*","business ÷ economy"],["basis","measured | modelled"]],["l","l"])
         +'<h2>sea (maritime cabins)</h2>'+tbl(["Column","Meaning"],[["cabin_class/operator","premium ladder + operator"],["occ","max occupancy"],["from_usd","from-price per cabin/night"],["mult_over_inside","from_usd ÷ cheapest inside berth"],["basis","measured | modelled"]],["l","l"])
         +'<h2>economics</h2>'+tbl(["Column","Meaning"],[["metric/value/unit","premium-share indicator"],["period/basis/source","when; measured|modelled; provenance"]],["l","l"])+foot(11))
    # sources + sample
    page('<h1>Sources, Method &amp; Sample Feed</h1>'
         '<ul class="chk"><li>Air business fares — comparison / consolidator sources &amp; airline data, 2026 (measured; ranges).</li>'
         '<li>Premium economics — Delta, Lufthansa, American, Airbus, Residual Research, 2025 (measured).</li>'
         '<li>Ferry cabins — Direct Ferries / Tallink Silja / Viking Line listings, 2026 (measured from-prices).</li>'
         '<li>Economy anchors &amp; suite midpoints — modelled, flagged.</li></ul>'
         +tbl(["File","What it is"],[["air_fares.csv","aviation business/economy fares + multiples"],["sea_cabins.csv","maritime premium cabin ladder"],["air_economics.csv","premium-share economics"],["build_cabs.py","this generator — scales with the data"]],["l","l"])
         +'<div class="note">This is a proof version (~12 pages). Same as the UK report, it scales: add routes, operators, cruise lines, rail first-class, or per-carrier / per-route pages to reach 35–100. Send your own sources (you mentioned some are written) and I fold them in 1:1.</div>'+foot(12))
    # CSVs
    with open("air_fares.csv","w",newline="") as f:
        w=csv.writer(f); w.writerow(["route","carriers","econ_return_usd","biz_published_usd","biz_consolidator_usd","mult_published","mult_consolidator","basis"])
        for (rt,car,ec,pub,con,b) in AIR:
            pm,cm,_=air_calc(ec,pub,con); w.writerow([rt,car,ec,pub,con,pm,cm,b])
    with open("sea_cabins.csv","w",newline="") as f:
        w=csv.writer(f); w.writerow(["cabin_class","operator","occupancy","from_usd","mult_over_inside","basis","note"])
        for (cb,op,occ,fr,b,nt) in SEA: w.writerow([cb,op,occ,fr,(round(fr/46,2) if fr else ""),b,nt])
    with open("air_economics.csv","w",newline="") as f:
        w=csv.writer(f); w.writerow(["metric","value","unit","period","basis","source"])
        for row in AVE: w.writerow(row)
    doc='<meta charset="utf-8">\n<style>'+CSS+'</style>\n'
    for i,b in enumerate(PAGES):
        doc+=f'<div class="{"page cover-page" if i==0 else "page"}">{b}</div>\n'
    open("cabs_report.html","w").write(doc)
    print(f"cabs_report.html written — {len(PAGES)} pages; CSVs: air_fares({len(AIR)}) sea_cabins({len(SEA)}) air_economics({len(AVE)})")

CSS='''
@page{size:A4 portrait;margin:15mm 14mm;}
*{box-sizing:border-box;}
body{font-family:"Segoe UI",Roboto,Helvetica,Arial,sans-serif;color:#222;margin:0;}
.page{page-break-after:always;} .page:last-child{page-break-after:auto;}
h1{color:#2f6ea5;font-size:22px;font-weight:800;margin:0 0 8px;}
h2{color:#2f6ea5;font-size:15px;font-weight:800;margin:16px 0 4px;}
.p{font-size:11.5px;line-height:1.55;color:#2c3138;margin:0 0 9px;}
.lead{color:#5a6068;font-size:11px;margin:2px 0 8px;}
table{width:100%;border-collapse:collapse;font-size:10px;margin:4px 0 8px;border:1px solid #b8cfe0;}
th{background:#3d88c4;color:#fff;font-weight:700;text-align:center;padding:5px 6px;border:1px solid #327ab5;line-height:1.2;}
th.l{text-align:left;}
td{padding:3.5px 6px;border:1px solid #dfe6ec;} td.c{text-align:center;} td.r{text-align:right;}
tr:nth-child(even) td{background:#f4f8fb;}
.m{color:#e8833a;font-weight:700;}
img.chart{width:100%;margin:8px 0;}
.note{font-size:10.5px;color:#6a5a20;background:#fbf7e6;border-left:4px solid #e2c65a;padding:8px 12px;border-radius:4px;margin:8px 0;}
ul.chk{margin:6px 0;padding-left:0;list-style:none;}
ul.chk li{font-size:11px;color:#3f4650;padding:3px 0 3px 20px;position:relative;}
ul.chk li:before{content:"▪";color:#2f6ea5;position:absolute;left:2px;}
.foot{color:#9aa;font-size:9px;border-top:1px solid #e2e6ea;margin-top:14px;padding-top:5px;position:absolute;bottom:12mm;left:14mm;right:14mm;}
ol.toc{list-style:none;padding:0;font-size:13px;}
ol.toc li{padding:7px 0;border-bottom:1px solid #eef2f5;color:#2c3138;}
ol.toc li span{display:inline-block;width:34px;color:#3d88c4;font-weight:800;}
.cover-page{position:relative;height:267mm;}
.cover{position:absolute;inset:-15mm -14mm;background:#0f1f33;color:#fff;display:flex;flex-direction:column;justify-content:space-between;}
.cv-band{height:10mm;background:linear-gradient(90deg,#0c6b6b,#14a293);}
.cv-band.b{background:linear-gradient(90deg,#14a293,#0c6b6b);}
.cv-mid{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:0 18mm;}
.cv-kicker{color:#5fd3c4;letter-spacing:3px;font-size:12px;font-weight:700;margin-bottom:14px;}
.cv-title{font-size:46px;font-weight:800;margin:0;color:#fff;letter-spacing:1px;}
.cv-title2{font-size:20px;font-weight:600;color:#cfe6ff;margin-top:4px;}
.cv-sub{color:#c4cdd8;font-size:14px;line-height:1.6;margin-top:14px;}
.cv-rule{width:120px;height:3px;background:#ec8a3c;margin:20px auto;}
.cv-meta{color:#8493a6;font-size:12px;}
'''
if __name__=="__main__": build()
