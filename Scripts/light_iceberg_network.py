"""Author the daylight and cloud setup in the Iceberg Network persistent level."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_slide_park import actors, levels, spawn, unreal


MAP = "/Game/Maps/IcebergNetwork"
CLOUD_ASSET_DIRECTORY = "/Game/IcebergNetwork/Materials"
CLOUD_TEXTURE = "/Engine/EngineSky/VolumetricClouds/T_CloudPattern"
CLOUD_LAYER = """
float2 P = (CanopyUV - 0.5) * float2(4.9, 6.1) + float2(0.57, 0.24);
P.x += WindTime * 0.0011;
float4 Cover = Texture2DSample(CloudPattern, CloudPatternSampler, P);
float4 Detail = Texture2DSample(CloudPattern, CloudPatternSampler, P * 2.07 + float2(0.33, 0.71));
float Density = max(Cover.r, 0.78 * Cover.g) * 0.76 + Detail.r * 0.24;
float HorizonFade = smoothstep(0.035, 0.16, ViewRay.z);
float Cloud = smoothstep(0.18, 0.48, Density) * HorizonFade;
float Shade = smoothstep(0.28, 0.66, Density);
float3 Silver = float3(1.24, 1.30, 1.40);
float3 Shadow = float3(0.40, 0.54, 0.70);
float3 Color = lerp(Silver, Shadow, Shade * 0.77);

// Fan out narrow shafts below the sun and fade them into the distance.
float X = dot(ViewRay, normalize(cross(float3(0, 0, 1), SunDirection)));
float Y = dot(ViewRay, cross(SunDirection, normalize(cross(float3(0, 0, 1), SunDirection))));
float Radius = length(float2(X, Y));
float A = atan2(Y, X);
float Rays = pow(saturate(0.50 + 0.30 * sin(A * 16.0 + 0.7) + 0.20 * sin(A * 27.0)), 5.0);
float Shafts = Rays * smoothstep(0.035, 0.10, Radius) * (1.0 - smoothstep(0.16, 0.48, Radius))
    * smoothstep(-0.02, 0.16, -Y) * step(0.0, dot(ViewRay, SunDirection)) * 0.48;
return float4(lerp(Color, float3(1.28, 1.21, 1.12), Shafts / max(Cloud + Shafts, 0.001)),
              saturate(Cloud + Shafts));
