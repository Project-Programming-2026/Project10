"""組み方と推定方法の対照実験のグラフ（SVG）と表（summary.md）を作る。標準ライブラリだけを使う。
使い方：python3 plot.py results/mse.csv 出力ディレクトリ
- 各runの値は20評価時点（10〜200試合）のMSEの平均。差は試行ごとの相対差（対応のある比較）の平均で、
  sdは標本標準偏差、範囲は試行間の最小〜最大。
- 色は手法に固定し、既定パレットの検証済みの順（青・橙・水色・黄・桃）に割り当てる。
  一部の色は白地とのコントラストが3:1未満のため、すべての棒・線に名前と値のラベルを付ける。
"""
import csv
import statistics as st
import sys
from collections import defaultdict
from html import escape
from pathlib import Path

ORDER = ["Glicko2", "WholeHistory", "Glicko", "Elo", "TrueSkill"]
LABEL = {"Glicko2": "Glicko-2", "WholeHistory": "全履歴推定", "Glicko": "Glicko", "Elo": "Elo",
         "TrueSkill": "TrueSkill"}
COLOR = {"Glicko2": "#257de0", "WholeHistory": "#f47230", "Glicko": "#00b894", "Elo": "#f2a600",
         "TrueSkill": "#ee7fac"}
TEXT, TEXT2, GRID, AXIS = "#191919", "#666666", "#e5e5e5", "#999999"
FONT = "Hiragino Sans, 'Hiragino Kaku Gothic ProN', 'Noto Sans CJK JP', 'Yu Gothic', sans-serif"
CHECKPOINTS = list(range(10, 201, 10))

rows = list(csv.DictReader(open(sys.argv[1], encoding="utf-8")))
out = Path(sys.argv[2])
(out / "figures").mkdir(parents=True, exist_ok=True)

# (phase, matcher, lambda, estimator) -> trial -> {checkpoint: mse}
curves = defaultdict(lambda: defaultdict(dict))
for r in rows:
    curves[(r["phase"], r["matcher"], r["lambda"], r["estimator"])][int(r["trial"])][int(r["matches_per_player"])] = \
        float(r["mse"])
for key, trials in curves.items():
    for t, c in trials.items():
        assert sorted(c) == CHECKPOINTS, (key, t)


def means(key):
    """試行 -> 20時点のMSEの平均。"""
    return {t: sum(c.values()) / len(c) for t, c in curves[key].items()}


def paired(a, b):
    """bに対するaの試行ごとの相対差（%）の要約。"""
    ma, mb = means(a), means(b)
    assert sorted(ma) == sorted(mb), (a, b)
    d = [(ma[t] / mb[t] - 1) * 100 for t in sorted(ma)]
    return {"mean": st.mean(d), "sd": st.stdev(d), "min": min(d), "max": max(d),
            "better": sum(v < 0 for v in d), "n": len(d)}


def fmt(p):
    return f"{p['mean']:+.1f}%（sd {p['sd']:.1f}、{p['min']:+.1f}〜{p['max']:+.1f}、改善 {p['better']}/{p['n']}）"


def avg(key):
    return st.mean(means(key).values())


# ---------- SVGの部品 ----------
class Svg:
    def __init__(self, width, height, title, subtitle):
        self.w, self.h = width, height
        self.parts = [f'<rect width="{width}" height="{height}" fill="#ffffff"/>']
        self.text(24, 34, title, size=18, weight="bold")
        self.text(24, 56, subtitle, size=13, color=TEXT2)

    def text(self, x, y, s, size=13, color=TEXT, anchor="start", weight="normal"):
        self.parts.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{color}" '
                          f'text-anchor="{anchor}" font-weight="{weight}">{escape(s)}</text>')

    def line(self, x1, y1, x2, y2, color=GRID, width=1, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{color}" '
                          f'stroke-width="{width}"{d}/>')

    def raw(self, s):
        self.parts.append(s)

    def save(self, name, desc):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" font-family="{FONT}" role="img">'
                f'<title>{escape(desc)}</title>'
                '<defs><pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" '
                'patternTransform="rotate(45)"><rect width="6" height="6" fill="#ffffff" fill-opacity="0.55"/>'
                '<line x1="0" y1="0" x2="0" y2="6" stroke="#ffffff" stroke-width="2.5"/></pattern></defs>')
        (out / "figures" / name).write_text(head + "".join(self.parts) + "</svg>\n", encoding="utf-8")


