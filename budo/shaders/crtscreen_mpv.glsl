// mpv .hook port of crtscreen.glsl
// Corners lifted from fake-crt-geom. Defaults set nicely.
// Parameter range extended to allow brightness compensation
// by Rogalian aka cools
// On sub-HD resolution devices, suggest Scanline Intensity: 0.0
// Modified by BUDOSTACK contributors (tuning defaults and feature toggles).

// Simple scanlines with curvature and mask effects lifted from crt-lottes
// by hunterk

//!HOOK MAIN
//!BIND HOOKED
//!DESC CRT Screen (mpv)

////////////////////////////////////////////////////////////////////
////////////////////////////  SETTINGS  ////////////////////////////
/////  comment these lines to disable effects and gain speed  //////
////////////////////////////////////////////////////////////////////

#define MASK // fancy, expensive phosphor mask effect
#define CURVATURE // applies barrel distortion to the screen
//#define SCANLINES  // applies horizontal scanline effect
//#define ROTATE_SCANLINES // for TATE games; also disables the mask effects, which look bad with it
#define EXTRA_MASKS // disable these if you need extra registers freed up
#define BORDER // border + rounded corners

////////////////////////////////////////////////////////////////////
//////////////////////////  END SETTINGS  //////////////////////////
////////////////////////////////////////////////////////////////////

///////////////////////  Runtime Parameters  ///////////////////////
// Edit these values to taste when using mpv's legacy vo=gpu shader loader.
#define shadowMask 3.0
#define warpX 0.01
#define warpY 0.01
#define maskDark 0.9
#define maskLight 1.1
#define crt_gamma 2.5
#define monitor_gamma 2.5
#define SCANLINE_SINE_COMP_A 0.05
#define SCANLINE_SINE_COMP_B 0.5
#define SCANLINE_BASE_BRIGHTNESS 1.5
#define bsmooth 100.0
#define a_corner 0.01

// prevent stupid behavior
#if defined ROTATE_SCANLINES && !defined SCANLINES
    #define SCANLINES
#endif

#define SourceSize vec4(HOOKED_size, 1.0 / HOOKED_size)
#define OutSize vec4(target_size, 1.0 / target_size)
#define scale vec2(SourceSize.xy / input_size.xy)

vec4 scanline(vec2 coord, vec4 frame)
{
#if defined SCANLINES
    vec2 omega = vec2(3.1415 * target_size.x, 2.0 * 3.1415 * HOOKED_size.y);
    vec2 sine_comp = vec2(SCANLINE_SINE_COMP_A, SCANLINE_SINE_COMP_B);
    vec3 res = frame.xyz;
    #ifdef ROTATE_SCANLINES
        sine_comp = sine_comp.yx;
        omega = omega.yx;
    #endif

    // -0.25 fixes scanline misplacement on pixels
    vec3 scanline = res * (SCANLINE_BASE_BRIGHTNESS + dot(sine_comp * sin((coord - vec2(0.0, 0.25 * SourceSize.w)) * omega), vec2(1.0, 1.0)));

    return vec4(scanline.x, scanline.y, scanline.z, 1.0);
#else
    return frame;
#endif
}

#ifdef CURVATURE
// Distortion of scanlines, and end of screen alpha.
vec2 Warp(vec2 pos)
{
    pos  = pos * 2.0 - 1.0;
    pos *= vec2(1.0 + (pos.y * pos.y) * warpX, 1.0 + (pos.x * pos.x) * warpY);

    return pos * 0.5 + 0.5;
}
#endif

