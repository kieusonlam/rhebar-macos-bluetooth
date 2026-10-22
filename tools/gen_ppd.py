"""Sinh PPD cho các máy in nhãn RHEBAR (TSPL) từ thông số trong bộ cài Windows 1.3.2.20."""
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

FILTER = "/Library/Printers/RHEBAR/Filters/rastertotspl"

# Model gốc (Manufacturer "Native" trong config.xml): dpi, tốc độ, khổ mặc định, max width
BASES = {
    "IP-583":   dict(dpi=203, speeds=[2, 3, 4, 5, 6], defspeed=4, maxw=110, paper="100x150", cutter=True),
    "IP-683":   dict(dpi=300, speeds=[2, 3, 4], defspeed=4, maxw=110, paper="100x150", cutter=True),
    "IP-685":   dict(dpi=300, speeds=[2, 3, 4, 5], defspeed=4, maxw=110, paper="100x150", cutter=True),
    "IP-592":   dict(dpi=203, speeds=[2, 3, 4, 5, 6], defspeed=4, maxw=110, paper="100x150"),
    "IP-348B":  dict(dpi=203, speeds=[2, 3, 4, 5, 6, 7, 8], defspeed=4, maxw=80, paper="78x120", cutter=True),
    "IP-368":   dict(dpi=203, speeds=[2, 3, 4, 5, 6, 7, 8, 9, 10], defspeed=4, maxw=80, paper="78x120"),
    "IP-801BT": dict(dpi=203, speeds=[2, 3, 4, 5, 6, 7, 8, 9, 10], defspeed=4, maxw=110, paper="76x130"),
    "IP-802":   dict(dpi=203, speeds=[2, 3, 4, 5], defspeed=4, maxw=110, paper="76x130"),
    "IP-802BT": dict(dpi=203, speeds=[2, 3, 4, 5], defspeed=4, maxw=110, paper="76x130"),
    "IP-410BT": dict(dpi=203, speeds=list(range(2, 13)), defspeed=4, maxw=110, paper="76x130"),
    "IP-C10":   dict(dpi=203, speeds=[2, 3], defspeed=3, maxw=110, paper="100x150"),
}

# Manufacturer "RHEBAR" trong config.xml (bỏ PD-A4/POS58/MP231 vì không dùng TSPL)
MODELS = {
    "BY-496": "IP-583", "BY-496BT": "IP-583", "BY-496P": "IP-583",
    "BY-498": "IP-685", "BY-498BT": "IP-683", "BY-498P": "IP-683", "BY-244PBT": "IP-683",
    "BY-426": "IP-592", "BY-426BT": "IP-592", "BY-482": "IP-592", "BY-482BT": "IP-592",
    "BY-480": "IP-592", "BY-480BT": "IP-592", "BY-486": "IP-592", "BY-486BT": "IP-592",
    "BY-290": "IP-592", "BY-290BT": "IP-592", "LTK-244": "IP-592", "LTK-244BT": "IP-592",
    "IP-486BT": "IP-592", "Label Printer 4inch": "IP-592",
    "BY-348B": "IP-348B", "BY-348BBT": "IP-348B",
    "BY-368": "IP-368", "BY-368BT": "IP-368", "QR-368C": "IP-368", "QR-368CBT": "IP-368",
    "Label Printer 3inch": "IP-368",
    "IP-801BT": "IP-801BT", "IP-802": "IP-802", "IP-802BT": "IP-802BT",
    "IP-805": "IP-802", "IP-805BT": "IP-802",
    "QR-410S": "IP-410BT", "C10Pro": "IP-C10",
}

# Papers type="label_rhebar" + "label" (mm)
PAPERS = [
    (50.8, 31.8), (76.2, 76.2), (76.2, 127), (101.6, 101.6), (101.6, 152.4),
    (40, 30), (40, 50), (50, 30), (50, 40), (50, 50), (50, 70), (50, 80),
    (60, 40), (60, 50), (70, 20), (70, 40), (70, 50), (75, 50), (75, 75), (75, 112),
    (75, 130), (76, 100), (76, 106), (76, 126), (76, 128), (76, 130), (78, 100),
    (78, 120), (80, 30), (80, 60), (80, 80), (80, 100), (80, 120),
    (100, 50), (100, 100), (100, 150), (100, 180), (102, 176),
]


