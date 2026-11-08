"""Replay only literal text replacements from the pinned upstream libxml recipe."""
import ast
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/full-source/DesktopEditors/core/Common/3dParty/libxml'

def main():
    tree = ast.parse((BASE / 'fetch.py').read_text())
    constants = {}
    changes = []
    def literal(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.Name) and node.id in constants:
            return constants[node.id]
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            return literal(node.left) + literal(node.right)
        raise RuntimeError('Non-literal upstream patch expression')
    body = next(node.body for node in tree.body if isinstance(node, ast.If))
    for node in body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            constants[node.targets[0].id] = literal(node.value)
        elif isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
            call = node.value
            if not isinstance(call.func, ast.Attribute) or call.func.attr not in ['replaceInFile', 'replaceInFileUtf8']:
                continue
            relative, old, new = map(literal, call.args)
            path = BASE / relative
            if not path.resolve().is_relative_to((BASE / 'libxml2').resolve()):
                raise RuntimeError('Patch target escaped libxml checkout')
            text = path.read_text(encoding='utf-8')
            if new not in text:
                if text.count(old) != 1:
                    raise RuntimeError('Unexpected patch context: ' + relative)
                path.write_text(text.replace(old, new), encoding='utf-8')
            changes.append({'path': relative, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    if len(changes) != 4:
        raise RuntimeError('Unexpected upstream patch count')
    (ROOT / 'evidence/full-source-libxml-patches.json').write_text(json.dumps(changes, indent=2) + '\n')
    print('LIBXML_UPSTREAM_PATCHES_APPLIED')

if __name__ == '__main__':
    main()
