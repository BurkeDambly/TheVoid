"""Author and export a separate flipper-stroke loop using the existing penguin rig."""

from math import cos, pi, radians, sin
from pathlib import Path

import bpy
from bpy_extras.anim_utils import action_get_channelbag_for_slot
from mathutils import Matrix, Vector


project = Path(__file__).resolve().parents[1]
source = project / "SourceAssets/Penguin/Blender/Cold Loot - Penguin.blend"
destination = project / "SourceAssets/Penguin/FBX/A_Swim_Paddle.fbx"
bpy.ops.wm.open_mainfile(filepath=str(source))
scene = bpy.context.scene
rig = bpy.data.objects["Penguin Rig"]
animation = rig.animation_data
animation.use_nla = False
animation.action = bpy.data.actions.new("Swim_Paddle")
root = rig.pose.bones["Root"]
head = rig.pose.bones["Head.Slide"]
flippers = [(rig.pose.bones["Flipper.L"], 1.0, 0.0), (rig.pose.bones["Flipper.R"], -1.0, 0.045)]
feet = (rig.pose.bones["Foot.L"], rig.pose.bones["Foot.R"])
for bone in rig.pose.bones:
    bone.matrix_basis = Matrix.Identity(4)

# The outstretched recovery is followed by a quick pull back toward the tail.
stroke = (
    (0.00, Vector((.32, .94, -.10))),
    (0.20, Vector((.72, .36, -.10))),
    (0.42, Vector((.87, -.50, -.13))),
    (0.55, Vector((.98, -.13, -.26))),
    (0.81, Vector((.52, .80, -.26))),
    (1.00, Vector((.32, .94, -.10))),
)


def direction_at(phase):
    for (start, first), (end, last) in zip(stroke, stroke[1:]):
        if phase <= end:
            t = (phase - start) / (end - start)
            return first.lerp(last, t * t * (3.0 - 2.0 * t)).normalized()
    return stroke[-1][1].normalized()


scene.frame_start, scene.frame_end = 1, 25
scene.render.fps = 30
for frame in range(scene.frame_start, scene.frame_end + 1):
    phase = (frame - scene.frame_start) / (scene.frame_end - scene.frame_start)
    turn = phase * 2.0 * pi
    root.rotation_mode = head.rotation_mode = "XYZ"
    root.rotation_euler = tuple(map(radians, (76.0 + 3.7 * sin(turn), 1.4 * sin(turn), 2.3 * cos(turn))))
    root.location = (0.0, .43 + .036 * sin(turn - .4), 0.0)
    head.rotation_euler = (radians(-17.0 + 2.0 * sin(turn - .6)), 0.0, 0.0)
    root.keyframe_insert("rotation_euler", frame=frame, group=root.name)
    root.keyframe_insert("location", frame=frame, group=root.name)
    head.keyframe_insert("rotation_euler", frame=frame, group=head.name)
    root_rotation = (root.bone.matrix_local @ root.matrix_basis).to_quaternion()
    for flipper, side, delay in flippers:
        direction = direction_at((phase + delay) % 1.0)
        direction.x *= side
        rest = root.bone.matrix_local.inverted() @ flipper.bone.matrix_local
        frame_rotation = root_rotation @ rest.to_quaternion()
        axis = frame_rotation @ Vector((0, 1, 0))
        swing = axis.rotation_difference(direction)
        flipper.rotation_mode = "QUATERNION"
        flipper.rotation_quaternion = frame_rotation.inverted() @ swing @ frame_rotation
        flipper.keyframe_insert("rotation_quaternion", frame=frame, group=flipper.name)
    for side, foot in enumerate(feet):
        foot.rotation_mode = "XYZ"
        foot.rotation_euler = (radians(11.0 + 7.0 * sin(2.0 * turn + side * pi)), 0.0, 0.0)
        foot.keyframe_insert("rotation_euler", frame=frame, group=foot.name)

# Follow the existing animation-only FBX convention so the import uses the same reference skeleton.
scale = Matrix.Scale(100, 4)
for obj in scene.objects:
    if obj.type in {"MESH", "ARMATURE"}:
        obj.data.transform(scale)
        obj.location *= 100
        inverse = obj.matrix_parent_inverse.copy()
        inverse.translation *= 100
        obj.matrix_parent_inverse = inverse
curves = action_get_channelbag_for_slot(animation.action, animation.action_slot).fcurves
for curve in curves:
    if curve.data_path.endswith(".location"):
        for key in curve.keyframe_points:
            key.co.y *= 100
            key.handle_left.y *= 100
            key.handle_right.y *= 100
        curve.update()

scene.unit_settings.scale_length = .01
rig.name = "Armature"
rig.data.pose_position = "POSE"
bpy.ops.object.select_all(action="DESELECT")
for obj in scene.objects:
    obj.select_set(obj.type in {"MESH", "ARMATURE"})
    if obj.type == "MESH":
        obj.name = "Mesh_" + obj.name
bpy.context.view_layer.objects.active = rig
scene.frame_set(scene.frame_start)
bpy.ops.export_scene.fbx(
    filepath=str(destination), use_selection=True, axis_forward="-Y", axis_up="Z",
    apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", add_leaf_bones=False,
    mesh_smooth_type="FACE", bake_anim=True, bake_anim_use_all_actions=False,
    bake_anim_use_nla_strips=False, bake_anim_simplify_factor=0,
)
