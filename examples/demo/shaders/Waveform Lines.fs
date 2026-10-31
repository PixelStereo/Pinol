/*{
	"ISFVSN": "2",
	"DESCRIPTION": "The waveform of the sound drawn line after line, the older lines receding in depth.",
	"CREDIT": "Pixel Stereo",
	"CATEGORIES": ["Generator", "Audio Visualizer"],
	"INPUTS": [
		{"NAME": "wave", "TYPE": "audio", "MAX": 256},
		{"NAME": "lines", "LABEL": "Lines", "TYPE": "float", "DEFAULT": 32, "MIN": 1, "MAX": 64},
		{"NAME": "amplitude", "LABEL": "Amplitude", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 3},
		{"NAME": "depth", "LABEL": "Depth", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0, "MAX": 1},
		{"NAME": "tilt", "LABEL": "Tilt", "TYPE": "float", "DEFAULT": 0.35, "MIN": -1, "MAX": 1},
		{"NAME": "thickness", "LABEL": "Thickness (px)", "TYPE": "float", "DEFAULT": 1.5, "MIN": 0.5, "MAX": 12},
		{"NAME": "hit", "LABEL": "Hit", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "tint", "LABEL": "Tint", "TYPE": "color", "DEFAULT": [1, 1, 1, 1]}
	],
	"PASSES": [
		{"TARGET": "history", "PERSISTENT": true, "FLOAT": true, "WIDTH": 256, "HEIGHT": 64},
		{}
	]
}*/

void main()
{
	if (PASSINDEX == 0) {
		// The history: each row moves up one, the bottom one is the waveform now
		vec2 uv = isf_FragNormCoord;
		float row = 1.0 / 64.0;
		if (uv.y < row) {
			float w = IMG_NORM_PIXEL(wave, vec2(uv.x, 0.25)).r;
			gl_FragColor = vec4(w, w, w, 1.0);
		} else {
			gl_FragColor = IMG_NORM_PIXEL(history, uv - vec2(0.0, row));
		}
		return;
	}
	vec2 p = gl_FragCoord.xy / RENDERSIZE.y;
	float aspect = RENDERSIZE.x / RENDERSIZE.y;
	float v = 0.0;
	int n = int(lines);
	for (int i = 0; i < 64; ++i) {
		if (i >= n) break;
		float k = float(i) / max(lines - 1.0, 1.0);    // 0: now (the front), 1: the oldest (the back)
		float scale = mix(1.0, 1.0 - depth * 0.75, k);  // receding
		float base = mix(0.12, 0.92, pow(k, 0.85));
		float x = (p.x - aspect * 0.5) / scale / aspect + 0.5;
		if (x < 0.0 || x > 1.0) continue;
		float w = IMG_NORM_PIXEL(history, vec2(x, (float(i) + 0.5) / 64.0)).r * 2.0 - 1.0;
		float y = base + (x - 0.5) * tilt * 0.3 + w * amplitude * 0.4 * scale * (1.0 + hit);
		float d = abs(p.y - y) * RENDERSIZE.y;
		float line = smoothstep(thickness * scale + 1.0, thickness * scale, d);
		v = max(v, line * mix(1.0, 0.25, k));
	}
	v = clamp(v, 0.0, 1.0);
	gl_FragColor = vec4(tint.rgb * v, v); // transparent between the lines
}
