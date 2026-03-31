# outline.py
import ast, sys

with open(sys.argv[1]) as f:
    tree = ast.parse(f.read())

for node in ast.walk(tree):
    if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
        print(f"Line {node.lineno}: {type(node).__name__} {node.name}")