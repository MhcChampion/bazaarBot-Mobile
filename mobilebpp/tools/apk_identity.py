#!/usr/bin/env python3
"""Patch APK identity in binary Android XML without rebuilding resources or Dex."""
import os
import re
import shutil
import struct
import subprocess
import zipfile
from dataclasses import dataclass
from pathlib import Path

ANDROID_NS = 'http://schemas.android.com/apk/res/android'
NO_INDEX = 0xFFFFFFFF
TYPE_REFERENCE = 1
TYPE_STRING = 3
TYPE_INT_DEC = 0x10
LABEL_SUFFIX = ' - bazaarBot Mobile'


def _chunks(data, start=8):
    while start < len(data):
        if start + 8 > len(data):
            raise ValueError('truncated binary XML chunk')
        kind, header, size = struct.unpack_from('<HHI', data, start)
        if header < 8 or size < header or start + size > len(data):
            raise ValueError('invalid binary XML chunk bounds')
        yield kind, header, data[start:start + size]
        start += size


def _length(data, offset, utf8):
    width, high, mask = (1, 0x80, 0x7F) if utf8 else (2, 0x8000, 0x7FFF)
    fmt = '<B' if utf8 else '<H'
    first = struct.unpack_from(fmt, data, offset)[0]
    if first & high:
        second = struct.unpack_from(fmt, data, offset + width)[0]
        return ((first & mask) << (8 * width)) | second, offset + 2 * width
    return first, offset + width


def _encode_length(value, utf8):
    if utf8:
        if value > 0x7FFF:
            raise ValueError('UTF-8 Android string is too long')
        return bytes([0x80 | (value >> 8), value & 0xFF]) if value > 0x7F else bytes([value])
    return struct.pack('<HH', 0x8000 | (value >> 16), value & 0xFFFF) if value > 0x7FFF else struct.pack('<H', value)


