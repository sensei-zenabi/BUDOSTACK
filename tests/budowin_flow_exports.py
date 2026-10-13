#!/usr/bin/env python3
"""Independently validate exported XML and decode every PCX scanline."""
import pathlib
import struct
import sys
import xml.etree.ElementTree as ET

root = pathlib.Path(sys.argv[1])
svg = ET.parse(root / "diagram.svg").getroot()
assert svg.tag == "{http://www.w3.org/2000/svg}svg"
assert any('A&<"B"> å' in ''.join(element.itertext()) for element in svg.iter())
assert svg.findall('.//{http://www.w3.org/2000/svg}polyline')
data = (root / "diagram.pcx").read_bytes()
assert data[:4] == bytes([10, 5, 1, 8]) and data[65] == 1
xmin, ymin, xmax, ymax = struct.unpack_from('<4H', data, 4)
width, height = xmax - xmin + 1, ymax - ymin + 1
stride = struct.unpack_from('<H', data, 66)[0]
assert (width, height) == (1200, 800) and stride == width
assert data[-769] == 12
position = 128
colors = set()
for row in range(height):
    decoded = 0
    while decoded < stride:
        value = data[position]
        position += 1
        count = 1
        if value >= 192:
            count = value & 63
            value = data[position]
            position += 1
        assert count and decoded + count <= stride
        decoded += count
        colors.add(value)
assert position == len(data) - 769 and colors == {0, 1, 2}
assert data[-768:-765] == b'\xff\xff\xff'
print('PASS: independent SVG escaping/XML and PCX palette/RLE/scanline verification')
