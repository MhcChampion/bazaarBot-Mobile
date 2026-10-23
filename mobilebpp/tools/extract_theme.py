"""Reuse original game UI sprites, carpet meshes and skill tokens in the companion UI."""
import io
import json
import re
import sqlite3
import zipfile
from pathlib import Path

import UnityPy
from PIL import Image
from fontTools.ttLib import TTFont

from extract_frames import ROOT, project_mesh

OUTPUT = ROOT / 'mobilebpp/web/theme'
SPRITES = {
    'panel-frame': 'Frame_General_TUI',
    'button-frame': 'Btn_Rct_Frame_S_TUI',
    'button-fill': 'Btn_Rct_Brown_S_Center_Active_TUI',
    'button-selected': 'Btn_Rct_Selected_TUI',
    'ornate-button': 'Button_Ornate_Active_TUI',
    'round-frame': 'Btn_Rnd_Frame_TUI',
    'round-fill': 'Btn_Rnd_Center_Filled_Active_TUI',
    'tab': 'Btn_Tab_Nav_Active_TUI',
    'tab-selected': 'Btn_Tab_Nav_Selected_TUI',
    'checkbox': 'Form_Checkbox_Frame_TUI',
    'check': 'Form_Checkbox_Check_TUI',
    'divider': 'Divider_Horz_01_TUI',
    'back-icon': 'Icon_RingBtn_Back_TUI',
    'list-icon': 'Icon_Menu_List_TUI',
    'star-icon': 'Icon_Menu_Favorite_TUI',
    'watch-icon': 'Icon_Menu_Watch_TUI',
    'gear-icon': 'Icon_RingBtn_Gear_TUI',
    'arrow-icon': 'Frame_Pointer_Arrow_TUI',
    'row-frame': 'Tooltip_Bronze_Frame_Active_TUI',
    'follow-banner': 'PlayerProfile_Leaderboard_ItemBanner_TUI',
}


def main():
    OUTPUT.mkdir(exist_ok=True)
    with sqlite3.connect((ROOT / 'work/GameData.db').as_uri() + '?mode=ro', uri=True) as db:
        collectibles = json.loads(db.execute("SELECT Data FROM collectibles WHERE Id='carpet'").fetchone()[0])
    labels = {c['InternalName'].lower(): c['Name'] for c in collectibles}
    report = {'source': 'TheBazaar.apk', 'carpets': [], 'skills': {}, 'sprites': {}, 'fonts': {}, 'skipped': []}
    with zipfile.ZipFile(ROOT / 'TheBazaar.apk') as apk:
        prefix = 'assets/aa/Android/'
        common = UnityPy.load(apk.read(prefix + 'carpet_common_assets_all.bundle'))
        carpet_mesh = next(o.read().export() for o in common.objects if o.type.name == 'Mesh' and o.read().m_Name == 'Carpet')
        for name in sorted(apk.namelist()):
            if not re.search(r'/carpet_.*_assets_all.bundle$', name) or re.search(r'_r[01]{4}_', name) or 'carpet_common_' in name:
                continue
            env = UnityPy.load(apk.read(name))
            textures = [o.read() for o in env.objects if o.type.name == 'Texture2D' and re.search(r'_(TD|D)$', o.read().m_Name)]
            if not textures:
                report['skipped'].append({'bundle': name, 'reason': 'No carpet diffuse; prefab variant only'})
                continue
            texture = textures[0]
            image, geometry = project_mesh(carpet_mesh, texture.image, 200)
            key = name.split('/')[-1].removesuffix('_assets_all.bundle')
            image.save(OUTPUT / (key + '.webp'), 'WEBP', quality=90, method=6)
            report['carpets'].append({'id': key, 'name': labels.get(key, key.removeprefix('carpet_').replace('_', ' ')), 'file': key + '.webp', 'bundle': name, 'texture': texture.m_Name, 'aspect': geometry['aspect']})
        skill_bundle = prefix + 'skills_assets_all.bundle'
        env = UnityPy.load(apk.read(skill_bundle))
        textures = {o.read().m_Name: o.read().image for o in env.objects if o.type.name == 'Texture2D' and 'frame' in o.read().m_Name.lower()}
        for o in env.objects:
            if o.type.name != 'Mesh':
                continue
            mesh = o.read()
            match = re.fullmatch(r'SkillFrame_(Bronze|Silver|Gold|Diamond|Legendary)_SM', mesh.m_Name)
            if not match:
                continue
            tier = match[1].lower()
            texture_name = 'Skill_Frame_Token_Legendary_D' if tier == 'legendary' else 'SkillFrame_Token_TD'
            image, geometry = project_mesh(mesh.export(), textures[texture_name], 160)
            image.save(OUTPUT / ('skill-' + tier + '.webp'), 'WEBP', lossless=True)
            report['skills'][tier] = dict(geometry, mesh=mesh.m_Name, texture=texture_name, bundle=skill_bundle)
        wanted = {v: k for k, v in SPRITES.items()}
        candidates = [n for n in apk.namelist() if n.endswith('.bundle') and ('ui_buttons_assets_' in n or 'ui_sprite_atlas_' in n or 'mainmenu_settingsdialog' in n or 'icon_ringbtn_' in n or 'icon_menu_' in n)]
        images = {}
        # UI prefabs refer to sprites packed in another bundle. Load their atlas
        # into the same environment so UnityPy can resolve the original PPtrs.
        env = UnityPy.load(*(apk.read(n) for n in candidates))
        for o in env.objects:
            if o.type.name != 'Sprite':
                continue
            data = o.read()
            alias = wanted.get(data.m_Name)
            if not alias or alias in images:
                continue
            image = data.image.convert('RGBA')
            image.save(OUTPUT / (alias + '.webp'), 'WEBP', lossless=True)
            images[alias] = image
            report['sprites'][alias] = {'sprite': data.m_Name, 'asset_file': o.assets_file.name, 'pixels': list(image.size), 'border': {a: getattr(data.m_Border, a) for a in 'xyzw'}}
        missing = set(SPRITES) - set(images)
        if missing:
            raise ValueError('Missing original UI sprites: ' + ', '.join(sorted(missing)))
        # The dock target consists only of the game's round button and back glyph.
        dock = Image.new('RGBA', (128, 128))
        for key, size in [('round-fill', 116), ('round-frame', 128), ('back-icon', 60)]:
            im = images[key].copy();im.thumbnail((size, size), Image.Resampling.LANCZOS)
            dock.alpha_composite(im, ((128 - im.width) // 2, (128 - im.height) // 2))
        dock.save(OUTPUT / 'dock.webp', 'WEBP', lossless=True)
        font_bundle = prefix + 'fonts_cn_assets_all.bundle'
        env = UnityPy.load(apk.read(font_bundle))
        for o in env.objects:
            if o.type.name != 'Font':
                continue
            font = o.read()
            if font.m_Name != 'NotoSerifSC-SemiBold':
                continue
            parsed = TTFont(io.BytesIO(bytes(font.m_FontData)));parsed.flavor = 'woff2'
            parsed.save(OUTPUT / 'game-heading.woff2')
            report['fonts']['game-heading'] = {'name': font.m_Name, 'bundle': font_bundle}
    report['carpets'].sort(key=lambda c: (c['id'] != 'carpet_1', c['id']))
    (OUTPUT.parent / 'theme-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f"Original assets: {len(report['carpets'])} carpets, {len(report['skills'])} skill tokens, {len(report['sprites'])} UI sprites, {len(report['fonts'])} font")


if __name__ == '__main__':
    main()
