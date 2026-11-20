"""Build the Iceberg Network's procedural water and seabed materials."""

import unreal

FOLDER = "/Game/IcebergNetwork/Materials"
edit = unreal.MaterialEditingLibrary


def material(name):
    path = f"{FOLDER}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path), False
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    return asset, True


def expression(asset, cls, x, y):
    return edit.create_material_expression(asset, cls, x, y)


def constant(asset, value, prop, y):
    node = expression(asset, unreal.MaterialExpressionConstant, -240, y)
    node.set_editor_property("r", value)
    edit.connect_material_property(node, "", prop)


def custom(asset, name, code, inputs, prop, y, *, vector=False):
    node = expression(asset, unreal.MaterialExpressionCustom, -240, y)
    node.set_editor_property("description", name)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3
                             if vector else unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for label, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", label)
        pins.append(pin)
    node.set_editor_property("inputs", pins)
    for label, source in inputs:
        if not edit.connect_material_expressions(source, "", node, label):
            raise RuntimeError(f"Could not connect {name}.{label}")
    if not edit.connect_material_property(node, "", prop):
        raise RuntimeError(f"Could not connect {name} to {prop}")


def save(asset):
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")
    return asset


def channel_surface(waves, *, rebuild=False):
    asset, created = material("M_ChannelWater")
    if not created and not rebuild:
        return asset
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    asset.set_editor_property("two_sided", True)
    asset.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property("use_translucency_vertex_fog", False)
    position = expression(asset, unreal.MaterialExpressionWorldPosition, -750, -300)
    time = expression(asset, unreal.MaterialExpressionTime, -750, -180)
    eye = expression(asset, unreal.MaterialExpressionCameraPositionWS, -750, -60)
    depth = expression(asset, unreal.MaterialExpressionDepthFade, -750, 0)
    depth.set_editor_property("fade_distance_default", 730.0)
    side = expression(asset, unreal.MaterialExpressionTwoSidedSign, -750, 290)
    vertex = expression(asset, unreal.MaterialExpressionVertexColor, -750, 440)
    wave_code = "float2 p = Position.xy * .01;\nfloat h = 0;\nfloat2 slope = 0;\n"
    for index, wave in enumerate(waves):
        phase = f"phase{index}"
        vector = f"float2({wave.x:.6f},{wave.y:.6f})"
        wave_code += f"float {phase} = dot(p,{vector}) - T*{wave.z:.6f};\n"
        wave_code += f"h += {wave.w:.6f} * sin({phase});\n"
        wave_code += f"slope += .01*{wave.w:.6f} * cos({phase}) * {vector};\n"
    custom(asset, "Ocean swell displacement", wave_code + "return float3(0,0,h*Fade.r);",
        [("Position", position), ("T", time), ("Fade", vertex)], unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, -285, vector=True)
    custom(asset, "Blue wave trains and shoreline foam", wave_code + """
        float a = dot(p, float2(.81, .59)) * 1.57 - T * 1.95;
        float b = dot(p, float2(-.45, .89)) * 2.63 - T * 1.48;
        float c = dot(p, float2(.97, -.24)) * 7.6 + T * 2.62;
        float d = dot(p, float2(.26, .97)) * 11.4 - T * 3.14;
        slope += .065 * cos(a) * float2(.81,.59) + .052 * cos(b) * float2(-.45,.89)
               + .026 * cos(c) * float2(.97,-.24) + .018 * cos(d) * float2(.26,.97);
        float3 normal = normalize(float3(-slope,1.0));
        float3 view = normalize(Eye-Position);
        float edge = pow(1.0-saturate(abs(dot(normal,view))),2.4);
        float glint = pow(saturate(dot(reflect(-normalize(float3(-.43,.32,.78)),normal),view)),42.0);
        float tracer = sin(p.x*2.4 + p.y*.73 - T*1.8) * cos(p.x*.51 - p.y*2.1 + T*1.1);
        float foam = smoothstep(.33,.81,tracer) * (1.0-smoothstep(.08,.32,Depth)) * smoothstep(.006,.032,Depth);
        float3 shallow = float3(.012,.198,.315);
        float3 deep = float3(.004,.043,.113);
        float3 sea = lerp(shallow, deep, pow(saturate(Depth),.73));
        sea = lerp(sea, float3(.038,.137,.248), edge*.67);
        sea *= .91 + .067 * sin(a) + .038 * sin(b) + .003 * h;
        sea += glint*float3(.11,.24,.37);
        sea = lerp(sea, float3(.42,.63,.74), foam*.69);
        return Side < 0 ? lerp(float3(.028,.135,.179), sea, edge*.77) : sea;
    """, [("Position", position), ("Eye", eye), ("T", time), ("Depth", depth), ("Side", side)],
        unreal.MaterialProperty.MP_EMISSIVE_COLOR, -70, vector=True)
    custom(asset, "Soft shoreline and upward view from below", wave_code + """
        float3 view = normalize(Eye-Position);
        float3 normal = normalize(float3(-slope,1.0));
        float cosAngle = saturate(abs(dot(normal,view)));
        if (Side < 0) return lerp(.88,.26,smoothstep(.61,.83,cosAngle));
        float contact = smoothstep(.004,.065,Depth);
        return lerp(.12,.975,contact) * lerp(.88,1.0,saturate(Depth*.80 + (1.0-cosAngle)*.70));
    """, [("Position", position), ("Eye", eye), ("T", time), ("Depth", depth), ("Side", side)], unreal.MaterialProperty.MP_OPACITY, 130)
    return save(asset)


