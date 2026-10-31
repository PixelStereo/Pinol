/*{
	"ISFVSN": "2",
	"DESCRIPTION": "Bars of light: counted, swept, turned, mirrored, outlined, crossed into a grid, strobed.",
	"CREDIT": "Pixel Stereo",
	"CATEGORIES": ["Generator", "Light"],
	"INPUTS": [
		{"NAME": "count", "LABEL": "Count", "TYPE": "float", "DEFAULT": 8, "MIN": 1, "MAX": 96},
		{"NAME": "width", "LABEL": "Width", "TYPE": "float", "DEFAULT": 0.12, "MIN": 0, "MAX": 1},
		{"NAME": "phase", "LABEL": "Phase", "TYPE": "float", "DEFAULT": 0, "MIN": -16, "MAX": 16},
		{"NAME": "speed", "LABEL": "Speed", "TYPE": "float", "DEFAULT": 0, "MIN": -8, "MAX": 8},
		{"NAME": "angle", "LABEL": "Angle (°)", "TYPE": "float", "DEFAULT": 0, "MIN": -180, "MAX": 180},
		{"NAME": "spread", "LABEL": "Spread", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 1},
		{"NAME": "jitter", "LABEL": "Jitter", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "hit", "LABEL": "Hit", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "strobe", "LABEL": "Strobe (Hz)", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 30},
		{"NAME": "mirror", "LABEL": "Mirror", "TYPE": "bool", "DEFAULT": false},
		{"NAME": "outline", "LABEL": "Outline", "TYPE": "bool", "DEFAULT": false},
		{"NAME": "grid", "LABEL": "Grid", "TYPE": "bool", "DEFAULT": false},
		{"NAME": "tint", "LABEL": "Tint", "TYPE": "color", "DEFAULT": [1, 1, 1, 1]}
	]
}*/

float h11(float n) { return fract(sin(n * 127.1) * 43758.5453); }

// Bars along u (0..1 across the spread)
float bars(float u, float px)
{
	if (mirror) u = abs(u - 0.5) * 2.0;
	float s = u * count + phase + TIME * speed;
	float i = floor(s);
	float f = fract(s) + (h11(i) - 0.5) * jitter;
	float w = clamp(width + hit * 0.35, 0.0, 1.0) * 0.5;
	float edge = px * count;
	float inside = smoothstep(w + edge, w, abs(f - 0.5));
	if (outline) inside -= smoothstep(w - edge * 1.5, w - edge * 2.5, abs(f - 0.5));
	return clamp(inside, 0.0, 1.0);
}

void main()
{
	vec2 p = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / RENDERSIZE.y;
	float a = radians(angle), aspect = RENDERSIZE.x / RENDERSIZE.y;
	vec2 q = vec2(cos(a) * p.x + sin(a) * p.y, -sin(a) * p.x + cos(a) * p.y);
	float span = mix(aspect, 1.0, abs(sin(a))) * max(spread, 1e-3);
	float spanOther = mix(1.0, aspect, abs(sin(a)));
	float px = 1.0 / RENDERSIZE.y / span;
	float u = q.x / span + 0.5;
	float v = (u >= 0.0 && u <= 1.0) ? bars(u, px) : 0.0;
	if (grid) {
		float w2 = q.y / spanOther + 0.5;
		v = max(v, bars(w2, 1.0 / RENDERSIZE.y / spanOther));
	}
	if (strobe > 0.0) v *= step(0.5, fract(TIME * strobe));
	v = clamp(v * (1.0 + hit * 0.5), 0.0, 1.0);
	gl_FragColor = vec4(tint.rgb * v, v); // transparent between the bars
}
