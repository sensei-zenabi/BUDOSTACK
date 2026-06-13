#!/bin/sh
#===================================================================================
# This shell script starts BUDOSTACK directly to it's in-built terminal application
#
# Description:
#
#  noise.glsl     = Simulates stochastic noise of CRT screen.
#  crtscreen.glsl = Simulates CRT curvature, phosphor mask, phosphor decay, and chroma bleed.
#===================================================================================

shader_quality=$(
    awk -F= '
        /^[[:space:]]*SHADER_QUALITY[[:space:]]*=/ {
            value = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print toupper(value)
            exit
        }
    ' ./config.ini 2>/dev/null
)

case "$shader_quality" in
    LOW|MEDIUM|HIGH)
        ;;
    *)
        shader_quality=MEDIUM
        ;;
esac

if [ -x ./apps/terminal ]; then
    case "$shader_quality" in
        LOW)
            ./apps/terminal \
                -s ./budo/shaders/crtscreen.glsl
            ;;
        HIGH)
            ./apps/terminal \
                -s ./budo/shaders/noise.glsl \
                -s ./budo/shaders/crtscreen.glsl
            ;;
        *)
            ./apps/terminal \
                -s ./budo/shaders/noise.glsl \
                -s ./budo/shaders/crtscreen.glsl
            ;;
    esac
else
    echo "apps/terminal is not available; starting BUDOSTACK in the current terminal."
    ./budostack
fi
