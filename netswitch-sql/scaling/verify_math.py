#!/usr/bin/env python3
# verify_math.py — reproduces exactly what the T-SQL functions in ggw_scaling.sql
# return, so you can check the scaling maths WITHOUT a database. Run: python3 verify_math.py
import math

def ddtm_constant():                 # dbo.ddtm_constant() — the precompute-once value
    return -0.090908889

def ddtm_constant_calc(T1, T2, LogLogRad, mg1=7/9, mg2=1/6, watts=120.0):
    # dbo.ddtm_constant_calc — your formula structure; give real T1/T2/LogLogRad to lock it
    inner = (T1 * mg1 * LogLogRad) / (pow(pow(T2, T1), mg2))
    if inner < 0: return None
    val = math.sqrt(inner) / watts
    return math.log10(val) if val > 0 else None

def thrice_sinwave(t, k, f=(440.0,660.0,880.0), a=(1.0,0.6,0.3)):
    return abs(k) * sum(a[i]*math.sin(2*math.pi*f[i]*t) for i in range(3))

def lnlog(x):                         # dbo.lnlog — ln of ln
    return math.log(math.log(x)) if x > 1 else None

def hyper_downscale(coef, factor):    # dbo.hyper_downscale
    return coef/factor if factor else coef

if __name__ == "__main__":
    print("ddtm_constant()             =", ddtm_constant())
    print("ddtm_constant_calc(1,2,1)   =", round(ddtm_constant_calc(1.0,2.0,1.0),6), "(placeholder inputs)")
    print("thrice_sinwave sweep (k=const):")
    for t in (0.0,0.001,0.002,0.003):
        print(f"   t={t}: s={round(thrice_sinwave(t, ddtm_constant()),6)}")
    print("lnlog(15.154)  ~ ln(ln(e^e)) =", round(lnlog(15.154),6))
    print("lnlog(48059)                 =", round(lnlog(48059),6))
    print("binary of 48059              =", format(48059,'016X'))
    print("hyper_downscale(1042.496, 8) =", round(hyper_downscale(1042.496,8),4))
