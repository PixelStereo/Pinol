/*{
	"ISFVSN": "2",
	"DESCRIPTION": "An endless lattice of thin beams to fly through: turned, twisted, with glow and fog.",
	"CREDIT": "Pixel Stereo",
	"CATEGORIES": ["Generator", "3D"],
	"INPUTS": [
		{"NAME": "speed", "LABEL": "Speed", "TYPE": "float", "DEFAULT": 0.4, "MIN": -6, "MAX": 6},
		{"NAME": "yaw", "LABEL": "Yaw (°)", "TYPE": "float", "DEFAULT": 0, "MIN": -180, "MAX": 180},
		{"NAME": "pitch", "LABEL": "Pitch (°)", "TYPE": "float", "DEFAULT": 0, "MIN": -90, "MAX": 90},
		{"NAME": "roll", "LABEL": "Roll (°)", "TYPE": "float", "DEFAULT": 0, "MIN": -180, "MAX": 180},
		{"NAME": "cell", "LABEL": "Cell", "TYPE": "float", "DEFAULT": 2, "MIN": 0.5, "MAX": 8},
		{"NAME": "thickness", "LABEL": "Thickness", "TYPE": "float", "DEFAULT": 0.012, "MIN": 0.001, "MAX": 0.3},
		{"NAME": "glow", "LABEL": "Glow", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 3},
		{"NAME": "fog", "LABEL": "Fog", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 2},
		{"NAME": "warp", "LABEL": "Twist", "TYPE": "float", "DEFAULT": 0, "MIN": -2, "MAX": 2},
		{"NAME": "axes", "LABEL": "Beams", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1, 2], "LABELS": ["All three", "Across", "Along"]},
		{"NAME": "fov", "LABEL": "Field of View (°)", "TYPE": "float", "DEFAULT": 70, "MIN": 10, "MAX": 140},
		{"NAME": "hit", "LABEL": "Hit", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "invert", "LABEL": "Invert", "TYPE": "bool", "DEFAULT": false},
		{"NAME": "tint", "LABEL": "Tint", "TYPE": "color", "DEFAULT": [1, 1, 1, 1]}
	]
}*/

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

float beams(vec3 p)
{
	p.xy = rot(p.z * warp * 0.12) * p.xy;
	vec3 q = mod(p, cell) - cell * 0.5;
	float across = min(length(q.yz), length(q.xz)), along = length(q.xy);
	float d = axes == 1 ? across : axes == 2 ? along : min(across, along);
	return d - thickness * (1.0 + hit * 1.5);
}

void main()
{
	vec2 uv = (gl_FragCoord.xy - 0.5 * RENDERSIZE) / RENDERSIZE.y;
	vec3 ro = vec3(1e-3, 1e-3, TIME * speed * cell);
	vec3 rd = normalize(vec3(uv, 0.5 / tan(radians(fov) * 0.5)));
	rd.xy = rot(radians(roll)) * rd.xy;
	rd.yz = rot(radians(pitch)) * rd.yz;
	rd.xz = rot(radians(yaw)) * rd.xz;
	float t = 0.0, halo = 0.0, solid = 0.0;
	for (int i = 0; i < 90; ++i) {
		float d = beams(ro + rd * t);
		halo += exp(-d * 60.0 / max(cell, 0.5)) * exp(-t * fog * 0.12) * 0.02;
		if (d < 1e-3) { solid = exp(-t * fog * 0.1); break; }
		t += max(d * 0.85, 2e-3);
		if (t > 60.0) break;
	}
	float v = clamp(solid + halo * glow + hit * 0.15, 0.0, 1.0);
	if (invert) v = 1.0 - v;
	gl_FragColor = vec4(tint.rgb * v, 1.0);
}
