"""Convert the fitted Blender scene's object adjustments into a rig hold pose."""
import json
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "SourceAssets/Penguin/Blender/Cold Loot - Penguin.blend"
SOURCE = ROOT / "SourceAssets/Weapons/Blender/Cold Loot - Penguin Weapon Fit.blend"
OUTPUT = ROOT / "SourceAssets/Weapons/FBX/FitPreview"
FLIPPERS = ("Flipper.R", "Flipper.L")


def unreal_transform(matrix):
    x, y, z = matrix.translation
    q = matrix.to_quaternion()
    return {"location": [100 * x, -100 * y, 100 * z], "rotation": [-q.x, q.y, -q.z, q.w]}


def flipper_transforms(rig):
    inverse = rig.matrix_world.inverted()
    return {name: inverse @ bpy.data.objects[name].matrix_world for name in FLIPPERS}


def main():
    bpy.ops.wm.open_mainfile(filepath=str(REFERENCE))
    baseline = flipper_transforms(bpy.data.objects["Penguin Rig"])
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene = bpy.context.scene
    rig = bpy.data.objects["Penguin Rig"]
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()
    authored = flipper_transforms(rig)
    receiver = rig.matrix_world.inverted() @ bpy.data.objects["Body"].matrix_world
    marker = bpy.data.objects.get("Flipper_Hinge")
    if marker:
        hinge = rig.matrix_world.inverted() @ marker.matrix_world.translation
    else:
        # The marked root area on the current source fit, snapped to the body.
        body = bpy.data.objects["Penguin"].evaluated_get(bpy.context.evaluated_depsgraph_get())
        world = rig.matrix_world @ Vector((-0.532, -0.073, 0.837))
        hit, point, _, _ = body.closest_point_on_mesh(body.matrix_world.inverted() @ world)
        if not hit:
            raise RuntimeError("Could not locate the flipper hinge on the penguin")
        hinge = rig.matrix_world.inverted() @ body.matrix_world @ point
    hold = {bone.name: bone.matrix_basis.copy() for bone in rig.pose.bones}
    for name in FLIPPERS:
        bone = rig.pose.bones[name]
        rest = bone.bone.matrix_local
        # The flippers are rigidly skinned. Express the mesh edit after the
        # authored bone pose so the existing in-game mesh lands in the same place.
        expected = bone.matrix @ rest.inverted() @ authored[name]
        fitted = expected @ baseline[name].inverted() @ rest
        bone.matrix = fitted
        hold[name] = bone.matrix_basis.copy()
        bpy.context.view_layer.update()
        actual = bone.matrix @ rest.inverted() @ baseline[name]
        assert max(abs(a - b) for row_a, row_b in zip(actual, expected) for a, b in zip(row_a, row_b)) < 0.0001

    rig.animation_data_clear()
    bpy.ops.object.select_all(action="DESELECT")
    # The existing FBX convention bakes centimeters before the UE importer
    # strips the Armature container from the animation skeleton.
    scale = Matrix.Scale(100, 4)
    rig.data.transform(scale)
    for obj in scene.objects:
        if obj.type == "MESH" and obj.parent == rig:
            obj.data.transform(scale)
            obj.location *= 100
            parent_inverse = obj.matrix_parent_inverse.copy()
            parent_inverse.translation *= 100
            obj.matrix_parent_inverse = parent_inverse
            obj.name = "Mesh_" + obj.name
            obj.select_set(True)
    rig.name = "Armature"
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    scene.unit_settings.scale_length = 0.01
    scene.frame_start, scene.frame_end = 1, 2
    for frame in (1, 2):
        scene.frame_set(frame)
        for bone in rig.pose.bones:
            pose = hold[bone.name].copy()
            pose.translation *= 100
            bone.rotation_mode = "QUATERNION"
            bone.matrix_basis = pose
            for channel in ("location", "rotation_quaternion", "scale"):
                bone.keyframe_insert(data_path=channel, frame=frame)
    scene.frame_set(1)

    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.fbx(
        filepath=str(OUTPUT / "A_StarterWeaponHold_Fitted.fbx"), use_selection=True,
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS", add_leaf_bones=False,
        mesh_smooth_type="FACE", bake_anim=True, bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False, bake_anim_simplify_factor=0,
    )
    (OUTPUT / "ReceiverFit.json").write_text(
        json.dumps({"receiver_on_penguin": unreal_transform(receiver),
                    "hinge_on_penguin": [100 * hinge.x, -100 * hinge.y, 100 * hinge.z]}, indent=2) + "\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