class _StringPool:
    def __init__(self, data):
        self.original = data
        _, self.header_size, _ = struct.unpack_from('<HHI', data)
        if self.header_size < 28:
            raise ValueError('invalid Android string pool header')
        count, styles, self.flags, start, style_start = struct.unpack_from('<IIIII', data, 8)
        arrays_end = self.header_size + 4 * (count + styles)
        if not arrays_end <= start <= len(data) or (style_start and not start <= style_start <= len(data)):
            raise ValueError('invalid Android string pool offsets')
        self.offsets = list(struct.unpack_from(f'<{count}I', data, self.header_size))
        self.style_offsets = data[self.header_size + count * 4:arrays_end]
        self.style_count = styles
        self.gap = data[arrays_end:start]
        self.string_data = data[start:style_start or len(data)]
        self.style_data = data[style_start:] if style_start else b''
        self.utf8 = bool(self.flags & 0x100)
        self.strings = []
        for offset in self.offsets:
            size, offset = _length(self.string_data, offset, self.utf8)
            if self.utf8:
                size, offset = _length(self.string_data, offset, True)
            end = offset + size * (1 if self.utf8 else 2)
            terminator = b'\0' if self.utf8 else b'\0\0'
            if self.string_data[end:end + len(terminator)] != terminator:
                raise ValueError('unterminated Android string')
            self.strings.append(self.string_data[offset:end].decode('utf-8' if self.utf8 else 'utf-16le'))
        self.indices = {}
        for i, value in enumerate(self.strings):
            self.indices.setdefault(value, i)
        self.added = bytearray()

    def add(self, value):
        if value in self.indices:
            return self.indices[value]
        index = len(self.strings)
        self.indices[value] = index
        self.strings.append(value)
        self.offsets.append(len(self.string_data) + len(self.added))
        encoded = value.encode('utf-16le')
        self.added += _encode_length(len(encoded) // 2, self.utf8)
        if self.utf8:
            encoded = value.encode('utf-8')
            self.added += _encode_length(len(encoded), True)
        self.added += encoded + (b'\0' if self.utf8 else b'\0\0')
        return index

    def build(self):
        if not self.added:
            return self.original
        strings = self.string_data + self.added
        strings += b'\0' * (-len(strings) % 4)
        start = self.header_size + len(self.offsets) * 4 + len(self.style_offsets) + len(self.gap)
        style_start = start + len(strings) if self.style_data else 0
        size = start + len(strings) + len(self.style_data)
        # New values are appended, so a formerly sorted pool is no longer sorted.
        header = struct.pack('<HHIIIIII', 1, self.header_size, size, len(self.offsets), self.style_count,
                             self.flags & ~1, start, style_start)
        return (header + self.original[28:self.header_size] + struct.pack(f'<{len(self.offsets)}I', *self.offsets)
                + self.style_offsets + self.gap + strings + self.style_data)


@dataclass
class Attribute:
    namespace: str | None
    name: str
    data_type: int
    value: str | int
    offset: int


@dataclass
class Element:
    name: str
    attributes: list
    parent: object
    chunk_index: int

    def get(self, name, namespace=ANDROID_NS):
        return next((a for a in self.attributes if a.name == name and a.namespace == namespace), None)


class BinaryManifest:
    """Small read/write view; unknown chunks and all existing string indices survive."""
    def __init__(self, data):
        if len(data) < 8 or struct.unpack_from('<HHI', data) != (3, 8, len(data)):
            raise ValueError('expected a complete binary AndroidManifest.xml')
        self.chunks = [bytearray(chunk) for _, _, chunk in _chunks(data)]
        pools = [i for i, chunk in enumerate(self.chunks) if struct.unpack_from('<H', chunk)[0] == 1]
        if len(pools) != 1:
            raise ValueError('expected one binary XML string pool')
        self.pool_index = pools[0]
        self.pool = _StringPool(self.chunks[self.pool_index])
        self.elements = []
        stack = []
        string = lambda i: None if i == NO_INDEX else self.pool.strings[i]
        for i, chunk in enumerate(self.chunks):
            kind, header, _ = struct.unpack_from('<HHI', chunk)
            if kind == 0x0102:
                if header != 16 or len(chunk) < 36:
                    raise ValueError('unsupported binary XML element header')
                _, name, attr_start, attr_size, count = struct.unpack_from('<IIHHH', chunk, 16)
                if attr_size < 20 or 16 + attr_start + count * attr_size > len(chunk):
                    raise ValueError('invalid binary XML attribute bounds')
                attributes = []
                for n in range(count):
                    offset = 16 + attr_start + n * attr_size
                    ns, key, _, value_size, _, value_type, value = struct.unpack_from('<IIIHBBI', chunk, offset)
                    if value_size != 8:
                        raise ValueError('unsupported binary XML typed value')
                    attributes.append(Attribute(string(ns), string(key), value_type,
                                                string(value) if value_type == TYPE_STRING else value, offset))
                node = Element(string(name), attributes, stack[-1] if stack else None, i)
                self.elements.append(node)
                stack.append(node)
            elif kind == 0x0103:
                if not stack or len(chunk) < 24 or string(struct.unpack_from('<I', chunk, 20)[0]) != stack[-1].name:
                    raise ValueError('unbalanced binary XML elements')
                stack.pop()
        if stack or not self.elements or self.elements[0].name != 'manifest':
            raise ValueError('invalid manifest element tree')
        self.root = self.elements[0]
        package = self.root.get('package', None)
        if package is None or package.data_type != TYPE_STRING:
            raise ValueError('manifest package must be a literal string')
        self.package = package.value

    def set(self, node, name, value, namespace=ANDROID_NS):
        attribute = node.get(name, namespace)
        if attribute is None:
            raise ValueError(f'manifest {node.name} is missing required attribute {name}')
        chunk = self.chunks[node.chunk_index]
        if isinstance(value, str):
            raw = typed = self.pool.add(value)
            kind = TYPE_STRING
        else:
            raw, typed, kind = NO_INDEX, value, TYPE_INT_DEC
        struct.pack_into('<I', chunk, attribute.offset + 8, raw)
        struct.pack_into('<HBBI', chunk, attribute.offset + 12, 8, 0, kind, typed)
        attribute.data_type, attribute.value = kind, value

    def build(self):
        self.chunks[self.pool_index] = self.pool.build()
        body = b''.join(self.chunks)
        return struct.pack('<HHI', 3, 8, len(body) + 8) + body


def _class_name(value, old_package):
    return old_package + value if value.startswith('.') else old_package + '.' + value if '.' not in value else value


def _identity_name(value, old_package, new_package):
    if value == old_package or value.startswith(old_package + '.'):
        return new_package + value[len(old_package):]
    return new_package + ('' if value.startswith('.') else '.') + value


def patch_manifest(data, *, package_name=None, app_label=None, version_name=None, version_code=None):
    manifest = BinaryManifest(data)
    old_package = manifest.package
    if package_name is not None:
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+', package_name):
            raise ValueError('invalid Android package name')
        if manifest.root.get('sharedUserId') is not None:
            raise ValueError('cannot isolate an APK with sharedUserId')
        permissions, authorities = {}, {}
        for node in manifest.elements:
            name = node.get('name')
            if node.name in ('permission', 'permission-tree', 'permission-group') and name:
                if name.data_type != TYPE_STRING:
                    raise ValueError('custom permission names must be literal strings')
                permissions[name.value] = _identity_name(name.value, old_package, package_name)
            authority = node.get('authorities')
            if node.name == 'provider' and node.parent and node.parent.name == 'application' and authority:
                if authority.data_type != TYPE_STRING:
                    raise ValueError('provider authorities must be literal strings')
                for value in authority.value.split(';'):
                    if not value:
                        raise ValueError('empty provider authority')
                    authorities[value] = _identity_name(value, old_package, package_name)
        for node in manifest.elements:
            for attribute in node.attributes:
                if attribute.namespace != ANDROID_NS or attribute.data_type != TYPE_STRING:
                    continue
                key, value = attribute.name, attribute.value
                replacement = None
                if key == 'name' and node.name in ('application', 'activity', 'activity-alias', 'service', 'receiver', 'provider', 'instrumentation'):
                    replacement = _class_name(value, old_package)
                elif key in ('targetActivity', 'parentActivityName', 'backupAgent', 'appComponentFactory', 'manageSpaceActivity', 'classLoader'):
                    replacement = _class_name(value, old_package)
                elif key in ('permission', 'readPermission', 'writePermission', 'permissionGroup') or (key == 'name' and node.name in ('permission', 'permission-tree', 'permission-group', 'uses-permission', 'uses-permission-sdk-23', 'uses-permission-sdk-m')):
                    replacement = permissions.get(value)
                elif key == 'authorities':
                    replacement = ';'.join(authorities.get(v, v) for v in value.split(';'))
                elif key in ('taskAffinity', 'process', 'targetPackage') and (value == old_package or value.startswith(old_package + '.')):
                    replacement = package_name + value[len(old_package):]
                if replacement is not None and replacement != value:
                    manifest.set(node, key, replacement)
        manifest.set(manifest.root, 'package', package_name, None)
    if app_label is not None:
        if not app_label.strip() or '\0' in app_label:
            raise ValueError('application label must be nonempty text')
        application = next((e for e in manifest.elements if e.name == 'application'), None)
        if application is None:
            raise ValueError('manifest is missing application')
        manifest.set(application, 'label', app_label)
        # An explicit launcher label overrides the application label in Android.
        for node in manifest.elements:
            name = node.get('name')
            if node.name == 'category' and name and name.value in ('android.intent.category.LAUNCHER', 'android.intent.category.LEANBACK_LAUNCHER'):
                component = node.parent.parent if node.parent else None
                if component and component.get('label'):
                    manifest.set(component, 'label', app_label)
    if version_name is not None:
        if not version_name or '\0' in version_name:
            raise ValueError('version name must be nonempty text')
        manifest.set(manifest.root, 'versionName', version_name)
    if version_code is not None:
        if not isinstance(version_code, int) or isinstance(version_code, bool) or not 1 <= version_code <= 0x7FFFFFFF:
            raise ValueError('version code must be a positive 32-bit integer')
        manifest.set(manifest.root, 'versionCode', version_code)
        if manifest.root.get('versionCodeMajor'):
            manifest.set(manifest.root, 'versionCodeMajor', 0)
    return manifest.build()


def original_app_label(apk, *, aapt=None):
    """Resolve the original resource-backed label using the existing Android SDK."""
    with zipfile.ZipFile(apk) as archive:
        manifest = BinaryManifest(archive.read('AndroidManifest.xml'))
    app = next((e for e in manifest.elements if e.name == 'application'), None)
    label = app.get('label') if app else None
    if label and label.data_type == TYPE_STRING:
        return label.value
    if aapt is None:
        configured = Path(os.environ['BPP_BUILDTOOLS']) / 'aapt.exe' if os.environ.get('BPP_BUILDTOOLS') else None
        bundled = Path(__file__).resolve().parents[2] / 'tools/buildtools/android-15/aapt.exe'
        aapt = next((str(p) for p in (configured, bundled) if p and p.is_file()), None) or shutil.which('aapt')
    if not aapt:
        raise ValueError('resource-backed app label requires aapt; set BPP_BUILDTOOLS or pass --app-label')
    # Older Windows aapt uses narrow argv for input paths. A Unicode working
    # directory is supported by Windows; the official APK basename is ASCII.
    apk = Path(apk).resolve()
    result = subprocess.run([str(aapt), 'dump', 'badging', apk.name], cwd=apk.parent,
                            capture_output=True, check=True)
    match = re.search(r"^application-label:'(.*)'\r?$", result.stdout.decode('utf-8'), re.MULTILINE)
    if not match or not match.group(1).strip():
        raise ValueError('aapt could not resolve the original application label')
    return match.group(1)


def mobile_app_label(apk):
    label = original_app_label(apk)
    return label if label.endswith(LABEL_SUFFIX) else label + LABEL_SUFFIX


def patch_resource_package(data, old_package, new_package):
    """Align Resources.getIdentifier(..., context.getPackageName()) with the clone.

    Only ResTable_package.name's fixed 128 UTF-16-unit field changes. Resource
    IDs, type/key pools, values and every chunk offset remain byte-identical.
    """
    if len(data) < 12:
        raise ValueError('truncated Android resource table')
    kind, header, size, declared_packages = struct.unpack_from('<HHII', data)
    if kind != 2 or header < 12 or size != len(data):
        raise ValueError('expected a complete Android resource table')
    if not new_package or '\0' in new_package:
        raise ValueError('resource package name must be nonempty text')
    encoded = new_package.encode('utf-16le')
    if len(encoded) > 254:
        raise ValueError('resource package name exceeds 127 UTF-16 units')
    result = bytearray(data)
    position, package_count, changed = header, 0, 0
    for chunk_kind, chunk_header, chunk in _chunks(data, header):
        if chunk_kind == 0x0200:
            package_count += 1
            if chunk_header < 268:
                raise ValueError('truncated resource package header')
            name_units = struct.unpack_from('<128H', chunk, 12)
            try:
                terminator = name_units.index(0)
            except ValueError as error:
                raise ValueError('unterminated resource package name') from error
            name = bytes(chunk[12:12 + terminator * 2]).decode('utf-16le')
            if name == old_package:
                result[position + 12:position + 268] = encoded + b'\0' * (256 - len(encoded))
                changed += 1
        position += len(chunk)
    if package_count != declared_packages:
        raise ValueError('resource table package count does not match its chunks')
    if not changed:
        raise ValueError(f'resource table does not declare source package {old_package}')
    return bytes(result)
