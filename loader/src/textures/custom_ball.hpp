#pragma once



#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace jvm {
namespace custom_ball {

static constexpr const char *kNodeName = "JVM_CustomBall";
static constexpr int kDivs = 20;

godot::Ref<godot::ArrayMesh> make_mesh(int divs = kDivs);
godot::Ref<godot::Shader> make_shader();
godot::Ref<godot::ShaderMaterial> make_material(const godot::Ref<godot::Texture2D> &albedo,
		const godot::Color &albedo_color, const godot::ShaderMaterial *tuned);

}
}