def bar(svg, x0, x1, y, h, color, hatch, tip):
    """x0（基準）からx1までの横棒。データ側の端だけ4pxの角丸にする。"""
    left, right = min(x0, x1), max(x0, x1)
    w = max(right - left, 1)
    r = min(4, w / 2, h / 2)
    if x1 >= x0:
        d = (f"M{left:.1f},{y:.1f} H{right - r:.1f} Q{right:.1f},{y:.1f} {right:.1f},{y + r:.1f} "
             f"V{y + h - r:.1f} Q{right:.1f},{y + h:.1f} {right - r:.1f},{y + h:.1f} H{left:.1f} Z")
    else:
        d = (f"M{right:.1f},{y:.1f} H{left + r:.1f} Q{left:.1f},{y:.1f} {left:.1f},{y + r:.1f} "
             f"V{y + h - r:.1f} Q{left:.1f},{y + h:.1f} {left + r:.1f},{y + h:.1f} H{right:.1f} Z")
    svg.raw(f'<g><title>{escape(tip)}</title><path d="{d}" fill="{color}"/>'
            + (f'<path d="{d}" fill="url(#hatch)"/>' if hatch else "") + "</g>")


def nice_ticks(lo, hi, count=5):
    span = hi - lo
    raw = span / count
    mag = 10 ** __import__("math").floor(__import__("math").log10(raw))
    step = min((m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw), default=10 * mag)
    start = __import__("math").floor(lo / step) * step
    ticks = []
    v = start
    while v <= hi + step * 1e-9:
        if v >= lo - step * 1e-9:
            ticks.append(round(v, 10))
        v += step
    return ticks


def legend_fill(svg, x, y, items):
    """items: (色, 斜線か, 名前)。"""
    for color, hatch, name in items:
        svg.raw(f'<rect x="{x}" y="{y - 10}" width="14" height="12" rx="2" fill="{color}"/>')
        if hatch:
            svg.raw(f'<rect x="{x}" y="{y - 10}" width="14" height="12" rx="2" fill="url(#hatch)"/>')
        svg.text(x + 20, y, name, size=12)
        x += 28 + 13 * len(name)


# ---------- 図1：2×2の平均MSE ----------
base = ("factorial", "random", "0", "Glicko2")
cells = [("random", "0", "Glicko2", "ランダム＋Glicko-2（基準）"),
         ("WholeHistory", "0.5", "Glicko2", "近い人どうし＋Glicko-2（組み方だけ変更）"),
         ("random", "0", "WholeHistory", "ランダム＋全履歴推定（推定方法だけ変更）"),
         ("WholeHistory", "0.5", "WholeHistory", "近い人どうし＋全履歴推定（両方変更）")]
svg = Svg(1000, 370, "組み方と推定方法を1つずつ変えた対照実験",
          "20時点のMSEの平均（評価用10試行、各人200試合）。点は各試行、棒は10試行の平均。短いほど良い")
X0, X1, Y0 = 330, 700, 92
xmax = 0.0075
for i, (matcher, lam, est, name) in enumerate(cells):
    key = ("factorial", matcher, lam, est)
    y = Y0 + i * 52
    m = avg(key)
    diff = "" if matcher == "random" and est == "Glicko2" else f"　基準比 {paired(key, base)['mean']:+.1f}%"
    svg.text(X0 - 12, y + 19, name, anchor="end")
    bar(svg, X0, X0 + (X1 - X0) * m / xmax, y + 4, 24, COLOR[est], matcher != "random", f"{name}: {m:.7f}")
    for t, v in means(key).items():
        cx = X0 + (X1 - X0) * v / xmax
        svg.raw(f'<circle cx="{cx:.1f}" cy="{y + 16}" r="2.6" fill="#ffffff" stroke="{TEXT}" stroke-width="1">'
                f'<title>試行{t}: {v:.7f}</title></circle>')
    right = max([m] + list(means(key).values()))
    svg.text(X0 + (X1 - X0) * right / xmax + 10, y + 21, f"{m:.5f}{diff}", size=12)
for v in nice_ticks(0, xmax):
    x = X0 + (X1 - X0) * v / xmax
    svg.line(x, Y0, x, Y0 + 4 * 52 - 6)
    svg.text(x, Y0 + 4 * 52 + 10, f"{v:.4f}".rstrip("0").rstrip(".") if v else "0", size=11, color=TEXT2,
             anchor="middle")
