"""Create the transparent outlines for Penguin Form and Editor Penguin Form."""
import unreal

DESTINATION = "/Game/Penguin/Mesh"
def create_outline(name, emissive):
    library = unreal.EditorAssetLibrary
    path = f"{DESTINATION}/{name}"
    assets = unreal.AssetToolsHelpers.get_asset_tools()
    material = (unreal.load_asset(path) if library.does_asset_exist(path)
                else assets.create_asset(name, DESTINATION, unreal.Material, unreal.MaterialFactoryNew()))
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_skeletal_mesh", True)

    edit = unreal.MaterialEditingLibrary
    edit.delete_all_material_expressions(material)
    fresnel = edit.create_material_expression(material, unreal.MaterialExpressionFresnel, -680, 5)
    fresnel.set_editor_property("exponent", 11.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.0)
    color = edit.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -180)
    color.set_editor_property("constant", unreal.LinearColor(*emissive, 1.0))

    edge = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -260, 10)
    edge.set_editor_property("const_b", 1.5)
    edit.connect_material_expressions(fresnel, "", edge, "A")
    edit.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.connect_material_property(edge, "", unreal.MaterialProperty.MP_OPACITY)

    normal = edit.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -470, 220)
    shell = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -260, 220)
    shell.set_editor_property("const_b", 0.8)
    edit.connect_material_expressions(normal, "", shell, "A")
    edit.connect_material_property(shell, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    edit.recompile_material(material)
    if not library.save_loaded_asset(material):
        raise RuntimeError(f"Unable to save {path}")
    unreal.log(f"Saved {path}")


def create_editor_outline():
    create_outline("M_EditorPenguinForm", (6.0, 0.045, 0.025))


def main():
    create_outline("M_PenguinForm", (0.045, 1.15, 6.0))
    create_editor_outline()


if __name__ == "__main__":
    main()
