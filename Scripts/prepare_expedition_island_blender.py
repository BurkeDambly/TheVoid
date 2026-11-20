"""Run with Blender in background mode to turn the Unreal PLY export into an editable scene."""

from pathlib import Path

import bpy


root = Path(__file__).resolve().parents[1] / "SourceAssets" / "World" / "Icebergs" / "ExpeditionIsland"
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.ply_import(filepath=str(root / "ExpeditionIsland-Detailed.ply"))
terrain = bpy.context.object
if not terrain or terrain.type != "MESH" or not terrain.data.color_attributes:
    raise RuntimeError("Expedition Island geometry or vertex colors did not import")
terrain.name = "Expedition Island Terrain"
terrain.data.name = "Expedition Island Surface"
for face in terrain.data.polygons:
    face.use_smooth = True

material = bpy.data.materials.new("Snow and blue ice")
material.use_nodes = True
attribute = material.node_tree.nodes.new("ShaderNodeVertexColor")
attribute.layer_name = terrain.data.color_attributes[0].name
shader = material.node_tree.nodes.get("Principled BSDF")
shader.inputs["Roughness"].default_value = 0.72
material.node_tree.links.new(attribute.outputs["Color"], shader.inputs["Base Color"])
terrain.data.materials.append(material)

scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.spaces.active.clip_end = 1200
            area.spaces.active.region_3d.view_distance = 245
            area.spaces.active.region_3d.view_location = (0, 0, 4)
            area.spaces.active.shading.color_type = "VERTEX"

# Keep the mesh origin at the iceberg's Unreal placement pivot, with one Blender unit per meter.
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(root / "ExpeditionIsland-Detailed.blend"))
