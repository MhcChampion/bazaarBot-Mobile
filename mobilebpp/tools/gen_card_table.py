"""Rebuild static card metadata and offline Chinese search data from game SQLite files."""
import argparse
import json
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--db', type=Path, default=ROOT / 'work/GameData.db')
    p.add_argument('--translations', type=Path, default=ROOT / 'work/zh-CN.bytes')
    args = p.parse_args()
    with sqlite3.connect(args.db.resolve().as_uri() + '?mode=ro', uri=True) as db:
        rows = [(guid, json.loads(data)) for guid, data in db.execute('SELECT Id, Data FROM cards ORDER BY Id')]
    with sqlite3.connect(args.translations.resolve().as_uri() + '?mode=ro', uri=True) as db:
        names = dict(db.execute('SELECT hash, text FROM translation'))
    cpp, cards, untranslated = [], [], []
    for guid, data in rows:
        title = (data.get('Localization') or {}).get('Title') or {}
        english = title.get('Text') or data.get('InternalName') or guid
        key = title.get('Key') or ''
        chinese = names.get(key) or english
        if not names.get(key) or names[key] == english:
            untranslated.append({'guid': guid, 'english': english, 'type': data.get('Type'),
                                 'title_key': key, 'reason': 'official_translation_absent' if key not in names else 'official_translation_equals_english'})
        hero, tags = ','.join(data.get('Heroes') or []), ','.join(data.get('Tags') or [])
        tier, size, art = data.get('StartingTier') or '', data.get('Size') or '', data.get('ArtKey') or ''
        values = [guid, english, key, hero, tags, tier, size, chinese, art]
        cpp.append('    {' + ', '.join(json.dumps(v, ensure_ascii=False) for v in values) + '},')
        cards.append(dict(g=guid, n=chinese, en=english, h=hero, tags=tags, t=tier, s=size, art=''))
    # Keep the binary-search implementation after the generated array.
    target = ROOT / 'mobilebpp/native/card_table.cpp'
    old = target.read_text(encoding='utf-8')
    prefix, rest = old.split('const StaticCard kCards[] = {', 1)
    _, suffix = rest.split('\n};', 1)
    target.write_text(prefix + 'const StaticCard kCards[] = {\n' + '\n'.join(cpp) + '\n};' + suffix, encoding='utf-8')
    art_dir = ROOT / 'mobilebpp/web/art'
    for c in cards:
        if (art_dir / (c['g'] + '.webp')).is_file():
            c['art'] = 'file:///android_asset/bpp/art/' + c['g'] + '.webp'
    (ROOT / 'mobilebpp/web/cards.json').write_text(json.dumps(cards, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    translated = sum(c['n'] != c['en'] for c in cards)
    coverage = {'cards': len(cards), 'translated': translated, 'fallbacks': untranslated}
    (ROOT / 'mobilebpp/web/translation-report.json').write_text(json.dumps(coverage, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'generated {len(cards)} cards; Chinese names: {translated}; pictures: {sum(bool(c["art"]) for c in cards)}')


if __name__ == '__main__':
    main()
