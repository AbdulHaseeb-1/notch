#version 440

// Buddy's ring in one pass: every style is analytic (rings, arcs, glow and dots
// as distance fields), so one quad replaces the stacks of Shape layers in
// BuddyRing.qml. Mirrors those styles; see qml/buddy/fx/BuddyShaderRing.qml.
//
// Units are pixels from the centre of the quad. Angles are turns: `cw` runs
// clockwise from 12 o'clock, a conical gradient position runs the other way.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 c0;
    vec4 c1;
    vec4 c2;
    vec4 c3;
    float quad;     // side of the quad, px
    float midR;     // centre line of the ring, px
    float thick;    // the crisp stroke, px
    float s;        // buddy scale
    float glow;     // 0 .. 1.5
    float time;     // seconds, ring speed already applied
    int style;      // neon aurora comet orbit dashes pulse prism halo
};

const float TAU = 6.28318530718;

vec3 stopCol(int i)
{
    int k = ((i % 4) + 4) % 4;
    return k == 0 ? c0.rgb : (k == 1 ? c1.rgb : (k == 2 ? c2.rgb : c3.rgb));
}

// The four colours at the neon ring's stops (0, .27, .52, .76, back to the first),
// starting `off` colours in. `g` is the conical gradient position.
vec3 neonPal(float g, int off)
{
    g = fract(g);
    if (g < 0.27) return mix(stopCol(off), stopCol(off + 1), g / 0.27);
    if (g < 0.52) return mix(stopCol(off + 1), stopCol(off + 2), (g - 0.27) / 0.25);
    if (g < 0.76) return mix(stopCol(off + 2), stopCol(off + 3), (g - 0.52) / 0.24);
    return mix(stopCol(off + 3), stopCol(off), (g - 0.76) / 0.24);
}

// Evenly spaced stops (BuddyRing.mix).
vec3 mixPal(float t)
{
    float x = fract(t) * 4.0;
    int i = int(floor(x));
    return mix(stopCol(i), stopCol(i + 1), fract(x));
}

vec4 over(vec4 src, vec4 dst) { return src + dst * (1.0 - src.a); }
float comb(float a, float b) { return a + b - a * b; }
float cover(float d, float halfW) { return clamp(halfW - abs(d) + 0.5, 0.0, 1.0); }
float cwTurns(vec2 p) { return fract(atan(p.x, -p.y) / TAU); }

