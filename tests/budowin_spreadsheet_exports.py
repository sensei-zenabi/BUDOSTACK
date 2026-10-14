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
