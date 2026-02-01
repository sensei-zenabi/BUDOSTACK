#===================================================================================
# This shell script starts BUDOSTACK directly to it's in-built terminal application
#
# Description:
#
#  noise.glsl     = Simulates stochastic noise of CRT screen.
#  effects.glsl   = Simulates phosphor decay and chroma bleed.
#  crtscreen.glsl = Simulates the CRT display curvature and phosphor mask.
#===================================================================================

./apps/terminal \
    -s ./shaders/noise.glsl \
    -s ./shaders/effects.glsl \
    -s ./shaders/crtscreen.glsl

