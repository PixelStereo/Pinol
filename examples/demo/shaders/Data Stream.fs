/*{
	"ISFVSN": "2",
	"DESCRIPTION": "Rows of barcodes and binary cells, each streaming at its own pace and lit by the spectrum of the sound: barcode, matrix or spectrum bars, with flicker, glitch, a band and a progress line.",
	"CREDIT": "Pixel Stereo",
	"CATEGORIES": ["Generator", "Data"],
	"INPUTS": [
		{"NAME": "fft", "TYPE": "audioFFT", "MAX": 64},
		{"NAME": "mode", "LABEL": "Mode", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1, 2], "LABELS": ["Barcode", "Matrix", "Spectrum"]},
		{"NAME": "rows", "LABEL": "Rows", "TYPE": "float", "DEFAULT": 24, "MIN": 1, "MAX": 160},
		{"NAME": "density", "LABEL": "Density", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 1},
		{"NAME": "speed", "LABEL": "Speed", "TYPE": "float", "DEFAULT": 0.4, "MIN": -4, "MAX": 4},
		{"NAME": "flicker", "LABEL": "Flicker", "TYPE": "float", "DEFAULT": 0.1, "MIN": 0, "MAX": 1},
		{"NAME": "band", "LABEL": "Band", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 1},
		{"NAME": "center", "LABEL": "Band Center", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 1},
		{"NAME": "hit", "LABEL": "Hit", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "glitch", "LABEL": "Glitch", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "invert", "LABEL": "Invert", "TYPE": "bool", "DEFAULT": false},
		{"NAME": "progress", "LABEL": "Progress", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "tint", "LABEL": "Tint", "TYPE": "color", "DEFAULT": [1, 1, 1, 1]}
	]
}*/

float h11(float n) { return fract(sin(n * 127.1) * 43758.5453); }
float h21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float spectrum(float x) { return IMG_NORM_PIXEL(fft, vec2(clamp(x, 0.0, 1.0), 0.25)).r; }

void main()
{
	vec2 uv = isf_FragNormCoord;
	float px = 1.0 / RENDERSIZE.y;
	float half_ = band * 0.5;
	float dy = uv.y - center;
	float v = 0.0;
	if (abs(dy) <= half_) {
		// Rows across the band (the whole screen at band 1)
		float y = band > 0.0 ? (dy + half_) / max(band, 1e-4) : 0.0;
		float r = floor(y * rows);
		float rh = h11(r + 1.0);
		float frame = floor(TIME * 24.0);
		float shift = step(1.0 - glitch * 0.7, h21(vec2(r, frame))) * (h21(vec2(r, frame + 7.0)) - 0.5) * glitch;
		float level = spectrum((r + 0.5) / rows * 0.6);
		float dir = h11(r + 9.0) < 0.5 ? -1.0 : 1.0;
		if (mode == 1) {
			// Matrix: square cells switching on the beat of their own clocks
			float cols = rows * RENDERSIZE.x / RENDERSIZE.y * band;
			vec2 g = vec2((uv.x + shift) * cols, y * rows);
			vec2 c = floor(g), f = fract(g);
			float tick = floor(TIME * mix(2.0, 18.0, h21(c.yy + 3.0)) * abs(speed));
			float on = step(h21(c + tick * 0.37), density * (0.6 + 0.8 * level));
			float inside = step(0.12, f.x) * step(f.x, 0.88) * step(0.12, f.y) * step(f.y, 0.88);
			v = on * inside;
		} else if (mode == 2) {
			// Spectrum: each row a bar as long as its frequency is loud, its inside still streaming
			float cells = mix(60.0, 300.0, rh);
			float x = (uv.x + shift) * cells + TIME * speed * cells * 0.3 * dir;
			float bar = step(uv.x, pow(level, 0.7) * 1.1);
			v = bar * step(fract(x), mix(0.25, 0.9, h21(vec2(floor(x), r))));
			v = max(v, step(abs(uv.x - pow(level, 0.7) * 1.1), px * 1.5)); // its end, a hair
		} else {
			// Barcode: bars of every width, each row its own speed and way
			float cells = mix(18.0, 420.0, pow(h11(r + 3.1), 2.0));
			float x = (uv.x + shift) * cells + TIME * speed * cells * mix(0.15, 1.2, rh) * dir;
			float c = floor(x);
			float on = step(h21(vec2(c, r)), density);
			v = on * step(fract(x), mix(0.18, 1.0, h21(vec2(c, r + 0.5))));
			v *= 0.8 + 0.2 * smoothstep(0.0, 0.35, level);
		}
		// Flicker: a row blinks out, or flips, for a frame
		float fl = h21(vec2(r, floor(TIME * 30.0)));
		if (fl < flicker * 0.5) v = 0.0;
		else if (fl < flicker) v = 1.0 - v;
		if (invert) v = 1.0 - v;
		if (hit > 0.0) v = mix(v, 1.0 - v, step(0.55, hit)) + hit * 0.25;
		// The band's edges, a hair each
		if (band < 0.999) v = max(v, step(half_ - abs(dy), px * 1.2));
	}
	// The soundtrack's progress, along the bottom
	if (uv.y < 3.0 * px && uv.x < progress) v = 1.0;
	if (progress > 0.0 && uv.y < 9.0 * px && fract(uv.x * 20.0) < 30.0 / RENDERSIZE.x) v = 0.6; // its ticks
	v = clamp(v, 0.0, 1.0);
	gl_FragColor = vec4(tint.rgb * v, v); // transparent where it is dark: laid over other layers
}
