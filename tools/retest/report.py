"""Offline HTML views of recorded evidence; never runs game or case code."""
from html import escape
import json
from pathlib import Path
from urllib.parse import quote


STYLE = """:root{color-scheme:light;font-family:Segoe UI,Microsoft YaHei,sans-serif;color:#203047;background:#f3f5f8}
*{box-sizing:border-box}body{margin:0}main{max-width:1160px;margin:auto;padding:36px 24px 70px}
h1{font-size:30px;margin:8px 0 14px}h2{font-size:19px;margin:0 0 16px}h3{font-size:15px;margin:0 0 10px}
p{line-height:1.7}a{color:#2356a8}small,.muted{color:#66758a}.eyebrow{letter-spacing:2px;font-size:12px;color:#526783}
.panel{background:white;border:1px solid #dfe5ee;border-radius:12px;padding:22px;margin:18px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}.grid .panel{margin:0;min-width:0}
.stats{grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}
.pill{display:inline-block;padding:4px 10px;border-radius:20px;font-size:12px;background:#eaf0f8;color:#3c5471}
.Passed{background:#e4f4eb;color:#166443}.Failed,.Error{background:#fff0e9;color:#9d3d22}
.evidence{border-left:4px solid #56799d}.evidence.warning{border-left-color:#ca762d}
.event{border-left:2px solid #d8e1ed;padding:0 0 18px 16px;margin-left:5px}.event:last-child{padding-bottom:0}
.event .pill{margin-right:8px}code{font-family:Consolas,monospace;font-size:12px;overflow-wrap:anywhere}
pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f5f7fa;padding:14px;border-radius:8px;font-size:12px}
table{border-collapse:collapse;width:100%;font-size:13px}td,th{text-align:left;vertical-align:top;border-bottom:1px solid #e7ecf3;padding:10px 7px;overflow-wrap:anywhere}
th{color:#64748b;font-weight:500}.scroll{overflow-x:auto}details{margin-top:12px}summary{cursor:pointer;color:#456184;font-size:13px}
dl{margin:0}dt{color:#718096;font-size:12px;margin-top:10px}dd{margin:3px 0 0;overflow-wrap:anywhere}ul{padding-left:22px;line-height:1.8}
@media(max-width:600px){main{padding:20px 14px}h1{font-size:24px}.panel{padding:16px}table{min-width:560px}}
"""


def text(value):
    return escape(str(value), quote=True)


def document(title, body):
    return ('<!doctype html><html lang="zh-CN"><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; style-src \'unsafe-inline\'">'
            f'<title>{text(title)}</title><style>{STYLE}</style><main>{body}</main></html>')
