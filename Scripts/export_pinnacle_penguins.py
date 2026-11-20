"""Export Blender faction models onto the shared penguin rig without saving the scenes."""
import json
from pathlib import Path
import re

import bpy
from mathutils import Matrix


ROOT = Path(__file__).resolve().parents[1] / "SourceAssets/Penguin"
OUTPUT = ROOT / "FBX/Pinnacle"
MODELS = {
    "Whiteout": "Cold Loot - Whiteout.blend",
    "Forgekeeper": "Cold Loot - Foundry Forgekeeper v2.blend",
}


def export_model(name, filename):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / "Blender" / filename))
    scene = bpy.context.scene
    scene.frame_set(1)
    source_rig = bpy.data.objects["Penguin Rig"]
    depsgraph = bpy.context.evaluated_depsgraph_get()
    posed_rig = source_rig.evaluated_get(depsgraph)
    # Move the mounted equipment from the render pose into the skeleton's bind pose.
    rest_from_pose = {
        bone.name: bone.bone.matrix_local @ bone.matrix.inverted() @ posed_rig.matrix_world.inverted()
        for bone in posed_rig.pose.bones
    }
    geometry = []
    for obj in scene.objects:
        anchor = obj.parent
        if not anchor or obj.type not in {"MESH", "CURVE"}:
            continue
        if anchor == source_rig and obj.type == "MESH":
            geometry.append((obj, obj.data.copy(), source_rig.matrix_world.inverted() @ obj.matrix_world, None))
        elif anchor.parent == source_rig and anchor.parent_type == "BONE" and not obj.hide_render:
            evaluated = obj.evaluated_get(depsgraph)
            mesh = bpy.data.meshes.new_from_object(evaluated)
            if not mesh.vertices:
                bpy.data.meshes.remove(mesh)
                continue
            bone = anchor.parent_bone
            geometry.append((obj, mesh, rest_from_pose[bone] @ evaluated.matrix_world, bone))

    rig = bpy.data.objects.new("Armature", source_rig.data.copy())
    rig.data.pose_position = "REST"
    scale = Matrix.Scale(100, 4)
    rig.data.transform(scale)
    scene.collection.objects.link(rig)
    bpy.ops.object.select_all(action="DESELECT")
    palette = {}
    materials = {}
    for source, data, transform, bone in geometry:
        data.transform(scale @ transform)
        mesh = bpy.data.objects.new("Mesh_" + source.name, data)
        scene.collection.objects.link(mesh)
        mesh.parent = rig
        if bone:
            mesh.vertex_groups.new(name=bone).add(range(len(data.vertices)), 1.0, "REPLACE")
        else:
            # Copy the group names in index order. The copied vertices keep their weights.
            for group in source.vertex_groups:
                mesh.vertex_groups.new(name=group.name)
        for index, material in enumerate(data.materials):
            if material.name not in materials:
                shader = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
                export_name = re.sub(r"[^a-zA-Z0-9]+", "_", material.name).strip("_")
                simple = bpy.data.materials.new(export_name)
                materials[material.name] = simple
                palette[simple.name] = {
                    "source": material.name,
                    "color": list(shader.inputs["Base Color"].default_value),
                    "roughness": shader.inputs["Roughness"].default_value,
                    "metallic": shader.inputs["Metallic"].default_value,
                    "emission": list(shader.inputs["Emission Color"].default_value),
                    "emission_strength": shader.inputs["Emission Strength"].default_value,
                }
            data.materials[index] = materials[material.name]
        mesh.select_set(True)

    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.join()
    mesh = bpy.context.object
    mesh.name = "Mesh_" + name
    modifier = mesh.modifiers.new("Penguin skeleton", "ARMATURE")
    modifier.object = rig
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    scene.unit_settings.scale_length = 0.01
    fbx = OUTPUT / f"SK_{name}.fbx"
    bpy.ops.export_scene.fbx(
        filepath=str(fbx), use_selection=True, axis_forward="-Y", axis_up="Z",
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        add_leaf_bones=False, mesh_smooth_type="FACE", bake_anim=False,
    )
    print(f"{name}: {len(mesh.data.vertices)} skinned vertices, {len(palette)} materials")
    return palette


if __name__ == "__main__":
    OUTPUT.mkdir(exist_ok=True)
    palettes = {name: export_model(name, filename) for name, filename in MODELS.items()}
    (OUTPUT / "Materials.json").write_text(json.dumps(palettes, indent=2) + "\n", encoding="utf-8")