"""
SUN_PITCH = -19.0
SUN_YAW = 157.0


def cloud_material():
    name = "M_ArcticClouds"
    path = f"{CLOUD_ASSET_DIRECTORY}/{name}"
    material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, CLOUD_ASSET_DIRECTORY, unreal.Material, unreal.MaterialFactoryNew()
        )
    edit = unreal.MaterialEditingLibrary
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    edit.delete_all_material_expressions(material)
    coords = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -800, 0)
    pattern = edit.create_material_expression(material, unreal.MaterialExpressionTextureObject, -800, -175)
    pattern.set_editor_property("texture", unreal.load_asset(CLOUD_TEXTURE))
    time = edit.create_material_expression(material, unreal.MaterialExpressionTime, -800, 175)
    view = edit.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -800, 350)
    negate = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -610, 350)
    negate.set_editor_property("const_b", -1.0)
    edit.connect_material_expressions(view, "", negate, "A")
    sun = edit.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -800, 525)
    sun_direction = -unreal.MathLibrary.get_forward_vector(unreal.Rotator(pitch=SUN_PITCH, yaw=SUN_YAW))
    sun.set_editor_property("constant", unreal.LinearColor(sun_direction.x, sun_direction.y, sun_direction.z, 1.0))
    density = edit.create_material_expression(material, unreal.MaterialExpressionCustom, -475, 0)
    density.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    density.set_editor_property("code", CLOUD_LAYER)
    inputs = []
    for name in ("CanopyUV", "CloudPattern", "WindTime", "ViewRay", "SunDirection"):
        shader_input = unreal.CustomInput()
        shader_input.set_editor_property("input_name", name)
        inputs.append(shader_input)
    density.set_editor_property("inputs", inputs)
    for expression, name in ((coords, "CanopyUV"), (pattern, "CloudPattern"), (time, "WindTime"),
                              (negate, "ViewRay"), (sun, "SunDirection")):
        if not edit.connect_material_expressions(expression, "", density, name):
            raise RuntimeError(f"Could not connect {name} for {path}")
    opacity = edit.create_material_expression(material, unreal.MaterialExpressionComponentMask, -220, 200)
    for channel in ("r", "g", "b", "a"):
        opacity.set_editor_property(channel, channel == "a")
    if not edit.connect_material_expressions(density, "", opacity, ""):
        raise RuntimeError(f"Could not connect the opacity input for {path}")
    edit.connect_material_property(density, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    edit.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")
    return material


def configure_daylight():
    current = actors.get_all_level_actors()

    def scene_actor(kind, name):
        return next((actor for actor in current if isinstance(actor, kind)), None) or spawn(kind, name)

    def tune(component, **settings):
        component.modify()
        for name, value in settings.items():
            component.set_editor_property(name, value)

    sun = scene_actor(unreal.DirectionalLight, "Arctic Sun")
    sun.set_actor_rotation(unreal.Rotator(pitch=SUN_PITCH, yaw=SUN_YAW), False)
    key = sun.get_component_by_class(unreal.DirectionalLightComponent)
    key.set_mobility(unreal.ComponentMobility.MOVABLE)
    tune(key,
         atmosphere_sun_light=True, intensity=5.8, light_color=unreal.Color(255, 242, 222, 255),
         light_source_angle=0.8, dynamic_shadow_distance_movable_light=34000.0,
         cast_volumetric_shadow=True, volumetric_scattering_intensity=1.3,
         enable_light_shaft_occlusion=True, occlusion_mask_darkness=0.32,
         enable_light_shaft_bloom=True, bloom_scale=0.09, bloom_threshold=1.1,
         cast_cloud_shadows=False)

    sky = scene_actor(unreal.SkyAtmosphere, "Arctic Atmosphere")
    tune(sky.get_component_by_class(unreal.SkyAtmosphereComponent),
         sky_luminance_factor=unreal.LinearColor(0.90, 0.93, 0.99, 1), mie_scattering_scale=0.011)

    skylight = scene_actor(unreal.SkyLight, "Snow and Sky Fill")
    fill = skylight.get_component_by_class(unreal.SkyLightComponent)
    fill.set_mobility(unreal.ComponentMobility.MOVABLE)
    tune(fill, real_time_capture=True, intensity=1.65, lower_hemisphere_is_black=True,
         lower_hemisphere_color=unreal.LinearColor(0.34, 0.41, 0.50, 1))

    for actor in current:
        if isinstance(actor, unreal.VolumetricCloud) and actor.get_actor_label() == "Arctic Cloud Layer":
            actors.destroy_actor(actor)
    clouds = next((actor for actor in current if isinstance(actor, unreal.StaticMeshActor)
                   and actor.get_actor_label() == "Arctic Cloud Canopy"), None)
    clouds = clouds or spawn(unreal.StaticMeshActor, "Arctic Cloud Canopy", (0, 0, 72000))
    clouds.set_actor_scale3d(unreal.Vector(45000, 45000, 1))
    component = clouds.get_component_by_class(unreal.StaticMeshComponent)
    component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    component.set_material(0, cloud_material())
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    tune(component, cast_shadow=False, affect_distance_field_lighting=False, visible_in_ray_tracing=False)

    haze = scene_actor(unreal.ExponentialHeightFog, "Atmospheric Light Shafts")
    tune(haze.get_component_by_class(unreal.ExponentialHeightFogComponent),
         fog_density=0.009, fog_height_falloff=0.24,
         fog_inscattering_luminance=unreal.LinearColor(0.42, 0.54, 0.67, 1),
         enable_volumetric_fog=True, volumetric_fog_distance=45000.0,
         volumetric_fog_scattering_distribution=0.68,
         volumetric_fog_albedo=unreal.Color(228, 239, 255, 255))

    exposure = scene_actor(unreal.PostProcessVolume, "Arctic Exposure")
    tune(exposure, unbound=True)
    grading = exposure.get_editor_property("settings")
    grading.set_editor_property("override_auto_exposure_bias", True)
    grading.set_editor_property("auto_exposure_bias", -0.45)
    exposure.set_editor_property("settings", grading)


if __name__ == "__main__":
    if not levels.load_level(MAP):
        raise RuntimeError(f"Could not load {MAP}")
    configure_daylight()
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {MAP}")
    unreal.log("Saved Iceberg Network daylight, cloud layer, and light shafts")
