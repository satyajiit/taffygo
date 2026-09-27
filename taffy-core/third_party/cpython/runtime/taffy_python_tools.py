# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Fixed, bounded Python entrypoints shipped in the interpreter image.

The request chooses one identifier and supplies data.  It never supplies Python
source, a module name, a path or arguments for a process.  Keeping this module
frozen into the utility binary makes that distinction structural: the standard
library descriptor can contribute library code, but it cannot replace this
dispatch table.
"""

import json
import zlib

_MAX_INPUT = 262_144
_MAX_TEXT = 4_000
_MAX_SECTIONS = 64
_MAX_PARAGRAPHS = 256
_MAX_SHEETS = 16
_MAX_COLUMNS = 64
_MAX_ROWS = 1_000
_MAX_CELLS = 32_000


class InputError(ValueError):
    """The declarative request is outside the registered input shape."""


def _object(value, name, keys):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise InputError(name + " has the wrong fields")
    return value


def _text(value, name, allow_empty=False):
    if not isinstance(value, str):
        raise InputError(name + " is not text")
    if ((not allow_empty and not value) or len(value) > _MAX_TEXT or
            any(not _xml_character(ord(char)) for char in value)):
        raise InputError(name + " is outside its text bound")
    return value


def _xml_character(code):
    """Whether one code point is legal in the XML 1.0 parts we emit."""
    return (code in (9, 10, 13) or 0x20 <= code <= 0xD7FF or
            0xE000 <= code <= 0xFFFD or 0x10000 <= code <= 0x10FFFF)


def _xml(value):
    return (value.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;")
            .replace("'", "&apos;"))


def _u16(value):
    return bytes((value & 255, (value >> 8) & 255))


def _u32(value):
    return _u16(value & 65535) + _u16((value >> 16) & 65535)


def _zip(parts):
    """Writes a deterministic stored ZIP without a path or temporary file."""
    body = bytearray()
    directory = bytearray()
    for name, content in parts:
        encoded_name = name.encode("ascii")
        payload = content.encode("utf-8") if isinstance(content, str) else content
        if len(encoded_name) > 255:
            raise InputError("archive member name is too long")
        crc = zlib.crc32(payload) & 0xFFFFFFFF
        offset = len(body)
        common = (_u16(20) + _u16(0) + _u16(0) + _u16(0) + _u16(33) +
                  _u32(crc) + _u32(len(payload)) + _u32(len(payload)) +
                  _u16(len(encoded_name)) + _u16(0))
        body += b"PK\x03\x04" + common + encoded_name + payload
        directory += (b"PK\x01\x02" + _u16(20) + common + _u16(0) + _u16(0) +
                      _u16(0) + _u32(0) + _u32(offset) + encoded_name)
    count = len(parts)
    end = (b"PK\x05\x06" + _u16(0) + _u16(0) + _u16(count) + _u16(count) +
           _u32(len(directory)) + _u32(len(body)) + _u16(0))
    return bytes(body + directory + end)


def _document(request):
    root = _object(request, "request", ("document",))
    document = _object(root["document"], "document", ("title", "sections"))
    title = _text(document["title"], "document.title")
    sections = document["sections"]
    if not isinstance(sections, list) or not sections or len(sections) > _MAX_SECTIONS:
        raise InputError("document.sections is outside its count bound")
    paragraphs = 0
    xml = [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>',
        '<w:p><w:pPr><w:pStyle w:val="Title"/></w:pPr><w:r><w:t>',
        _xml(title), '</w:t></w:r></w:p>',
    ]
    for index, raw in enumerate(sections):
        section = _object(raw, f"section[{index}]", ("heading", "paragraphs"))
        heading = _text(section["heading"], f"section[{index}].heading")
        values = section["paragraphs"]
        if not isinstance(values, list) or not values:
            raise InputError(f"section[{index}].paragraphs is empty")
        paragraphs += len(values)
        if paragraphs > _MAX_PARAGRAPHS:
            raise InputError("document has too many paragraphs")
        xml.extend(('<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>',
                    _xml(heading), '</w:t></w:r></w:p>'))
        for row, value in enumerate(values):
            text = _text(value, f"section[{index}].paragraphs[{row}]", True)
            xml.extend(('<w:p><w:r><w:t xml:space="preserve">', _xml(text),
                        '</w:t></w:r></w:p>'))
    xml.append('<w:sectPr/></w:body></w:document>')
    types = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="xml" ContentType="application/xml"/>'
             '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
             '</Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
            '</Relationships>')
    return _zip((("[Content_Types].xml", types), ("_rels/.rels", rels),
                 ("word/document.xml", "".join(xml))))


def _csv_rows(value, name, columns=None):
    """Reads bounded RFC-4180 rows; newlines and quotes are data, not code."""
    text = _text(value, name, True)
    rows, row, cell, quoted, closed, index = [], [], [], False, False, 0
    while index < len(text):
        char = text[index]
        if quoted:
            if char == '"' and index + 1 < len(text) and text[index + 1] == '"':
                cell.append('"')
                index += 1
            elif char == '"':
                quoted = False
                closed = True
            else:
                cell.append(char)
        elif closed:
            if char == ',':
                row.append("".join(cell)); cell = []; closed = False
            elif char in "\r\n":
                row.append("".join(cell)); cell = []; closed = False
                rows.append(row); row = []
                if char == "\r" and index + 1 < len(text) and text[index + 1] == "\n":
                    index += 1
            else:
                raise InputError(name + " has bytes after a quoted cell")
        elif char == '"' and not cell:
            quoted = True
        elif char == '"':
            raise InputError(name + " has a quote inside an unquoted cell")
        elif char == ',':
            row.append("".join(cell)); cell = []
        elif char in "\r\n":
            row.append("".join(cell)); cell = []
            rows.append(row); row = []
            if char == "\r" and index + 1 < len(text) and text[index + 1] == "\n":
                index += 1
        else:
            cell.append(char)
        index += 1
    if quoted:
        raise InputError(name + " has an unterminated quoted cell")
    if closed or cell or row:
        row.append("".join(cell)); rows.append(row)
    expected = columns if columns is not None else (len(rows[0]) if rows else 0)
    if (len(rows) > _MAX_ROWS or expected > _MAX_COLUMNS or
            any(len(row) != expected for row in rows)):
        raise InputError(name + " is outside its row or column bound")
    return rows


def _csv_text(rows):
    def cell(value):
        return '"' + value.replace('"', '""') + '"'
    return "\r\n".join(",".join(cell(value) for value in row) for row in rows)


def _column_name(index):
    value, result = index + 1, ""
    while value:
        value, remainder = divmod(value - 1, 26)
        result = chr(65 + remainder) + result
    return result


def _sheet_xml(columns, rows):
    xml = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
           '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>']
    for row_index, values in enumerate((columns, *rows), 1):
        xml.append(f'<row r="{row_index}">')
        for column_index, value in enumerate(values):
            ref = _column_name(column_index) + str(row_index)
            xml.extend((f'<c r="{ref}" t="inlineStr"><is><t xml:space="preserve">',
                        _xml(value), '</t></is></c>'))
        xml.append('</row>')
    xml.append('</sheetData></worksheet>')
    return "".join(xml)


def _spreadsheet(request):
    if isinstance(request, dict) and set(request) == {"sheet_name", "csv"}:
        name = _text(request["sheet_name"], "sheet_name")
        if any(char in name for char in "[]:*?/\\") or len(name) > 31:
            raise InputError("sheet_name is outside its name bound")
        rows = _csv_rows(request["csv"], "csv")
        if not rows or not rows[0]:
            raise InputError("csv has no header row")
        columns = [_text(value, "csv.header", True) for value in rows[0]]
        return _spreadsheet({
            "workbook": {"sheets": [{"name": name, "columns": columns}]},
            "rows": {name: _csv_text(rows[1:])},
        })
    root = _object(request, "request", ("workbook", "rows"))
    workbook = _object(root["workbook"], "workbook", ("sheets",))
    sheets, raw_rows = workbook["sheets"], root["rows"]
    if (not isinstance(sheets, list) or not sheets or len(sheets) > _MAX_SHEETS or
            not isinstance(raw_rows, dict)):
        raise InputError("workbook has the wrong sheet set")
    parsed, names, folded_names, cells = [], [], [], 0
    for index, raw in enumerate(sheets):
        sheet = _object(raw, f"sheet[{index}]", ("name", "columns"))
        name = _text(sheet["name"], f"sheet[{index}].name")
        columns = sheet["columns"]
        folded_name = name.casefold()
        if (folded_name in folded_names or any(char in name for char in "[]:*?/\\") or
                len(name) > 31 or
                not isinstance(columns, list) or not columns or len(columns) > _MAX_COLUMNS):
            raise InputError(f"sheet[{index}] is outside its name or column bound")
        columns = [_text(value, f"sheet[{index}].column", True) for value in columns]
        if name not in raw_rows:
            raise InputError(name + " has no rows input")
        rows = _csv_rows(raw_rows[name], "rows." + name, len(columns))
        cells += len(columns) * (len(rows) + 1)
        if cells > _MAX_CELLS:
            raise InputError("workbook has too many cells")
        names.append(name); folded_names.append(folded_name)
        parsed.append((columns, rows))
    if set(raw_rows) != set(names):
        raise InputError("rows names do not equal workbook sheet names")
    workbook_xml = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
                    '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>']
    relations = ['<?xml version="1.0" encoding="UTF-8"?>',
                 '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">']
    overrides = []
    parts = []
    for index, (name, data) in enumerate(zip(names, parsed), 1):
        workbook_xml.append(f'<sheet name="{_xml(name)}" sheetId="{index}" r:id="rId{index}"/>')
        relations.append(f'<Relationship Id="rId{index}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{index}.xml"/>')
        overrides.append(f'<Override PartName="/xl/worksheets/sheet{index}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>')
        parts.append((f"xl/worksheets/sheet{index}.xml", _sheet_xml(*data)))
    workbook_xml.append('</sheets></workbook>'); relations.append('</Relationships>')
    types = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="xml" ContentType="application/xml"/>'
             '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>' +
             "".join(overrides) + '</Types>')
    root_rels = ('<?xml version="1.0" encoding="UTF-8"?>'
                 '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                 '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>'
                 '</Relationships>')
    return _zip((("[Content_Types].xml", types), ("_rels/.rels", root_rels),
                 ("xl/workbook.xml", "".join(workbook_xml)),
                 ("xl/_rels/workbook.xml.rels", "".join(relations)), *parts))


_ENTRYPOINTS = {"document.build": _document, "spreadsheet.build": _spreadsheet}


def execute(entrypoint, payload):
    """Runs exactly one compiled entrypoint over one bounded JSON value."""
    if entrypoint not in _ENTRYPOINTS:
        raise InputError("entrypoint is not registered for Python")
    if not isinstance(payload, bytes) or len(payload) > _MAX_INPUT:
        raise InputError("input is not bounded bytes")
    try:
        request = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise InputError("input is not one UTF-8 JSON document") from error
    return _ENTRYPOINTS[entrypoint](request)
