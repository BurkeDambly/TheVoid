"""Import the penguin FBXs and create the playable Slide Park map in Unreal Editor."""
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[1]
FBX = ROOT / "SourceAssets/Penguin/FBX"
PENGUIN = "/Game/Penguin"
PARK = "/Game/SlidePark"
MAP = "/Game/Maps/SlidePark"
assets = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def import_fbx(name, folder, skeleton=None, source_dir=FBX, *, mesh_skeleton=None, import_materials=True,
               replace_existing=False):
    existing = f"{folder}/{name}"
    if not replace_existing and unreal.EditorAssetLibrary.does_asset_exist(existing):
        return unreal.load_asset(existing)
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = (
        unreal.FBXImportType.FBXIT_ANIMATION if skeleton
        else unreal.FBXImportType.FBXIT_SKELETAL_MESH
    )
    options.import_as_skeletal = True
    options.import_mesh = skeleton is None
    options.import_materials = skeleton is None and import_materials
    options.import_textures = False
    options.import_animations = skeleton is not None
    options.create_physics_asset = False
    options.skeleton = skeleton if skeleton else mesh_skeleton
    if skeleton:
        sequence_options = options.get_editor_property("anim_sequence_import_data")
        sequence_options.set_editor_property("convert_scene_unit", True)
        sequence_options.set_editor_property(
            "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME
        )
        sequence_options.set_editor_property("use_default_sample_rate", True)
    else:
        mesh_options = options.get_editor_property("skeletal_mesh_import_data")
        mesh_options.set_editor_property("convert_scene_unit", True)
    task = unreal.AssetImportTask()
    task.filename = str(source_dir / f"{name}.fbx")
    task.destination_path = folder
    task.automated = True
    task.save = True
    task.replace_existing = replace_existing
    task.replace_existing_settings = replace_existing
    task.options = options
    assets.import_asset_tasks([task])
    result = unreal.load_asset(existing)
    if not result:
        raise RuntimeError(f"Failed to import {name}: {task.imported_object_paths}")
    return result


def material(name, node_type, color=None, roughness=0.8):
    path = f"{PARK}/Materials/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = assets.create_asset(name, f"{PARK}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    base = edit.create_material_expression(mat, node_type, -360, -60)
    if color is not None:
        base.constant = color
    edit.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = edit.create_material_expression(mat, unreal.MaterialExpressionConstant, -360, 110)
    rough.r = roughness
    edit.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def spawn(cls, label, pos=(0, 0, 0), rot=(0, 0, 0)):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*pos), unreal.Rotator(*rot))
    if not actor:
        raise RuntimeError(f"Could not place {label}")
    actor.set_actor_label(label)
    return actor


def configure_sunset(sun, skylight, haze, atmosphere):
    sun.set_actor_rotation(unreal.Rotator(pitch=-8, yaw=-155), False)
    key = sun.get_component_by_class(unreal.DirectionalLightComponent)
    key.set_mobility(unreal.ComponentMobility.MOVABLE)
    key.set_editor_property("atmosphere_sun_light", True)
    key.set_editor_property("intensity", 3.8)
    key.set_editor_property("light_color", unreal.Color(255, 177, 119, 255))

    fill = skylight.get_component_by_class(unreal.SkyLightComponent)
    fill.set_mobility(unreal.ComponentMobility.MOVABLE)
    fill.set_editor_property("real_time_capture", True)
    fill.set_editor_property("intensity", 1.1)

    air = atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent)
    air.set_editor_property("mie_scattering_scale", 0.008)
    air.set_editor_property("sky_luminance_factor", unreal.LinearColor(1.15, 0.88, 0.71, 1))
    fog = haze.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog.set_editor_property("fog_density", 0.024)
    fog.set_editor_property("start_distance", 2400.0)
    fog.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.44, 0.24, 0.26, 1))


def place_practice_barrels():
    cls = unreal.load_class(None, "/Script/ColdLoot.ColdLootPracticeTarget")
    amber = material("M_PracticeBarrel", unreal.MaterialExpressionConstant3Vector,
                     unreal.LinearColor(0.56, 0.16, 0.035, 1), 0.56)
    for index, (x, y) in enumerate(((350, -225), (475, -135), (510, 115), (285, 255)), start=1):
        barrel = spawn(cls, f"Practice barrel {index}", (x, y, 56))
        barrel.set_actor_scale3d(unreal.Vector(0.65, 0.65, 1.08))
        barrel.get_component_by_class(unreal.StaticMeshComponent).set_material(0, amber)


def restore_foot_material(mesh):
    slots = mesh.get_editor_property("materials")
    for index, slot in enumerate(slots):
        if str(slot.get_editor_property("material_slot_name")) == "Feet | Burnished orange":
            if slot.get_editor_property("material_interface"):
                return
            foot = unreal.load_asset(f"{PENGUIN}/Mesh/Feet___Burnished_orange")
            if not foot:
                raise RuntimeError("The imported foot material is missing")
            slot.set_editor_property("material_interface", foot)
            slots[index] = slot
            mesh.set_editor_property("materials", slots)
            return True
    return False


