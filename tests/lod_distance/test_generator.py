"""Synthetic instruction/label fixtures; no retail code or game files."""
import ast
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import generate


def sites():
    tree = ast.parse((ROOT / "scripts/generate.py").read_text(encoding="utf-8"))
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "lod_hooks")
    assignment = next(n for n in function.body if isinstance(n, ast.Assign)
                      and any(isinstance(t, ast.Name) and t.id == "sites" for t in n.targets))
    return ast.literal_eval(assignment.value)


def fixture(name, entries):
    text = "void " + name + "(void)\n{\nloc_" + name[4:] + ": ;\n"
    for label, native, _ in entries:
        text += "loc_" + label + ": ;\n    " + native + "\n    untouched();\n"
    count = 3 if name == "sub_001C8360" else 5 if name == "sub_001C55A0" else 1
    for i in range(count):
        text += "loc_synthetic_return_" + str(i) + ": ;\n    return;\n"
    return text + "}\n"


class LodGeneratorTests(unittest.TestCase):
    def test_all_verified_sites_and_idempotence(self):
        table = sites()
        self.assertEqual(len(table), 6)
        self.assertEqual(sum(map(len, table.values())), 11)
        for name, entries in table.items():
            with self.subTest(name=name):
                original = fixture(name, entries)
                hooked = generate.lod_hooks(name, original)
                self.assertEqual(generate.lod_hooks(name, hooked), hooked)
                self.assertEqual(original.count("untouched();"), hooked.count("untouched();"))
                for _, native, replacement in entries:
                    self.assertNotIn(native, hooked)
                    self.assertEqual(hooked.count(replacement), 1)
                if name in ("sub_001C8360", "sub_001C55A0"):
                    self.assertEqual(hooked.count("const uint32_t black_lod_object"), 1)
                    self.assertEqual(hooked.count("black_lod_trace("), original.count("return;"))

    def test_changed_missing_duplicate_or_moved_boundaries_fail_closed(self):
        for name, entries in sites().items():
            original = fixture(name, entries)
            for label, native, _ in entries:
                marker = "loc_" + label + ": ;\n"
                invalid = [original.replace(marker, "", 1),
                           original.replace(marker, marker + marker, 1),
                           original.replace(native, native.replace("xmm", "changed_xmm"), 1),
                           original.replace(native, native + "\n    " + native, 1),
                           original.replace(native, "", 1).replace("}\n", native + "\n}\n")]
                for body in invalid:
                    with self.subTest(name=name, label=label):
                        with self.assertRaises(ValueError):
                            generate.lod_hooks(name, body)

    def test_selector_entry_and_returns_are_guarded(self):
        for name in ("sub_001C8360", "sub_001C55A0"):
            original = fixture(name, sites()[name])
            for body in [original.replace("return;", "changed_return;", 1),
                         original.replace("loc_" + name[4:] + ": ;", "changed_entry:", 1)]:
                with self.assertRaises(ValueError):
                    generate.lod_hooks(name, body)

    def test_other_functions_are_unchanged(self):
        original = "void other(void) { untouched(); }"
        self.assertEqual(generate.lod_hooks("other", original), original)


if __name__ == "__main__":
    unittest.main()
