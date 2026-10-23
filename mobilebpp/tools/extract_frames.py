"""Project the game's original textured card-frame meshes into transparent UI assets."""
import json
import re
import warnings
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image
import UnityPy

ROOT = Path(__file__).resolve().parents[2]
UnityPy.config.FALLBACK_UNITY_VERSION = '6000.3.11f1'
warnings.filterwarnings('ignore', message='No valid Unity version found.*')


def project_mesh(obj, texture, height=400):
    vertices, uvs, faces = [], [], []
    frame_surface = True
    for line in obj.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'g':
            # Submesh 0 uses the frame diffuse. Other submeshes are the glass
            # and card back, each with different materials; do not paint them.
            frame_surface = parts[1].endswith('_0')
        elif parts[0] == 'v':
            vertices.append([float(v) for v in parts[1:4]])
        elif parts[0] == 'vt':
            uvs.append([float(v) for v in parts[1:3]])
        elif parts[0] == 'f' and frame_surface:
            refs = [[int(i) - 1 for i in p.split('/')[:2]] for p in parts[1:]]
            for i in range(1, len(refs) - 1):
                faces.append([refs[0], refs[i], refs[i + 1]])
    vertices, uvs = np.array(vertices), np.array(uvs)
    minimum, maximum = vertices.min(axis=0), vertices.max(axis=0)
    ratio = (maximum[0] - minimum[0]) / (maximum[2] - minimum[2])
    width = round(height * ratio)
    screen = np.column_stack(((vertices[:, 0] - minimum[0]) / (maximum[0] - minimum[0]) * (width - 1),
                              (maximum[2] - vertices[:, 2]) / (maximum[2] - minimum[2]) * (height - 1)))
    output = np.zeros((height, width, 4), dtype=np.uint8)
    depth = np.full((height, width), -np.inf)
    pixels = np.array(texture.convert('RGB'))
    for face in faces:
        vi = [r[0] for r in face]
        a, b, c = screen[vi]
        denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denominator) < 1e-8:
            continue
        points = screen[vi]
        x0, y0 = np.maximum(np.floor(points.min(axis=0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(points.max(axis=0)).astype(int), (width - 1, height - 1))
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        wa = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / denominator
        wb = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / denominator
        wc = 1 - wa - wb
        weights = np.stack((wa, wb, wc), axis=-1)
        # The illustrated front faces negative Y in the exported Unity mesh.
        z = -(weights @ vertices[vi, 1])
        visible = (weights.min(axis=-1) >= -1e-6) & (z > depth[y0:y1 + 1, x0:x1 + 1])
        uv = weights @ uvs[[r[1] for r in face]]
        tx = np.clip(np.rint(uv[..., 0] * (pixels.shape[1] - 1)).astype(int), 0, pixels.shape[1] - 1)
        ty = np.clip(np.rint((1 - uv[..., 1]) * (pixels.shape[0] - 1)).astype(int), 0, pixels.shape[0] - 1)
        region = output[y0:y1 + 1, x0:x1 + 1]
        region[visible, :3] = pixels[ty[visible], tx[visible]]
        region[visible, 3] = 255
        depth[y0:y1 + 1, x0:x1 + 1][visible] = z[visible]
    return Image.fromarray(output), {'width': float(maximum[0] - minimum[0]),
                                     'height': float(maximum[2] - minimum[2]), 'aspect': float(ratio)}


def main():
    output = ROOT / 'mobilebpp/web/frames'
    output.mkdir(parents=True, exist_ok=True)
    bundle = 'assets/aa/Android/cardframes_assets_all.bundle'
    report = {'source': bundle, 'projection': 'orthographic X/Z, original mesh UV and diffuse', 'frames': {}}
    with zipfile.ZipFile(ROOT / 'TheBazaar.apk') as apk:
        env = UnityPy.load(apk.read(bundle))
        textures, meshes = {}, {}
        for path, ptr in env.container.items():
            match = re.search(r'/Card_Frame_(Bronze|Silver|Gold|Diamond|Legendary)_([SML])\.png$', path)
            if match:
                textures[match.groups()] = ptr.read().image
        # All variants can share serialized files; index each file only once.
        files = {id(ptr.assetsfile): ptr.assetsfile for ptr in env.container.values()}
        for file in files.values():
            for reader in file.objects.values():
                if reader.type.name != 'Mesh':
                    continue
                mesh = reader.read()
                match = re.fullmatch(r'CardFrame_(Bronze|Silver|Gold|Diamond|Legendary)_([SML])', mesh.m_Name)
                if match:
                    meshes[match.groups()] = mesh
        for key, texture in textures.items():
            if key not in meshes:
                raise ValueError('Missing frame mesh: ' + str(key))
            image, dimensions = project_mesh(meshes[key].export(), texture)
            filename = '-'.join(key).lower() + '.webp'
            image.save(output / filename, 'WEBP', lossless=True)
            report['frames'][filename] = dict(dimensions, mesh=meshes[key].m_Name, pixels=list(image.size))
            print(filename, image.size, round(dimensions['aspect'], 6), flush=True)
    (ROOT / 'mobilebpp/web/frame-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
