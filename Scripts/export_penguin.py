"""Run with Blender 5.3 in background mode. The source blend is never saved."""
from pathlib import Path

import bpy
from bpy_extras.anim_utils import action_get_channelbag_for_slot
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / "SourceAssets/Penguin/Blender/Cold Loot - Penguin.blend"
output = ROOT / "SourceAssets/Penguin/FBX"
output.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(source))

scene = bpy.context.scene
rig = bpy.data.objects["Penguin Rig"]
anim = rig.animation_data
anim.use_nla = False
# Bake centimeters into the rig and animation channels. Unreal's animation-only
# FBX import strips the Armature object and its scale.
centimeters = 100
unit_transform = Matrix.Scale(centimeters, 4)
for obj in scene.objects:
    if obj.type in {"MESH", "ARMATURE"}:
        obj.data.transform(unit_transform)
        obj.location *= centimeters
        parent_inverse = obj.matrix_parent_inverse.copy()
        parent_inverse.translation *= centimeters
        obj.matrix_parent_inverse = parent_inverse
for action in bpy.data.actions:
    for curve in action_get_channelbag_for_slot(action, action.slots[0]).fcurves:
        if curve.data_path.endswith(".location"):
            for point in curve.keyframe_points:
                point.co.y *= centimeters
                point.handle_left.y *= centimeters
                point.handle_right.y *= centimeters
            curve.update()
scene.unit_settings.scale_length = 0.01

slide = bpy.data.actions["Slide_Belly"]
slide_curves = action_get_channelbag_for_slot(slide, slide.slots[0]).fcurves
# Root's local Z is the forward axis of this Blender rig.
forward = slide_curves.find('pose.bones["Root"].location', index=2)
for point in forward.keyframe_points:
    point.co.y = point.handle_left.y = point.handle_right.y = 0
forward.update()

rig.name = "Armature"  # UE's legacy FBX importer strips this Blender container node.
bpy.ops.object.select_all(action="DESELECT")
for obj in scene.objects:
    obj.select_set(obj.type in {"MESH", "ARMATURE"})
    if obj.type == "MESH":
        obj.name = "Mesh_" + obj.name
bpy.context.view_layer.objects.active = rig

for filename, action_name in (
    ("SK_Penguin", None),
    ("A_Walk_Shuffle", "Walk_Shuffle"),
    ("A_Sprint_Spring", "Sprint_Spring"),
    ("A_Slide_Belly", "Slide_Belly"),
):
    anim.action = bpy.data.actions[action_name] if action_name else None
    if action_name:
        anim.action_slot = anim.action.slots[0]
        scene.frame_start, scene.frame_end = map(round, anim.action.frame_range)
    rig.data.pose_position = "POSE" if action_name else "REST"
    scene.frame_set(scene.frame_start)
    bpy.ops.export_scene.fbx(
        filepath=str(output / f"{filename}.fbx"),
        use_selection=True,
        axis_forward="-Y",
        axis_up="Z",
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        add_leaf_bones=False,
        mesh_smooth_type="FACE",
        bake_anim=bool(action_name),
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        bake_anim_simplify_factor=0,
    )
