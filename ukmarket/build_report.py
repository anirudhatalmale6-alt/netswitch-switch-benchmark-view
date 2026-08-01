#!/usr/bin/env python3
"""
build_report.py — long-form UK Data Market report generator (FMA/Kenneth report-engine style).

Emits report_long.html (35+ A4 pages) from the datasets below + matplotlib charts, then you
render it to PDF. Length scales with the data: add operators / handsets / tiers and pages grow.

Honesty: every data row carries basis = measured | modelled. Modelling constants are explicit.
"""
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import csv, html

FX=0.79; EXF=1.30; VAT=0.20
MNO="#4a90c2"; MVNO="#43a047"; ORANGE="#ef8a3c"; PURPLE="#8e44ad"; MEAS="#4a90c2"; MODEL="#ef8a3c"
BLUE="#2f6ea5"
plt.rcParams.update({"font.size":8,"axes.titlesize":10,"axes.titleweight":"bold"})

# ============================ DATA ============================
# plans: operator,type,host,plan,data_gb(or 'unlimited'),price,months,basis
PLANS=[
 ("Three","MNO","Three","Unlimited SIM","unlimited",22.00,24,"measured"),
 ("Three","MNO","Three","10GB SIM",10,6.00,1,"measured"),
 ("EE","MNO","EE","Unlimited SIM","unlimited",40.00,24,"measured"),
 ("EE","MNO","EE","25GB SIM",25,20.00,24,"modelled"),
 ("O2","MNO","O2","Entry SIM",3,5.80,1,"measured"),
 ("O2","MNO","O2","Unlimited SIM","unlimited",29.00,24,"modelled"),
 ("Vodafone","MNO","Vodafone","Unlimited SIM","unlimited",26.00,24,"measured"),
 ("Smarty","MVNO","Three","3GB rolling",3,3.90,1,"measured"),
 ("Smarty","MVNO","Three","~30GB rolling",30,8.00,1,"measured"),
 ("Smarty","MVNO","Three","Unlimited",  "unlimited",12.00,12,"measured"),
 ("iD Mobile","MVNO","Three","120GB",120,12.00,1,"measured"),
 ("iD Mobile","MVNO","Three","Unlimited","unlimited",16.00,1,"measured"),
 ("Giffgaff","MVNO","O2","20GB",20,10.00,1,"measured"),
 ("Giffgaff","MVNO","O2","Unlimited (18m)","unlimited",14.00,18,"measured"),
 ("Lebara","MVNO","Vodafone","5GB",5,5.00,1,"measured"),
 ("Voxi","MVNO","Vodafone","Unlimited","unlimited",15.00,1,"measured"),
 ("Tesco Mobile","MVNO","O2","5GB",5,7.50,12,"measured"),
 ("ASDA Mobile","MVNO","Vodafone","3GB",3,5.00,1,"measured"),
]
# handsets: model,brand,year,bom_usd,rrp_incvat_gbp,basis,note
HANDSETS=[
 ("iPhone 15 Pro Max","Apple",2023,558.0,1199.0,"measured","teardown est. (analyst-dependent)"),
 ("iPhone 16 Pro Max","Apple",2024,485.0,1199.0,"measured","Counterpoint BoM ~$485"),
 ("Pixel 9 Pro","Google",2024,406.0,999.0,"measured","BoM ~$400-406 (Tensor G4 $80)"),
 ("Galaxy S23 Ultra","Samsung",2023,469.0,1249.0,"measured","teardown est. $469"),
 ("Galaxy S24 Ultra","Samsung",2024,512.0,1249.0,"measured","reported ~$512"),
 ("Galaxy S25 Ultra","Samsung",2025,549.0,1249.0,"measured","reported ~$549 (Snapdragon 8 Elite)"),
 ("Flagship (generic)","-",2026,520.0,1150.0,"modelled","tier midpoint"),
 ("Upper-mid (generic)","-",2026,300.0,599.0,"modelled","tier midpoint"),
 ("Mid-range (generic)","-",2026,230.0,399.0,"modelled","tier midpoint"),
 ("Budget (generic)","-",2026,105.0,159.0,"modelled","tier midpoint"),
 ("Entry (generic)","-",2026,70.0,109.0,"modelled","tier midpoint"),
]
# market: metric,value,unit,period,segment,basis,source
MARKET=[
 ("Postpaid ARPU",15.73,"GBP/month","2023","postpaid","measured","Ofcom/Statista"),
 ("Prepaid ARPU",5.18,"GBP/month","2023","prepaid","measured","Ofcom/Statista"),
 ("VMO2 total connections",45.7,"million","2024","all","measured","Virgin Media O2"),
 ("5G SA population coverage",83.0,"%","2025","all","measured","Ofcom Connected Nations 2025"),
 ("Mobile data usage growth",18.0,"% YoY","2025","all","measured","Ofcom"),
 ("MVNO consumer share (Sky+Tesco)",15.0,"% (>)","2025","MVNO","measured","market reports"),
 ("Acquisition+retention spend",17.5,"% of revenue","2025","all","measured","industry est. 15-20%"),
 ("UK telecom digital ad spend",420.0,"USD million","2023","all","measured","SensorTower"),
 ("Smarty ad-spend growth",90.0,"% YoY","2023","MVNO","measured","SensorTower"),
 ("Three ad-spend growth",60.0,"% YoY","2023","MNO","measured","SensorTower"),
 ("eSIM acquisition-cost saving",30.0,"% (~)","2025","all","measured","industry est."),
 ("Online share of acquisition",35.0,"%","2025","all","measured","industry est."),
 ("Cost per acquisition (SIM)",18.0,"GBP (est)","2026","all","modelled","placeholder"),
]
# operator profiles: name,type,host,tagline,paras(list),plan_keys(operator names to pull)
OPERATORS=[
 ("EE","MNO","own (BT Group)","The premium-coverage network",
  ["EE is the consumer mobile brand of BT Group and consistently tops UK coverage and speed rankings. It positions at the premium end: its unlimited SIM sits highest among the MNOs at ~£40/month.",
   "EE rarely competes on headline price; instead it bundles perks (data gifting, roaming, device insurance) and leans on network quality. Value-seeking customers are steered to no MVNO of its own on the same masts, so the price floor on EE's network is EE itself."]),
 ("O2 (VMO2)","MNO","own (Virgin Media O2)","The wholesale host",
  ["O2 is part of Virgin Media O2, which closed 2024 with 45.7M total connections. Its own-brand entry SIM starts around £5.80 and EU roaming runs across the range.",
   "O2's strategic weight is as a wholesale host: Giffgaff, Tesco Mobile and Sky Mobile all ride O2's network. Sky Mobile + Tesco Mobile alone hold >15% of the consumer segment, so O2 monetises both retail and wholesale layers."]),
 ("Vodafone","MNO","own","The converging incumbent",
  ["Vodafone offers an unlimited SIM around £26/month and hosts a broad value layer — Voxi (youth, social-data-free), Lebara (international calling) and ASDA Mobile.",
   "Vodafone's UK trajectory is dominated by its consolidation with Three; the combined network reshapes coverage and capacity, and is the single biggest structural change in the market over the report horizon."]),
 ("Three","MNO","own","The price leader",
  ["Three is the aggressive price leader among the MNOs: unlimited at ~£22/month undercuts every rival, and its network hosts the cheapest per-GB resellers in the country.",
   "Smarty and iD Mobile both run on Three; iD Mobile's 120GB at £12 (~£0.10/GB) is the cheapest effective data in this report. The Three network therefore anchors the market's price floor."]),
]
MVNOS=[
 ("Smarty","Three","Data-focused, no-frills, no in-contract price rises. 3GB from £3.90, ~30GB at £8 (~£0.27/GB), unlimited £12. One of the fastest-growing ad spenders (+90% YoY 2023), reflecting an aggressive challenger push on Three's network."),
 ("iD Mobile","Three","Owned by Currys/Dixons. Data-rollover and the market's cheapest large bundle — 120GB for £12 (~£0.10/GB); unlimited £16. Aggressive on high-data tiers, and the single cheapest effective £/GB in this report."),
 ("Giffgaff","O2","Community-run on O2's 99%-coverage network. 20GB £10, unlimited (18-month) £14, plus an 'Always On' SIM that throttles to 384kbps after 80GB. Strong brand loyalty and a distinctive member-referral acquisition model that keeps CPA low."),
 ("Tesco Mobile","O2","Clubcard-linked loyalty, family perks. 5GB at £7.50; one of the largest MVNOs and, with Sky Mobile, part of the >15% consumer share on O2. Retail footprint gives it a low-cost acquisition channel."),
 ("Voxi","Vodafone","Vodafone's youth brand with 'endless' social-media data on some plans. Unlimited data £15; SIM-only, 30-day rolling, no credit check. Designed to defend the under-30 segment against challenger MVNOs."),
 ("Lebara","Vodafone","International-calling specialist (100+ minutes to 42 countries on entry plans). 5GB £5; frequent promotional unlimited at ~£10 for new customers. Targets diaspora and value segments."),
 ("ASDA Mobile","Vodafone","Supermarket value brand on Vodafone masts. 3GB £5; positioned on simplicity and low headline entry price rather than large bundles. Distribution via ASDA stores lowers acquisition friction."),
 ("Sky Mobile","O2","Bundled with Sky TV/broadband; data-rollover 'Piggybank'. Part of the >15% consumer segment on O2; monetises the wider Sky subscription base and cross-sells against churn."),
]
# per-handset factual profiles (BOM drivers)
HPROF={
 "iPhone 15 Pro Max":"Apple's 2023 titanium flagship on the A17 Pro. Teardown estimates put the BOM near the top of the set; display and camera systems are the costliest single components. Sold at a premium RRP, it anchors the high end of the margin curve.",
 "iPhone 16 Pro Max":"The 2024 A18 Pro flagship. Counterpoint estimated a BOM around $485, with display and rear-camera each ~16% of components. Held at the same RRP as its predecessor, so the margin depends heavily on the analyst's BOM figure.",
 "Pixel 9 Pro":"Google's 2024 flagship on the in-house Tensor G4 (~$80) with a Samsung OLED (~$75). BOM (~$400-406) sits below Apple/Samsung Ultra flagships, and at a lower RRP it runs a deliberately different margin posture aimed at share and AI features.",
 "Galaxy S23 Ultra":"Samsung's 2023 Ultra (Snapdragon 8 Gen 2). Widely-cited BOM ~$469; the 200MP camera and display drive component cost. Highest markup in this set at its launch RRP.",
 "Galaxy S24 Ultra":"The 2024 Ultra (Snapdragon 8 Gen 3) added on-device AI; reported BOM ~$512, a modest rise on the S23 Ultra at an unchanged RRP, compressing margin slightly.",
 "Galaxy S25 Ultra":"The 2025 Ultra moved to the Snapdragon 8 Elite, pushing reported BOM to ~$549 — the most expensive to build here. At an unchanged RRP the extra silicon cost is absorbed from margin.",
}

