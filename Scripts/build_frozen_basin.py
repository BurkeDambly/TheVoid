"""Build the terrain-only Frozen Basin map using the configured Slide Park penguin."""

import csv
from math import atan2, degrees, hypot
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_slide_park import MAP as PARK_MAP, actors, assets, levels, spawn, unreal


MAP = "/Game/Maps/FrozenBasin"
MATERIALS = "/Game/FrozenBasin/Materials"
CAPTURE_TAG = "ColdzoneGeneratedView"
LEGACY_CAVERN_LIGHT_TAG = "ColdzoneGeneratedCavernLight"


def sea_material():
    name = "M_PolarSea"
    path = f"{MATERIALS}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    material = assets.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    color = edit.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, -60)
    color.set_editor_property("constant", unreal.LinearColor(0.028, 0.075, 0.115, 1.0))
    edit.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 80)
    roughness.set_editor_property("r", 0.34)
    edit.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")
    return material


def capture_views(terrain):
    root = Path(__file__).resolve().parents[1] / "SourceAssets" / "World"
    root.mkdir(parents=True, exist_ok=True)
    offsets = ((-11900,-10000), (-19000,7400), (-18400,-16700), (-40500,-4300), (-21000,9600),
               (-16000,-11400), (-16300,11100), (17700,-7800), (-18000,9200), (7000,-20000),
               (-16000,8200), (-18100,-9400))
    with (root / "ColdzoneZoneViews.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("id", "zone", "center_x_cm", "center_y_cm", "center_z_cm", "camera_x_cm",
                         "camera_y_cm", "camera_z_cm", "yaw_degrees", "pitch_degrees", "horizontal_fov_degrees"))
        for index, name in enumerate(terrain.zone_names()):
            target = terrain.zone_location(index)
            offset_x, offset_y = offsets[index]
            x, y = target.x + offset_x, target.y + offset_y
            if index == 3:
                z = max(target.z + 21800, terrain.surface_height(x, y) + 8500)
            else:
                z = max(target.z + (4300 if index == 0 else 3500), terrain.surface_height(x,y) + 1400)
            pitch = degrees(atan2(target.z + 500 - z, hypot(offset_x, offset_y)))
            yaw = degrees(atan2(-offset_y, -offset_x))
            label = f"{index + 1:02d} - {name}"
            camera = spawn(unreal.CameraActor, label, (x, y, z))
            camera.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw), False)
            camera.set_actor_label(label)
            camera.set_folder_path("Coldzone/Zone Reference Cameras")
            camera.set_editor_property("is_editor_only_actor", True)
            camera.set_editor_property("tags", [CAPTURE_TAG])
            camera.get_component_by_class(unreal.CameraComponent).set_editor_property("field_of_view", 67.0)
            writer.writerow((index + 1, name, round(target.x), round(target.y), round(target.z),
                             round(x), round(y), round(z), round(yaw, 3), round(pitch, 3), 67.0))


def main(*, rebuild=False):
    terrain_class = unreal.load_class(None, "/Script/ColdLoot.FrozenBasinTerrain")
    if not terrain_class:
        raise RuntimeError("Build ColdLootEditor before generating the map")

    new_map = not unreal.EditorAssetLibrary.does_asset_exist(MAP)
    if not new_map:
        if not levels.load_level(MAP):
            raise RuntimeError(f"Unable to load {MAP}")
        if not rebuild and any(actor.get_class() == terrain_class for actor in actors.get_all_level_actors()):
            unreal.log(f"Preserving existing map {MAP}")
            return
    elif not levels.new_level_from_template(MAP, PARK_MAP):
        raise RuntimeError(f"Unable to create {MAP} from {PARK_MAP}")

    lighting_classes = (unreal.DirectionalLight, unreal.SkyLight, unreal.ExponentialHeightFog,
                        unreal.SkyAtmosphere, unreal.VolumetricCloud, unreal.PostProcessVolume)
    penguin_class = unreal.load_class(None, "/Script/ColdLoot.PenguinPawn")
    penguins = []
    for actor in actors.get_all_level_actors():
        if actor.get_class() == penguin_class:
            penguins.append(actor)
        elif ((new_map and not isinstance(actor, lighting_classes)) or actor.get_class() == terrain_class
              or actor.get_actor_label() == "Polar Sea" or actor.actor_has_tag(CAPTURE_TAG)
              or actor.actor_has_tag(LEGACY_CAVERN_LIGHT_TAG)):
            actors.destroy_actor(actor)
    if len(penguins) != 1:
        raise RuntimeError(f"Expected one configured penguin, found {len(penguins)}")

    terrain = spawn(terrain_class, "Frozen Basin Terrain")
    terrain.set_folder_path("Frozen Basin/Terrain")
    snow = unreal.load_asset("/Game/SlidePark/Materials/M_Snow")
    terrain.set_editor_property("snow_material", snow)
    terrain.get_component_by_class(unreal.ProceduralMeshComponent).set_material(0, snow)

    penguin = penguins[0]
    penguin.set_actor_location(unreal.Vector(-450, 0, terrain.surface_height(-450, 0) + 1), False, True)

    sea = spawn(unreal.StaticMeshActor, "Polar Sea", (0, 0, -440))
    sea.set_folder_path("Frozen Basin/Terrain")
    sea.set_actor_scale3d(unreal.Vector(48000, 48000, 1))
    sea.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    sea.static_mesh_component.set_material(0, sea_material())
    sea.static_mesh_component.set_collision_profile_name("NoCollision")

    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            actor.set_actor_rotation(unreal.Rotator(pitch=-13, yaw=43), False)
            sun = actor.get_component_by_class(unreal.DirectionalLightComponent)
            sun.set_editor_property("dynamic_shadow_distance_movable_light", 194000.0)
            sun.set_editor_property("dynamic_shadow_cascades", 4)
        elif isinstance(actor, unreal.ExponentialHeightFog):
            fog = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
            fog.set_editor_property("fog_density", 0.007)

    capture_views(terrain)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    editor.set_level_viewport_camera_info(unreal.Vector(281000, -145000, 214000), unreal.Rotator(pitch=-37, yaw=153))
    if not levels.save_current_level():
        raise RuntimeError(f"Unable to save {MAP}")
    unreal.log(f"Built {MAP} with {len(actors.get_all_level_actors())} actors")


if __name__ == "__main__":
    main()