def apply_matte_finish(mesh):
    edit = unreal.MaterialEditingLibrary
    for slot in mesh.get_editor_property("materials"):
        mat = slot.get_editor_property("material_interface")
        if not isinstance(mat, unreal.Material):
            continue
        changed = False
        for prop, value, y in (
            (unreal.MaterialProperty.MP_ROUGHNESS, 0.86, 120),
            (unreal.MaterialProperty.MP_SPECULAR, 0.12, 220),
        ):
            node = edit.get_material_property_input_node(mat, prop)
            if not isinstance(node, unreal.MaterialExpressionConstant):
                node = edit.create_material_expression(mat, unreal.MaterialExpressionConstant, -350, y)
                edit.connect_material_property(node, "", prop)
                changed = True
            if abs(node.get_editor_property("r") - value) > 0.0001:
                node.set_editor_property("r", value)
                changed = True
        if changed:
            edit.recompile_material(mat)
            if not unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False):
                raise RuntimeError(f"Unable to save matte finish on {mat.get_name()}")


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    new_mesh = not unreal.EditorAssetLibrary.does_asset_exist(f"{PENGUIN}/Mesh/SK_Penguin")
    mesh = import_fbx("SK_Penguin", f"{PENGUIN}/Mesh")
    foot_restored = restore_foot_material(mesh)
    apply_matte_finish(mesh)
    skeleton = mesh.get_editor_property("skeleton")
    walk, sprint, slide = (
        import_fbx(name, f"{PENGUIN}/Animations", skeleton) for name in
        ("A_Walk_Shuffle", "A_Sprint_Spring", "A_Slide_Belly")
    )
    # The FBX import task does not save its generated skeleton or materials.
    if new_mesh and not unreal.EditorAssetLibrary.save_directory(PENGUIN, only_if_is_dirty=False):
        raise RuntimeError("Unable to save imported penguin assets")
    if foot_restored and not new_mesh and not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
        raise RuntimeError("Unable to save the restored foot material slot")
    unreal.log(f"Penguin clips: {walk.get_play_length():.3f}, "
               f"{sprint.get_play_length():.3f}, {slide.get_play_length():.3f}")
    # Preserve an existing park and its per-level tuning on subsequent runs.
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        return
    if not levels.new_level(MAP):
        raise RuntimeError(f"Unable to create {MAP}")

    snow = material("M_Snow", unreal.MaterialExpressionVertexColor)
    teal = material("M_TrailPost", unreal.MaterialExpressionConstant3Vector,
                    unreal.LinearColor(0.016, 0.28, 0.42, 1), 0.7)
    terrain = spawn(unreal.load_class(None, "/Script/ColdLoot.SlideParkTerrain"), "Slide Park Terrain")
    terrain.set_editor_property("snow_material", snow)

    pawn = spawn(unreal.load_class(None, "/Script/ColdLoot.PenguinPawn"), "Penguin", (-450, 0, 1))
    pawn.set_editor_property("walk_animation", walk)
    pawn.set_editor_property("sprint_animation", sprint)
    pawn.set_editor_property("slide_animation", slide)
    # The imported mesh faces +Y. Apply the same facing in the editor and on Play.
    pawn.set_editor_property("mesh_yaw_offset", -90.0)
    penguin = pawn.get_editor_property("penguin_mesh")
    penguin.set_skeletal_mesh_asset(mesh)
    penguin.set_relative_rotation(unreal.Rotator(yaw=-90), False, True)

    configure_sunset(
        spawn(unreal.DirectionalLight, "Low Winter Sun", (0, 0, 2000)),
        spawn(unreal.SkyLight, "Skylight", (0, 0, 1000)),
        spawn(unreal.ExponentialHeightFog, "Distance Haze"),
        spawn(unreal.SkyAtmosphere, "Sky Atmosphere"),
    )

    marker_mesh = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    for x, label in ((600, "Descent"), (2600, "Runout"), (3900, "Uphill")):
        t = min(max((x - 600) / 2000, 0), 1)
        height = -580 * t * t * (3 - 2 * t)
        for side in (-1, 1):
            post = spawn(unreal.StaticMeshActor, f"{label} marker", (x, side * 350, height + 63))
            post.set_actor_scale3d(unreal.Vector(0.14, 0.14, 1.30))
            component = post.static_mesh_component
            component.set_static_mesh(marker_mesh)
            component.set_material(0, teal)
            component.set_collision_profile_name("TrailMarker")

    place_practice_barrels()
    if not levels.save_current_level():
        raise RuntimeError(f"Unable to save {MAP}")


if __name__ == "__main__":
    main()
