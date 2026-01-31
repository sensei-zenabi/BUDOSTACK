// mpv .hook port of effects.glsl
// VHS shader
// by hunterk
// adapted from ompuco's more AVdistortion shadertoy:
// https://www.shadertoy.com/view/XlsczN
// Modified by BUDOSTACK contributors (parameter adjustments).

//!HOOK MAIN
//!BIND HOOKED
//!DESC VHS Effects (mpv)

///////////////////////  Runtime Parameters  ///////////////////////
// Edit these values to taste when using mpv's legacy vo=gpu shader loader.
#define wiggle 0.0
#define smear 0.10
#define phosphor_decay_enable 1.0
#define phosphor_decay_time_ms 300.0
#define phosphor_decay_threshold 20.0

#define iTime mod(float(frame), 7.0)

// YIQ/RGB conversion
vec3 rgb2yiq(vec3 c)
{
    return vec3(
        (0.2989 * c.x + 0.5959 * c.y + 0.2115 * c.z),
        (0.5870 * c.x - 0.2744 * c.y - 0.5229 * c.z),
        (0.1140 * c.x - 0.3216 * c.y + 0.3114 * c.z)
    );
}

vec3 yiq2rgb(vec3 c)
{
    return vec3(
        (1.0 * c.x + 1.0 * c.y + 1.0 * c.z),
        (0.956 * c.x - 0.2720 * c.y - 1.1060 * c.z),
        (0.6210 * c.x - 0.6474 * c.y + 1.7046 * c.z)
    );
}

vec2 Circle(float Start, float Points, float Point)
{
    float Rad = (3.141592 * 2.0 * (1.0 / Points)) * (Point + Start);
    return vec2(-(0.3 + Rad), cos(Rad));
}

vec3 Blur(vec2 uv, float d)
{
    float t = (sin(iTime * 5.0 + uv.y * 5.0)) / 10.0;
    float b = 1.0;
    t = 0.0;
    vec2 PixelOffset = vec2(d + 0.0005 * t, 0.0);

    float Start = 2.0 / 14.0;
    vec2 Scale = 0.66 * 4.0 * 2.0 * PixelOffset.xy;

    vec3 N0 = HOOKED_tex(uv + Circle(Start, 14.0, 0.0) * Scale).rgb;
    vec3 N1 = HOOKED_tex(uv + Circle(Start, 14.0, 1.0) * Scale).rgb;
    vec3 N2 = HOOKED_tex(uv + Circle(Start, 14.0, 2.0) * Scale).rgb;
    vec3 N3 = HOOKED_tex(uv + Circle(Start, 14.0, 3.0) * Scale).rgb;
    vec3 N4 = HOOKED_tex(uv + Circle(Start, 14.0, 4.0) * Scale).rgb;
    vec3 N5 = HOOKED_tex(uv + Circle(Start, 14.0, 5.0) * Scale).rgb;
    vec3 N6 = HOOKED_tex(uv + Circle(Start, 14.0, 6.0) * Scale).rgb;
    vec3 N7 = HOOKED_tex(uv + Circle(Start, 14.0, 7.0) * Scale).rgb;
    vec3 N8 = HOOKED_tex(uv + Circle(Start, 14.0, 8.0) * Scale).rgb;
    vec3 N9 = HOOKED_tex(uv + Circle(Start, 14.0, 9.0) * Scale).rgb;
    vec3 N10 = HOOKED_tex(uv + Circle(Start, 14.0, 10.0) * Scale).rgb;
    vec3 N11 = HOOKED_tex(uv + Circle(Start, 14.0, 11.0) * Scale).rgb;
    vec3 N12 = HOOKED_tex(uv + Circle(Start, 14.0, 12.0) * Scale).rgb;
    vec3 N13 = HOOKED_tex(uv + Circle(Start, 14.0, 13.0) * Scale).rgb;
    vec3 N14 = HOOKED_tex(uv).rgb;

    vec4 clr = HOOKED_tex(uv);
    float W = 1.0 / 15.0;

    clr.rgb =
        (N0 * W) +
        (N1 * W) +
        (N2 * W) +
        (N3 * W) +
        (N4 * W) +
        (N5 * W) +
        (N6 * W) +
        (N7 * W) +
        (N8 * W) +
        (N9 * W) +
        (N10 * W) +
        (N11 * W) +
        (N12 * W) +
        (N13 * W) +
        (N14 * W);
    return vec3(clr.xyz) * b;
}

