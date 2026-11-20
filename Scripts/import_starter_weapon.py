"""Import the starter weapon, preserve its part pivots, and save its fitted mount."""
import json
from pathlib import Path
import sys

import unreal

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build_slide_park import import_fbx

SOURCE = HERE.parent / "SourceAssets/Weapons/FBX"
DEST = "/Game/Weapons/Starter"
assets = unreal.AssetToolsHelpers.get_asset_tools()


def transform(data):
    return unreal.Transform(
        location=unreal.Vector(*data["location"]),
        rotation=unreal.Quat(*data["rotation"]).rotator(),
    )


def import_part(name):
    path = f"{DEST}/Meshes/{name}"
    existing = None
    is_drum = name == "SM_StarterDrum"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        existing = unreal.load_asset(path)
        if not is_drum or existing.get_editor_property("asset_import_data").get_editor_property(
            "normal_generation_method"
        ) == unreal.FBXNormalGenerationMethod.BUILT_IN:
            return existing

    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    options.import_as_skeletal = False
    options.import_materials = True
    options.import_textures = False
    settings = options.get_editor_property("static_mesh_import_data")
    settings.set_editor_property("convert_scene_unit", True)
    settings.set_editor_property("auto_generate_collision", False)
    settings.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    if is_drum:
        settings.set_editor_property("normal_generation_method", unreal.FBXNormalGenerationMethod.BUILT_IN)
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / f"{name}.fbx")
    task.destination_path = f"{DEST}/Meshes"
    task.automated = True
    task.save = True
    task.replace_existing = existing is not None
    task.replace_existing_settings = existing is not None
    task.options = options
    assets.import_asset_tasks([task])
    mesh = unreal.load_asset(path)
    if not mesh:
        raise RuntimeError(f"Unable to import {name}: {task.imported_object_paths}")
    if not unreal.EditorAssetLibrary.save_directory(f"{DEST}/Meshes", only_if_is_dirty=False):
        raise RuntimeError(f"Unable to save the mesh and materials for {name}")
    return mesh


def set_socket(mesh, name, placement):
    socket = mesh.find_socket(name)
    if not socket:
        socket = unreal.new_object(unreal.StaticMeshSocket, outer=mesh)
        socket.set_editor_property("socket_name", name)
        mesh.add_socket(socket)
    socket.set_editor_property("relative_location", placement.translation)
    socket.set_editor_property("relative_rotation", placement.rotation.rotator())


def tracer_material():
    path = f"{DEST}/M_StarterTracer"
    mat = (unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path)
           else assets.create_asset("M_StarterTracer", DEST, unreal.Material, unreal.MaterialFactoryNew()))
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    edit = unreal.MaterialEditingLibrary
    emissive = unreal.MaterialProperty.MP_EMISSIVE_COLOR
    glow = edit.get_material_property_input_node(mat, emissive)
    if not isinstance(glow, unreal.MaterialExpressionConstant3Vector):
        glow = edit.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
        edit.connect_material_property(glow, "", emissive)
    glow.set_editor_property("constant", unreal.LinearColor(0.12, 1.45, 5.4, 1))
    edit.recompile_material(mat)
    return mat


def import_shot_sound():
    name = "S_StarterSuppressed"
    path = f"{DEST}/Audio/{name}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.filename = str(SOURCE.parent / "Audio" / f"{name}.wav")
        task.destination_path = f"{DEST}/Audio"
        task.automated = True
        task.save = True
        assets.import_asset_tasks([task])
    sound = unreal.load_asset(path)
    if not isinstance(sound, unreal.SoundWave):
        raise RuntimeError(f"Unable to import {name}")
    pcm = unreal.SoundAssetCompressionType.PCM
    if sound.get_editor_property("sound_asset_compression_type") != pcm:
        sound.set_sound_asset_compression_type(pcm)
        if not unreal.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False):
            raise RuntimeError(f"Unable to save {name} as PCM")
    return sound


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    import_shot_sound()
    fit = json.loads((SOURCE / "StarterWeaponFit.json").read_text(encoding="utf-8"))
    receiver, bolt, drum = (import_part(name) for name in
                            ("SM_StarterReceiver", "SM_StarterBolt", "SM_StarterDrum"))

    for name, field in (("Weapon_Contact", "weapon_contact"), ("Optic_Mount", "optic_mount"), ("Muzzle", "muzzle")):
        set_socket(receiver, name, transform(fit[field]))

    skeleton = unreal.load_asset("/Game/Penguin/Mesh/SK_Penguin_Skeleton")
    hold = import_fbx("A_StarterWeaponHold", f"{DEST}/Animation", skeleton, SOURCE)
    root_pose, flipper_pose = unreal.AnimationLibrary.get_bone_poses_for_frame(
        hold, ["Root", "Flipper_R"], 0, False)
    flipper_on_penguin = unreal.MathLibrary.compose_transforms(flipper_pose, root_pose)
    grip_on_flipper = unreal.MathLibrary.make_relative_transform(
        transform(fit["grip_on_penguin"]), flipper_on_penguin
    )

    definition = unreal.load_asset(f"{DEST}/DA_StarterWeapon") if unreal.EditorAssetLibrary.does_asset_exist(
        f"{DEST}/DA_StarterWeapon") else None
    if not definition:
        cls = unreal.load_class(None, "/Script/ColdLoot.ColdLootWeaponDefinition")
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        definition = assets.create_asset("DA_StarterWeapon", DEST, cls, factory)
    for field, value in (
        ("receiver", receiver), ("bolt", bolt), ("drum", drum), ("hold_pose", hold),
        ("tracer_material", tracer_material()),
        ("grip_on_flipper", grip_on_flipper),
        ("receiver_on_grip", transform(fit["receiver_on_grip"])),
        ("bolt_on_receiver", transform(fit["bolt_on_receiver"])),
        ("drum_on_receiver", transform(fit["drum_on_receiver"])),
    ):
        definition.set_editor_property(field, value)

    if not unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=False):
        raise RuntimeError("Unable to save starter weapon assets")
    unreal.log(f"Starter weapon fit saved: {grip_on_flipper}")


if __name__ == "__main__":
    main()
