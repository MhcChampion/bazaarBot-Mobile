"""Export card illustrations by CardData.cardGUID -> material -> diffuse texture.

Uses exact serialized references instead of fuzzy name matching. Game-only placeholder
or dynamically downloaded art stays absent and is listed in art-report.json.
"""
import argparse
import gc
import json
import shutil
import sqlite3
import struct
import warnings
import zipfile
from pathlib import Path

import UnityPy

ROOT = Path(__file__).resolve().parents[2]
UnityPy.config.FALLBACK_UNITY_VERSION = '6000.3.11f1'
warnings.filterwarnings('ignore', message='No valid Unity version found.*')


def card_illustration(texture):
    # Card shaders use diffuse alpha as an effect mask, not image transparency.
    # Drop it BEFORE resizing: RGBA resampling destroys RGB under the mask.
    image = texture.image.convert('RGB')
    image.thumbnail((320, 320))
    return image


def addressable_names(apk, records):
    """Resolve exact ArtKey entries in the game's length-prefixed catalog strings."""
    catalogs = [n for n in apk.namelist() if n.startswith('assets/aa/Android/catalog_') and n.endswith('.bin')]
    result = {}
    if not catalogs:
        return result
    catalog = apk.read(sorted(catalogs)[-1])
    for guid, card in records.items():
        key = card.get('ArtKey') or ''
        if len(key) != 32 or any(c not in '0123456789abcdefABCDEF' for c in key):
            continue
        token = struct.pack('<I', len(key)) + key.encode('ascii')
        at = catalog.find(token)
        if at < 0:
            continue
        end = at + len(token)
        if end + 4 > len(catalog):
            continue
        size = struct.unpack_from('<I', catalog, end)[0]
        if size > 512 or end + 4 + size > len(catalog):
            continue
        try:
            asset = catalog[end+4:end+4+size].decode('utf-8')
        except UnicodeDecodeError:
            continue
        if asset.endswith(('_CardData.asset', '_QuestData.asset', '_QuestData1.asset', '_MerchantData.asset', '_CombatData.asset', '.png')):
            result.setdefault(Path(asset).name.casefold(), []).append(guid)
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--apk', type=Path, default=ROOT / 'TheBazaar.apk')
    p.add_argument('--db', type=Path, default=ROOT / 'work/GameData.db')
    p.add_argument('--bundle', help='Optional single bundle basename, e.g. card_karnok_assets_all.bundle')
    p.add_argument('--refresh-card-art', action='store_true', help='Re-export diffuse RGB, replacing old masked thumbnails')
    p.add_argument('--icons-only', '--supplemental-only', dest='supplemental_only', action='store_true', help='Add shared icons and encounter portraits without rescanning card bundles')
    p.add_argument('--output', type=Path, default=ROOT / 'mobilebpp/web/art')
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(args.db.resolve().as_uri() + '?mode=ro', uri=True) as db:
        records = {g: json.loads(d) for g, d in db.execute('SELECT Id, Data FROM cards')}
    report_path = args.output.parent / 'art-report.json'
    previous = json.loads(report_path.read_text(encoding='utf-8')) if report_path.exists() else {}
    exported = {g: source for g, source in previous.get('sources', {}).items() if g in records and (args.output / (g + '.webp')).exists()}
    errors = []
    with zipfile.ZipFile(args.apk) as apk:
        catalog_targets = addressable_names(apk, records)
        bundles = [n for n in apk.namelist() if n.rsplit('/',1)[-1].startswith('card_') and n.endswith('.bundle') and '_vfx_' not in n]
        if args.bundle:
            bundles = [n for n in bundles if n.endswith('/' + args.bundle)]
            if not bundles:
                raise ValueError('Requested bundle not found')
        if args.supplemental_only:
            bundles = []
        for bundle in bundles:
            env = UnityPy.load(apk.read(bundle))
            count = 0
            for asset_path, ptr in env.container.items():
                if not asset_path.endswith('_CardData.asset'):
                    continue
                try:
                    card = ptr.read_typetree()
                    guid = card.get('cardGUID')
                    targets = set(catalog_targets.get(Path(asset_path).name.casefold(), []))
                    if guid in records:
                        targets.add(guid)
                    targets = [g for g in targets if args.refresh_card_art or not (args.output / (g + '.webp')).exists()]
                    if not targets:
                        continue
                    material_ref = card.get('cardMaterial') or {}
                    if material_ref.get('m_FileID') != 0:
                        continue
                    material = ptr.assetsfile.objects[material_ref['m_PathID']].read_typetree()
                    found = None
                    main_texture = None
                    for property_name, info in material.get('m_SavedProperties', {}).get('m_TexEnvs', []):
                        ref = info['m_Texture']
                        if ref['m_FileID'] != 0 or not ref['m_PathID']:
                            continue
                        reader = ptr.assetsfile.objects.get(ref['m_PathID'])
                        if not reader or reader.type.name != 'Texture2D':
                            continue
                        texture = reader.read()
                        texture_name = texture.m_Name.strip()
                        if texture_name.startswith('CF_') and texture_name.endswith('_D'):
                            found = texture
                            break
                        if property_name == '_MainTex' and texture_name.startswith('CF_') and not any(k in texture_name.lower() for k in ['mask', 'flowmap']):
                            main_texture = texture
                    if found is None:
                        found = main_texture
                    if found:
                        image = card_illustration(found)
                        for guid in targets:
                            image.save(args.output / (guid + '.webp'), 'WEBP', quality=80, method=4)
                            exported[guid] = {'bundle': bundle, 'card_data': asset_path, 'texture': found.m_Name,
                                              'matched_by': 'cardGUID' if guid == card.get('cardGUID') else 'catalog ArtKey',
                                              'alpha': 'effect_mask_discarded'}
                            count += 1
                except Exception as error:
                    errors.append({'asset': asset_path, 'error': str(error)})
            print(f'{bundle.rsplit("/",1)[-1]}: exported {count}', flush=True)
            del env
            gc.collect()
        if not args.bundle:
            icon_records = {}
            for guid, data in records.items():
                key = Path(data.get('ArtKey') or '').stem.casefold()
                if key and not (args.output / (guid + '.webp')).exists():
                    icon_records.setdefault(key, []).append(guid)
            for bundle_name in ['skills_assets_all.bundle', 'rewards_assets_all.bundle', 'encounter_assets_all.bundle']:
                bundle = 'assets/aa/Android/' + bundle_name
                env = UnityPy.load(apk.read(bundle))
                count = 0
                for asset_path, ptr in env.container.items():
                    guids = set(icon_records.get(Path(asset_path).stem.casefold(), []))
                    guids.update(catalog_targets.get(Path(asset_path).name.casefold(), []))
                    if not guids:
                        continue
                    try:
                        image = getattr(ptr.read(), 'image', None)
                        if image is None:
                            continue
                        image = image.convert('RGBA')
                        image.thumbnail((160, 160))
                        for guid in guids:
                            if (args.output / (guid + '.webp')).exists():
                                continue
                            image.save(args.output / (guid + '.webp'), 'WEBP', quality=80, method=4)
                            exported[guid] = {'bundle': bundle, 'icon': asset_path}
                            count += 1
                    except Exception as error:
                        errors.append({'asset': asset_path, 'error': str(error)})
                print(f'{bundle_name}: exported icons {count}', flush=True)
                del env
                gc.collect()
            for bundle_name in ['merchant_assets_all.bundle', 'quest_assets_all.bundle', 'combat_assets_all.bundle']:
                bundle = 'assets/aa/Android/' + bundle_name
                env = UnityPy.load(apk.read(bundle))
                count = 0
                for asset_path, ptr in env.container.items():
                    guids = [g for g in catalog_targets.get(Path(asset_path).name.casefold(), [])
                             if not (args.output / (g + '.webp')).exists()]
                    if not guids:
                        continue
                    try:
                        data = ptr.read_typetree()
                        reader, source_field = None, ''
                        for field in ['portraitTextureReference', 'backgroundTextureReference']:
                            ref = data.get(field) or {}
                            if ref.get('m_FileID') != 0 or not ref.get('m_PathID'):
                                continue
                            candidate = ptr.assetsfile.objects.get(ref['m_PathID'])
                            if candidate and candidate.type.name in ['Texture2D', 'Sprite']:
                                reader, source_field = candidate, field
                                break
                        if not reader:
                            continue
                        texture = reader.read()
                        image = texture.image.convert('RGBA')
                        image.thumbnail((200, 240))
                        for guid in guids:
                            image.save(args.output / (guid + '.webp'), 'WEBP', quality=80, method=4)
                            exported[guid] = {'bundle': bundle, 'portrait_data': asset_path, 'texture': texture.m_Name,
                                              'matched_by': 'catalog ArtKey + ' + source_field}
                            count += 1
                    except Exception as error:
                        errors.append({'asset': asset_path, 'error': str(error)})
                print(f'{bundle_name}: exported portraits {count}', flush=True)
                del env
                gc.collect()
    # Multiple card records can intentionally reuse the same illustration.
    by_art = {records[g].get('ArtKey'): g for g in exported if records[g].get('ArtKey')}
    for guid, data in records.items():
        source = by_art.get(data.get('ArtKey'))
        target = args.output / (guid + '.webp')
        if source and (not target.exists() or (args.refresh_card_art and guid not in exported)):
            shutil.copyfile(args.output / (source + '.webp'), target)
            exported[guid] = {'shared_with': source}
    missing = [g for g in records if not (args.output / (g + '.webp')).exists()]
    missing_details = []
    known_catalog_targets = {g for targets in catalog_targets.values() for g in targets}
    for guid in missing:
        record = records[guid]
        key = record.get('ArtKey') or ''
        missing_details.append({'guid': guid, 'type': record.get('Type'), 'name': record.get('InternalName'), 'art_key': key,
                                'reason': 'no_game_art_key' if not key else 'catalog_asset_not_exported' if guid in known_catalog_targets else 'art_not_resolved_in_current_apk'})
    report = {'cards': len(records), 'pictures': len(records)-len(missing), 'missing': missing, 'missing_details': missing_details, 'sources': exported, 'errors': errors}
    (args.output.parent / 'art-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'pictures: {report["pictures"]}/{len(records)}; errors: {len(errors)}', flush=True)


if __name__ == '__main__':
    main()
