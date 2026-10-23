"""Identity changes must keep Android's compiled XML, components and resources usable."""
import contextlib
import io
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'mobilebpp/tools'))
import apk_identity
import patch_apk

NEW_PACKAGE = 'com.tempogames.thebazaar.bazaarbotmobile'
ANDROID = apk_identity.ANDROID_NS


def synthetic_manifest(utf8=False, shared_user=False):
    """A compiled fixture with relative classes and deliberately shared string values."""
    old = 'com.example.game'
    text = lambda value: (3, value)
    number = lambda value: (0x10, value)
    tree = ('manifest', {
        'package': text(old), 'versionCode': number(99), 'versionName': text('9.9'),
        'versionCodeMajor': number(4),
    }, [
        ('permission', {'name': text(old + '.PRIVATE')}, []),
        ('uses-permission', {'name': text(old + '.PRIVATE')}, []),
        ('uses-permission', {'name': text('android.permission.INTERNET')}, []),
        ('application', {'name': text('.GameApp'), 'label': text('Original'), 'backupAgent': text('Backup'),
                         'appComponentFactory': text('androidx.core.app.CoreComponentFactory')}, [
            ('activity', {'name': text('.Main'), 'parentActivityName': text('Parent'),
                          'permission': text(old + '.PRIVATE'), 'label': text('Launcher'),
                          'taskAffinity': text(old), 'process': text(':private')}, [
                ('intent-filter', {}, [('action', {'name': text('android.intent.action.MAIN')}, []),
                                       ('category', {'name': text('android.intent.category.LAUNCHER')}, [])]),
            ]),
            ('activity-alias', {'name': text('Alias'), 'targetActivity': text('.Main')}, []),
            ('provider', {'name': text('com.example.game.RealProvider'),
                          'authorities': text(old + '.files;shared.authority'),
                          'readPermission': text(old + '.PRIVATE')}, []),
            ('service', {'name': text('Worker'), 'process': text(old + '.worker')}, []),
            ('meta-data', {'name': text('oauth.original.package'), 'value': text(old)}, []),
            ('meta-data', {'name': text('unicode'), 'value': text('雪😀' * 70)}, []),
        ]),
        ('queries', {}, [('provider', {'authorities': text('external.provider')}, [])]),
    ])
    strings = []
    def index(value):
        if value not in strings:
            strings.append(value)
        return strings.index(value)
    def node(name, attrs, children):
        name_index = index(name)
        values = bytearray()
        for key, (kind, value) in attrs.items():
            ns = apk_identity.NO_INDEX if key == 'package' else index(ANDROID)
            key_index = index(key)
            data = index(value) if kind == 3 else value
            raw = data if kind == 3 else apk_identity.NO_INDEX
            values += struct.pack('<IIIHBBI', ns, key_index, raw, 8, 0, kind, data)
        start = (struct.pack('<HHIII', 0x0102, 16, 36 + len(values), 1, apk_identity.NO_INDEX)
                 + struct.pack('<IIHHHHHH', apk_identity.NO_INDEX, name_index, 20, 20, len(attrs), 0, 0, 0) + values)
        end = struct.pack('<HHIIIII', 0x0103, 16, 24, 1, apk_identity.NO_INDEX, apk_identity.NO_INDEX, name_index)
        return start + b''.join(node(*child) for child in children) + end
    if shared_user:
        tree[1]['sharedUserId'] = text('com.example.shared')
    nodes = node(*tree)
    encoded, offsets = bytearray(), []
    for value in strings:
        offsets.append(len(encoded))
        utf16 = value.encode('utf-16le')
        if utf8:
            raw = value.encode('utf-8')
            def length(n):
                return bytes([0x80 | (n >> 8), n & 255]) if n > 127 else bytes([n])
            encoded += length(len(utf16) // 2) + length(len(raw)) + raw + b'\0'
        else:
            encoded += struct.pack('<H', len(utf16) // 2) + utf16 + b'\0\0'
    encoded += b'\0' * (-len(encoded) % 4)
    start = 28 + 4 * len(strings)
    pool = struct.pack('<HHIIIIII', 1, 28, start + len(encoded), len(strings), 0,
                       0x100 if utf8 else 0, start, 0) + struct.pack(f'<{len(offsets)}I', *offsets) + encoded
    unknown = struct.pack('<HHII', 0x7777, 8, 12, 0xDEADBEEF)
    body = pool + unknown + nodes
    return struct.pack('<HHI', 3, 8, len(body) + 8) + body


class BinaryIdentityTests(unittest.TestCase):
    def test_utf8_and_utf16_preserve_classes_and_only_rewrite_owned_identifiers(self):
        for utf8 in (False, True):
            with self.subTest(utf8=utf8):
                source = synthetic_manifest(utf8)
                before = apk_identity.BinaryManifest(source)
                data = apk_identity.patch_manifest(source, package_name=NEW_PACKAGE, app_label='雪😀 - bazaarBot Mobile',
                                                   version_name='0.0.1', version_code=1)
                after = apk_identity.BinaryManifest(data)
                self.assertEqual(after.package, NEW_PACKAGE)
                self.assertEqual(after.root.get('versionCodeMajor').value, 0)
                self.assertEqual(after.pool.strings[:len(before.pool.strings)], before.pool.strings)
                self.assertEqual(after.chunks[1], before.chunks[1])
                nodes = after.elements
                self.assertEqual(next(n for n in nodes if n.name == 'application').get('name').value, 'com.example.game.GameApp')
                activity = next(n for n in nodes if n.name == 'activity')
                self.assertEqual(activity.get('name').value, 'com.example.game.Main')
                self.assertEqual(activity.get('parentActivityName').value, 'com.example.game.Parent')
                self.assertEqual(activity.get('label').value, '雪😀 - bazaarBot Mobile')
                self.assertEqual(activity.get('process').value, ':private')
                self.assertEqual(activity.get('taskAffinity').value, NEW_PACKAGE)
                alias = next(n for n in nodes if n.name == 'activity-alias')
                self.assertEqual(alias.get('targetActivity').value, 'com.example.game.Main')
                provider = next(n for n in nodes if n.name == 'provider')
                self.assertEqual(provider.get('name').value, 'com.example.game.RealProvider')
                self.assertEqual(provider.get('authorities').value, NEW_PACKAGE + '.files;' + NEW_PACKAGE + '.shared.authority')
                self.assertEqual(provider.get('readPermission').value, NEW_PACKAGE + '.PRIVATE')
                metadata = next(n for n in nodes if n.name == 'meta-data' and n.get('name').value == 'oauth.original.package')
                self.assertEqual(metadata.get('value').value, 'com.example.game')
                external = next(n for n in nodes if n.name == 'provider' and n.parent.name == 'queries')
                self.assertEqual(external.get('authorities').value, 'external.provider')
                permissions = [n.get('name').value for n in nodes if n.name in ('permission', 'uses-permission')]
                self.assertEqual(permissions, [NEW_PACKAGE + '.PRIVATE', NEW_PACKAGE + '.PRIVATE', 'android.permission.INTERNET'])

    def test_no_options_is_byte_preserving_and_invalid_input_fails_closed(self):
        original = synthetic_manifest()
        self.assertEqual(apk_identity.patch_manifest(original), original)
        for kwargs in ({'package_name': 'bad/package'}, {'version_code': 0}, {'version_code': 1 << 31},
                       {'app_label': ''}, {'version_name': ''}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                apk_identity.patch_manifest(original, **kwargs)
        with self.assertRaises(ValueError):
            apk_identity.patch_manifest(original[:-1], package_name=NEW_PACKAGE)
        with self.assertRaisesRegex(ValueError, 'sharedUserId'):
            apk_identity.patch_manifest(synthetic_manifest(shared_user=True), package_name=NEW_PACKAGE)

    def test_appended_string_pool_keeps_existing_style_data_and_clears_sorted_flag(self):
        raw = synthetic_manifest()
        original = apk_identity.BinaryManifest(raw).pool.original
        pool = bytearray(original)
        count = struct.unpack_from('<I', pool, 8)[0]
        old_start = struct.unpack_from('<I', pool, 20)[0]
        style_data = struct.pack('<IIII', 0, 0, 1, 0xFFFFFFFF)
        styled = bytearray(pool[:old_start] + b'\0\0\0\0' + pool[old_start:] + style_data)
        struct.pack_into('<I', styled, 4, len(styled))
        struct.pack_into('<I', styled, 12, 1)
        struct.pack_into('<III', styled, 16, 1, old_start + 4, len(styled) - len(style_data))
        parsed = apk_identity._StringPool(styled)
        index = parsed.add('new appended label')
        rebuilt = apk_identity._StringPool(parsed.build())
        self.assertEqual(rebuilt.strings[index], 'new appended label')
        self.assertEqual(rebuilt.style_data, style_data)
        self.assertEqual(rebuilt.style_count, 1)
        self.assertFalse(rebuilt.flags & 1)
        self.assertEqual(rebuilt.strings[:count], parsed.strings[:count])

    def test_resource_namespace_patch_preserves_other_packages_ids_and_all_other_bytes(self):
        def package(package_id, name):
            encoded = name.encode('utf-16le')
            return (struct.pack('<HHII', 0x0200, 288, 300, package_id)
                    + encoded + b'\0' * (256 - len(encoded)) + b'\xAD' * 32)
        own = package(0x7F, 'com.example.game')
        external = package(0x80, 'com.example.library')
        source = struct.pack('<HHII', 2, 12, 12 + len(own) + len(external), 2) + own + external
        changed = apk_identity.patch_resource_package(source, 'com.example.game', NEW_PACKAGE)
        self.assertEqual(len(changed), len(source))
        self.assertEqual(changed[:24], source[:24])
        self.assertEqual(changed[280:], source[280:])
        self.assertEqual(changed[24:280].decode('utf-16le').rstrip('\0'), NEW_PACKAGE)
        self.assertEqual(apk_identity.patch_resource_package(source, 'com.example.game', 'com.example.game'), source)
        for invalid in ('', 'a' * 128, 'a' * 126 + '😀', 'embedded\0name'):
            with self.subTest(invalid=repr(invalid)), self.assertRaises(ValueError):
                apk_identity.patch_resource_package(source, 'com.example.game', invalid)
        with self.assertRaisesRegex(ValueError, 'does not declare'):
            apk_identity.patch_resource_package(source, 'com.missing', NEW_PACKAGE)
        with self.assertRaises(ValueError):
            apk_identity.patch_resource_package(source[:-1], 'com.example.game', NEW_PACKAGE)


class OfficialManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.apk = ROOT / 'TheBazaar.apk'
        cls.aapt = ROOT / 'tools/buildtools/android-15/aapt.exe'
        if not cls.apk.exists() or not cls.aapt.exists():
            raise unittest.SkipTest('official APK and Android SDK aapt are required for integration checks')
        with zipfile.ZipFile(cls.apk) as archive:
            cls.original = archive.read('AndroidManifest.xml')
            cls.resources = archive.read('resources.arsc')

    def test_real_resource_label_and_android_parser_accept_patched_manifest(self):
        self.assertEqual(apk_identity.original_app_label(self.apk), 'The Bazaar')
        label = apk_identity.mobile_app_label(self.apk)
        self.assertEqual(label, 'The Bazaar - bazaarBot Mobile')
        before = apk_identity.BinaryManifest(self.original)
        application = next(n for n in before.elements if n.name == 'application')
        self.assertEqual(application.get('label').data_type, apk_identity.TYPE_REFERENCE)
        result = apk_identity.patch_manifest(self.original, package_name=NEW_PACKAGE, app_label=label,
                                             version_name='0.0.1', version_code=1)
        after = apk_identity.BinaryManifest(result)
        allowed = {('manifest', None, 'package'), ('manifest', ANDROID, 'versionCode'),
                   ('manifest', ANDROID, 'versionName'), ('application', ANDROID, 'label'),
                   ('permission', ANDROID, 'name'), ('uses-permission', ANDROID, 'name'),
                   ('provider', ANDROID, 'authorities')}
        self.assertEqual(len(before.elements), len(after.elements))
        for old, new in zip(before.elements, after.elements):
            self.assertEqual(old.name, new.name)
            self.assertEqual(len(old.attributes), len(new.attributes))
            for a, b in zip(old.attributes, new.attributes):
                if (old.name, a.namespace, a.name) not in allowed:
                    self.assertEqual((a.namespace, a.name, a.data_type, a.value), (b.namespace, b.name, b.data_type, b.value))
        providers = [n.get('authorities').value for n in after.elements if n.name == 'provider']
        self.assertEqual(providers, [NEW_PACKAGE + '.androidx-startup', NEW_PACKAGE + '.playgamesinitprovider'])
        resources = apk_identity.patch_resource_package(self.resources, before.package, NEW_PACKAGE)
        with tempfile.TemporaryDirectory() as directory:
            apk = Path(directory) / 'manifest-only.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('AndroidManifest.xml', result)
                archive.writestr('resources.arsc', resources)
            parsed = subprocess.run([str(self.aapt), 'dump', 'badging', str(apk)], capture_output=True,
                                    text=True, encoding='utf8', check=True).stdout
            self.assertIn("package: name='" + NEW_PACKAGE + "' versionCode='1' versionName='0.0.1'", parsed)
            self.assertIn("application-label:'The Bazaar - bazaarBot Mobile'", parsed)
            self.assertIn("launchable-activity: name='com.unity3d.player.UnityPlayerActivity'", parsed)
            self.assertIn("uses-permission: name='" + NEW_PACKAGE + ".DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION'", parsed)
            xmltree = subprocess.run([str(self.aapt), 'dump', 'xmltree', str(apk), 'AndroidManifest.xml'],
                                     capture_output=True, text=True, encoding='utf8', check=True).stdout
            for authority in providers:
                self.assertIn('="' + authority + '"', xmltree)
            self.assertNotIn('="com.tempogames.thebazaar.androidx-startup"', xmltree)
            self.assertNotIn('="com.tempogames.thebazaar.playgamesinitprovider"', xmltree)
            parsed_resources = subprocess.run([str(self.aapt), 'dump', 'resources', str(apk)],
                                              capture_output=True, text=True, encoding='utf8', check=True).stdout
            self.assertIn('Package Group 0 id=0x7f packageCount=1 name=' + NEW_PACKAGE, parsed_resources)
            self.assertIn('Package 0 id=0x7f name=' + NEW_PACKAGE, parsed_resources)
            self.assertIn('spec resource 0x7f0b0006 ' + NEW_PACKAGE + ':string/', parsed_resources)
            self.assertNotIn('com.tempogames.thebazaar:string/', parsed_resources)

    def test_real_resource_package_change_touches_only_fixed_name_field(self):
        result = apk_identity.patch_resource_package(self.resources, 'com.tempogames.thebazaar', NEW_PACKAGE)
        self.assertEqual(len(result), len(self.resources))
        offset = struct.unpack_from('<H', self.resources, 2)[0]
        changed = []
        while offset < len(self.resources):
            kind, header, size = struct.unpack_from('<HHI', self.resources, offset)
            if kind == 0x0200:
                self.assertEqual(struct.unpack_from('<I', self.resources, offset + 8)[0], 0x7F)
                changed.extend(range(offset + 12, offset + 268))
            offset += size
        self.assertEqual(len(changed), 256)
        allowed = set(changed)
        self.assertTrue(all(a == b for i, (a, b) in enumerate(zip(self.resources, result)) if i not in allowed))

    def test_real_manifest_has_no_shared_uid_or_uri_callback_to_rewrite(self):
        manifest = apk_identity.BinaryManifest(self.original)
        self.assertIsNone(manifest.root.get('sharedUserId'))
        self.assertFalse(any(n.name == 'data' for n in manifest.elements))
        self.assertFalse(any(n.name == 'provider' and 'FileProvider' in n.get('name').value for n in manifest.elements))

    def test_repacker_optional_identity_flags_align_resource_namespace_and_preserve_dex(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for folder in ('mobilebpp/tools', 'mobilebpp/web/art', 'mobilebpp/web/theme'):
                (root / folder).mkdir(parents=True)
            source, output = root / 'source.apk', root / 'patched.apk'
            lib, host = root / 'libbpp.so', root / 'libmain.so'
            lib.write_bytes(b'native mod')
            host.write_bytes(b'patched native host')
            with zipfile.ZipFile(source, 'w') as archive:
                archive.writestr('AndroidManifest.xml', self.original)
                archive.writestr('resources.arsc', self.resources)
                archive.writestr('classes.dex', b'original Java namespaces and OAuth configuration')
                archive.writestr('lib/arm64-v8a/libmain.so', b'original native host')
            original_bytes = source.read_bytes()
            args = ['patch_apk.py', '--apk', str(source), '--out', str(output), '--lib', str(lib),
                    '--libmain-patched', str(host), '--package-name', NEW_PACKAGE,
                    '--version-name', '0.0.1', '--version-code', '1']
            with patch.object(sys, 'argv', args), patch.object(patch_apk, '__file__', str(root / 'mobilebpp/tools/patch_apk.py')), contextlib.redirect_stdout(io.StringIO()):
                patch_apk.main()
            self.assertEqual(source.read_bytes(), original_bytes)
            with zipfile.ZipFile(output) as archive:
                manifest = apk_identity.BinaryManifest(archive.read('AndroidManifest.xml'))
                self.assertEqual(manifest.package, NEW_PACKAGE)
                self.assertEqual(manifest.root.get('versionName').value, '0.0.1')
                self.assertEqual(manifest.root.get('versionCode').value, 1)
                self.assertEqual(next(n for n in manifest.elements if n.name == 'application').get('label').value,
                                 'The Bazaar - bazaarBot Mobile')
                self.assertEqual(archive.read('resources.arsc'),
                                 apk_identity.patch_resource_package(self.resources, 'com.tempogames.thebazaar', NEW_PACKAGE))
                self.assertEqual(archive.read('classes.dex'), b'original Java namespaces and OAuth configuration')
                self.assertEqual(archive.read('lib/arm64-v8a/libmain.so'), host.read_bytes())


if __name__ == '__main__':
    unittest.main()