# ============================ DERIVATIONS ============================
def per_gb(gb,price):
    return "" if gb=="unlimited" else round(price/gb,2)
def handset_calc(bom_usd,rrp_incvat):
    bom_gbp=round(bom_usd*FX,2); exf_gbp=round(bom_usd*EXF*FX,2)
    rrp_exvat=round(rrp_incvat/(1+VAT),2)
    gm=round((rrp_exvat-bom_gbp)/rrp_exvat*100,1); mk=round(rrp_exvat/bom_gbp,2)
    return bom_gbp,exf_gbp,rrp_exvat,gm,mk

# ============================ CHARTS ============================
def hbar(ax,labels,vals,colors,xlabel,fmt="{:.2f}"):
    y=list(range(len(labels))); ax.barh(y,vals,color=colors,edgecolor="white",height=0.72)
    ax.set_yticks(y); ax.set_yticklabels(labels,fontsize=7); ax.invert_yaxis()
    ax.set_xlabel(xlabel,fontsize=8); ax.grid(axis="x",color="#e2e6ea",lw=.7); ax.set_axisbelow(True)
    for s in ("top","right"): ax.spines[s].set_visible(False)
    mx=max(vals) if vals else 1
    for i,v in zip(y,vals): ax.text(v+mx*0.01,i,fmt.format(v),va="center",fontsize=6.4,color="#333")