svg.line(X0, Y0, X0, Y0 + 4 * 52 - 6, color=AXIS)
legend_fill(svg, X0, 350, [(COLOR["Glicko2"], False, "Glicko-2"), (COLOR["WholeHistory"], False, "全履歴推定"),
                           ("#999999", True, "斜線：全履歴推定のレートで近い人どうしを組む（λ 0.5）")])
svg.save("factorial_mean.svg", "2×2の対照実験の平均MSE")

# ---------- 図2：基準に対するMSEの比（評価時点ごと） ----------
svg = Svg(960, 420, "基準（ランダム＋Glicko-2）に対するMSEの比",
          "評価時点ごとの10試行平均MSEの比。1より下が基準より良い。実線：ランダムに組む、破線：近い人どうしで組む")
PX0, PX1, PY0, PY1 = 80, 720, 80, 370
ymin, ymax = 0.6, 1.1
bm = {p: st.mean(curves[base][t][p] for t in curves[base]) for p in CHECKPOINTS}
for v in [0.6, 0.7, 0.8, 0.9, 1.0, 1.1]:
    y = PY1 - (PY1 - PY0) * (v - ymin) / (ymax - ymin)
    svg.line(PX0, y, PX1, y, color=AXIS if v == 1.0 else GRID, width=1.5 if v == 1.0 else 1)
    svg.text(PX0 - 8, y + 4, f"{v:.1f}", size=11, color=TEXT2, anchor="end")
for p in [10, 50, 100, 150, 200]:
    x = PX0 + (PX1 - PX0) * (p - 10) / 190
    svg.text(x, PY1 + 18, str(p), size=11, color=TEXT2, anchor="middle")
svg.text((PX0 + PX1) / 2, PY1 + 38, "各人の対戦数", size=12, color=TEXT2, anchor="middle")
svg.text(PX1 + 8, PY1 - (PY1 - PY0) * (1.0 - ymin) / (ymax - ymin) + 4, "基準 = 1", size=12, color=TEXT2)
for matcher, lam, est, name in cells[1:]:
    key = ("factorial", matcher, lam, est)
    pts = []
    for p in CHECKPOINTS:
        ratio = st.mean(curves[key][t][p] for t in curves[key]) / bm[p]
        pts.append((PX0 + (PX1 - PX0) * (p - 10) / 190, PY1 - (PY1 - PY0) * (ratio - ymin) / (ymax - ymin), p, ratio))
    dash = ' stroke-dasharray="7 5"' if matcher != "random" else ""
    svg.raw(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y, _, _ in pts)}" fill="none" '
            f'stroke="{COLOR[est]}" stroke-width="2"{dash}/>')
    for x, y, p, ratio in pts:
        svg.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{COLOR[est]}" stroke="#ffffff" stroke-width="2">'
                f'<title>{name} {p}試合: {ratio:.3f}</title></circle>')
    short = {"Glicko2": "Glicko-2", "WholeHistory": "全履歴推定"}[est] + ("・近い人どうし" if matcher != "random" else "・ランダム")
    svg.text(PX1 + 8, pts[-1][1] + 4, f"{short} {pts[-1][3]:.2f}", size=12)
svg.save("factorial_ratio.svg", "基準に対するMSEの比の推移")

# ---------- 図3：組み方だけを変えた効果（手法ごと） ----------
have_eval = all(len(curves[("evaluation", e, lam, e)]) == 10
                for e in ORDER for lam in {k[2] for k in curves if k[0] == "evaluation" and k[1] == e})
own_lambda = {k[1]: k[2] for k in curves if k[0] == "evaluation"}
series = [("WholeHistory", lambda e: ("factorial", "WholeHistory", "0.5", e), False, "全履歴推定のレートで組む（λ 0.5）")]
if have_eval and len(own_lambda) == 5:
    series.append(("own", lambda e: ("evaluation", e, own_lambda[e], e), True, "自分のレートで組む（調整したλ）"))
svg = Svg(1000, 120 + 5 * 30 * len(series) + 40, "組み方だけを変えたときのMSEの変化（推定方法は固定）",
          "ランダムに組んだ同じ試行との相対差（評価用10試行の平均）。線は試行間の最小〜最大。左（負）が改善")
