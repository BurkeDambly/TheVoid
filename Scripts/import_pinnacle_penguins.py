"""Import the faction meshes and place their wandering actors in Base Island."""
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_slide_park import FBX, PENGUIN, actors, assets, import_fbx, levels, spawn, unreal


NETWORK = "/Game/Maps/IcebergNetwork"
BASE_ISLAND = "/Game/Maps/IcebergIslands/BaseIsland"
FOLDER = f"{PENGUIN}/Pinnacle"
SOURCE = FBX / "Pinnacle"
ROSTER = (
    ("Whiteout", "Whiteout Pinnacle", -80, -430, 155, 290, 100),
    ("Forgekeeper", "Foundry Forgekeeper", 150, 420, -145, 290, 90),
    ("Stormstitch", "Weave Stormstitch", 520, -680, 145, 175, 95),
    ("Sunmarshal", "Dawnward Sunmarshal", 600, 670, -145, 190, 88),
    ("Nightglass", "PenGru Nightglass", -760, 110, 25, 185, 105),
)
library = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary


def make_material(name, settings):
    path = f"{FOLDER}/Materials/{name}"
    if library.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = assets.create_asset(name, f"{FOLDER}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    prop = unreal.MaterialProperty
    for key, target, y in (
        ("color", prop.MP_BASE_COLOR, -100),
        ("roughness", prop.MP_ROUGHNESS, 40),
        ("metallic", prop.MP_METALLIC, 140),
        ("emission", prop.MP_EMISSIVE_COLOR, 240),
    ):
        value = settings[key]
        if key == "emission":
            value = [channel * settings["emission_strength"] for channel in value[:3]] + [1.0]
            if not any(value[:3]):
                continue
        if isinstance(value, list):
            node = edit.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -340, y)
            node.set_editor_property("constant", unreal.LinearColor(*value))
        else:
            node = edit.create_material_expression(mat, unreal.MaterialExpressionConstant, -340, y)
            node.set_editor_property("r", value)
        edit.connect_material_property(node, "", target)
    mat.set_editor_property("two_sided", True)
    edit.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    edit.recompile_material(mat)
    if not library.save_loaded_asset(mat):
        raise RuntimeError(f"Could not save {name}")
    return mat


def import_model(name, palette, skeleton, base_materials):
    mesh = import_fbx(f"SK_{name}", FOLDER, source_dir=SOURCE, mesh_skeleton=skeleton, import_materials=False)
    if mesh.get_editor_property("skeleton") != skeleton:
        raise RuntimeError(f"{name} did not import onto the shared penguin skeleton")
    slots = mesh.get_editor_property("materials")
    changed = False
    for index, slot in enumerate(slots):
        slot_name = str(slot.get_editor_property("imported_material_slot_name"))
        settings = palette[slot_name]
        mat = base_materials.get(settings["source"]) or make_material(f"M_{slot_name}", settings)
        if slot.get_editor_property("material_interface") != mat:
            slot.set_editor_property("material_interface", mat)
            slots[index] = slot
            changed = True
    if changed:
        mesh.set_editor_property("materials", slots)
        if not library.save_loaded_asset(mesh, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {mesh.get_name()}")
    return mesh


def main():
    selection = os.environ.get("COLDLOOT_PINNACLE_MODELS", "").split()
    roster = [entry for entry in ROSTER if not selection or entry[0] in selection]
    if selection and set(selection) != {entry[0] for entry in roster}:
        raise ValueError(f"Unknown faction model in {selection}")
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    npc_class = unreal.load_class(None, "/Script/ColdLoot.PinnaclePenguin")
    island_class = unreal.load_class(None, "/Script/ColdLoot.IcebergIsland")
    base_mesh = unreal.load_asset(f"{PENGUIN}/Mesh/SK_Penguin")
    shuffle = unreal.load_asset(f"{PENGUIN}/Animations/A_Walk_Shuffle")
    if not all((npc_class, island_class, base_mesh, shuffle)):
        raise RuntimeError("Build ColdLootEditor and import the base penguin before placing the faction NPCs")
    if not levels.load_level(NETWORK):
        raise RuntimeError(f"Could not load {NETWORK}")
    all_actors = actors.get_all_level_actors()
    island = next(actor for actor in all_actors if actor.get_class() == island_class
                  and actor.get_editor_property("base_island"))
    origin = island.get_actor_location()
    if not levels.set_current_level_by_name("BaseIsland"):
        raise RuntimeError(f"Could not make {BASE_ISLAND} the current level")

    base_materials = {
        str(slot.get_editor_property("material_slot_name")): slot.get_editor_property("material_interface")
        for slot in base_mesh.get_editor_property("materials")
    }
    palettes = json.loads((SOURCE / "Materials.json").read_text(encoding="utf-8"))
    for model, label, x, y, yaw, radius, speed in roster:
        mesh = import_model(model, palettes[model], base_mesh.get_editor_property("skeleton"), base_materials)
        npc = next((actor for actor in all_actors if actor.get_class() == npc_class
                    and actor.get_actor_label() == label), None)
        if npc:
            position = npc.get_actor_location()
            x, y = position.x, position.y
            npc.set_actor_location(unreal.Vector(x, y, island.height_at(x, y)), False, True)
        else:
            x, y = x + origin.x, y + origin.y
            npc = spawn(npc_class, label, (x, y, island.height_at(x, y)))
        npc.set_actor_rotation(unreal.Rotator(yaw=yaw), False)
        npc.set_editor_property("island", island)
        npc.set_editor_property("shuffle_animation", shuffle)
        npc.set_editor_property("wander_radius", radius)
        npc.set_editor_property("walk_speed", speed)
        component = npc.get_component_by_class(unreal.SkeletalMeshComponent)
        component.set_skeletal_mesh_asset(mesh)
        bounds = mesh.get_bounds()
        component.set_relative_location(unreal.Vector(0, 0, bounds.box_extent.z - bounds.origin.z), False, True)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {BASE_ISLAND}")
    unreal.log(f"Placed faction NPCs on Base Island: {', '.join(entry[1] for entry in roster)}")


if __name__ == "__main__":
    main()
