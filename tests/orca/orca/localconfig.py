"""The ORC 'local.xml': build one from options, or merge one over a caller's file."""
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from urllib.parse import urlsplit


@dataclass(frozen=True)
class UploadSpec:
    """An <upload> destination. 'bits' selects BITS (and async), 'allow_invalid_uri' emits a
    single 'uri' attribute so the value can carry environment variables ORC expands itself."""
    uri: str
    bits: bool = False
    allow_invalid_uri: bool = False


def upload_attributes(spec: UploadSpec) -> dict[str, str]:
    """The attribute set 'New-OrcLocalConfig' emits, so either tool reads the other's file."""
    attributes = {
        "job": "orc",
        "method": "bits" if spec.bits else "filecopy",
        "mode": "async" if spec.bits else "sync",
        "operation": "move",
    }

    if spec.allow_invalid_uri:
        attributes["uri"] = spec.uri
        return attributes

    parts = urlsplit(spec.uri)
    attributes["server"] = f"{parts.scheme}://{parts.hostname or ''}"
    attributes["path"] = parts.path + (f"?{parts.query}" if parts.query else "")
    return attributes


def build(*, output: str | None = None, temporary: str | None = None,
          public_key: str | None = None, upload: UploadSpec | None = None,
          enable_keys: list[str] | None = None, disable_keys: list[str] | None = None,
          keys: list[str] | None = None) -> str:
    """A '<dfir-orc>' document holding only the elements the options ask for.

    Element order follows 'New-OrcLocalConfig' so a diff between a file orca wrote and one
    PowerShell wrote shows differences in content, not in ordering.
    """
    root = ET.Element("dfir-orc")

    if public_key:
        with open(public_key, "r", encoding="utf-8") as source:
            recipient = ET.SubElement(root, "recipient", {"name": "test", "archive": "*"})
            recipient.text = source.read().strip()

    if upload:
        ET.SubElement(root, "upload", upload_attributes(upload))

    for tag, values in (("enable_key", enable_keys), ("disable_key", disable_keys),
                        ("key", keys)):
        for value in values or []:
            ET.SubElement(root, tag).text = value

    for tag, value in (("output", output), ("temporary", temporary)):
        if value:
            ET.SubElement(root, tag).text = value

    return serialize(root)


def serialize(root: ET.Element) -> str:
    """Indented, no XML declaration: what 'ConvertTo-IndentedXml' produces."""
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="unicode")


def merge(base_xml: str | bytes, override_xml: str) -> str:
    """Merge generated elements over a caller's 'local.xml'. The generated ones win.

    Each generated element replaces EVERY same-named element of the base, so '--upload'
    overrides its '<upload>' and '--enable-key' overrides its whole list, which is what
    'Merge-OrcLocalConfig' does. Anything orca never emits ('<download>', '<log>',
    '<console>') and the root attributes are kept as they are.

    Fail closed on a foreign document: silently merging into one would hand ORC a '/local='
    file it ignores or rejects at run time, and the run would look configured when it is not.
    """
    try:
        base = ET.fromstring(base_xml)
    except ET.ParseError as error:
        raise RuntimeError(f"local configuration is not valid XML: {error}") from error

    if base.tag != "dfir-orc":
        raise RuntimeError(f"unexpected root '<{base.tag}>' in the local configuration, "
                           f"expected '<dfir-orc>'")

    override = ET.fromstring(override_xml)
    for element in list(override):
        for existing in base.findall(element.tag):
            base.remove(existing)
        base.append(element)

    return serialize(base)


def merge_file(base_path: str, override_xml: str) -> str:
    # Read bytes and let the XML parser pick the encoding from the BOM and the declaration:
    # 'Out-File' under PowerShell 5 writes UTF-16LE, so a local.xml left by a PowerShell run
    # is exactly the file a caller is most likely to pass here.
    try:
        with open(base_path, "rb") as source:
            base_xml = source.read()
    except OSError as error:
        raise RuntimeError(f"cannot read the local configuration '{base_path}': {error}") \
            from error

    return merge(base_xml, override_xml)