#if defined MASK && !defined ROTATE_SCANLINES
    // Shadow mask.
    vec4 Mask(vec2 pos)
    {
        vec3 mask = vec3(maskDark, maskDark, maskDark);

        // Very compressed TV style shadow mask.
        if (shadowMask == 1.0)
        {
            float line = maskLight;
            float odd = 0.0;

            if (fract(pos.x * 0.166666666) < 0.5) {
                odd = 1.0;
            }
            if (fract((pos.y + odd) * 0.5) < 0.5) {
                line = maskDark;
            }

            pos.x = fract(pos.x * 0.333333333);

            if (pos.x < 0.333) {
                mask.r = maskLight;
            } else if (pos.x < 0.666) {
                mask.g = maskLight;
            } else {
                mask.b = maskLight;
            }
            mask *= line;
        }

        // Aperture-grille.
        else if (shadowMask == 2.0)
        {
            pos.x = fract(pos.x * 0.333333333);

            if (pos.x < 0.333) {
                mask.r = maskLight;
            } else if (pos.x < 0.666) {
                mask.g = maskLight;
            } else {
                mask.b = maskLight;
            }
        }
    #ifdef EXTRA_MASKS
        // These can cause moire with curvature and scanlines
        // so they're an easy target for freeing up registers

        // Stretched VGA style shadow mask (same as prior shaders).
        else if (shadowMask == 3.0)
        {
            pos.x += pos.y * 3.0;
            pos.x  = fract(pos.x * 0.166666666);

            if (pos.x < 0.333) {
                mask.r = maskLight;
            } else if (pos.x < 0.666) {
                mask.g = maskLight;
            } else {
                mask.b = maskLight;
            }
        }

        // VGA style shadow mask.
        else if (shadowMask == 4.0)
        {
            pos.xy  = floor(pos.xy * vec2(1.0, 0.5));
            pos.x  += pos.y * 3.0;
            pos.x   = fract(pos.x * 0.166666666);

            if (pos.x < 0.333) {
                mask.r = maskLight;
            } else if (pos.x < 0.666) {
                mask.g = maskLight;
            } else {
                mask.b = maskLight;
            }
        }
    #endif

        else {
            mask = vec3(1.0, 1.0, 1.0);
        }

        return vec4(mask, 1.0);
    }
#endif

#ifdef BORDER
float corner(vec2 coord)
{
    coord = min(coord, vec2(1.0) - coord);
    vec2 cdist = vec2(a_corner);
    coord = (cdist - min(coord, cdist));
    float dist = sqrt(dot(coord, coord));
    return clamp((cdist.x - dist) * bsmooth, 0.0, 1.0);
}
#endif

vec4 hook()
{
#ifdef CURVATURE
    vec2 pos = Warp(HOOKED_pos * (HOOKED_size / input_size)) * (input_size / HOOKED_size);
#else
    vec2 pos = HOOKED_pos;
#endif

#if defined MASK && !defined ROTATE_SCANLINES
    // mask effects look bad unless applied in linear gamma space
    vec4 in_gamma = vec4(crt_gamma, crt_gamma, crt_gamma, 1.0);
    vec4 out_gamma = vec4(1.0 / monitor_gamma, 1.0 / monitor_gamma, 1.0 / monitor_gamma, 1.0);
    vec4 res = pow(HOOKED_tex(pos), in_gamma);
#else
    vec4 res = HOOKED_tex(pos);
#endif

#if defined MASK && !defined ROTATE_SCANLINES
    // apply the mask; looks bad with vert scanlines so make them mutually exclusive
    res *= Mask(gl_FragCoord.xy * 1.0001);
#endif

#if defined CURVATURE && defined GL_ES
    // hacky clamp fix for GLES
    vec2 bordertest = (pos);
    if (bordertest.x > 0.0001 && bordertest.x < 0.9999 && bordertest.y > 0.0001 && bordertest.y < 0.9999) {
        res = res;
    } else {
        res = vec4(0.0, 0.0, 0.0, 0.0);
    }
#endif

#ifdef BORDER
    if (a_corner > 0.0) {
        res *= corner(pos * scale);
    }
#endif

#if defined MASK && !defined ROTATE_SCANLINES
    // re-apply the gamma curve for the mask path
    return pow(scanline(pos, res), out_gamma);
#else
    return scanline(pos, res);
#endif
}
