"""Independently verify chart axes, grid switches and true-color PCX scanlines."""
import pathlib
import struct
import sys
import xml.etree.ElementTree as ET

root = pathlib.Path(sys.argv[1])
namespace = {"svg": "http://www.w3.org/2000/svg"}
for flags in range(16):
    svg = ET.parse(root / f"grid-{flags}.svg").getroot()
    assert svg.tag == "{http://www.w3.org/2000/svg}svg"
    labels = svg.findall("svg:text", namespace)
    xlabel = next(label for label in labels if label.text == "Time [s]")
    ylabel = next(label for label in labels if label.text == "Voltage [V]")
    assert "rotate(-90)" in ylabel.attrib["transform"]
    assert float(xlabel.attrib["y"]) > 30 + 500 - 96
    assert "transform" not in xlabel.attrib
    grid_counts = {"hmajor": 0, "vmajor": 0, "hminor": 0, "vminor": 0}
    for path in svg.findall("svg:path", namespace):
        stroke = path.attrib.get("stroke")
        if stroke not in ("#c8c8c8", "#e8e8e8"):
            continue
        x0, y0, x1, y1 = map(int, path.attrib["d"].replace("M", "").replace("L", "").split())
        assert y0 == y1 or x0 == x1
        axis = "h" if y0 == y1 else "v"
        grid_counts[axis + ("major" if stroke == "#c8c8c8" else "minor")] += 1
    assert grid_counts == {
        "hmajor": 3 if flags & 1 else 0,
        "vmajor": 3 if flags & 2 else 0,
        "hminor": 16 if flags & 4 else 0,
        "vminor": 16 if flags & 8 else 0,
    }

for kind in range(5):
    data = (root / f"figure-{kind}.pcx").read_bytes()
    assert data[:4] == bytes((10, 5, 1, 8)) and data[65] == 3
    xmin, ymin, xmax, ymax = struct.unpack_from("<4H", data, 4)
    width, height = xmax - xmin + 1, ymax - ymin + 1
    stride = struct.unpack_from("<H", data, 66)[0]
    assert (width, height, stride) == (800, 500, 800)
    position = 128
    pixels = bytearray(width * height * 3)
    for row in range(height):
        decoded = bytearray()
        while len(decoded) < stride * 3:
            value = data[position]
            position += 1
            run = 1
            if value >= 192:
                run = value & 63
                value = data[position]
                position += 1
            assert run and len(decoded) + run <= stride * 3
            decoded.extend(bytes((value,)) * run)
        for column in range(width):
            offset = (row * width + column) * 3
            pixels[offset:offset+3] = bytes(decoded[plane * stride + column] for plane in range(3))
    assert position == len(data)
    colors = {bytes(pixels[i:i+3]) for i in range(0, len(pixels), 3)}
    assert b"\xff\xff\xff" in colors and b"\x20\x60\xb0" in colors
    if kind == 1:
        # Rotated Y label is entirely to the left of the tick labels and plot.
        dark = [
            (x, y) for y in range(30, 434) for x in range(4, 13)
            if pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] == b"\x30\x30\x30"
        ]
        assert dark
        assert max(x for x, _ in dark) - min(x for x, _ in dark) <= 6
        assert max(y for _, y in dark) - min(y for _, y in dark) > 50
print("PASS: spreadsheet SVG axis rotation, 16 grid combinations and five RGB PCX exports")

# Exact source table: dates must position points by elapsed days, never by row.
expected_days = (0, 31, 60, 91, 121, 152, 182)
blue, red = "#2060b0", "#c04030"

def plot_points(name, color):
    svg = ET.parse(root / name).getroot()
    points = [
        (int(node.attrib["x"]), int(node.attrib["y"]))
        for node in svg.findall("svg:rect", namespace)
        if node.attrib.get("fill") == color and node.attrib.get("width") == "3"
        and node.attrib.get("height") == "3"
    ]
    return svg, points

for name, count in (("dates-both-line.svg", 2), ("dates-both-scatter.svg", 2),
                    ("dates-weight.svg", 1), ("dates-bench.svg", 1)):
    svg, points = plot_points(name, blue)
    assert len(points) == 7
    expected_x = [max(76, min(780, 76 + int(day / 182 * 706) - 1)) for day in expected_days]
    assert [point[0] for point in points] == expected_x
    _, second = plot_points(name, red)
    assert len(second) == (7 if count == 2 else 0)
    labels = svg.findall("svg:text", namespace)
    text = [node.text for node in labels]
    assert "2020-01-01" in text and "2020-07-01" in text and "Date" in text
    if name == "dates-weight.svg":
        assert "110.2" in text
    legend = [node.text for node in labels if "transform" not in node.attrib]
    assert ("Weight" in legend) == (count == 2 or name == "dates-weight.svg")
    assert ("Bench" in legend) == (count == 2 or name == "dates-bench.svg")
    if name == "dates-both-line.svg":
        series_lines = [node for node in svg.findall("svg:path", namespace) if node.attrib.get("stroke") in (blue, red)]
        assert len(series_lines) == 12

svg, points = plot_points("weight-bench.svg", blue)
assert len(points) == 7
weights = (110.2, 110, 109, 108.7, 105, 106.2, 106)
expected_x = [max(76, min(780, 76 + int((weight - 105) / (110.2 - 105) * 706) - 1)) for weight in weights]
assert [point[0] for point in points] == expected_x
labels = [node.text for node in svg.findall("svg:text", namespace)]
assert "Weight" in labels and "Bench" in labels and not any(label and label.startswith("2020-") for label in labels)
print("PASS: date/weight/bench plots, exact series choices and proportional leap-year date spacing")

_, missing = plot_points("dates-missing-line.svg", blue)
assert len(missing) == 6
assert [point[0] for point in missing] == [max(76, min(780, 76 + int(day / 182 * 706) - 1)) for i, day in enumerate(expected_days) if i != 1]
