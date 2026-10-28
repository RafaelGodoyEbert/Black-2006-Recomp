"""Guard the two authored drag boundaries without retail code."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import generate


class PhysicsGeneratorTests(unittest.TestCase):
    source = ("xmm1.f[0] = xmm1.f[0] - MEMF(l_eax + 0x24);\n"
              "xmm0.f[0] = xmm0.f[0] - MEMF(l_eax + 0x20);\n"
              "untouched();\n")

    def test_both_drag_inputs_only(self):
        output = generate.title_hooks("sub_00077910", self.source)
        self.assertEqual(output.count("black_physics_drag("), 2)
        self.assertEqual(output.replace("black_physics_drag(MEMF(l_eax + 0x24))", "MEMF(l_eax + 0x24)")
                         .replace("black_physics_drag(MEMF(l_eax + 0x20))", "MEMF(l_eax + 0x20)"), self.source)

    def test_native_boundaries_are_required(self):
        for offset in ("0x24", "0x20"):
            with self.assertRaises(ValueError):
                generate.title_hooks("sub_00077910", self.source.replace(offset, "0x28"))
        with self.assertRaises(ValueError):
            generate.title_hooks("sub_00077910", self.source + self.source)

    def test_unrelated_function_unchanged(self):
        self.assertEqual(generate.title_hooks("unrelated", self.source), self.source)


if __name__ == "__main__":
    unittest.main()
