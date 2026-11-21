"""Export the editable Arctic Fish and its in-place swim loop from Blender."""
from pathlib import Path

import bpy
from bpy_extras.anim_utils import action_get_channelbag_for_slot
from mathutils import Matrix


ROOT = Path(__file__).resolve().parents[1] / "SourceAssets/Fish"
OUTPUT = ROOT / "FBX"
OUTPUT.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "Blender/Cold Loot - Arctic Fish.blend"))

scene = bpy.context.scene
fish = bpy.data.collections["Arctic Fish"]
rig = fish.objects["Arctic Fish Rig"]
animation = rig.animation_data
swim = animation.action
channels = action_get_channelbag_for_slot(swim, animation.action_slot)
# The actor controls world position and pitch. Only export the skeletal stroke.
for curve in list(channels.fcurves):
    if not curve.data_path.startswith("pose.bones["):
        channels.fcurves.remove(curve)
rig.location = (0, 0, 0)
rig.rotation_euler = (0, 0, 0)
scene.frame_set(1)
rig.name = "Armature"

surface = bpy.data.materials.new("ArcticFish")
bpy.ops.object.select_all(action="DESELECT")
for obj in fish.objects:
    if obj.type != "MESH":
        continue
    obj.select_set(True)
    mesh = obj.data
    material = mesh.materials[0]
    ramp = next((node.color_ramp for node in material.node_tree.nodes if node.type == "VALTORGB"), None)
    pigment = mesh.attributes.get("Pigment")
    colors = mesh.color_attributes.new(name="Fish Color", type="FLOAT_COLOR", domain="POINT")
    values = [tuple(ramp.evaluate(pigment.data[vertex.index].value)) if ramp
              else tuple(material.diffuse_color) for vertex in mesh.vertices]
    colors.data.foreach_set("color", [channel for color in values for channel in color])
    mesh.color_attributes.active_color = colors
    mesh.materials.clear()
    mesh.materials.append(surface)
    if not mesh.uv_layers:
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for loop in mesh.loops:
            position = mesh.vertices[loop.vertex_index].co
            uv_layer.uv[loop.index].vector = (0.5 + position.x, 0.5 + position.z + 0.25 * position.y)

body = fish.objects["Body"]
bpy.context.view_layer.objects.active = body
bpy.ops.object.join()
body.name = "ArcticFish"

# Unreal strips the Armature object's scale on animation-only import. Bake
# centimeters into the rest bones and geometry before exporting either FBX.
centimeters = Matrix.Scale(100, 4)
for obj in (body, rig):
    obj.data.transform(centimeters)
    obj.location *= 100
scene.unit_settings.scale_length = 0.01

rig.select_set(True)
bpy.context.view_layer.objects.active = rig


def export(name, animated):
    bpy.ops.export_scene.fbx(
        filepath=str(OUTPUT / f"{name}.fbx"),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        axis_forward="-Y",
        axis_up="Z",
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        colors_type="LINEAR",
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
        bake_anim=animated,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        bake_anim_simplify_factor=0,
    )


animation.action = None
rig.data.pose_position = "REST"
export("SK_ArcticFish", False)

animation.action = swim
rig.data.pose_position = "POSE"
scene.frame_end = 49
scene.frame_set(1)
body.select_set(False)
export("A_ArcticFishSwim", True)