def channel_seabed():
    asset, created = material("M_ChannelSeabed")
    if not created:
        return asset
    position = expression(asset, unreal.MaterialExpressionWorldPosition, -750, -130)
    time = expression(asset, unreal.MaterialExpressionTime, -750, 70)
    custom(asset, "Arctic sand ripples", """
        float2 p = Position.xy * 0.01;
        float ribs = .5 + .5*sin(p.x*2.1 + p.y*1.2 + sin(p.y*.46)*1.65 + sin(p.x*.19));
        float grain = frac(sin(dot(floor(p*9), float2(127.1,311.7))) * 43758.5453123);
        return lerp(float3(.035,.104,.119), float3(.095,.19,.181), ribs*.67 + grain*.13);
    """, [("Position", position)], unreal.MaterialProperty.MP_BASE_COLOR, -160, vector=True)
    custom(asset, "Moving underwater light bands", """
        float2 p = Position.xy * 0.01;
        float a = sin(p.x*1.37 + p.y*.87 + sin(p.y*.93+T*.36) - T*.81);
        float b = sin(-p.x*.77 + p.y*1.54 + sin(p.x*1.09+T*.47) + T*.63);
        float caustic = 1.0 - smoothstep(.06,.32, abs(a+b));
        return float3(.011,.035,.044) + float3(.029,.092,.107)*caustic;
    """, [("Position", position), ("T", time)], unreal.MaterialProperty.MP_EMISSIVE_COLOR, 10, vector=True)
    constant(asset, .91, unreal.MaterialProperty.MP_ROUGHNESS, 220)
    return save(asset)


def channel_ice():
    asset, created = material("M_ChannelIce")
    if not created:
        return asset
    position = expression(asset, unreal.MaterialExpressionWorldPosition, -750, -130)
    time = expression(asset, unreal.MaterialExpressionTime, -750, 70)
    vertex = expression(asset, unreal.MaterialExpressionVertexColor, -750, 135)
    custom(asset, "Frost grain and submerged blue ice", """
        float3 p = Position.xyz * .01;
        float textureGrain = sin(p.x*8.3+p.z*2.7) * cos(p.y*7.1-p.z*1.7);
        float striation = .5 + .5 * sin(p.z * 13.0 + sin(p.y*1.2+p.x*.74)*1.35);
        float wet = 1.0 - smoothstep(-.27,.12,p.z);
        return lerp(Ice.rgb * (1.0 + textureGrain*.039), Ice.rgb * (.36 + striation*.13), wet * .84);
    """, [("Position", position), ("Ice", vertex)], unreal.MaterialProperty.MP_BASE_COLOR, -160, vector=True)
    custom(asset, "Refracted light on ice below the surface", """
        float3 p = Position.xyz * .01;
        float a = sin(p.x*1.37 + p.y*.87 + sin(p.y*.93+T*.36) - T*.81);
        float b = sin(-p.x*.77 + p.y*1.54 + sin(p.x*1.09+T*.47) + T*.63);
        float band = (1.0-smoothstep(.055,.28,abs(a+b))) * exp(min(p.z,0.0) *.185);
        float wet = 1.0 - smoothstep(-.22,.07,p.z);
        return wet * (float3(.010,.024,.033) + band * float3(.053,.125,.145));
    """, [("Position", position), ("T", time)], unreal.MaterialProperty.MP_EMISSIVE_COLOR, 70, vector=True)
    constant(asset, .73, unreal.MaterialProperty.MP_ROUGHNESS, 260)
    return save(asset)


def underwater_fog():
    asset, created = material("M_ChannelFog")
    if not created:
        return asset
    asset.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    color = expression(asset, unreal.MaterialExpressionSceneTexture, -780, -100)
    color.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = expression(asset, unreal.MaterialExpressionSceneTexture, -780, 100)
    depth.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_SCENE_DEPTH)
    custom(asset, "Depth-dependent underwater haze", """
        float distanceCm = max(Depth.r - 350.0, 0.0);
        float haze = (1.0 - exp(-distanceCm * .00035)) * .71;
        float3 water = float3(.029,.157,.211);
        return lerp(Scene.rgb, water, haze);
    """, [("Scene", color), ("Depth", depth)], unreal.MaterialProperty.MP_EMISSIVE_COLOR, 0, vector=True)
    return save(asset)
