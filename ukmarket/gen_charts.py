#!/usr/bin/env python3
"""Charts for the UK Data Market report in the FMA/Kenneth report-engine style
(matplotlib horizontal bars, coloured by category, side-by-side subplots + legend)."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

MNO="#4a90c2"; MVNO="#43a047"; ORANGE="#ef8a3c"; PURPLE="#8e44ad"
MEAS="#4a90c2"; MODEL="#ef8a3c"
plt.rcParams.update({"font.size":8, "axes.titlesize":10, "axes.titleweight":"bold"})

def hbar(ax, labels, vals, colors, xlabel, fmt="{:.2f}"):
    y=range(len(labels))
    ax.barh(list(y), vals, color=colors, edgecolor="white", height=0.7)
    ax.set_yticks(list(y)); ax.set_yticklabels(labels, fontsize=7)
    ax.invert_yaxis(); ax.set_xlabel(xlabel, fontsize=8)
    ax.grid(axis="x", color="#e2e6ea", linewidth=.7); ax.set_axisbelow(True)
    for s in ("top","right"): ax.spines[s].set_visible(False)
    mx=max(vals)
    for i,v in zip(y,vals):
        ax.text(v+mx*0.01, i, fmt.format(v), va="center", fontsize=6.6, color="#333")

# ---- Chart 1: SIM-only pricing ----
plans=[("O2 3GB",1.93,MNO),("ASDA 3GB",1.67,MVNO),("Tesco 5GB",1.50,MVNO),
       ("Lebara 5GB",1.00,MVNO),("Three 10GB",0.60,MNO),("Smarty 30GB",0.27,MVNO),
       ("iD 120GB",0.10,MVNO)]
monthly=[("EE Unltd",40,MNO),("Vodafone Unltd",26,MNO),("Three Unltd",22,MNO),
         ("iD Unltd",16,MVNO),("Voxi Unltd",15,MVNO),("Smarty Unltd",12,MVNO)]
fig,(a1,a2)=plt.subplots(1,2,figsize=(9.4,3.2))
hbar(a1,[p[0] for p in plans],[p[1] for p in plans],[p[2] for p in plans],"Price per GB (£)")
a1.set_title("Price per GB")
hbar(a2,[p[0] for p in monthly],[p[1] for p in monthly],[p[2] for p in monthly],"£ per month","{:.0f}")
a2.set_title("Unlimited — monthly price")
fig.legend(handles=[Patch(color=MNO,label="MNO (network-owned)"),Patch(color=MVNO,label="MVNO (reseller)")],
           loc="lower center", ncol=2, fontsize=8, frameon=False, bbox_to_anchor=(0.5,-0.04))
fig.suptitle("UK SIM-Only Pricing", fontsize=12, fontweight="bold", y=1.02)
fig.tight_layout(rect=[0,0.05,1,1]); fig.savefig("chart_pricing.png",dpi=150,bbox_inches="tight"); plt.close(fig)

# ---- Chart 2: handset margins ----
hs=[("iPhone 15 Pro Max",2.27,55.9,MEAS),("Galaxy S23 Ultra",2.81,64.4,MEAS),
    ("Flagship*",2.33,57.1,MODEL),("Mid-range*",1.83,45.4,MODEL),("Budget*",1.60,37.4,MODEL)]
fig,(b1,b2)=plt.subplots(1,2,figsize=(9.4,2.9))
hbar(b1,[h[0] for h in hs],[h[1] for h in hs],[h[3] for h in hs],"× RRP(ex-VAT) over BOM","{:.2f}x")
b1.set_title("Markup over bill-of-materials")
hbar(b2,[h[0] for h in hs],[h[2] for h in hs],[h[3] for h in hs],"Gross margin (%)","{:.0f}%")
b2.set_title("Gross margin of trade value")
fig.legend(handles=[Patch(color=MEAS,label="measured"),Patch(color=MODEL,label="modelled (*)")],
           loc="lower center", ncol=2, fontsize=8, frameon=False, bbox_to_anchor=(0.5,-0.06))
fig.suptitle("Handset Ex-Factory Margins", fontsize=12, fontweight="bold", y=1.03)
fig.tight_layout(rect=[0,0.06,1,1]); fig.savefig("chart_handsets.png",dpi=150,bbox_inches="tight"); plt.close(fig)

# ---- Chart 3: market context ----
fig,(c1,c2)=plt.subplots(1,2,figsize=(9.4,2.6))
hbar(c1,["Postpaid","Prepaid"],[15.73,5.18],[MNO,MNO],"ARPU (£/month, 2023)","£{:.2f}")
c1.set_title("Average revenue per user")
mets=[("5G SA coverage",83),("Data usage +YoY",18),("MVNO share (>)",15),("Mktg %rev*",12)]
hbar(c2,[m[0] for m in mets],[m[1] for m in mets],[MNO,MNO,MVNO,MODEL],"value (%)","{:.0f}%")
c2.set_title("Market indicators (%)")
fig.legend(handles=[Patch(color=MODEL,label="modelled placeholder (*)")],
           loc="lower center", ncol=1, fontsize=8, frameon=False, bbox_to_anchor=(0.5,-0.08))
fig.suptitle("Market & Marketing Context", fontsize=12, fontweight="bold", y=1.04)
fig.tight_layout(rect=[0,0.07,1,1]); fig.savefig("chart_market.png",dpi=150,bbox_inches="tight"); plt.close(fig)
print("charts written: chart_pricing.png chart_handsets.png chart_market.png")