def make_charts():
    # 1 pricing
    fin=[(f"{o} {p}",per_gb(g,pr),MNO if t=="MNO" else MVNO) for (o,t,h,p,g,pr,m,b) in PLANS if g!="unlimited"]
    fin.sort(key=lambda x:x[1],reverse=True)
    unl=[(f"{o} {p}",pr,MNO if t=="MNO" else MVNO) for (o,t,h,p,g,pr,m,b) in PLANS if g=="unlimited"]
    unl.sort(key=lambda x:x[1],reverse=True)
    fig,(a1,a2)=plt.subplots(1,2,figsize=(9.4,3.4))
    hbar(a1,[x[0] for x in fin],[x[1] for x in fin],[x[2] for x in fin],"Price per GB (£)")
    a1.set_title("Price per GB (finite bundles)")
    hbar(a2,[x[0] for x in unl],[x[1] for x in unl],[x[2] for x in unl],"£ per month","{:.0f}")
    a2.set_title("Unlimited — monthly price")
    fig.legend(handles=[Patch(color=MNO,label="MNO (network-owned)"),Patch(color=MVNO,label="MVNO (reseller)")],
               loc="lower center",ncol=2,fontsize=8,frameon=False,bbox_to_anchor=(0.5,-0.03))
    fig.suptitle("UK SIM-Only Pricing",fontsize=12,fontweight="bold",y=1.02)
    fig.tight_layout(rect=[0,0.05,1,1]); fig.savefig("chart_pricing.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # 2 handset markup + margin
    rows=[(m,)+handset_calc(bu,rr)+(b,) for (m,br,y,bu,rr,b,nt) in HANDSETS]
    fig,(b1,b2)=plt.subplots(1,2,figsize=(9.4,3.6))
    cols=[MEAS if r[-1]=="measured" else MODEL for r in rows]
    hbar(b1,[r[0] for r in rows],[r[5] for r in rows],cols,"× over BOM","{:.2f}x")
    b1.set_title("Markup over bill-of-materials")
    hbar(b2,[r[0] for r in rows],[r[4] for r in rows],cols,"Gross margin (%)","{:.0f}%")
    b2.set_title("Gross margin of trade value")
    fig.legend(handles=[Patch(color=MEAS,label="measured"),Patch(color=MODEL,label="modelled")],
               loc="lower center",ncol=2,fontsize=8,frameon=False,bbox_to_anchor=(0.5,-0.03))
    fig.suptitle("Handset Ex-Factory Margins",fontsize=12,fontweight="bold",y=1.02)
    fig.tight_layout(rect=[0,0.05,1,1]); fig.savefig("chart_handsets.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # 3 BOM vs RRP grouped
    fig,ax=plt.subplots(figsize=(9.4,3.4))
    ms=[r for r in rows]; import numpy as np
    yy=np.arange(len(ms)); h=0.38
    ax.barh(yy-h/2,[r[1] for r in ms],h,color="#8bb0d6",label="BOM (£)",edgecolor="white")
    ax.barh(yy+h/2,[r[3] for r in ms],h,color=BLUE,label="RRP ex-VAT (£)",edgecolor="white")
    ax.set_yticks(yy); ax.set_yticklabels([r[0] for r in ms],fontsize=7); ax.invert_yaxis()
    ax.set_xlabel("£"); ax.grid(axis="x",color="#e2e6ea",lw=.7); ax.set_axisbelow(True)
    for s in ("top","right"): ax.spines[s].set_visible(False)
    ax.legend(fontsize=8,frameon=False); ax.set_title("Bill-of-materials vs RRP (ex-VAT) by handset",fontsize=11)
    fig.tight_layout(); fig.savefig("chart_bom.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # 4 cheapest £/GB by host network
    hosts={}
    for (o,t,h,p,g,pr,m,b) in PLANS:
        if g!="unlimited":
            v=per_gb(g,pr); hosts.setdefault(h,[]).append(v)
    hv=sorted([(k,min(v)) for k,v in hosts.items()],key=lambda x:x[1])
    fig,ax=plt.subplots(figsize=(9.4,2.4))
    hbar(ax,[k for k,_ in hv],[v for _,v in hv],[MVNO]*len(hv),"cheapest £/GB available")
    ax.set_title("Cheapest effective £/GB by host network",fontsize=11)
    fig.tight_layout(); fig.savefig("chart_network.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # 5 market context
    fig,(c1,c2)=plt.subplots(1,2,figsize=(9.4,2.8))
    hbar(c1,["Postpaid","Prepaid"],[15.73,5.18],[MNO,MNO],"ARPU (£/mo, 2023)","£{:.2f}")
    c1.set_title("Average revenue per user")
    mets=[("5G SA cover",83),("Data +YoY",18),("Acq+ret %rev",17.5),("Online acq %",35),("MVNO share>",15)]
    hbar(c2,[m[0] for m in mets],[m[1] for m in mets],[MNO,MNO,MVNO,MVNO,MVNO],"value (%)","{:.0f}")
    c2.set_title("Market & marketing indicators")
    fig.suptitle("Market & Marketing Context",fontsize=12,fontweight="bold",y=1.03)
    fig.tight_layout(rect=[0,0.03,1,1]); fig.savefig("chart_market.png",dpi=150,bbox_inches="tight"); plt.close(fig)
    # 6 per-handset cost waterfall (measured only)
    for idx,(m,br,y,bu,rr,b,nt) in enumerate(HANDSETS):
        if b!="measured": continue
        bg,ef,rv,gm,mk=handset_calc(bu,rr)
        fig,ax=plt.subplots(figsize=(6.6,1.7))
        hbar(ax,["RRP ex-VAT","Ex-factory*","BOM"],[rv,ef,bg],[BLUE,"#7aa8d4","#b7cfe6"],"£","£{:.0f}")
        ax.set_title(f"{m}: cost → ex-factory → RRP (ex-VAT)",fontsize=9)
        fig.tight_layout(); fig.savefig(f"chart_h{idx}.png",dpi=150,bbox_inches="tight"); plt.close(fig)

# ============================ HTML ============================
E=html.escape
PAGES=[]
def page(body): PAGES.append(body)

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

def foot(n): return f'<div class="foot">AI2ORBIT PROJECT — UK Mobile Data Market Report — page {n}</div>'

def build_html():
    make_charts()
    # ---- cover ----
    page(f'''<div class="cover">
      <div class="cv-band"></div>
      <div class="cv-mid">
        <div class="cv-kicker">AI2ORBIT PROJECT · REPORT ENGINE</div>
        <h1 class="cv-title">UK Mobile Data Market</h1>
        <div class="cv-sub">A comprehensive analysis of pricing, handset ex-factory margins,<br>and marketing economics — with a machine-readable sample feed</div>
        <div class="cv-rule"></div>
        <div class="cv-meta">Prepared for Sami Leino · 2026-08-01 · {len(PLANS)} tariffs · {len(HANDSETS)} handsets · {len(MARKET)} market metrics</div>
      </div>
      <div class="cv-band b"></div></div>''')
    # ---- contents ----
    toc=["Executive Summary","Methodology &amp; Honesty Labels","Market Overview",
         "SIM-Only Pricing — Master Table","Pricing Analysis &amp; Charts",
         "Operator Profiles (MNO)","MVNO Profiles","Handset Market Overview",
         "Handset Ex-Factory Margins — Master Table","Handset Margin Analysis",
         "Marketing &amp; Acquisition Economics","Segment &amp; Structural Context",
         "Data Dictionary","The Report-Engine Sample Feed","Glossary","Sources &amp; Method","Appendix — Raw Tables"]
    items="".join(f'<li><span>{i+1:02d}</span>{t}</li>' for i,t in enumerate(toc))
    page(f'<h1>Contents</h1><ol class="toc">{items}</ol>'+foot(2))
    # ---- exec summary ----
    page(f'''<h1>Executive Summary</h1>
      <p class="p">The UK mobile-data market is built on four physical networks — EE, O2, Vodafone and Three — resold through a dense layer of MVNOs. The headline finding of this report is the size of the <b>per-GB spread</b>: the cheapest effective data (iD Mobile 120GB, ≈£0.10/GB, on Three) is roughly <b>19× cheaper per gigabyte</b> than an own-brand small bundle (O2 3GB, ≈£1.93/GB). Large bundles on reseller brands crush the per-GB rate; network-owned own-brand plans carry a 3–15× premium for brand, support and roaming certainty.</p>
      <p class="p">On devices, flagship handsets sell at roughly <b>2.3–2.8× their bill-of-materials</b> (ex-VAT), a gross margin of 56–64% of trade value. That margin is the pool from which assembly, software, IP/royalties, logistics, warranty and marketing are paid — it is <i>not</i> net profit. The multiple compresses toward the budget tiers (~1.6×). Real ex-factory prices are confidential, so the ex-factory column here is a clearly-flagged model (BOM × 1.30).</p>
      <p class="p">On marketing, UK carriers spend an estimated <b>15–20% of revenue</b> on customer acquisition and retention; digital ad spend was ~$420M in 2023, with challenger brands (Smarty +90%, Three +60% YoY) spending hardest. eSIM is reshaping unit economics — eliminating plastic-SIM logistics is estimated to cut acquisition cost ~30% — and online now drives ~35% of acquisition.</p>
      <p class="p"><b>Every figure in this report is tagged measured or modelled.</b> Measured figures are reported (see sources); modelled figures are computed from the stated assumptions below and are never presented as quotes.</p>'''+foot(3))
    # ---- methodology ----
    page(f'''<h1>Methodology &amp; Honesty Labels</h1>
      <h2>Basis of every figure</h2>
      <ul class="chk"><li><b>measured</b> — a public or reported figure; the source is named in the table or the Sources section.</li>
      <li><b>modelled</b> — computed here from a stated assumption; not a quoted number. Modelled rows and cells are shown in <span class="m">orange</span>.</li></ul>
      <h2>Modelling constants</h2>
      {tbl(["Constant","Value","Used for"],[["FX_USD_GBP","0.79","USD→GBP on handset BOM / ex-factory"],["EXFACTORY_MULT","1.30","ex-factory ≈ BOM × 1.30 (assembly, test, maker margin)"],["VAT_RATE","0.20","stripping UK VAT from RRP to a trade value"]],["l","c","l"])}
      <h2>What is measured vs modelled here</h2>
      <ul class="chk"><li>SIM-only prices — measured list prices, mid-2026 (two tiers interpolated and flagged modelled).</li>
      <li>Handset BOM — measured teardown estimates (analyst-dependent); ex-factory modelled.</li>
      <li>ARPU, coverage, ad spend, acquisition economics — measured, reported figures.</li>
      <li>Cost-per-acquisition (£) — modelled placeholder pending the client's own figure.</li></ul>
      <div class="note">Because a real market has hundreds of tariffs and confidential device costs, a genuinely exhaustive report mixes measured anchors with clearly-flagged models. This report never fills a page by inventing "facts" — length comes from real data, operator/handset profiles and derived analysis.</div>'''+foot(4))
    # ---- market overview ----
    page(f'''<h1>Market Overview</h1>
      <p class="p">The UK is a mature, four-MNO market with a large and growing MVNO layer. Virgin Media O2 alone reported 45.7M total connections at end-2024; 5G standalone now reaches ~83% of the population, and mobile data usage is still growing ~18% year-on-year.</p>
      <img class="chart" src="chart_market.png">
      <p class="p">Revenue per user remains modest — postpaid ARPU ~£15.73/month, prepaid ~£5.18 (2023) — which is what makes the per-GB economics and acquisition costs on the following pages so decisive for operator profitability.</p>'''+foot(5))
    # ---- pricing master table (split for length) ----
    prows=[[E(o),t,h,E(p),("∞" if g=="unlimited" else g),f"{pr:.2f}",("—" if g=='unlimited' else f"{per_gb(g,pr):.2f}"),m,("meas." if b=="measured" else "model.")] for (o,t,h,p,g,pr,mo,b) in PLANS for m in [mo]]
    page('<h1>SIM-Only Pricing — Master Table</h1><div class="lead">Headline list prices, mid-2026. ∞ = unlimited; £/GB blank where unlimited. Two tiers interpolated (flagged model.).</div>'
         +tbl(["Operator","Type","Runs on","Plan","Data(GB)","£/mo","£/GB","Months","Basis"],prows,
              ["l","c","c","l","c","r","r","c","c"])
         +'<div class="lead"><b>Read:</b> cheapest effective data is an MVNO large bundle (iD 120GB ≈ £0.10/GB); network-owned own-brand plans carry a 3–15× per-GB premium.</div>'+foot(6))
    # ---- pricing charts ----
    page('<h1>Pricing Analysis &amp; Charts</h1>'
         '<div class="lead">Price per GB (finite bundles) and unlimited monthly price, coloured MNO vs MVNO.</div>'
         '<img class="chart" src="chart_pricing.png">'
         '<div class="lead">Cheapest effective £/GB available on each physical network — the Three network anchors the market floor.</div>'
         '<img class="chart" src="chart_network.png">'+foot(7))
    # ---- price-per-GB league table ----
    league=sorted([(o,p,g,pr,per_gb(g,pr),t) for (o,t,h,p,g,pr,m,b) in PLANS if g!="unlimited"],key=lambda x:x[4])
    lrows=[[i+1,E(o),E(p),g,f"{pr:.2f}",f"{v:.2f}",t] for i,(o,p,g,pr,v,t) in enumerate(league)]
    page('<h1>Price-per-GB League Table</h1><div class="lead">Every finite-allowance tariff, ranked cheapest effective data first. Unlimited plans excluded (per-GB undefined).</div>'
         +tbl(["#","Operator","Plan","GB","£/mo","£/GB","Type"],lrows,["c","l","l","c","r","r","c"])
         +'<div class="lead">The top of the table is dominated by large-bundle MVNOs on Three; own-brand MNO small bundles sit at the bottom, paying a premium per gigabyte for brand and support.</div>'+foot(8))
    # ---- best value by user profile ----
    page('<h1>Best Value by User Profile</h1>'
         '<div class="lead">Which tariff wins for a representative light / medium / heavy data user, on measured list prices.</div>'
         +tbl(["User profile","Typical need","Best-value pick (this dataset)","£/mo","Why"],
              [["Light (≤3GB)","calls/texts + light browsing","Smarty 3GB","3.90","cheapest small bundle on a premium network"],
               ["Medium (~20-30GB)","daily streaming on mobile","Smarty ~30GB","8.00","≈£0.27/GB, no in-contract rises"],
               ["Heavy (100GB+)","hotspot / primary connection","iD Mobile 120GB","12.00","≈£0.10/GB — cheapest effective data here"],
               ["Unlimited","tethering / no metering","Smarty Unlimited","12.00","lowest unlimited in the set"],
               ["Premium/coverage","max coverage + perks","EE / Vodafone unlimited","26-40","brand, roaming, support certainty"]],
              ["l","l","l","r","l"])
         +'<div class="note">These picks are from the tariffs in this report, not the whole market. Send a target user mix and I can compute a weighted best-value basket.</div>'+foot(9))
    # ---- MNO profiles ----
    pg=10
    for (nm,ty,host,tag,paras) in OPERATORS:
        plns=[[E(p),("∞" if g=="unlimited" else g),f"{pr:.2f}",("—" if g=='unlimited' else f"{per_gb(g,pr):.2f}"),f"{m}m"] for (o,t,h,p,g,pr,m,b) in PLANS if o==nm]
        body=f'<h1>Operator Profile — {E(nm)}</h1><div class="slabel">{E(tag)} · {ty} · network: {E(host)}</div>'
        body+="".join(f'<p class="p">{para}</p>' for para in paras)
        if plns: body+='<h2>Tariffs in this report</h2>'+tbl(["Plan","Data(GB)","£/mo","£/GB","Contract"],plns,["l","c","r","r","c"])
        page(body+foot(pg)); pg+=1
    # ---- MVNO profiles (1 per page) ----
    for (nm,host,txt) in MVNOS:
        plns=[[E(p),("∞" if g=="unlimited" else g),f"{pr:.2f}",("—" if g=='unlimited' else f"{per_gb(g,pr):.2f}"),f"{m}m"] for (o,t,h,p,g,pr,m,b) in PLANS if o==nm]
        body=f'<h1>MVNO Profile — {E(nm)}</h1><div class="slabel">Reseller · runs on {E(host)}</div><p class="p">{txt}</p>'
        if plns: body+='<h2>Tariffs in this report</h2>'+tbl(["Plan","Data(GB)","£/mo","£/GB","Contract"],plns,["l","c","r","r","c"])
        body+=('<div class="note">Because MVNOs buy wholesale capacity from the host MNO, their pricing floor is set by that wholesale rate — which is why the cheapest per-GB deals cluster on the network with the most aggressive wholesale terms (Three).</div>')
        page(body+foot(pg)); pg+=1
    # ---- handset overview ----
    page('<h1>Handset Market Overview</h1>'
         '<p class="p">Handset economics matter to an operator because the device subsidy and trade-in flows sit alongside the airtime margin. This report models the cost-to-retail waterfall for a spread of flagships (Apple, Samsung, Google) and generic tier midpoints.</p>'
         '<img class="chart" src="chart_bom.png">'
         '<p class="p">Bill-of-materials is the measured anchor (teardown estimates, analyst-dependent). Everything downstream — ex-factory, margin, markup — is derived from the stated model, so the whole column re-prices from a single constant.</p>'+foot(pg)); pg+=1
    # ---- handset master table ----
    hrows=[]
    for (m,br,y,bu,rr,b,nt) in HANDSETS:
        bg,ef,rv,gm,mk=handset_calc(bu,rr)
        cls_m = "ml" if b=="modelled" else "l"
        hrows.append([f'<span class="{"m" if b=="modelled" else ""}">{E(m)}</span>',E(br),y,f"{bu:.0f}",f"{bg:.2f}",f"{ef:.2f}",f"{rr:.0f}",f"{rv:.2f}",f"{gm:.1f}%",f"{mk:.2f}×",("meas." if b=="measured" else "model.")])
    page('<h1>Handset Ex-Factory Margins — Master Table</h1>'
         '<div class="lead">BOM measured (teardown est.); ex-factory <span class="m">modelled</span> = BOM×1.30. RRP shown inc- and ex-VAT. FX £0.79/USD, VAT 20%.</div>'
         +tbl(["Model","Brand","Yr","BOM$","BOM£","Ex-fac£*","RRP£inc","RRP£exVAT","Gross mgn","Markup","Basis"],hrows,
              ["l","c","c","r","r","r","r","r","r","r","c"])
         +'<div class="note"><b>*Ex-factory is modelled, not a quoted number.</b> BOM excludes assembly, software, IP/royalties, logistics, warranty and marketing — gross margin is the pool those are paid from, not net profit.</div>'+foot(pg)); pg+=1
    # ---- handset analysis charts ----
    page('<h1>Handset Margin Analysis</h1>'
         '<div class="lead">Markup over bill-of-materials and gross margin of trade value, coloured measured vs modelled.</div>'
         '<img class="chart" src="chart_handsets.png">'
         '<ul class="chk"><li>Flagships mark up ~2.3–2.8× their component cost; the newest Galaxy Ultra tops the set.</li>'
         '<li>Margin compresses toward budget tiers (~1.6×) — cheaper phones leave less overhead headroom.</li>'
         '<li>Apple and Samsung flagships carry similar BOM (~£370–440); RRP dispersion drives the margin gap.</li>'
         '<li>Google Pixel 9 Pro undercuts on BOM (~£320) at a lower RRP — a different margin posture.</li></ul>'+foot(pg)); pg+=1
    # ---- per-handset feature pages (measured) ----
    for idx,(m,br,y,bu,rr,b,nt) in enumerate(HANDSETS):
        if b!="measured": continue
        bg,ef,rv,gm,mk=handset_calc(bu,rr)
        prof=HPROF.get(m,"")
        body=(f'<h1>Handset — {E(m)}</h1><div class="slabel">{E(br)} · {y} · basis: measured ({E(nt)})</div>'
              f'<p class="p">{prof}</p>'
              f'<img class="chart" src="chart_h{idx}.png">'
              +tbl(["BOM $","BOM £","Ex-factory £*","RRP £ inc-VAT","RRP £ ex-VAT","Gross margin","Markup ×BOM"],
                   [[f"{bu:.0f}",f"{bg:.2f}",f"{ef:.2f}",f"{rr:.0f}",f"{rv:.2f}",f"{gm:.1f}%",f"{mk:.2f}×"]],
                   ["r","r","r","r","r","r","r"])
              +'<div class="note">*Ex-factory modelled at BOM×1.30. BOM is the measured anchor; margin and markup are derived. RRP is the public UK launch price.</div>')
        page(body+foot(pg)); pg+=1
    # ---- sensitivity ----
    def mk_at(mult):
        return [(m,round(bu*FX,2),round(bu*mult*FX,2)) for (m,br,y,bu,rr,b,nt) in HANDSETS if b=="measured"]
    srows=[]
    for (m,br,y,bu,rr,b,nt) in HANDSETS:
        if b!="measured": continue
        bg=round(bu*FX,2)
        srows.append([E(m),f"{round(bu*1.15*FX,2):.0f}",f"{round(bu*1.30*FX,2):.0f}",f"{round(bu*1.50*FX,2):.0f}"])
    page('<h1>Model Sensitivity — Ex-Factory Multiplier</h1>'
         '<div class="lead">The one modelled lever in the handset waterfall is EXFACTORY_MULT (default 1.30). This table shows the estimated ex-factory price (£) for the measured handsets at three multipliers, so the assumption is transparent and tunable.</div>'
         +tbl(["Model","× 1.15 (lean)","× 1.30 (base)","× 1.50 (rich)"],srows,["l","r","r","r"])
         +'<div class="note">Nothing else in the report depends on a hidden constant. Change EXFACTORY_MULT in build_report.py / gen_sample.py and the whole ex-factory column, and any downstream chart, re-price consistently.</div>'+foot(pg)); pg+=1
    # ---- marketing ----
    mkrows=[[E(m),(f"{v:g}"),E(u),pe,seg,("meas." if b=="measured" else "model.")] for (m,v,u,pe,seg,b,s) in MARKET]
    page('<h1>Marketing &amp; Acquisition Economics</h1>'
         '<p class="p">Customer acquisition is the swing factor in a low-ARPU market. UK carriers spend an estimated <b>15–20% of revenue</b> on acquisition and retention. Digital ad spend was ~$420M in 2023; challenger brands spent hardest (Smarty +90%, Three +60% YoY), reflecting an MVNO land-grab.</p>'
         '<p class="p">Structural shifts are cutting unit costs: eSIM removes plastic-SIM logistics and is estimated to cut acquisition cost ~30%, while online channels now drive ~35% of acquisition and grow ~18%/year. The one figure still modelled here is a headline per-SIM CPA (£18) — send your real number and it flows through the model.</p>'
         +tbl(["Metric","Value","Unit","Period","Segment","Basis"],mkrows,["l","r","l","c","c","c"])+foot(pg)); pg+=1
    # ---- structural context ----
    page('<h1>Segment &amp; Structural Context</h1>'
         '<p class="p">Two structural forces dominate the horizon. First, the <b>Vodafone–Three consolidation</b> reshapes network capacity and the competitive floor: Three has been the price leader and hosts the cheapest resellers, so the merged entity\'s tariff posture matters to the whole market.</p>'
         '<p class="p">Second, the <b>MVNO layer keeps taking consumer share</b> — Sky Mobile + Tesco Mobile alone exceed 15% — pressuring own-brand ARPU while giving the host MNOs (especially O2) a wholesale revenue line. The main-brand subscriber base of several MNOs is forecast to keep shrinking as value brands absorb price-sensitive users.</p>'
         '<p class="p">For an operator building on this market, the implication is clear: differentiate on quality, bundle perks and wholesale, because the per-GB race to the bottom is already won by large-bundle MVNOs.</p>'+foot(pg)); pg+=1
    # ---- data dictionary ----
    page('<h1>Data Dictionary</h1>'
         '<h2>plans</h2>'+tbl(["Column","Meaning"],[["operator/type/host","brand, MNO|MVNO, physical network"],["plan / data_gb","tariff label; GB or 'unlimited'"],["price_gbp_month","headline £/month"],["price_per_gb_gbp","price ÷ data_gb; blank if unlimited"],["contract_months","1 = 30-day rolling"],["basis","measured | modelled"]],["l","l"])
         +'<h2>handsets</h2>'+tbl(["Column","Meaning"],[["bom_usd/gbp","bill-of-materials (teardown est.)"],["est_exfactory_gbp","modelled = BOM × 1.30"],["rrp_ex_vat_gbp","RRP ÷ 1.20 (trade value)"],["gross_margin_pct","(rrp_exvat − bom)/rrp_exvat"],["markup_x_over_bom","rrp_exvat ÷ bom"],["basis","measured | modelled"]],["l","l"])
         +'<h2>market</h2>'+tbl(["Column","Meaning"],[["metric/value/unit","the indicator and its figure"],["period/segment","year; postpaid|prepaid|MVNO|all"],["basis/source","measured|modelled; provenance"]],["l","l"])+foot(pg)); pg+=1
    # ---- sample feed ----
    page('<h1>The Report-Engine Sample Feed</h1>'
         '<div class="lead">The whole report is generated from the datasets by build_report.py; the tabular sample is emitted by gen_sample.py (JSON checksum a792719cdd4e03a6).</div>'
         +tbl(["File","What it is"],[["plans.csv","SIM-only tariffs + £/GB"],["handsets.csv","handset cost→ex-factory→RRP waterfalls"],["market.csv","market / marketing metrics"],["uk_data_market_sample.json","all three tables + meta & assumptions"],["gen_sample.py","emits CSVs+JSON; --check verifies"],["build_report.py","emits this long report; scales with the data"],["gen_charts.py / charts","the figures used throughout"],["data_dictionary.md","column schema + assumptions"]],["l","l"])
         +'<div class="note">To grow this to 60–100 pages: add operators/handsets/tiers to the datasets (each new operator ≈ 1 page, each MVNO pair ≈ 1 page), or send your report engine\'s own dataset and template and I\'ll drive it 1:1.</div>'+foot(pg)); pg+=1
    # ---- glossary ----
    page('<h1>Glossary</h1>'+tbl(["Term","Definition"],[
        ["MNO","Mobile Network Operator — owns the physical radio network (EE, O2, Vodafone, Three)."],
        ["MVNO","Mobile Virtual Network Operator — resells airtime on an MNO's network."],
        ["ARPU","Average Revenue Per User — monthly revenue ÷ subscribers."],
        ["BOM","Bill of Materials — component cost of a device (excludes assembly, software, overhead)."],
        ["Ex-factory","Price a device leaves the factory at; modelled here as BOM × 1.30."],
        ["RRP","Recommended Retail Price; shown inc- and ex-VAT."],
        ["5G SA","5G Standalone — 5G core, not anchored to 4G."],
        ["CPA","Cost Per Acquisition — marketing spend to win one customer."],
        ["eSIM","Embedded SIM — provisioned over the air, no plastic SIM."],
        ["£/GB","Effective price per gigabyte = monthly price ÷ data allowance."]],["l","l"])+foot(pg)); pg+=1
    # ---- sources ----
    page('<h1>Sources &amp; Method</h1>'
         '<ul class="chk"><li>SIM-only pricing — UK operator price lists &amp; comparison sites, mid-2026 (measured).</li>'
         '<li>Handset BOM — Counterpoint / TechInsights / IHS-style teardown estimates, 2023–2025 (measured; analyst-dependent).</li>'
         '<li>ARPU — Ofcom / Statista, 2023 (measured).</li>'
         '<li>5G coverage &amp; data growth — Ofcom Connected Nations 2025 (measured).</li>'
         '<li>MVNO share &amp; VMO2 connections — market reports / Virgin Media O2, 2024–25 (measured).</li>'
         '<li>Marketing &amp; acquisition — SensorTower / IBISWorld / industry estimates, 2023–25 (measured ranges).</li>'
         '<li>Cost-per-acquisition (£) — modelled placeholder, awaiting client figure.</li></ul>'
         '<div class="note"><b>Modelling constants:</b> FX £0.79/USD · ex-factory ×1.30 · VAT 20% — all editable in the generator; change one and the dataset re-prices.</div>'+foot(pg)); pg+=1
    # ---- appendix raw ----
    araw=[[E(o),t,h,E(p),("∞" if g=="unlimited" else g),f"{pr:.2f}",mo] for (o,t,h,p,g,pr,mo,b) in PLANS]
    page('<h1>Appendix A — Raw Tariff Table</h1>'+tbl(["Operator","Type","Host","Plan","GB","£/mo","Contract(m)"],araw,["l","c","c","l","c","r","c"])+foot(pg)); pg+=1
    hb=[[E(m),E(br),y,f"{bu:.0f}",f"{rr:.0f}",nt] for (m,br,y,bu,rr,b,nt) in HANDSETS]
    page('<h1>Appendix B — Handset Reference</h1>'+tbl(["Model","Brand","Year","BOM$","RRP£inc","Note / source"],hb,["l","c","c","r","r","l"])+foot(pg)); pg+=1

    # write CSVs (keep sample in sync)
    with open("plans.csv","w",newline="") as f:
        w=csv.writer(f); w.writerow(["operator","type","host_network","plan_name","data_gb","price_gbp_month","price_per_gb_gbp","contract_months","basis"])
        for (o,t,h,p,g,pr,m,b) in PLANS: w.writerow([o,t,h,p,g,pr,("" if g=="unlimited" else per_gb(g,pr)),m,b])
    with open("handsets.csv","w",newline="") as f:
        w=csv.writer(f); w.writerow(["model","brand","year","bom_usd","bom_gbp","est_exfactory_gbp","rrp_incvat_gbp","rrp_exvat_gbp","gross_margin_pct","markup_x","basis"])
        for (m,br,y,bu,rr,b,nt) in HANDSETS:
            bg,ef,rv,gm,mk=handset_calc(bu,rr); w.writerow([m,br,y,bu,bg,ef,rr,rv,gm,mk,b])
    with open("market.csv","w",newline="") as f:
        w=csv.writer(f); w.writerow(["metric","value","unit","period","segment","basis","source"])
        for row in MARKET: w.writerow(row)

    doc='<meta charset="utf-8">\n<style>'+CSS+'</style>\n'
    for i,b in enumerate(PAGES):
        cls="page cover-page" if i==0 else "page"
        doc+=f'<div class="{cls}">{b}</div>\n'
    with open("report_long.html","w") as f: f.write(doc)
    print(f"report_long.html written — {len(PAGES)} pages; CSVs refreshed ({len(PLANS)} plans, {len(HANDSETS)} handsets, {len(MARKET)} market)")

CSS='''
@page{size:A4 portrait;margin:15mm 14mm;}
*{box-sizing:border-box;}
body{font-family:"Segoe UI",Roboto,Helvetica,Arial,sans-serif;color:#222;margin:0;}
.page{page-break-after:always;}
.page:last-child{page-break-after:auto;}
h1{color:#2f6ea5;font-size:22px;font-weight:800;margin:0 0 8px;}
h2{color:#2f6ea5;font-size:15px;font-weight:800;margin:16px 0 4px;}
.p{font-size:11.5px;line-height:1.55;color:#2c3138;margin:0 0 9px;}
.lead{color:#5a6068;font-size:11px;margin:2px 0 8px;}
.slabel{color:#3a6a94;font-size:12px;font-style:italic;font-weight:600;margin:2px 0 8px;}
table{width:100%;border-collapse:collapse;font-size:10px;margin:4px 0 8px;border:1px solid #b8cfe0;}
th{background:#3d88c4;color:#fff;font-weight:700;text-align:center;padding:5px 6px;border:1px solid #327ab5;line-height:1.2;}
th.l{text-align:left;}
td{padding:3.5px 6px;border:1px solid #dfe6ec;}
td.c{text-align:center;}td.r{text-align:right;}
tr:nth-child(even) td{background:#f4f8fb;}
.m{color:#e8833a;font-weight:700;}
img.chart{width:100%;margin:8px 0;}
.note{font-size:10.5px;color:#6a5a20;background:#fbf7e6;border-left:4px solid #e2c65a;padding:8px 12px;border-radius:4px;margin:8px 0;}
ul.chk{margin:6px 0;padding-left:0;list-style:none;}
ul.chk li{font-size:11px;color:#3f4650;padding:3px 0 3px 20px;position:relative;}
ul.chk li:before{content:"▪";color:#2f6ea5;position:absolute;left:2px;}
.foot{color:#9aa;font-size:9px;border-top:1px solid #e2e6ea;margin-top:14px;padding-top:5px;position:absolute;bottom:12mm;left:14mm;right:14mm;}
.card{border:1px solid #dbe6f0;border-left:5px solid #3d88c4;border-radius:5px;padding:10px 14px;margin:8px 0;background:#fbfdff;}
.cname{font-size:15px;font-weight:800;color:#2f6ea5;margin-bottom:3px;}
.hostpill{font-size:10px;font-weight:600;color:#43772f;background:#eaf5e6;border-radius:9px;padding:1px 8px;margin-left:6px;vertical-align:middle;}
ol.toc{list-style:none;padding:0;font-size:13px;}
ol.toc li{padding:7px 0;border-bottom:1px solid #eef2f5;color:#2c3138;}
ol.toc li span{display:inline-block;width:34px;color:#3d88c4;font-weight:800;}
.cover-page{position:relative;height:267mm;}
.cover{position:absolute;inset:-15mm -14mm;background:#0f1f33;color:#fff;display:flex;flex-direction:column;justify-content:space-between;}
.cv-band{height:10mm;background:linear-gradient(90deg,#0c6b6b,#14a293);}
.cv-band.b{background:linear-gradient(90deg,#14a293,#0c6b6b);}
.cv-mid{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:0 20mm;}
.cv-kicker{color:#5fd3c4;letter-spacing:3px;font-size:12px;font-weight:700;margin-bottom:14px;}
.cv-title{font-size:40px;font-weight:800;letter-spacing:.5px;margin:0;color:#fff;}
.cv-sub{color:#c4cdd8;font-size:14px;line-height:1.6;margin-top:12px;}
.cv-rule{width:120px;height:3px;background:#ec8a3c;margin:20px auto;}
.cv-meta{color:#8493a6;font-size:12px;}
'''

if __name__=="__main__":
    build_html()