X0, X1, Y0 = 250, 860, 84
lo, hi = -30, 165
zero = X0 + (X1 - X0) * (0 - lo) / (hi - lo)
for v in range(-25, 151, 25):
    x = X0 + (X1 - X0) * (v - lo) / (hi - lo)
    svg.line(x, Y0, x, Y0 + 5 * 30 * len(series), color=AXIS if v == 0 else GRID)
    svg.text(x, Y0 + 5 * 30 * len(series) + 16, f"{v:+d}%" if v else "0", size=11, color=TEXT2, anchor="middle")
y = Y0
for e in ORDER:
    svg.text(X0 - 12 - (0 if len(series) == 1 else 0), y + 15 + (15 if len(series) == 2 else 0) - (0), LABEL[e],
             anchor="end", weight="bold")
    for _, keyfn, hatch, sname in series:
        p = paired(keyfn(e), ("factorial", "random", "0", e))
        to = X0 + (X1 - X0) * (p["mean"] - lo) / (hi - lo)
        bar(svg, zero, to, y + 6, 18, COLOR[e], hatch, f"{LABEL[e]}・{sname}: {fmt(p)}")
        xa = X0 + (X1 - X0) * (p["min"] - lo) / (hi - lo)
        xb = X0 + (X1 - X0) * (p["max"] - lo) / (hi - lo)
        svg.line(xa, y + 15, xb, y + 15, color=TEXT, width=1.2)
        tx = max(to, xb) + 8 if p["mean"] >= 0 else max(xb, zero) + 8
        svg.text(tx, y + 20, f"{p['mean']:+.1f}%", size=12)
        y += 30
legend_fill(svg, X0, y + 36, [("#999999", h, n) for _, _, h, n in series])
svg.save("matching_effect.svg", "組み方だけを変えたときのMSEの変化")

# ---------- 図4：自分のレートで組む場合のλの調整（調整用5試行） ----------
tuning = defaultdict(dict)
for k in curves:
    if k[0] == "tuning" and k[1] != "random":
        tuning[k[1]][k[2]] = k
chosen = {}
W, PH = 900, 150
svg = Svg(W, 90 + 5 * (PH + 30), "自分のレートで組むときの乱数の倍率λ（調整用5試行）",
          "ランダムに組んだ同じ試行との相対差の平均と、試行間の最小〜最大。リングが選んだλ。手法ごとに縦軸の範囲が違う")
for i, e in enumerate(ORDER):
    top = 80 + i * (PH + 30)
    lams = sorted(tuning[e], key=float)
    ps = [paired(tuning[e][lam], ("tuning", "random", "0", e)) for lam in lams]
    best = min(range(len(lams)), key=lambda j: avg(tuning[e][lams[j]]))
    chosen[e] = lams[best]
    lo = min(min(p["min"] for p in ps), 0)
    hi = max(max(p["max"] for p in ps), 0)
    ticks = nice_ticks(lo, hi, 4)
    lo, hi = min(ticks[0], lo), max(ticks[-1], hi)
    PX0, PX1, PY0, PY1 = 190, 860, top + 10, top + PH - 10
    unit = "Eloポイント" if e == "Elo" else "×不確実性"
    svg.text(24, top + PH / 2, LABEL[e], weight="bold")
    svg.text(24, top + PH / 2 + 18, f"λ（{unit}）", size=11, color=TEXT2)
    for v in ticks:
        yv = PY1 - (PY1 - PY0) * (v - lo) / (hi - lo)
        svg.line(PX0, yv, PX1, yv, color=AXIS if v == 0 else GRID)
        svg.text(PX0 - 8, yv + 4, f"{v:+.0f}%" if v else "0", size=11, color=TEXT2, anchor="end")
    pts = []
    for j, (lam, p) in enumerate(zip(lams, ps)):
        x = PX0 + 30 + (PX1 - PX0 - 60) * j / (len(lams) - 1)
        ym = PY1 - (PY1 - PY0) * (p["mean"] - lo) / (hi - lo)
        ya = PY1 - (PY1 - PY0) * (p["min"] - lo) / (hi - lo)
        yb = PY1 - (PY1 - PY0) * (p["max"] - lo) / (hi - lo)
        svg.line(x, ya, x, yb, color=COLOR[e], width=2)
        pts.append((x, ym))
        svg.text(x, PY1 + 14, lam, size=11, color=TEXT2, anchor="middle")
        svg.text(x + 7, ym - 6, f"{p['mean']:+.1f}%", size=11)
    svg.raw(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in pts)}" fill="none" '
            f'stroke="{COLOR[e]}" stroke-width="2"/>')
    for j, (x, ym) in enumerate(pts):
        svg.raw(f'<circle cx="{x:.1f}" cy="{ym:.1f}" r="4" fill="{COLOR[e]}" stroke="#ffffff" stroke-width="2">'
                f'<title>{LABEL[e]} λ={lams[j]}: {fmt(ps[j])}</title></circle>')
    bx, by = pts[best]
    svg.raw(f'<circle cx="{bx:.1f}" cy="{by:.1f}" r="8" fill="none" stroke="{TEXT}" stroke-width="1.5"/>')
