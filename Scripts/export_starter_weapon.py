"""Export the confirmed Blender fit and weapon parts without saving the source scene."""
import json
from pathlib import Path

import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "SourceAssets/Weapons/Blender/Cold Loot - Penguin Weapon Fit.blend"
OUTPUT = ROOT / "SourceAssets/Weapons/FBX"
OUTPUT.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))

scene = bpy.context.scene
objects = bpy.data.objects
rig = objects["Penguin Rig"]
if bpy.context.object and bpy.context.object.mode != "OBJECT":
    bpy.ops.object.mode_set(mode="OBJECT")
receiver = objects["Body"]
contact = objects["Weapon_Contact"]
parts = {"Body": "SM_StarterReceiver", "Slide": "SM_StarterBolt", "Drum Magazine": "SM_StarterDrum"}
scale_cm = Matrix.Scale(100, 4)


def ue_transform(matrix):
    x, y, z = matrix.translation
    q = matrix.to_quaternion()
    return {"location": [100*x, -100*y, 100*z], "rotation": [-q.x, q.y, -q.z, q.w]}


def export(path, animated=False):
    bpy.ops.export_scene.fbx(
        filepath=str(OUTPUT / f"{path}.fbx"), use_selection=True,
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS", add_leaf_bones=False,
        mesh_smooth_type="FACE", bake_anim=animated,
        bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
        bake_anim_simplify_factor=0,
    )


bpy.context.view_layer.update()
body_from_world = receiver.matrix_world.inverted()
assert (contact.matrix_world.translation - objects["Flipper_Weapon_Contact"].matrix_world.translation).length < 0.001
fit = {
    "grip_on_penguin": ue_transform(objects["Flipper_Weapon_Contact"].matrix_world),
    "receiver_on_grip": ue_transform(contact.matrix_world.inverted() @ receiver.matrix_world),
    "bolt_on_receiver": ue_transform(body_from_world @ objects["Slide"].matrix_world),
    "drum_on_receiver": ue_transform(body_from_world @ objects["Drum Magazine"].matrix_world),
    "weapon_contact": ue_transform(body_from_world @ contact.matrix_world),
    "optic_mount": ue_transform(body_from_world @ objects["Optic_Mount"].matrix_world),
}
depsgraph = bpy.context.evaluated_depsgraph_get()
front = max(v.co.x for v in receiver.data.vertices)
rim = [v.co for v in receiver.data.vertices if v.co.x >= front - 0.008]
muzzle_z = (min(v.z for v in rim) + max(v.z for v in rim)) * 0.5
muzzle = Matrix.Translation((front, 0, muzzle_z))
fit["muzzle"] = ue_transform(muzzle)

scene.unit_settings.scale_length = 0.01
for object_name, asset_name in parts.items():
    orig = objects[object_name]
    mesh = bpy.data.meshes.new_from_object(orig.evaluated_get(depsgraph))
    mesh.transform(scale_cm)
    copy = bpy.data.objects.new(asset_name, mesh)
    scene.collection.objects.link(copy)
    bpy.ops.object.select_all(action="DESELECT")
    copy.select_set(True)
    bpy.context.view_layer.objects.active = copy
    export(asset_name)
    bpy.data.objects.remove(copy, do_unlink=True)

# A two-frame sequence carries the exact fitted bone transform into Unreal.
rig.animation_data_clear()
hold = {bone.name: bone.matrix_basis.copy() for bone in rig.pose.bones}
bpy.ops.object.select_all(action="DESELECT")
for obj in scene.objects:
    if obj == rig or (obj.type == "MESH" and obj.parent == rig):
        obj.data.transform(scale_cm)
        obj.location *= 100
        parent_inverse = obj.matrix_parent_inverse.copy()
        parent_inverse.translation *= 100
        obj.matrix_parent_inverse = parent_inverse
        obj.select_set(True)
        if obj != rig:
            obj.name = "Mesh_" + obj.name

rig.name = "Armature"
bpy.context.view_layer.objects.active = rig
scene.frame_start, scene.frame_end = 1, 2
for frame in (1, 2):
    scene.frame_set(frame)
    for bone in rig.pose.bones:
        bone.rotation_mode = "QUATERNION"
        basis = hold[bone.name].copy()
        basis.translation *= 100
        bone.matrix_basis = basis
        for channel in ("location", "rotation_quaternion", "scale"):
            bone.keyframe_insert(data_path=channel, frame=frame)
scene.frame_set(1)
export("A_StarterWeaponHold", animated=True)
(OUTPUT / "StarterWeaponFit.json").write_text(json.dumps(fit, indent=2) + "\n", encoding="utf-8")
