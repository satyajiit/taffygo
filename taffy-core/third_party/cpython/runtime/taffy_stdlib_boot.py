# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Serves the standard library out of a memory-mapped archive, with no path.

This is the bootstrap decision 0046 names. The worker never learns where the
library is: the browser opens the verified artifact and hands the sandboxed
process a descriptor, and everything below reads the mapping over that
descriptor. `sys.path` stays empty for the life of the process, which is the
only way to be sure nothing here fell back to a filesystem that is not there.

Written against what a frozen module may assume: builtins, and the modules
CPython freezes into every interpreter. It imports nothing from the archive it
is about to make importable.
"""

import marshal
import sys
import zlib

_EOCD_SIGNATURE = b"PK\x05\x06"
_CENTRAL_SIGNATURE = b"PK\x01\x02"
#: A zip end-of-central-directory record is 22 bytes plus a comment of at most
#: 65535. Scanning further back than that is scanning member data.
_EOCD_SEARCH_LIMIT = 22 + 65535
_STORED = 0
_DEFLATED = 8
_MAX_MEMBERS = 20_000
_MAX_MEMBER_BYTES = 16 * 1024 * 1024
#: A byte-code file is a 16-byte header and then a marshalled code object. The
#: header's first four bytes are the interpreter's magic number, and a mismatch
#: means the archive was built for a different interpreter than the one that
#: shipped -- which is a refusal, not a recompile: there is nowhere to write a
#: cache and no reason to trust a library the installer did not expect.
_PYC_HEADER_BYTES = 16


def _u16(view, offset):
    if offset < 0 or offset + 2 > len(view):
        raise ArchiveError("truncated 16-bit archive field")
    return view[offset] | (view[offset + 1] << 8)


def _u32(view, offset):
    if offset < 0 or offset + 4 > len(view):
        raise ArchiveError("truncated 32-bit archive field")
    return (
        view[offset]
        | (view[offset + 1] << 8)
        | (view[offset + 2] << 16)
        | (view[offset + 3] << 24)
    )


class ArchiveError(Exception):
    """The mapping is not an archive this loader can serve."""


class Archive:
    """One zip archive, read through a buffer and never through a path."""

    def __init__(self, buffer):
        self._view = memoryview(buffer)
        self._members = {}
        self._read_directory()

    def _read_directory(self):
        view = self._view
        size = len(view)
        limit = min(size, _EOCD_SEARCH_LIMIT)
        start = -1
        for back in range(22, limit + 1):
            at = size - back
            if bytes(view[at:at + 4]) == _EOCD_SIGNATURE:
                start = at
                break
        if start < 0:
            raise ArchiveError("no end-of-central-directory record")

        if start + 22 > size or _u16(view, start + 20) != size - start - 22:
            raise ArchiveError("end-of-central-directory length does not match")
        if _u16(view, start + 4) or _u16(view, start + 6):
            raise ArchiveError("multi-disk archives are not supported")
        count = _u16(view, start + 10)
        if count != _u16(view, start + 8) or count > _MAX_MEMBERS:
            raise ArchiveError("central-directory member count is invalid")
        directory = _u32(view, start + 16)
        directory_size = _u32(view, start + 12)
        if directory + directory_size != start:
            raise ArchiveError("central-directory bounds do not match")
        at = directory
        for _ in range(count):
            if at + 46 > start:
                raise ArchiveError("truncated central-directory entry")
            if bytes(view[at:at + 4]) != _CENTRAL_SIGNATURE:
                raise ArchiveError("central directory entry is not one")
            flags = _u16(view, at + 8)
            method = _u16(view, at + 10)
            compressed = _u32(view, at + 20)
            uncompressed = _u32(view, at + 24)
            name_length = _u16(view, at + 28)
            extra_length = _u16(view, at + 30)
            comment_length = _u16(view, at + 32)
            offset = _u32(view, at + 42)
            end = at + 46 + name_length + extra_length + comment_length
            if end > start or uncompressed > _MAX_MEMBER_BYTES:
                raise ArchiveError("central-directory entry exceeds its bounds")
            try:
                name = bytes(view[at + 46:at + 46 + name_length]).decode("utf-8")
            except UnicodeDecodeError as error:
                raise ArchiveError("member name is not UTF-8") from error
            if (not name or name.startswith(("/", "\\")) or "\\" in name or
                    any(part in ("", ".", "..") for part in name.split("/"))):
                raise ArchiveError("member name is not a relative archive name")
            if name in self._members:
                raise ArchiveError("archive has a duplicate member")
            if flags & 1 or method not in (_STORED, _DEFLATED):
                raise ArchiveError("member encryption or compression is unsupported")
            self._members[name] = (method, offset, compressed, uncompressed)
            at = end
        if at != start:
            raise ArchiveError("central-directory size does not match its entries")

    def __contains__(self, name):
        return name in self._members

    def names(self):
        return self._members.keys()

    def read(self, name):
        """The member's bytes, decompressed. Raises KeyError if it is absent."""
        method, offset, compressed, uncompressed = self._members[name]
        view = self._view
        # The local header repeats the name and carries its own extra field,
        # which is usually a different length from the central one.
        if offset + 30 > len(view) or bytes(view[offset:offset + 4]) != b"PK\x03\x04":
            raise ArchiveError(f"{name}: local header is missing")
        if _u16(view, offset + 8) != method:
            raise ArchiveError(f"{name}: local compression method differs")
        name_length = _u16(view, offset + 26)
        extra_length = _u16(view, offset + 28)
        at = offset + 30 + name_length + extra_length
        if at + compressed > len(view):
            raise ArchiveError(f"{name}: compressed bytes exceed the archive")
        local_name = bytes(view[offset + 30:offset + 30 + name_length])
        if local_name != name.encode("utf-8"):
            raise ArchiveError(f"{name}: local member name differs")
        raw = bytes(view[at:at + compressed])
        if method == _STORED:
            data = raw
        elif method == _DEFLATED:
            data = zlib.decompress(raw, -15)
        else:
            raise ArchiveError(f"{name}: compression method {method}")
        if len(data) != uncompressed:
            raise ArchiveError(f"{name}: {len(data)} bytes, expected {uncompressed}")
        return data


