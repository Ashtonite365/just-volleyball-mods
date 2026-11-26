#include "custom_ball.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

namespace jvm {
namespace custom_ball {




static const char *kShader = R"SHADER(
shader_type spatial;
render_mode unshaded, cull_back;
stencil_mode write, compare_always, 7;

uniform float world_lit = 0.0;
global uniform vec3 jv_body_tint;
global uniform float jv_night_rim;

uniform sampler2D albedo_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec4 albedo_color : source_color = vec4(1.0, 1.0, 1.0, 1.0);
uniform vec3 local_center = vec3(0.0, 0.0, 0.0);

uniform vec3 key_dir = vec3(-0.45, 0.62, 0.65);
uniform float band_edge = 0.45;
uniform float shade = 0.465;
uniform float band_softness = 0.0;

uniform float rim_amount : hint_range(0.0, 1.0) = 0.0;
uniform float rim_width : hint_range(0.0, 1.0) = 0.085;
uniform float rim_side : hint_range(0.0, 1.0) = 1.0;
uniform float rim_softness = 0.03;

varying vec3 sphere_n;
varying vec3 obj_dir;

vec3 jv_srgb_to_linear(vec3 c) {
	return mix(pow((c + vec3(0.055)) / 1.055, vec3(2.4)), c / 12.92,
		vec3(lessThan(c, vec3(0.04045))));
}
vec3 jv_linear_to_srgb(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - vec3(0.055), c * 12.92,
		vec3(lessThan(c, vec3(0.0031308))));
}
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
vec3 jv_in(vec3 c) { return jv_srgb_to_linear(c); }
vec3 jv_out(vec3 c) { return jv_linear_to_srgb(c); }
#else
vec3 jv_in(vec3 c) { return c; }
vec3 jv_out(vec3 c) { return c; }
#endif

vec3 jv_toon_rim(vec3 c, vec3 n, vec3 v, float k, vec3 l, float lit) {
	float amount = max(rim_amount, jv_night_rim * lit);
	if (amount <= 0.0) { return c; }
	float ndv = clamp(dot(n, normalize(v)), 0.0, 1.0);
	float edge = sqrt(1.0 - ndv * ndv);
	float w = rim_width * k * mix(0.55, 1.0, clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
	float aa = clamp(fwidth(edge) * 0.75, 0.0005, rim_softness);
	float r = smoothstep(1.0 - w - aa, 1.0 - w + aa, edge);
	float away = dot(normalize(n.xy + vec2(0.0001)), -normalize(l.xy + vec2(0.0001)));
	r *= mix(1.0, smoothstep(-0.05, 0.05, away), rim_side);
	return mix(c, vec3(1.0), r * amount * k);
}

// 2:1 equirect, V=0 at the north pole (top of the PNG), U seam on the back (-Z).
vec2 equirect_uv(vec3 d) {
	d = normalize(d);
	float u = atan(d.x, d.z) / (2.0 * PI) + 0.5;
	float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
	return vec2(u, v);
}

void vertex() {
	vec3 dir = VERTEX - local_center;
	float len = length(dir);
	obj_dir = len > 1e-6 ? dir / len : vec3(0.0, 1.0, 0.0);
	sphere_n = (MODELVIEW_MATRIX * vec4(obj_dir, 0.0)).xyz;
}

void fragment() {
	vec3 base = jv_in(texture(albedo_tex, equirect_uv(obj_dir)).rgb) * jv_in(albedo_color.rgb);
	float lit = smoothstep(band_edge - band_softness, band_edge + band_softness,
		dot(normalize(sphere_n), normalize(key_dir)));
	vec3 c = base * mix(1.0 - shade, 1.0, lit);
	c = jv_toon_rim(c, normalize(sphere_n), VIEW, 1.0, normalize(key_dir), world_lit);
	ALBEDO = jv_out(c * mix(vec3(1.0), jv_body_tint, world_lit));
	ALPHA = 1.0;
}
)SHADER";

static Vector3 cube_to_sphere(const Vector3 &p) {

	double x = p.x, y = p.y, z = p.z;
	double x2 = x * x, y2 = y * y, z2 = z * z;
	return Vector3(
			float(x * std::sqrt(std::max(0.0, 1.0 - 0.5 * y2 - 0.5 * z2 + (y2 * z2) / 3.0))),
			float(y * std::sqrt(std::max(0.0, 1.0 - 0.5 * z2 - 0.5 * x2 + (z2 * x2) / 3.0))),
			float(z * std::sqrt(std::max(0.0, 1.0 - 0.5 * x2 - 0.5 * y2 + (x2 * y2) / 3.0))));
}

static Vector2 dir_to_uv(const Vector3 &d) {
	float u = std::atan2(d.x, d.z) / (2.0f * float(3.14159265358979323846)) + 0.5f;
	if (u < 0.0f) u += 1.0f;
	if (u >= 1.0f) u -= 1.0f;
	float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / float(3.14159265358979323846);
	return Vector2(u, v);
}

Ref<ArrayMesh> make_mesh(int divs) {
	if (divs < 2) divs = 2;

	struct Face {
		Vector3 origin, u_axis, v_axis;
	};
	const Face faces[6] = {
		{ Vector3(1, -1, -1), Vector3(0, 0, 2), Vector3(0, 2, 0) },
		{ Vector3(-1, -1, 1), Vector3(0, 0, -2), Vector3(0, 2, 0) },
		{ Vector3(-1, 1, -1), Vector3(2, 0, 0), Vector3(0, 0, 2) },
		{ Vector3(-1, -1, 1), Vector3(2, 0, 0), Vector3(0, 0, -2) },
		{ Vector3(-1, -1, 1), Vector3(2, 0, 0), Vector3(0, 2, 0) },
		{ Vector3(1, -1, -1), Vector3(-2, 0, 0), Vector3(0, 2, 0) },
	};

	PackedVector3Array verts, normals;
	PackedVector2Array uvs;
	PackedInt32Array indices;

	for (int f = 0; f < 6; f++) {
		int base = verts.size();
		int stride = divs + 1;
		PackedVector2Array raw_uv;
		raw_uv.resize(stride * stride);
		for (int j = 0; j <= divs; j++) {
			float tv = float(j) / float(divs);
			for (int i = 0; i <= divs; i++) {
				float tu = float(i) / float(divs);
				Vector3 p = faces[f].origin + faces[f].u_axis * tu + faces[f].v_axis * tv;
				Vector3 s = cube_to_sphere(p);
				verts.push_back(s);
				normals.push_back(s);
				raw_uv[j * stride + i] = dir_to_uv(s);
			}
		}

		auto unwrap = [](Vector2 a, Vector2 b, Vector2 c) {
			float mu = (a.x + b.x + c.x) / 3.0f;
			auto fix = [mu](Vector2 p) {
				if (p.x - mu > 0.5f) p.x -= 1.0f;
				if (mu - p.x > 0.5f) p.x += 1.0f;
				return p;
			};
			return std::array<Vector2, 3>{ fix(a), fix(b), fix(c) };
		};




		for (int n = 0; n < stride * stride; n++) uvs.push_back(raw_uv[n]);

		for (int j = 0; j < divs; j++) {
			for (int i = 0; i < divs; i++) {
				int i00 = base + j * stride + i;
				int i10 = i00 + 1;
				int i01 = i00 + stride;
				int i11 = i01 + 1;
				auto emit = [&](int a, int b, int c) {




					Vector3 pa = verts[a], pb = verts[b], pc = verts[c];
					if ((pb - pa).cross(pc - pa).dot(pa + pb + pc) > 0.0f) std::swap(b, c);
					Vector2 ua = uvs[a], ub = uvs[b], uc = uvs[c];
					if (std::abs(ua.x - ub.x) > 0.5f || std::abs(ua.x - uc.x) > 0.5f || std::abs(ub.x - uc.x) > 0.5f) {
						auto uv3 = unwrap(ua, ub, uc);
						int na = verts.size();
						verts.push_back(verts[a]); normals.push_back(normals[a]); uvs.push_back(uv3[0]);
						verts.push_back(verts[b]); normals.push_back(normals[b]); uvs.push_back(uv3[1]);
						verts.push_back(verts[c]); normals.push_back(normals[c]); uvs.push_back(uv3[2]);
						indices.push_back(na);
						indices.push_back(na + 1);
						indices.push_back(na + 2);
					} else {
						indices.push_back(a);
						indices.push_back(b);
						indices.push_back(c);
					}
				};
				emit(i00, i10, i11);
				emit(i00, i11, i01);
			}
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

Ref<Shader> make_shader() {
	Ref<Shader> sh;
	sh.instantiate();
	sh->set_code(kShader);
	return sh;
}

static void copy_param(ShaderMaterial *dst, const ShaderMaterial *src, const char *name) {
	if (!src) return;
	Variant v = src->get_shader_parameter(StringName(name));
	if (v.get_type() != Variant::NIL) dst->set_shader_parameter(StringName(name), v);
}

Ref<ShaderMaterial> make_material(const Ref<Texture2D> &albedo, const Color &albedo_color,
		const ShaderMaterial *tuned) {
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(make_shader());
	if (albedo.is_valid()) mat->set_shader_parameter("albedo_tex", albedo);
	mat->set_shader_parameter("albedo_color", albedo_color);
	mat->set_shader_parameter("local_center", Vector3());
	const char *kCopied[] = { "key_dir", "band_edge", "shade", "band_softness",
		"rim_amount", "rim_width", "rim_side", "rim_softness", "world_lit" };
	for (const char *n : kCopied) copy_param(mat.ptr(), tuned, n);
	return mat;
}

}
}
