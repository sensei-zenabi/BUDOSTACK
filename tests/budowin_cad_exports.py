"""Validate physical dimensions, escaped notes, layers and DXF model geometry."""
import math
import pathlib
import sys
import xml.etree.ElementTree as ET

root = pathlib.Path(sys.argv[1])
ns = {"s": "http://www.w3.org/2000/svg"}
svg = ET.parse(root / "drawing.svg").getroot()
assert svg.attrib["width"].endswith("mm")
assert svg.attrib["height"].endswith("mm")
view = list(map(float, svg.attrib["viewBox"].split()))
assert math.isclose(float(svg.attrib["width"][:-2]), view[2])
assert math.isclose(float(svg.attrib["height"][:-2]), view[3])
assert len(svg.findall("s:g", ns)) == 4
assert svg.find("s:g[@id='layer3']", ns).attrib["stroke-dasharray"] == "6 4"
assert svg.find("s:g[@id='layer3']/s:title", ns).text == "Construction"
circle = svg.find(".//s:circle", ns)
assert circle is not None and float(circle.attrib["r"]) == 100
labels = [el.text for el in svg.findall(".//s:text", ns)]
assert "300.00 mm" in labels and "350.00 mm" in labels and "460.98 mm" in labels
assert "Panel & <note> ä" in labels
assert "18 mm plywood" in labels

lines = (root / "drawing.dxf").read_text().splitlines()
assert len(lines) % 2 == 0
pairs = [(int(lines[i]), lines[i + 1]) for i in range(0, len(lines), 2)]
assert pairs[-1] == (0, "EOF")
assert (9, "$INSUNITS") in pairs
index = pairs.index((9, "$INSUNITS"))
assert pairs[index + 1] == (70, "4")
records = []
for code, value in pairs:
    if code == 0:
        records.append((value, []))
    elif records:
        records[-1][1].append((code, value))
entities = [(kind, dict(values)) for kind, values in records if kind in {"LINE", "CIRCLE", "TEXT"}]
assert sum(kind == "CIRCLE" for kind, _ in entities) == 1
kind, circle = next((kind, data) for kind, data in entities if kind == "CIRCLE")
assert float(circle[10]) == 200 and float(circle[20]) == 200 and float(circle[40]) == 100
assert all(data[8] in {"L0_Geometry", "L1_Dimensions", "L2_Notes", "L3_Construction"} for _, data in entities)
assert all(math.isfinite(float(value)) for _, data in entities for key, value in data.items() if key in {10, 20, 11, 21, 40})
labels = [data[1] for kind, data in entities if kind == "TEXT"]
assert "Panel & <note> \\U+00E4" in labels
assert "300.00 mm" in labels and "350.00 mm" in labels
assert sum(kind == "LAYER" for kind, _ in records) == 4
hidden = ET.parse(root / "hidden.svg").getroot()
assert hidden.find("s:g[@id='layer0']", ns) is None
assert hidden.find(".//s:circle", ns) is None
assert len(hidden.findall("s:g", ns)) == 3
try:
    import ezdxf
except ImportError:
    pass
else:
    for name in ["drawing.dxf", "hidden.dxf"]:
        drawing = ezdxf.readfile(root / name)
        audit = drawing.audit()
        assert not audit.errors and not audit.fixes
        assert drawing.units == 4
        assert len(drawing.modelspace().query("CIRCLE")) == 1
    layer = drawing.layers.get("L0_Geometry")
    assert layer.is_off() and layer.is_locked()
for dimension_type, measurement in [(4, "300.00 mm"), (5, "400.00 mm"), (6, "500.00 mm")]:
    before = ET.parse(root / f"labels-{dimension_type}-before.svg").getroot()
    after = ET.parse(root / f"labels-{dimension_type}.svg").getroot()
    assert [e.attrib for e in before.findall(".//s:line", ns)] == [e.attrib for e in after.findall(".//s:line", ns)]
    old_label = before.find(".//s:text", ns)
    new_label = after.find(".//s:text", ns)
    assert old_label.text == new_label.text == measurement
    assert math.isclose(float(new_label.attrib["x"]) - float(old_label.attrib["x"]), 74)
    assert math.isclose(float(new_label.attrib["y"]) - float(old_label.attrib["y"]), 38)
    if "ezdxf" in sys.modules:
        original = ezdxf.readfile(root / f"labels-{dimension_type}-before.dxf")
        moved = ezdxf.readfile(root / f"labels-{dimension_type}.dxf")
        audit = moved.audit()
        assert not audit.errors and not audit.fixes
        old_lines = [(tuple(e.dxf.start), tuple(e.dxf.end)) for e in original.modelspace().query("LINE")]
        new_lines = [(tuple(e.dxf.start), tuple(e.dxf.end)) for e in moved.modelspace().query("LINE")]
        assert old_lines == new_lines
        old_text = original.modelspace().query("TEXT")[0]
        new_text = moved.modelspace().query("TEXT")[0]
        assert old_text.dxf.text == new_text.dxf.text == measurement
        delta = new_text.dxf.insert - old_text.dxf.insert
        assert math.isclose(delta.x, 74) and math.isclose(delta.y, -38)
print("BUDOWIN CAD SVG and DXF export checks passed.")
