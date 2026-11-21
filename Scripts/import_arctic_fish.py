"""Import the Arctic Fish assets used by the Iceberg Network."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_slide_park import ROOT, assets, import_fbx, unreal


FOLDER = "/Game/Fish/ArcticFish"
SOURCE = ROOT / "SourceAssets/Fish/FBX"
library = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
property_type = unreal.MaterialProperty


def new_material(name):
    path = f"{FOLDER}/{name}"
    return (unreal.load_asset(path), False) if library.does_asset_exist(path) else (
        assets.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew()), True)


def save_material(mat):
    edit.recompile_material(mat)
    if not library.save_loaded_asset(mat, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {mat.get_name()}")


def fish_material():
    mat, _ = new_material("M_ArcticFish")
    color = edit.get_material_property_input_node(mat, property_type.MP_BASE_COLOR)
    if not color:
        color = edit.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -420, -100)
        if not edit.connect_material_property(color, "", property_type.MP_BASE_COLOR):
            raise RuntimeError("Could not connect the imported fish colors")
    roughness = edit.get_material_property_input_node(mat, property_type.MP_ROUGHNESS)
    if not roughness:
        roughness = edit.create_material_expression(mat, unreal.MaterialExpressionConstant, -370, 110)
        edit.connect_material_property(roughness, "", property_type.MP_ROUGHNESS)
    roughness.set_editor_property("r", 0.54)
    # Preserve the flank and belly colors where the ice blocks direct light.
    fill = edit.get_material_property_input_node(mat, property_type.MP_EMISSIVE_COLOR)
    if not fill:
        fill = edit.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, -40)
        if not (edit.connect_material_expressions(color, "", fill, "A")
                and edit.connect_material_property(fill, "", property_type.MP_EMISSIVE_COLOR)):
            raise RuntimeError("Could not connect the fish fill")
    fill.set_editor_property("const_b", 0.22)
    edit.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    save_material(mat)
    return mat


def ripple_material():
    mat, created = new_material("M_ArcticFishRipple")
    if created:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        tint = edit.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -380, -100)
        tint.set_editor_property("constant", unreal.LinearColor(0.48, 0.76, 0.90, 1.0))
        edit.connect_material_property(tint, "", property_type.MP_EMISSIVE_COLOR)
        vertex = edit.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -390, 70)
        fade = edit.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -390, 220)
        fade.set_editor_property("parameter_name", "RippleOpacity")
        fade.set_editor_property("default_value", 0.8)
        opacity = edit.create_material_expression(mat, unreal.MaterialExpressionMultiply, -175, 140)
        edit.connect_material_expressions(vertex, "A", opacity, "A")
        edit.connect_material_expressions(fade, "", opacity, "B")
        edit.connect_material_property(opacity, "", property_type.MP_OPACITY)
        save_material(mat)
    return mat


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    mesh = import_fbx("SK_ArcticFish", FOLDER, source_dir=SOURCE, import_materials=False,
                      replace_existing=True, vertex_colors=unreal.VertexColorImportOption.REPLACE)
    slots = mesh.get_editor_property("materials")
    if len(slots) != 1:
        raise RuntimeError(f"Expected a single fish material slot, got {len(slots)}")
    slot = slots[0]
    slot.set_editor_property("material_interface", fish_material())
    slots[0] = slot
    skeleton = mesh.get_editor_property("skeleton")
    if not skeleton or not library.save_loaded_asset(skeleton, only_if_is_dirty=False):
        raise RuntimeError("Could not save the fish skeleton")
    mesh.modify()
    mesh.set_editor_property("materials", slots)
    if not library.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError("Could not save the fish mesh")
    swim = import_fbx("A_ArcticFishSwim", FOLDER, skeleton, source_dir=SOURCE, replace_existing=True)
    if not 1.95 <= swim.get_play_length() <= 2.05:
        raise RuntimeError(f"Expected a two-second fish swim loop, got {swim.get_play_length():.3f}s")
    ripple_material()
    unreal.log(f"Imported the Arctic Fish and its {swim.get_play_length():.2f}s swim loop")


if __name__ == "__main__":
    main()
