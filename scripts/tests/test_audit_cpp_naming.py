# Copyright byteyang. All Rights Reserved.
"""基础协议保持既有拼写，Capability 字段仍执行命名检查。"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_cpp_naming import check_protocol


def test_transport_fields_are_scoped_to_wire_code():
    code = '''
    Obj->SetStringField(TEXT("server"), TEXT("Nexus-Unreal"));
    Obj->SetBoolField(TEXT("authRequired"), true);
    Obj->SetBoolField(TEXT("hasPlayWorld"), false);
    Obj->SetBoolField(TEXT("ok"), true);
    Obj->TryGetStringField(TEXT("token"), Presented);
    Obj->SetNumberField(TEXT("mcpPort"), 45000);
    '''
    assert check_protocol(code, mcp=True) == []
    assert check_protocol(code, mcp=False)
    assert check_protocol('Obj->SetBoolField(TEXT("enabled"), true);', mcp=True)


def test_capability_boolean_fields_still_require_prefix():
    assert check_protocol('Obj->SetBoolField(TEXT("Enabled"), true);')
    assert check_protocol('Obj->SetBoolField(TEXT("bEnabled"), true);') == []


def test_schema_keywords_used_as_business_names_are_checked():
    code = '''
    .Prop(TEXT("type"), FNexusSchema::Str(TEXT("Asset kind")))
    .Required(TEXT("description"), FNexusSchema::Str(TEXT("Summary")))
    .Required({TEXT("properties")})
    Args.Str(TEXT("type"));
    '''
    for wire in (False, True):
        messages = [message for _, message in check_protocol(code, mcp=wire)]
        for name in ("type", "description", "properties"):
            assert any(f"协议名 {name} " in message for message in messages)
    assert check_protocol('Schema->SetStringField(TEXT("type"), TEXT("object"));') == []
    assert check_protocol('.Prop(TEXT("Type"), FNexusSchema::Str(TEXT("Asset kind")))') == []
