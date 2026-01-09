# Shooter sprite overrides

Place BMP files in this folder to override the built-in procedural textures.
When a file is missing or invalid, the demo keeps the fallback sprite.

## Format

- **BMP, 24-bit or 32-bit, uncompressed (BI_RGB)**.
- Exact dimensions are required.
- For `enemy.bmp`, `weapon_idle.bmp`, and `weapon_fire.bmp`, pure black (0,0,0)
  is treated as transparent.

## Required files and sizes

| File name         | Size (px) |
|-------------------|-----------|
| `wall.bmp`        | 16 × 16   |
| `floor.bmp`       | 16 × 16   |
| `ceiling.bmp`     | 16 × 16   |
| `enemy.bmp`       | 16 × 32   |
| `weapon_idle.bmp` | 64 × 32   |
| `weapon_fire.bmp` | 64 × 32   |
