"""Build the water level and the two independently editable iceberg sublevels."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_iceberg_water import channel_ice, channel_seabed, channel_surface, underwater_fog
from build_slide_park import MAP as PARK_MAP, actors, levels, spawn, unreal
from light_iceberg_network import configure_daylight


MAP = "/Game/Maps/IcebergNetwork"
ISLAND_LEVELS = "/Game/Maps/IcebergIslands"
HANDOFF = Path(__file__).resolve().parents[1] / "SourceAssets" / "World" / "Icebergs" / "ExpeditionIsland"


def write_expedition_mesh(island):
    HANDOFF.mkdir(parents=True, exist_ok=True)
    result = HANDOFF / "ExpeditionIsland-Detailed.ply"
    if not island.export_blender_mesh(str(result)):
        raise RuntimeError(f"Could not export {result}")


def place_island(cls, label, level_name, x, is_base, material):
    existing = next((actor for actor in actors.get_all_level_actors()
                     if actor.get_class() == cls and actor.get_editor_property("island_name") == label), None)
    if existing:
        return existing
    path = f"{ISLAND_LEVELS}/{level_name}"
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    destination = unreal.GameplayStatics.get_streaming_level(world, path)
    if not destination and unreal.EditorAssetLibrary.does_asset_exist(path):
        destination = unreal.EditorLevelUtils.add_level_to_world_with_transform(
            world, path, unreal.LevelStreamingAlwaysLoaded, unreal.Transform())
    if not destination:
        destination = unreal.EditorLevelUtils.create_new_streaming_level(unreal.LevelStreamingAlwaysLoaded, path, False)
    if not destination:
        raise RuntimeError(f"Could not load the {label} sublevel")
    unreal.EditorLevelUtils.make_level_current(destination)
    island = spawn(cls, label, (x, 0, 0))
    island.set_editor_property("island_name", label)
    island.set_editor_property("base_island", is_base)
    island.set_editor_property("island_material", material)
    island.get_component_by_class(unreal.ProceduralMeshComponent).set_material(0, material)
    if not levels.set_current_level_by_name("IcebergNetwork"):
        raise RuntimeError("Could not return to the persistent level")
    return island


def main(*, refresh_water=False):
    island_class = unreal.load_class(None, "/Script/ColdLoot.IcebergIsland")
    network_class = unreal.load_class(None, "/Script/ColdLoot.IcebergNetworkTerrain")
    pawn_class = unreal.load_class(None, "/Script/ColdLoot.PenguinPawn")
    park_class = unreal.load_class(None, "/Script/ColdLoot.SlideParkTerrain")
    if not all((island_class, network_class, pawn_class, park_class)):
        raise RuntimeError("Build ColdLootEditor before creating Iceberg Network")

    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        if not levels.load_level(MAP):
            raise RuntimeError(f"Could not load {MAP}")
    elif not levels.new_level_from_template(MAP, PARK_MAP):
        raise RuntimeError(f"Could not create {MAP}")

    current = actors.get_all_level_actors()
    if any(actor.get_class() == park_class for actor in current):
        lighting = (unreal.DirectionalLight, unreal.SkyLight, unreal.ExponentialHeightFog,
                    unreal.SkyAtmosphere, unreal.VolumetricCloud, unreal.PostProcessVolume)
        for actor in current:
            if actor.get_class() != pawn_class and not isinstance(actor, lighting):
                actors.destroy_actor(actor)
    unreal.EditorAssetLibrary.make_directory(ISLAND_LEVELS)
    snow = channel_ice()

    base = place_island(island_class, "Base Island", "BaseIsland", 0, True, snow)
    expedition = place_island(island_class, "Expedition Island", "ExpeditionIsland", 16800, False, snow)
    for island in (base, expedition):
        island.modify()
        island.get_component_by_class(unreal.ProceduralMeshComponent).modify()
        island.set_editor_property("island_material", snow)
        island.rebuild_surface()
    current = actors.get_all_level_actors()
    network = next((actor for actor in current if actor.get_class() == network_class), None)
    if not network:
        network = spawn(network_class, "Iceberg Network Terrain")
    network.refresh_islands()
    if list(network.zone_names()) != ["Base Island", "Expedition Island"]:
        raise RuntimeError("Both island levels must be available to the network")
    player = next(actor for actor in current if actor.get_class() == pawn_class)
    player.set_actor_location(unreal.Vector(-450, 0, network.surface_height(-450, 0) + 1.0), False, True)
    swim_path = "/Game/Penguin/Animations/A_Swim_Paddle"
    if unreal.EditorAssetLibrary.does_asset_exist(swim_path) and not player.get_editor_property("swim_animation"):
        player.set_editor_property("swim_animation", unreal.load_asset(swim_path))

    plane = unreal.load_asset("/Engine/BasicShapes/Plane")
    network.set_editor_property("underwater_fog", underwater_fog())
    network.set_editor_property("ocean_material", channel_surface(network.ocean_waves(), rebuild=refresh_water))
    network.modify()
    network.get_component_by_class(unreal.ProceduralMeshComponent).modify()
    network.rebuild_ocean()
    legacy_plane = next((item for item in current if item.get_actor_label() == "Channel Water"), None)
    if legacy_plane:
        actors.destroy_actor(legacy_plane)
    seabed = next((item for item in current if item.get_actor_label() == "Channel Seabed"), None)
    if not seabed:
        seabed = spawn(unreal.StaticMeshActor, "Channel Seabed", (8400, 0, -835))
    seabed.static_mesh_component.set_static_mesh(plane)
    seabed.set_actor_scale3d(unreal.Vector(2200, 2200, 1))
    seabed.static_mesh_component.set_material(0, channel_seabed())
    seabed.static_mesh_component.set_collision_profile_name("NoCollision")
    seabed.static_mesh_component.set_cast_shadow(False)

    configure_daylight()

    write_expedition_mesh(expedition)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    editor.set_level_viewport_camera_info(unreal.Vector(-6200, -33100, 22300), unreal.Rotator(pitch=-31, yaw=66))
    if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False) or not levels.save_current_level():
        raise RuntimeError(f"Could not save {MAP} and its island sublevels")
    unreal.log("Built /Game/Maps/IcebergNetwork with two island sublevels and exported Expedition Island")


if __name__ == "__main__":
    main()
