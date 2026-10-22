"""Extract the authored native definition so regressions exercise the shipping pages."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
text = args.source.read_text(encoding='utf-8')
match = re.search(r'black_menu_definition\[\]\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', text)
if not match:
    raise SystemExit('could not find the production native menu definition')
replaced = re.search(r'black_menu_replaced\[\]\s*=\s*(\{[^}]+\});', text)
if not replaced:
    raise SystemExit('could not find the production native menu migration fingerprints')
previous = json.loads((Path(__file__).parent / 'fixtures/menu-before-tuning.json').read_text(encoding='utf-8'))
previous_literals = '\n'.join(json.dumps(line) for line in previous.splitlines(keepends=True))
args.output.write_text('static const char k_black_menu_default[] =\n' + match[1] + ';\n'
                      + 'static const uint64_t k_black_menu_replaced[] = ' + replaced[1] + ';\n'
                      + 'static const char k_black_menu_before_tuning[] =\n'
                      + previous_literals
                      + ';\n', encoding='utf-8')