float onOff(float a, float b, float c, float framecount)
{
    return step(c, sin((framecount * 0.001) + a * cos((framecount * 0.001) * b)));
}

vec3 apply_phosphor_decay(vec2 uv, vec3 current_color)
{
    if (phosphor_decay_enable < 0.5) {
        return current_color;
    }

    // mpv shaders do not provide a previous-frame texture; fallback to current.
    vec3 prev_color = current_color;
    float threshold = clamp(phosphor_decay_threshold / 100.0, 0.0, 1.0);
    float current_luma = dot(current_color, vec3(0.299, 0.587, 0.114));

    if (current_luma >= threshold) {
        return current_color;
    }

    float prev_luma = dot(prev_color, vec3(0.299, 0.587, 0.114));
    if (prev_luma <= 0.0) {
        return current_color;
    }

    float decay_ms = max(phosphor_decay_time_ms, 1.0);
    float frame_ms = 1000.0 / 60.0;
    float decay = exp(-frame_ms / decay_ms);
    /* Decay starts at the threshold and falls toward zero as pixels turn off. */
    float start_luma = min(prev_luma, threshold);
    float decayed_luma = start_luma * decay;
    float scale = decayed_luma / max(prev_luma, 0.0001);
    vec3 decayed_color = prev_color * scale;

    return max(current_color, decayed_color);
}

vec2 jumpy(vec2 uv, float framecount)
{
    vec2 look = uv;
    float window = 1.0 / (1.0 + 80.0 * (look.y - mod(framecount / 4.0, 1.0)) * (look.y - mod(framecount / 4.0, 1.0)));
    look.x += 0.05 * sin(look.y * 10.0 + framecount) / 20.0 * onOff(4.0, 4.0, 0.3, framecount) * (0.5 + cos(framecount * 20.0)) * window;
    float vShift = (0.1 * wiggle) * 0.4 * onOff(2.0, 3.0, 0.9, framecount) * (sin(framecount) * sin(framecount * 20.0) +
                                         (0.5 + 0.1 * sin(framecount * 200.0) * cos(framecount)));
    look.y = mod(look.y - 0.01 * vShift, 1.0);
    return uv; // removing the jumpy effect by returning the original uv
}

vec4 hook()
{
    float d = 0.1 - ceil(mod(iTime / 3.0, 1.0) + 0.5) * 0.1;
    vec2 uv = jumpy(HOOKED_pos.xy, iTime);
    vec2 uv2 = uv;

    float s = 0.0001 * -d + 0.0001 * wiggle * sin(iTime);

    float e_base = max(0.0, cos(uv.y * 4.0 + 0.3) - 0.75) * (s + 0.5) * 1.0;
    float e = min(0.30, e_base * e_base * e_base) * 25.0;
    float r = (iTime * (2.0 * s));
    (void)r;

    d = 0.051 + abs(sin(s / 4.0));
    float c = max(0.0001, 0.002 * d) * smear;
    vec2 uvo = uv;
    (void)uvo;
    vec4 final;
    final.xyz = Blur(uv, c + c * (uv.x));
    float y = rgb2yiq(final.xyz).r;

    c *= 6.0;
    final.xyz = Blur(uv, c);
    float i = rgb2yiq(final.xyz).g;

    c *= 2.50;
    final.xyz = Blur(uv, c);
    float q = rgb2yiq(final.xyz).b;
    float se = s + e * 2.0;
    final = vec4(yiq2rgb(vec3(y, i, q)) - (se * se * se), 1.0);
    final.xyz = apply_phosphor_decay(uv2, final.xyz);

    return final;
}