svg.save("selfmatch_tuning.svg", "自分のレートで組むときのλの調整")

# ---------- 表 ----------
lines = ["# 集計表（plot.pyが生成）", "",
         "値は20評価時点（10〜200試合）のMSEの平均。差は試行ごとの相対差の平均（sdは標本標準偏差、範囲は最小〜最大）。", "",
         "## 2×2の対照実験（評価用10試行）", "",
         "| 組み方 | 推定方法 | MSEの平均 | 基準との差 |", "| --- | --- | --- | --- |"]
for matcher, lam, est, name in cells:
    key = ("factorial", matcher, lam, est)
    d = "—" if key == base else fmt(paired(key, base))
    lines.append(f"| {'ランダム' if matcher == 'random' else '全履歴推定のレートで組む（λ 0.5）'} | {LABEL[est]} | "
                 f"{avg(key):.7f} | {d} |")
lines += ["", "推定方法だけを変えた差（Glicko-2 → 全履歴推定）：",
          f"- ランダムに組む：{fmt(paired(('factorial', 'random', '0', 'WholeHistory'), base))}",
          "- 全履歴推定のレートで組む："
          + fmt(paired(("factorial", "WholeHistory", "0.5", "WholeHistory"), ("factorial", "WholeHistory", "0.5", "Glicko2"))),
          "", "## 組み方だけを変えた差（推定方法は固定、評価用10試行）", "",
          "| 推定方法 | ランダム | 全履歴推定のレートで組む（λ 0.5） | 差 | 自分のレートで組む | λ | 差 |",
          "| --- | --- | --- | --- | --- | --- | --- |"]
for e in ORDER:
    r = ("factorial", "random", "0", e)
    c = ("factorial", "WholeHistory", "0.5", e)
    own = ("evaluation", e, own_lambda.get(e, ""), e)
    own_cells = (f"{avg(own):.7f} | {own_lambda[e]} | {fmt(paired(own, r))}"
                 if have_eval and e in own_lambda else "実行中 | — | —")
    lines.append(f"| {LABEL[e]} | {avg(r):.7f} | {avg(c):.7f} | {fmt(paired(c, r))} | {own_cells} |")
lines += ["", "## 評価時点ごとのMSE（10試行の平均）", "",
          "| 条件 | " + " | ".join(str(p) for p in (10, 20, 50, 100, 150, 200)) + " |",
          "| --- |" + " --- |" * 6]
for matcher, lam, est, name in cells:
    key = ("factorial", matcher, lam, est)
    lines.append(f"| {name} | " + " | ".join(f"{st.mean(curves[key][t][p] for t in curves[key]):.6f}"
                                             for p in (10, 20, 50, 100, 150, 200)) + " |")
lines += ["", "## 自分のレートで組むときのλの調整（調整用5試行、ランダムとの差）", ""]
for e in ORDER:
    lines.append(f"### {LABEL[e]}（ランダム {avg(('tuning', 'random', '0', e)):.7f}、選んだλ = {chosen[e]}）")
    lines.append("")
    lines.append("| λ | MSEの平均 | ランダムとの差 |")
    lines.append("| --- | --- | --- |")
    for lam in sorted(tuning[e], key=float):
        lines.append(f"| {lam} | {avg(tuning[e][lam]):.7f} | {fmt(paired(tuning[e][lam], ('tuning', 'random', '0', e)))} |")
    lines.append("")
(out / "results" / "summary.md").write_text("\n".join(lines).rstrip("\n") + "\n", encoding="utf-8")
if own_lambda:
    for e, lam in own_lambda.items():
        assert float(lam) == float(chosen[e]), f"評価用のλ（{e}: {lam}）が調整結果（{chosen[e]}）と違います"
print("chosen", chosen, "evaluation complete" if have_eval else "evaluation incomplete")