float smoothFall(float x)
{
    float t = clamp((x - 0.25) / 0.75, 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

// The soft halo: the stacked, widening, fading strokes of BuddyRing.haloLayers as
// the smooth profile they approximate. `w` are full widths in px, `a` their alphas.
float halo(float u, vec4 w, vec4 a)
{
    float sum = 0.0;
    for (int k = 0; k < 4; ++k) {
        if (w[k] > 0.0)
            sum += a[k] * smoothFall(abs(u) / (w[k] * 0.5));
    }
    return min(1.0, sum * glow);
}

// A full conic-gradient ring, optionally turning, with its halo.
vec4 fullRing(float r, float cw, float theta, int off, float R, float w, bool crisp, vec4 hw, vec4 ha)
{
    float a = halo(r - R, hw, ha);
    if (crisp)
        a = comb(a, cover(r - R, w * 0.5));
    return vec4(neonPal(theta - cw, off) * a, a);
}

vec4 plainRing(float r, float cw, float R, float w, float alpha)
{
    float a = cover(r - R, w * 0.5) * alpha;
    return vec4(neonPal(-cw, 0) * a, a);
}

// A comet: bright head, a tail that thins out behind it.
vec4 arcCol(float g, vec3 head, vec3 mid, vec3 tail, float L)
{
    float m = 0.42 * L;
    if (g < 0.01) return vec4(head, g / 0.01);
    if (g < m) {
        float f = (g - 0.01) / (m - 0.01);
        return vec4(mix(head, mid, f), mix(1.0, 0.72, f));
    }
    if (g < L) {
        float f = (g - m) / (L - m);
        return vec4(mix(mid, tail, f), mix(0.72, 0.0, f));
    }
    return vec4(tail, 0.0);
}

vec4 arc(float r, float cw, float theta, float R, float w, vec3 head, vec3 mid, vec3 tail, float L)
{
    vec4 c = arcCol(fract(theta - cw), head, mid, tail, L);
    float h = halo(r - R, vec4(5.2, 3.0, 1.7, 0.0) * w, vec4(0.07, 0.14, 0.30, 0.0));
    float a = comb(h, cover(r - R, w * 0.5)) * c.a;
    return vec4(c.rgb * a, a);
}

// One soft round light (BuddyBlob).
vec4 blob(vec2 p, vec2 c, float R, vec3 col, float a)
{
    float x = length(p - c) / max(R, 0.001);
    float f = exp(-3.4 * x * x) * (1.0 - smoothstep(0.65, 1.0, x));
    float al = a * f;
    return vec4(col * al, al);
}

vec4 dotAt(vec2 p, vec2 c, float rad, vec3 col)
{
    float a = clamp(rad - length(p - c) + 0.5, 0.0, 1.0);
    return vec4(col * a, a);
}

vec2 onRing(float R, float turns)
{
    float a = turns * TAU;
    return R * vec2(sin(a), -cos(a));
}

void main()
{
    vec2 p = (qt_TexCoord0 - 0.5) * quad;
    float r = length(p);
    float cw = cwTurns(p);
    vec4 o = vec4(0.0);

    if (style == 0) {                                   // neon
        o = fullRing(r, cw, time / 9.0, 0, midR, thick, true,
                     vec4(15.0, 9.5, 5.5, 0.0) * s, vec4(0.06, 0.12, 0.26, 0.0));
    } else if (style == 1) {                            // aurora
        o = fullRing(r, cw, 0.0, 0, midR, thick, false, vec4(11.0, 0.0, 0.0, 0.0) * s, vec4(0.05, 0.0, 0.0, 0.0));
        o = over(arc(r, cw, time / 5.6, midR, thick, c0.rgb, c1.rgb, c1.rgb, 0.62), o);
        o = over(arc(r, cw, 0.5 - time / 7.8, midR, thick, c2.rgb, c3.rgb, c3.rgb, 0.58), o);
        o = over(arc(r, cw, 0.25 + time / 11.0, midR + 4.5 * s, thick * 0.55, c1.rgb, c2.rgb, c2.rgb, 0.3), o);
    } else if (style == 2) {                            // comet
        o = plainRing(r, cw, midR, max(1.0, thick * 0.45), 0.16);
        float th = time / 3.2;
        o = over(arc(r, cw, th, midR, thick * 1.15, vec3(1.0), c0.rgb, c1.rgb, 0.86), o);
        vec2 head = onRing(midR, th);
        o = over(blob(p, head, thick * 5.5, c0.rgb, min(1.0, 0.55 * glow)), o);
        o = over(dotAt(p, head, thick * 1.3, vec3(1.0)), o);
    } else if (style == 3) {                            // orbit
        o = plainRing(r, cw, midR, max(1.0, thick * 0.5), 0.22);
        // per moon: radius offset, diameter, period (s)
        const vec3 rd[3] = vec3[3](vec3(3.0, 5.2, 5.2), vec3(8.5, 4.2, 7.6), vec3(14.0, 3.2, 3.4));
        const float dir[3] = float[3](1.0, -1.0, 1.0);
        const float from[3] = float[3](20.0 / 360.0, 140.0 / 360.0, 250.0 / 360.0);
        const float ct[3] = float[3](0.0, 0.5, 0.25);
        for (int i = 0; i < 3; ++i) {
            vec2 m = onRing(midR + rd[i].x * s, from[i] + dir[i] * time / rd[i].z);
            vec3 col = mixPal(ct[i]);
            o = over(blob(p, m, rd[i].y * s * 2.6, col, min(1.0, 0.65 * glow)), o);
            o = over(dotAt(p, m, rd[i].y * s * 0.5, col), o);
        }
    } else if (style == 4) {                            // dashes (beads)
        const float N = 30.0;
        float cycle = 2.0;
        float w = max(1.8, thick * 1.05);
        float h = thick * 3.4;
        float R0 = midR + thick * 0.6;
        float f = cw * N;
        float idx = floor(f + 0.5);
        float tang = (f - idx) / N * TAU * R0;
        float L = max(0.0, h * 0.5 - w * 0.5);
        vec2 q = vec2(tang, r - R0);
        float dist = length(q - vec2(0.0, clamp(q.y, -L, L))) - w * 0.5;
        float a = clamp(0.5 - dist, 0.0, 1.0);
        float tt = time - idx * cycle / N;
        float x = tt < 0.0 ? 1.0e6 : mod(tt, cycle) * 1000.0;
        float fall = cycle * 420.0;
        float lit = x < 110.0 ? mix(0.3, 1.0, x / 110.0)
                  : (x < 110.0 + fall ? mix(1.0, 0.3, (x - 110.0) / fall) : 0.3);
        a *= lit;
        o = vec4(mixPal(mod(idx, N) / N) * a, a);
    } else if (style == 5) {                            // pulse
        float cycle = 2.4;
        o = plainRing(r, cw, midR, thick, 1.0);
        o = over(plainRing(r, cw, midR, thick * 3.2, min(1.0, 0.16 * glow)), o);
        for (int i = 0; i < 3; ++i) {
            float tt = time - float(i) * cycle / 3.0;
            if (tt < 0.0)
                continue;
            float u = fract(tt / cycle);
            float sc = 1.0 + 0.62 * u * (2.0 - u);
            float v = max(0.0, (u - 0.12) / 0.88);
            float op = u < 0.12 ? 0.8 * u / 0.12 : 0.8 * (1.0 - v) * (1.0 - v);
            o = over(plainRing(r, cw, (midR + 1.5 * s) * sc, max(1.2, thick * 0.8) * sc, op), o);
        }
    } else if (style == 6) {                            // prism
        o = fullRing(r, cw, time / 9.0, 0, midR, thick * 0.8, true,
                     vec4(8.0, 0.0, 0.0, 0.0) * s, vec4(0.12, 0.0, 0.0, 0.0));
        o = over(fullRing(r, cw, -time / 6.5, 1, midR + 4.0 * s, thick * 0.6, true,
                          vec4(6.0, 0.0, 0.0, 0.0) * s, vec4(0.10, 0.0, 0.0, 0.0)), o);
        o = over(fullRing(r, cw, time / 12.5, 2, midR + 8.0 * s, thick * 0.45, true,
                          vec4(5.0, 0.0, 0.0, 0.0) * s, vec4(0.08, 0.0, 0.0, 0.0)), o);
    } else {                                            // halo
        float sc = 1.01 - 0.04 * cos(TAU * time / 4.8);
        o = fullRing(r / sc, cw, time / 16.0, 0, midR, thick, false,
                     vec4(30.0, 21.0, 14.0, 8.0) * s, vec4(0.05, 0.08, 0.14, 0.24));
    }

    fragColor = o * qt_Opacity;
}
