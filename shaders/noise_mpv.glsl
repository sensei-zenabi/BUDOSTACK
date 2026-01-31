// mpv .hook port of noise.glsl
// film noise
// by hunterk
// license: public domain
// Modified by BUDOSTACK contributors (parameter tuning).

//!HOOK MAIN
//!BIND HOOKED
//!DESC Film Noise (mpv)

///////////////////////  Runtime Parameters  ///////////////////////
// Edit these values to taste when using mpv's legacy vo=gpu shader loader.
#define x_off_r 0.03
#define y_off_r 0.03
#define x_off_g -0.03
#define y_off_g -0.03
#define x_off_b -0.03
#define y_off_b 0.03
#define grain_str 0.6
#define grain_intensity 0.1
#define hotspot 1.0
#define vignette 1.0
#define noise_toggle 0.0

float hash(float n)
{
    return fract(sin(n) * 43758.5453123);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// https://www.shadertoy.com/view/4sXSWs strength= 16.0
float filmGrain(vec2 uv, float strength, float timer)
{
    vec2 p = floor(uv * HOOKED_size);
    vec2 t = vec2(timer, timer * 0.37);
    float noise = hash21(p + t);
    return (noise - 0.5) * strength;
}

vec4 filmNoise(vec2 uv, float seed)
{
    vec2 base = uv * HOOKED_size + vec2(seed, seed * 1.37);
    float n0 = hash21(base + vec2(12.34, 45.67));
    float n1 = hash21(base + vec2(98.76, 54.32));
    float n2 = hash21(base + vec2(11.11, 22.22));
    float a = hash21(base + vec2(77.77, 33.33));
    return vec4(n0, n1, n2, a);
}

vec4 hook()
{
    vec2 vTexCoord = HOOKED_pos;
    float timer = float(frame);

    // a simple calculation for the vignette/hotspot effects
    vec2 middle = vTexCoord - 0.5;
    float len = length(middle);
    float vig = smoothstep(0.30, 1.25, len); // 0.3, 1.25
    float hot = smoothstep(0.15, 1.25, len); // own region for hotspot

    vec4 film_noise1 = filmNoise(vTexCoord * 2.0 * sin(hash(mod(float(frame), 47.0))), timer);
    vec4 film_noise2 = filmNoise(vTexCoord * 2.0 * cos(hash(mod(float(frame), 92.0))), timer + 19.0);

    vec2 red_coord = vTexCoord + 0.01 * vec2(x_off_r, y_off_r);
    vec3 red_light = HOOKED_tex(red_coord).rgb;
    vec2 green_coord = vTexCoord + 0.01 * vec2(x_off_g, y_off_g);
    vec3 green_light = HOOKED_tex(green_coord).rgb;
    vec2 blue_coord = vTexCoord + 0.01 * vec2(x_off_b, y_off_b);
    vec3 blue_light = HOOKED_tex(blue_coord).rgb;

    vec3 film = vec3(red_light.r, green_light.g, blue_light.b);
    float grain = filmGrain(vTexCoord.xy, grain_str, timer);
    film = mix(film, film + grain, grain_intensity); // Film grain

    film *= (vignette > 0.5) ? (1.0 - vig) : 1.0; // Vignette
    film += ((1.0 - hot) * 0.15) * hotspot; // Hotspot

    // Apply noise effects (or not)
    if (hash(float(frame)) > 0.99 && noise_toggle > 0.5) {
        return vec4(mix(film, film_noise1.rgb, film_noise1.a), 1.0);
    } else if (hash(float(frame)) < 0.01 && noise_toggle > 0.5) {
        return vec4(mix(film, film_noise2.rgb, film_noise2.a), 1.0);
    }

    return vec4(film, 1.0);
}