def pt(mm):
    return round(mm * 72 / 25.4, 2)


def fmt(v):
    return f"{v:g}"


def paper_key(w, h):
    return f"{fmt(w)}x{fmt(h)}mm".replace(".", "_")


def paper_text(w, h):
    return f"{fmt(w)} x {fmt(h)} mm"


def build(model, base):
    b = BASES[base]
    dpi = b["dpi"]
    papers = [p for p in PAPERS if p[0] <= b["maxw"] + 0.5]
    dw, dh = (float(x) for x in b["paper"].split("x"))
    default = paper_key(dw, dh)
    if not any(paper_key(*p) == default for p in papers):
        papers.append((dw, dh))
    nick = f"RHEBAR {model} (TSPL, {dpi}dpi)"
    fname = "RHEBAR-" + model.replace(" ", "-") + ".ppd"

    L = []
    a = L.append
    a('*PPD-Adobe: "4.3"')
    a('*FormatVersion: "4.3"')
    a('*FileVersion: "1.0.0"')
    a('*LanguageVersion: English')
    a('*LanguageEncoding: ISOLatin1')
    a(f'*PCFileName: "{fname[:-4].upper()[:8]}.PPD"')
    a('*Manufacturer: "RHEBAR"')
    a(f'*Product: "({model})"')
    a(f'*ModelName: "RHEBAR {model}"')
    a(f'*ShortNickName: "RHEBAR {model}"')
    a(f'*NickName: "{nick}"')
    a('*PSVersion: "(3010.000) 0"')
    a('*LanguageLevel: "3"')
    a('*ColorDevice: False')
    a('*DefaultColorSpace: Gray')
    a('*FileSystem: False')
    a('*Throughput: "1"')
    a('*LandscapeOrientation: Plus90')
    a('*TTRasterizer: Type42')
    a('*cupsVersion: 2.2')
    a('*cupsModelNumber: 0')
    a('*cupsManualCopies: True')
    a(f'*cupsFilter: "application/vnd.cups-raster 0 {FILTER}"')
    a('*cupsLanguages: "en"')
    a('')
    a('*OpenUI *PageSize/Media Size: PickOne')
    a('*OrderDependency: 10 AnySetup *PageSize')
    a(f'*DefaultPageSize: {default}')
    for w, h in papers:
        a(f'*PageSize {paper_key(w, h)}/{paper_text(w, h)}: "<</PageSize[{pt(w)} {pt(h)}]/ImagingBBox null>>setpagedevice"')
    a('*CloseUI: *PageSize')
    a('')
    a('*OpenUI *PageRegion/Media Size: PickOne')
    a('*OrderDependency: 10 AnySetup *PageRegion')
    a(f'*DefaultPageRegion: {default}')
    for w, h in papers:
        a(f'*PageRegion {paper_key(w, h)}/{paper_text(w, h)}: "<</PageSize[{pt(w)} {pt(h)}]/ImagingBBox null>>setpagedevice"')
    a('*CloseUI: *PageRegion')
    a('')
    a(f'*DefaultImageableArea: {default}')
    for w, h in papers:
        a(f'*ImageableArea {paper_key(w, h)}/{paper_text(w, h)}: "0 0 {pt(w)} {pt(h)}"')
    a(f'*DefaultPaperDimension: {default}')
    for w, h in papers:
        a(f'*PaperDimension {paper_key(w, h)}/{paper_text(w, h)}: "{pt(w)} {pt(h)}"')
    a('')
    a(f'*MaxMediaWidth: "{pt(b["maxw"])}"')
    a(f'*MaxMediaHeight: "{pt(1000)}"')
    a('*HWMargins: 0 0 0 0')
    a('*CustomPageSize True: "pop pop pop <</PageSize[5 -2 roll]/ImagingBBox null>>setpagedevice"')
    a(f'*ParamCustomPageSize Width: 1 points {pt(10)} {pt(b["maxw"])}')
    a(f'*ParamCustomPageSize Height: 2 points {pt(10)} {pt(1000)}')
    a('*ParamCustomPageSize WidthOffset: 3 points 0 0')
    a('*ParamCustomPageSize HeightOffset: 4 points 0 0')
    a('*ParamCustomPageSize Orientation: 5 int 0 0')
    a('')
    a('*OpenUI *Resolution/Resolution: PickOne')
    a('*OrderDependency: 10 AnySetup *Resolution')
    a(f'*DefaultResolution: {dpi}dpi')
    a(f'*Resolution {dpi}dpi/{dpi} DPI: "<</HWResolution[{dpi} {dpi}]/cupsBitsPerColor 8/cupsRowCount 0/cupsRowFeed 0/cupsRowStep 0/cupsColorSpace 0/cupsColorOrder 0>>setpagedevice"')
    a('*CloseUI: *Resolution')
    a('')
    a('*OpenUI *ColorModel/Color Mode: PickOne')
    a('*OrderDependency: 10 AnySetup *ColorModel')
    a('*DefaultColorModel: Gray')
    a('*ColorModel Gray/Grayscale: "<</cupsColorOrder 0/cupsColorSpace 0/cupsBitsPerColor 8>>setpagedevice"')
    a('*CloseUI: *ColorModel')
    a('')

    def int_option(key, label, idx, choices, default_key):
        a(f'*OpenUI *{key}/{label}: PickOne')
        a(f'*OrderDependency: 20 AnySetup *{key}')
        a(f'*Default{key}: {default_key}')
        for k, text, val in choices:
            a(f'*{key} {k}/{text}: "<</cupsInteger{idx} {val}>>setpagedevice"')
        a(f'*CloseUI: *{key}')
        a('')

    a('*OpenGroup: Printer/Printer Settings')
    a('')
    int_option("Speed", "Print Speed", 0,
               [(str(s), f"{s} inch/s", s) for s in b["speeds"]], str(b["defspeed"]))
    int_option("Density", "Darkness", 1,
               [(str(d), str(d), d + 1) for d in range(16)], "8")
    int_option("MediaTracking", "Media Tracking", 2,
               [("Gap", "Gap / notch labels", 1), ("BlackMark", "Black mark", 2),
                ("Continuous", "Continuous roll", 3)], "Gap")
    int_option("GapSize", "Gap / Mark Height", 3,
               [(str(g), f"{g} mm", g) for g in (1, 2, 3, 4, 5)], "3")
    int_option("PrintDirection", "Print Direction", 4,
               [("Normal", "Normal", 1), ("Rotate180", "Rotated 180 degrees", 2)], "Normal")
    int_option("Halftone", "Halftone", 5,
               [("Threshold", "Text / barcode (sharp)", 1), ("Dither", "Photo (dither)", 2)], "Threshold")
    if b.get("cutter"):
        int_option("PostAction", "After Each Label", 7,
                   [("Tear", "Tear off", 1), ("Cut", "Cut (cutter installed)", 2)], "Tear")
    int_option("InvertBitmap", "Invert Output (troubleshooting)", 6,
               [("False", "No", 1), ("True", "Yes", 2)], "False")
    a('*CloseGroup: Printer')
    a('')
    a('*DefaultFont: Courier')
    a('*Font Courier: Standard "(001.004)" Standard ROM')
    a('')
    return fname, "\n".join(L) + "\n"


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for model, base in MODELS.items():
        name, text = build(model, base)
        (out / name).write_text(text, encoding="ascii", newline="\n")
    print(f"{len(MODELS)} PPD -> {out}")


if __name__ == "__main__":
    main()