class Loader:
    """Executes one module out of the archive."""

    def __init__(self, archive, name, member, is_package):
        self._archive = archive
        self._name = name
        self._member = member
        self._is_package = is_package

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        exec(self._code(), module.__dict__)

    def _code(self):
        payload = self._archive.read(self._member)
        if not self._member.endswith("c"):
            return compile(payload, f"<taffy:{self._member}>", "exec", dont_inherit=True)
        magic = payload[:4]
        if magic != _PYC_MAGIC:
            raise ArchiveError(
                f"{self._member}: built for magic {magic.hex()}, "
                f"this interpreter is {_PYC_MAGIC.hex()}"
            )
        return marshal.loads(payload[_PYC_HEADER_BYTES:])

    def is_package(self, fullname):
        return self._is_package

    def get_source(self, fullname):
        if self._member.endswith("c"):
            return None
        return self._archive.read(self._member).decode("utf-8")

    def get_code(self, fullname):
        return self._code()

    def get_data(self, path):
        return self._archive.read(path)


class Finder:
    """A meta-path finder that answers only out of the archive."""

    def __init__(self, archive):
        self._archive = archive

    def find_spec(self, fullname, path=None, target=None):
        stem = fullname.replace(".", "/")
        # Byte code first: an archive that carries both is one being migrated,
        # and the compiled member is the one the shipped interpreter matches.
        for suffix, is_package in (
            ("/__init__.pyc", True),
            ("/__init__.py", True),
            (".pyc", False),
            (".py", False),
        ):
            member = stem + suffix
            if member in self._archive:
                break
        else:
            return None
        loader = Loader(self._archive, fullname, member, is_package)
        spec = _spec_from_loader(fullname, loader, is_package)
        spec.origin = f"taffy-archive:{member}"
        return spec

    def invalidate_caches(self):
        pass


def _spec_from_loader(fullname, loader, is_package):
    # importlib is in the archive this finder has not made importable yet, so
    # the spec type comes from the frozen copy every interpreter already has.
    bootstrap = sys.modules["_frozen_importlib"]
    spec = bootstrap.ModuleSpec(fullname, loader, is_package=is_package)
    if is_package:
        spec.submodule_search_locations = []
    return spec


#: The magic of the interpreter this bootstrap is frozen into. Taken from the
#: frozen copy of importlib rather than from `importlib.util`, which is in the
#: archive this module has not made importable yet.
_PYC_MAGIC = sys.modules["_frozen_importlib_external"].MAGIC_NUMBER


def install(buffer):
    """Makes the archive importable and answers how many members it holds."""
    archive = Archive(buffer)
    sys.meta_path.insert(0, Finder(archive))
    return len(list(archive.names()))
