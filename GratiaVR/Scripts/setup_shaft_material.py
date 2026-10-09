"""Shape-fitting deformation of skin and clothing around engaged shafts: MPC + material function + wiring.

Editor commandlet, run after the C++ editor build and setup_soft_press_material.py. Creates
- /Game/Gratia/Physics/MPC_GratiaShafts: four shaft slots S0..S3, each 12 points along the shaft from its tip
  (S<n>P00..P11: world xyz, distance from the tip in w), S<n>A (form, radius, length, inserted depth), S<n>B (tip
  length, base scale, the channel's closed radius, 1 when the slot is in use) and S<n>C (the radius the opening morph
  has opened the entrance to, how deep it reaches); Config (the character's front xyz, belly
  swelling cm), Config2 (reach cm, -, -, strength), Config3 (belly radius cm, shaft radius of the full swelling, floor
  height, -). UGratiaPenetration writes it every frame.
- /Game/Gratia/CharacterMaterials/MF_GratiaShaftPress: a world position offset on top of the opening morph and wall
  bones. Where the inserted part of a shaft is wider than what they opened (R0: the closed radius, or the morph's
  opening near the entrance), every vertex near it moves away from its axis so the area between them is conserved -
  r' = sqrt(r^2 + R^2 - R0^2), R the shaft's radius from its form's profile at that point (the same profile as
  GratiaPenetration::FShaft::RadiusAt): the walls take the shaft's exact cross-section (a head, a knot, a bead),
  surrounding tissue moves less the further it is, fading out over the reach; skin the shaft would pass through anywhere
  along it (lips outside the entrance, a thigh) is pushed out to its surface; the skin in front of the inserted part (the belly, above the floor) moves forward by up
  to Config.w with the shaft's radius where it passes, fading across the belly; and the turn of the surface normal for the toon
  light (the inverse transpose of the field's Jacobian per vertex, interpolated to the pixels).
and wires it into the skin and both clothing materials after the soft press (offsets add, the normal is bent further),
so skin and clothing move together without crossing (the mapping keeps their order). Sets DA_Gratia's ShaftCollection.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SLOTS, POINTS = 4, 12
NAMES = [f'S{s}P{p:02d}' for s in range(SLOTS) for p in range(POINTS)] + [f'S{s}{k}' for s in range(SLOTS) for k in 'ABC']
NAMES += ['Config', 'Config2', 'Config3']
TARGETS = ['/Game/Gratia/CharacterMaterials/M_Gratia_Body_skin', '/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_1',
           '/Game/Gratia/CharacterMaterials/M_Gratia_Default_cloth_2']

# ------------------------------------------------------------------ collection
MPC_PATH = '/Game/Gratia/Physics/MPC_GratiaShafts'
if lib.does_asset_exist(MPC_PATH):
    collection = lib.load_asset(MPC_PATH)
else:
    collection = tools.create_asset('MPC_GratiaShafts', '/Game/Gratia/Physics', unreal.MaterialParameterCollection,
                                    unreal.MaterialParameterCollectionFactoryNew())
existing = {str(p.get_editor_property('parameter_name')) for p in collection.get_editor_property('vector_parameters')}
vectors = list(collection.get_editor_property('vector_parameters'))
for name in NAMES:
    if name in existing:
        continue
    parameter = unreal.CollectionVectorParameter()
    parameter.set_editor_property('parameter_name', name)
    default = {'Config': unreal.LinearColor(1, 0, 0, 0), 'Config2': unreal.LinearColor(14, 0, 0, 0),
               'Config3': unreal.LinearColor(11, 4.5, 0, 0)}.get(name, unreal.LinearColor(0, 0, 0, 0))
    parameter.set_editor_property('default_value', default)
    vectors.append(parameter)
collection.set_editor_property('vector_parameters', vectors)
assert lib.save_loaded_asset(collection, only_if_is_dirty=False)

# ------------------------------------------------------------------ HLSL
# Helper struct (the usual way to have functions in a Custom node). RadiusAt mirrors FShaft::RadiusAt exactly.
FIELD = r'''
struct FGratiaShaftField
{
    float Smooth(float X) { X = saturate(X); return X * X * (3.0 - 2.0 * X); }
    float Cap(float U, float Over) { float X = saturate(U / max(Over, 0.01)); return sqrt(max(0.0, 1.0 - (1.0 - X) * (1.0 - X))); }
    float Bump(float U, float C, float W) { float D = (U - C) / max(W, 0.01); return exp(-D * D); }
    // A: form, radius, length; B: tip length, base scale.
    float RadiusAt(float U, float4 A, float4 B)
    {
        float L = max(A.z, 0.01), R = A.y;
        if (U <= 0.0 || U > A.z + 1e-4) return 0.0;
        float T = saturate(U / L);
        int Form = (int)floor(A.x + 0.5);
        if (Form == 1)
        {
            float Head = max(1.5, 0.2 * L);
            float Body = R * lerp(1.0, 1.1, T);
            if (U < Head) return 1.1 * R * Cap(U, 0.7 * Head);
            if (U < Head + 0.5) return lerp(1.1 * R, 0.86 * R, (U - Head) / 0.5);
            return lerp(0.86 * R, Body, Smooth((U - Head - 0.5) / 2.5));
        }
        if (Form == 2) return R * (0.85 * (0.3 + 0.7 * Smooth(U / (0.3 * L))) * Cap(U, 1.0) + 0.6 * Bump(U, 0.8 * L, 0.07 * L));
        if (Form == 3)
        {
            int Count = (int)clamp(floor(L / (3.2 * R + 1.0) + 0.5), 3.0, 8.0);
            float Total = 0.0;
            [loop] for (int b = 0; b < 8; ++b)
                if (b < Count) Total += (b > 0 ? 0.35 * R : 0.0) + 2.0 * R * (0.55 + 0.45 * b / (float)(Count - 1));
            float Fit = L / max(Total, 0.01), Along = 0.0, Best = 0.22 * R;
            [loop] for (int c = 0; c < 8; ++c)
            {
                if (c >= Count) break;
                float Size = R * (0.55 + 0.45 * c / (float)(Count - 1));
                Along += (c > 0 ? 0.35 * R * Fit : 0.0) + Size * Fit;
                float Half = Size * Fit, Off = U - Along;
                if (abs(Off) < Half) Best = max(Best, Size * sqrt(1.0 - (Off / Half) * (Off / Half)));
                Along += Size * Fit;
            }
            return Best;
        }
        if (Form == 4) return R * lerp(0.3, 1.5, pow(T, 0.85)) * Cap(U, 1.2);
        if (Form == 5)
        {
            float Body = R * lerp(1.0, B.y, T) * Cap(U, B.x);
            float Period = max(1.2, 0.07 * L);
            return U > B.x ? Body * (1.0 + 0.13 * (0.5 + 0.5 * cos(6.2831853 * (U - B.x) / Period))) : Body;
        }
        if (Form == 6)
        {
            float Head = max(1.5, 0.09 * L), Neck = 0.04 * L;
            float Body = R * lerp(0.95, 1.12, T) * (1.0 + 0.16 * Bump(U, 0.45 * L, 0.035 * L));
            if (U < Head) return 1.4 * R * sqrt(Cap(U, Head));
            if (U < Head + Neck) return lerp(1.4 * R, 0.86 * R, Smooth((U - Head) / Neck));
            return lerp(0.86 * R, Body, Smooth((U - Head - Neck) / (0.1 * L)));
        }
        if (Form == 7)
        {
            float Body = R * lerp(0.12, 1.35, pow(T, 0.75)) * Cap(U, 0.6);
            return T > 0.2 ? Body * (1.0 + 0.06 * sin(6.2831853 * U / 1.8)) : Body;
        }
        float Plain = R * lerp(1.0, B.y, T);
        return U >= B.x ? Plain : Plain * Cap(U, B.x);
    }
    // Nearest point of a shaft's path (points from the tip, w = distance from the tip) to P: the distance r, the
    // direction away from the path, the distance U from the tip there and the path's direction T (toward the base).
    void Nearest(float3 P, float4 Pts[12], out float r, out float3 Dir, out float U, out float3 T)
    {
        float Best = 1e20;
        float3 Q = P;
        U = 0.0;
        T = float3(0, 0, 1);
        [loop] for (int k = 0; k < 11; ++k)
        {
            float3 a = Pts[k].xyz, ab = Pts[k + 1].xyz - a;
            float t = saturate(dot(P - a, ab) / max(dot(ab, ab), 1e-6));
            float3 q = a + ab * t;
            float d = dot(P - q, P - q);
            if (d < Best) { Best = d; Q = q; U = lerp(Pts[k].w, Pts[k + 1].w, t); T = ab; }
        }
        r = sqrt(Best);
        Dir = r > 1e-3 ? (P - Q) / r : float3(0, 0, 0);
        T = T - Dir * dot(T, Dir);
        T = dot(T, T) > 1e-8 ? normalize(T) : float3(0, 0, 1);
    }
    // How far tissue at distance r from the path, U from the tip, moves away from it. Inside the body (fading over
    // 1.5 cm outside the entrance), beyond what the opening morph opened (C.x at the entrance, fading back to the closed
    // radius B.z over C.y into the channel), the area between is kept: r' = sqrt(r^2 + R^2 - Open^2), fading out over
    // the reach. Anywhere along the shaft, skin it would pass through goes out to its surface.
    float Field(float r, float U, float4 A, float4 B, float4 C, float4 Cfg2)
    {
        float Range = max(Cfg2.x, 1.0);
        if (r > Range) return 0.0;
        float Rs = RadiusAt(U, A, B);
        if (Rs <= 0.0) return 0.0;
        float Contact = U < A.w + 3.5 ? max(0.0, Rs + 0.15 - r) : 0.0;
        float Inside = saturate((A.w + 1.5 - U) / 1.5);
        float Open = lerp(B.z, max(C.x, B.z), 1.0 - Smooth((A.w - U) / max(C.y, 0.5)));
        float Area = Rs * Rs - Open * Open;
        float Out = 0.0;
        if (Area > 0.0 && Inside > 0.0) Out = (sqrt(r * r + Area) - r) * (1.0 - Smooth((r - Rs) / max(Range - Rs, 1.0))) * Inside;
        return max(Out, Contact);
    }
    // Point of the path at distance U from the tip.
    float3 PathAt(float4 Pts[12], float U)
    {
        float3 At = Pts[11].xyz;
        [loop] for (int k = 0; k < 11; ++k)
            if (U <= Pts[k + 1].w) { At = lerp(Pts[k].xyz, Pts[k + 1].xyz, saturate((U - Pts[k].w) / max(Pts[k + 1].w - Pts[k].w, 1e-4))); break; }
        return At;
    }
    // Belly: the skin in front of the inserted path (the front F = Cfg.xyz) moves forward by up to Cfg.w, fully where
    // the shaft passing it is Cfg3.y thick (40 % of that shows nothing), fading across the belly over Cfg3.x and in
    // over 5 cm above the floor Cfg3.z (the entrances, the pubic area and the thighs stay). Out: the direction across
    // the belly and the push's slope along it (for the normal).
    float3 Belly(float3 P, float4 Pts[12], float4 A, float4 B, float4 Cfg, float4 Cfg3, out float3 Across, out float Slope)
    {
        Across = float3(0, 0, 0);
        Slope = 0.0;
        float3 F = Cfg.xyz;
        float Rb = max(Cfg3.x, 1.0);
        if (Cfg.w <= 0.0 || P.z <= Cfg3.z) return float3(0, 0, 0);
        // The path point straight behind P: the nearest one seen along the front.
        float4 Flat[12];
        [loop] for (int k = 0; k < 12; ++k) Flat[k] = float4(Pts[k].xyz - F * dot(Pts[k].xyz, F), Pts[k].w);
        float a, U;
        float3 Dir, T;
        Nearest(P - F * dot(P, F), Flat, a, Dir, U, T);
        float Inside = saturate((A.w - U) / 1.5);
        if (a >= Rb || Inside <= 0.0) return float3(0, 0, 0);
        float Ahead = dot(P - PathAt(Pts, U), F);
        if (Ahead < -2.0 || Ahead > 25.0) return float3(0, 0, 0);
        float Full = max(Cfg3.y, 0.5);
        float W = saturate((RadiusAt(U, A, B) - 0.4 * Full) / (0.6 * Full));
        float Amount = Cfg.w * W * Inside * Smooth((Ahead + 2.0) / 4.0) * Smooth((P.z - Cfg3.z) / 5.0);
        if (Amount <= 0.0) return float3(0, 0, 0);
        Across = Dir;
        float Fade = 1.0 - Smooth(a / Rb);
        Slope = Amount * ((1.0 - Smooth((a + 0.25) / Rb)) - Fade) / 0.25;
        return normalize(F + Dir * (0.35 * a / Rb)) * (Amount * Fade);
    }
    // Offset of P by every shaft in use, one after the other.
    float3 All(float3 P, float4 Pts[4][12], float4 SA[4], float4 SB[4], float4 SC[4], float4 Cfg, float4 Cfg2, float4 Cfg3)
    {
        float3 Offset = float3(0, 0, 0);
        [loop] for (int s = 0; s < 4; ++s)
        {
            if (SB[s].w <= 0.0) continue;
            float r, U;
            float3 Dir, T;
            Nearest(P + Offset, Pts[s], r, Dir, U, T);
            float3 Across;
            float Slope;
            float3 Swell = Belly(P, Pts[s], SA[s], SB[s], Cfg, Cfg3, Across, Slope);
            if (r >= 1e-3) Offset += Dir * Field(r, U, SA[s], SB[s], SC[s], Cfg2);
            Offset += Swell;
        }
        return Offset * Cfg2.w;
    }
    // Turn of the normal N0 by the strongest shaft at P: the moved surface's normal is the inverse transpose of the
    // field's Jacobian J = I + Dir (dg/dr Dir + dg/dU T)^T + g/r (I - Dir Dir^T - T T^T) applied to N0.
    float3 Turn(float3 P, float3 N0, float4 Pts[4][12], float4 SA[4], float4 SB[4], float4 SC[4], float4 Cfg, float4 Cfg2, float4 Cfg3)
    {
        float G = 0.0, Br = 1.0, BU = 0.0;
        float3 BDir = float3(0, 0, 0), BT = float3(0, 0, 1), Tilt = float3(0, 0, 0);
        int Bs = -1;
        [loop] for (int s = 0; s < 4; ++s)
        {
            if (SB[s].w <= 0.0) continue;
            // The belly's swelling tilts surfaces facing the front away from its middle.
            float3 Across;
            float Slope;
            Belly(P, Pts[s], SA[s], SB[s], Cfg, Cfg3, Across, Slope);
            Tilt -= Across * (Slope * saturate(dot(N0, Cfg.xyz)));
            float r, U;
            float3 Dir, T;
            Nearest(P, Pts[s], r, Dir, U, T);
            if (r < 1e-3) continue;
            float g = Field(r, U, SA[s], SB[s], SC[s], Cfg2);
            if (g > G) { G = g; Br = r; BU = U; BDir = Dir; BT = T; Bs = s; }
        }
        Tilt *= Cfg2.w;
        if (Bs < 0 || G < 1e-4) return dot(Tilt, Tilt) > 1e-10 ? normalize(N0 + Tilt) - N0 : float3(0, 0, 0);
        const float E = 0.25;
        float Gr = (Field(Br + E, BU, SA[Bs], SB[Bs], SC[Bs], Cfg2) - G) / E;
        float Gu = (Field(Br, BU + E, SA[Bs], SB[Bs], SC[Bs], Cfg2) - G) / E;
        float3 Grad = (Gr * BDir + Gu * BT) * Cfg2.w;
        float Hoop = G * Cfg2.w / max(Br, 1e-3);
        float3 C0 = float3(1, 0, 0) + BDir * Grad.x + Hoop * (float3(1, 0, 0) - BDir * BDir.x - BT * BT.x);
        float3 C1 = float3(0, 1, 0) + BDir * Grad.y + Hoop * (float3(0, 1, 0) - BDir * BDir.y - BT * BT.y);
        float3 C2 = float3(0, 0, 1) + BDir * Grad.z + Hoop * (float3(0, 0, 1) - BDir * BDir.z - BT * BT.z);
        float3 Nd = N0.x * cross(C1, C2) + N0.y * cross(C2, C0) + N0.z * cross(C0, C1);
        if (dot(Nd, Nd) < 1e-12) return float3(0, 0, 0);
        Nd = normalize(Nd);
        if (dot(Nd, N0) < 0.0) Nd = -Nd;
        return normalize(Nd + Tilt) - N0;
    }
};
'''
ARRAYS = ('float4 Pts[4][12] = {' + ', '.join('{' + ', '.join(f'S{s}P{p:02d}' for p in range(POINTS)) + '}' for s in range(SLOTS)) + '};\n'
          + 'float4 SA[4] = {S0A, S1A, S2A, S3A};\nfloat4 SB[4] = {S0B, S1B, S2B, S3B};\nfloat4 SC[4] = {S0C, S1C, S2C, S3C};\n')
WPO_CODE = FIELD + ARRAYS + r'''
if (SB[0].w + SB[1].w + SB[2].w + SB[3].w <= 0.0 || Config2.w <= 0.0) return float3(0, 0, 0);
FGratiaShaftField F;
return F.All(P, Pts, SA, SB, SC, Config, Config2, Config3);
'''
NORMAL_CODE = FIELD + ARRAYS + r'''
if (SB[0].w + SB[1].w + SB[2].w + SB[3].w <= 0.0 || Config2.w <= 0.0) return float3(0, 0, 0);
FGratiaShaftField F;
return F.Turn(P, normalize(VN), Pts, SA, SB, SC, Config, Config2, Config3);
'''

# ------------------------------------------------------------------ material function
MF_PATH = '/Game/Gratia/CharacterMaterials/MF_GratiaShaftPress'
if lib.does_asset_exist(MF_PATH):
    function = lib.load_asset(MF_PATH)
    # UE 5.8 deletes only part of the expressions per call (it removes from the array it iterates).
    for _ in range(64):
        if mel.get_num_material_expressions_in_function(function) == 0:
            break
        mel.delete_all_material_expressions_in_function(function)
    assert mel.get_num_material_expressions_in_function(function) == 0, 'MF_GratiaShaftPress could not be cleared'
else:
    function = tools.create_asset('MF_GratiaShaftPress', '/Game/Gratia/CharacterMaterials', unreal.MaterialFunction,
                                  unreal.MaterialFunctionFactoryNew())
function.set_editor_property('description', 'Gratia shaft press: skin and clothing fit the engaged shafts (MPC_GratiaShafts).')
expr = lambda cls, x, y: mel.create_material_expression_in_function(function, cls, x, y)
normal_in = expr(unreal.MaterialExpressionFunctionInput, -1600, -200)
normal_in.set_editor_property('input_name', 'BaseNormal')
normal_in.set_editor_property('input_type', unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3)
world_plain = expr(unreal.MaterialExpressionWorldPosition, -1600, 0)
world_plain.set_editor_property('world_position_shader_offset', unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
vertex_normal = expr(unreal.MaterialExpressionVertexNormalWS, -1600, 150)
params = {}
for index, name in enumerate(NAMES):
    node = expr(unreal.MaterialExpressionCollectionParameter, -1200, -1800 + index * 60)
    node.set_editor_property('collection', collection)
    node.set_editor_property('parameter_name', name)
    params[name] = node


def custom(code, output_type, x, y, extra):
    node = expr(unreal.MaterialExpressionCustom, x, y)
    inputs = []
    for name in extra + NAMES:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    node.set_editor_property('inputs', inputs)
    node.set_editor_property('code', code)
    node.set_editor_property('output_type', output_type)
    for name in NAMES:
        assert mel.connect_material_expressions(params[name], '', node, name), name
    return node


wpo = custom(WPO_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -600, -200, ['P'])
wpo.set_editor_property('description', 'GratiaShaftPressOffset')
assert mel.connect_material_expressions(world_plain, '', wpo, 'P')
# The normal's turn is worked out per vertex (vertex shader) and interpolated: the pixels only add it.
shade = custom(NORMAL_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -600, 200, ['P', 'VN'])
shade.set_editor_property('description', 'GratiaShaftPressNormalTurn')
assert mel.connect_material_expressions(world_plain, '', shade, 'P')
assert mel.connect_material_expressions(vertex_normal, '', shade, 'VN')
interpolated = expr(unreal.MaterialExpressionVertexInterpolator, -300, 200)
assert mel.connect_material_expressions(shade, '', interpolated, 'VS')
turned = expr(unreal.MaterialExpressionAdd, -150, 100)
assert mel.connect_material_expressions(normal_in, '', turned, 'A')
assert mel.connect_material_expressions(interpolated, 'PS', turned, 'B')
bent = expr(unreal.MaterialExpressionNormalize, -50, 100)
assert mel.connect_material_expressions(turned, '', bent, 'VectorInput')
for index, (name, source) in enumerate((('WPO', wpo), ('Normal', bent))):
    out = expr(unreal.MaterialExpressionFunctionOutput, 0, -200 + index * 300)
    out.set_editor_property('output_name', name)
    out.set_editor_property('sort_priority', index)
    assert mel.connect_material_expressions(source, '', out, '')
mel.update_material_function(function)
assert lib.save_loaded_asset(function, only_if_is_dirty=False)

# ------------------------------------------------------------------ materials
soft_press = lib.load_asset('/Game/Gratia/CharacterMaterials/MF_GratiaSoftPress')
report = []
for path in TARGETS:
    material = lib.load_asset(path)
    expressions = list(mel.get_material_expressions(material))
    tagged = lambda tag: next((e for e in expressions if str(e.get_editor_property('desc')) == 'GratiaVR_' + tag), None)
    press = next((e for e in expressions if isinstance(e, unreal.MaterialExpressionMaterialFunctionCall)
                  and e.get_editor_property('material_function') == soft_press), None)
    dot = tagged('KeyDot')
    assert press and dot, (path, 'soft press call or toon key dot not found; run setup_soft_press_material.py first')
    call, add = tagged('ShaftPress'), tagged('ShaftPressOffset')
    status = 'rewired' if call else 'wired'
    if call is None:
        call = mel.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall, -600, 700)
        call.set_editor_property('desc', 'GratiaVR_ShaftPress')
    if add is None:
        add = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -300, 900)
        add.set_editor_property('desc', 'GratiaVR_ShaftPressOffset')
    # Setting the function again refreshes the call's pins after the function was rebuilt; then every link is made anew.
    call.set_editor_property('material_function', function)
    # Normal: soft press -> shaft press -> toon key light; offsets add.
    assert mel.connect_material_expressions(press, 'Normal', call, 'BaseNormal')
    assert mel.connect_material_expressions(call, 'Normal', dot, 'A')
    assert mel.connect_material_expressions(press, 'WPO', add, 'A')
    assert mel.connect_material_expressions(call, 'WPO', add, 'B')
    assert mel.connect_material_property(add, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    errors = mel.recompile_material(material)
    assert not errors, (path, list(errors))
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
    report.append({'material': path, 'status': status})

profile = lib.load_asset('/Game/Characters/Profiles/DA_Gratia')
settings = profile.get_editor_property('penetration')
settings.set_editor_property('shaft_collection', collection)
profile.set_editor_property('penetration', settings)
assert lib.save_loaded_asset(profile, only_if_is_dirty=False)
out = ROOT / 'evidence/06/shaft_material_setup.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps({'collection': MPC_PATH, 'function': MF_PATH, 'materials': report}, indent=2), encoding='utf-8')
unreal.log('GRATIA_SHAFT_MATERIAL_PASS ' + json.dumps(report))
