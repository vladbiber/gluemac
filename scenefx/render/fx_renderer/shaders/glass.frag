// gluemac: Liquid Glass. The blurred backdrop is drawn through the shape of
// the transparency mask; near its edge the samples are pushed outwards like
// light bent by a rounded lens, the colours split a little, and the rim
// catches a light from the top left.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform sampler2D tex;
uniform sampler2D mask;

uniform vec2 buf_size;
uniform vec4 mask_box; // x, y, width, height in buffer pixels
uniform vec4 mask_src; // u, v, width, height in mask texture coordinates
uniform float band;
uniform float strength;
uniform float alpha;

const int DIRS = 12;
const int STEPS = 6;
// translucent panels paint little alpha; above this a pixel is the panel
const float EDGE = 0.1;

float coverage(vec2 p) {
	vec2 q = (p - mask_box.xy) / mask_box.zw;
	if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0) {
		return 0.0;
	}
	return texture2D(mask, mask_src.xy + q * mask_src.zw).a;
}

void main() {
	vec2 p = v_texcoord * buf_size;
	// a panel's soft shadow is blurred as before, the glass is the panel
	if (coverage(p) < EDGE) {
		gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb * alpha, alpha);
		return;
	}
	float dist = band;
	vec2 normal = vec2(0.0);

	for (int i = 0; i < DIRS; i++) {
		float a = float(i) * 6.2831853 / float(DIRS);
		vec2 dir = vec2(cos(a), sin(a));
		for (int k = 1; k <= STEPS; k++) {
			float r = band * float(k) / float(STEPS);
			if (r >= dist) {
				break;
			}
			if (coverage(p + dir * r) < EDGE) {
				dist = r;
				break;
			}
		}
		normal += dir * (coverage(p + dir * band * 0.6) < EDGE ? 1.0 : 0.0);
	}
	float nl = length(normal);
	normal = nl > 0.001 ? normal / nl : vec2(0.0);

	// 1 on the rim, 0 a band's width inside: a quarter circle bezel
	float e = 1.0 - clamp(dist / max(band, 1.0), 0.0, 1.0);
	float bend = 1.0 - sqrt(max(0.0, 1.0 - e * e));
	float shift = strength * band * 0.9 * bend;
	float split = 0.35 * strength * bend;

	vec2 o = normal * shift;
	vec3 color;
	color.r = texture2D(tex, (p + o * (1.0 + split)) / buf_size).r;
	color.g = texture2D(tex, (p + o) / buf_size).g;
	color.b = texture2D(tex, (p + o * (1.0 - split)) / buf_size).b;

	// a thin bright rim, stronger where it faces the light
	vec2 light = normalize(vec2(-1.0, -1.0));
	float facing = dot(normal, light);
	float rim = exp(-dist / 1.2) * (0.22 + 0.38 * max(facing, 0.0) + 0.12 * max(-facing, 0.0));
	float sheen = e * e * 0.06 * (0.5 + 0.5 * facing);
	color += (rim + sheen) * strength;

	gl_FragColor = vec4(clamp(color, 0.0, 1.0) * alpha, alpha);
}
