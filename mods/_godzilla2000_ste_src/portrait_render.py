# Retrato do HUD a partir do modelo do STE: blender -b -P portrait_render.py -- <rig.glb> <prefixo de saida> <portrait_shot.json>
# Renderiza a cabeca (fundo transparente) e grava <prefixo>_<name>.png; portrait_compose.py monta o retrato.
# Cada tomada: {"clip": "a92", "frame": 60, "az": 45, "el": 8, "dist": 48, "name": "final", "jaw": 30, "jawAxis": "Z",
#               "look": [dx,dy,dz], "size": 512, "lens": 50, "keyE": 6, "rimE": 7, "key": [frente, lado, cima]}
# clip/frame: a pose (animacao do rig.glb, congelada no quadro); jaw: graus a mais na Bip01_Jaw (Z abre a boca);
# az/el/dist: camera em volta do meio da cabeca (+Y e a frente do monstro: az 0 = perfil virado para a direita).
import bpy, sys, json, math
from mathutils import Vector, Matrix, Euler

argv = sys.argv[sys.argv.index("--") + 1:]
glb, out, shots = argv[0], argv[1], json.loads(open(argv[2]).read())

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
body = next(o for o in bpy.data.objects if o.type == "MESH")
scn = bpy.context.scene

# materiais: base color da textura, sem metal, um pouco de brilho
for m in bpy.data.materials:
    if not m.use_nodes:
        continue
    for n in m.node_tree.nodes:
        if n.type == "BSDF_PRINCIPLED":
            n.inputs["Metallic"].default_value = 0.0
            n.inputs["Roughness"].default_value = 0.55
            if "Specular IOR Level" in n.inputs:
                n.inputs["Specular IOR Level"].default_value = 0.35
    # recortes da textura: o alfa do STE vai de 0 a 128 (128 = opaco); alfa x 2 no Principled (Mechagodzilla 3: pecas transparentes)
    nt = m.node_tree
    tex = next((n for n in nt.nodes if n.type == "TEX_IMAGE"), None)
    bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if tex is not None and bsdf is not None:
        mul = nt.nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.use_clamp = True
        mul.inputs[1].default_value = 2.0
        nt.links.new(tex.outputs["Alpha"], mul.inputs[0])
        nt.links.new(mul.outputs[0], bsdf.inputs["Alpha"])
    m.use_backface_culling = False

scn.render.engine = "BLENDER_EEVEE"
scn.render.film_transparent = True
scn.render.image_settings.file_format = "PNG"
scn.render.image_settings.color_mode = "RGBA"
scn.view_settings.view_transform = "Standard"
scn.world = bpy.data.worlds.new("w")
scn.world.use_nodes = True
bg = scn.world.node_tree.nodes["Background"]
bg.inputs[0].default_value = (0.25, 0.25, 0.28, 1)
bg.inputs[1].default_value = 0.6

cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scn.collection.objects.link(cam)
scn.camera = cam

def light(name, kind, energy, rot, color=(1, 1, 1)):
    l = bpy.data.lights.new(name, kind)
    l.energy = energy
    l.color = color
    o = bpy.data.objects.new(name, l)
    o.rotation_euler = Euler([math.radians(a) for a in rot])
    scn.collection.objects.link(o)
    return o

key = light("key", "SUN", 4.0, (0, 0, 0))
rim = light("rim", "SUN", 3.0, (0, 0, 0), (0.9, 0.95, 1.0))
fill = light("fill", "SUN", 0.8, (0, 0, 0))

def bone_world(name, head=True):
    pb = arm.pose.bones[name]
    return arm.matrix_world @ (pb.head if head else pb.tail)

def look_at(obj, target):
    d = (target - obj.location).normalized()
    obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()

def aim_sun(obj, direction):
    obj.rotation_euler = (-direction.normalized()).to_track_quat("-Z", "Y").to_euler()

for s in shots:
    act = bpy.data.actions.get(s["clip"])
    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = act
    if act is not None and hasattr(arm.animation_data, "action_slot") and act.slots:
        arm.animation_data.action_slot = act.slots[0]
    scn.frame_set(int(s.get("frame", 0)))
    arm.animation_data.action = None  # congela a pose do quadro (o render reaplicaria a animacao)
    jaw = s.get("jaw", 0)
    jawname = s.get("jawBone", "Bip01_Jaw")   # Mechagodzilla 3: "Bone01"
    pj = arm.pose.bones.get(jawname)
    if pj is not None and jaw:
        pj.rotation_mode = "QUATERNION"
        ax = s.get("jawAxis", "Z")
        e = [0, 0, 0]
        e["XYZ".index(ax)] = math.radians(jaw)
        pj.rotation_quaternion = pj.rotation_quaternion @ Euler(e).to_quaternion()
    bpy.context.view_layer.update()

    head = bone_world("Bip01_Head")
    jawp = bone_world(jawname)
    tgt = (head + jawp) * 0.5 + Vector(s.get("look", [0, 0, 0]))
    az, el, dist = math.radians(s.get("az", 90)), math.radians(s.get("el", 10)), s.get("dist", 2.0)
    off = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el))) * dist
    cam.location = tgt + off
    look_at(cam, tgt)
    cam.data.lens = s.get("lens", 50)
    # luz: chave de cima e da frente da camera, recorte de tras
    fwd = (tgt - cam.location).normalized()
    up = Vector((0, 0, 1))
    side = fwd.cross(up).normalized()
    kd = s.get("key", [0.6, -0.5, 0.7])
    aim_sun(key, fwd * kd[0] + side * kd[1] - up * kd[2])
    aim_sun(rim, -fwd * 1.0 - side * 0.4 - up * 0.3)
    aim_sun(fill, fwd * 0.5 + side * 0.8 + up * 0.2)
    key.data.energy = s.get("keyE", 4.0)
    rim.data.energy = s.get("rimE", 3.0)
    sz = s.get("size", 512)
    scn.render.resolution_x = sz
    scn.render.resolution_y = sz
    scn.render.filepath = "%s_%s.png" % (out, s["name"])
    bpy.ops.render.render(write_still=True)
print("pronto")
