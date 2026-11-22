"""Run a grammar's corpus through a parse driver and compare with the expected trees.

usage: corpus.py <corpus dir> <driver> <work dir>

Reads the corpus the way tree-sitter does, including header suffixes
(`===|||` ... `---|||`). AsciiDoc inputs contain lines that look like corpus
syntax (`====` example blocks, `----` listing blocks), so a header is only
accepted once the previous case has reached its expected tree, and a case's
divider is its last dashed line that is followed by an s-expression.
Field names are ignored, as `tree-sitter test` ignores them when the expected
tree leaves them out.
"""
import pathlib
import re
import subprocess
import sys

corpus, driver, work = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
work.mkdir(parents=True, exist_ok=True)
HEADER = re.compile(r'^(={3,})(\S*)\n(.*?)\n\1\2\n', re.M)


def dividers(body, suffix):
    pattern = r'^-{3,}' + re.escape(suffix) + r'[ \t]*$'
    return [d for d in re.finditer(pattern, body, re.M)
            if body[d.end():].lstrip().startswith('(')]


def normalise(tree):
    tree = re.sub(r'\w+: ', '', tree)
    tree = re.sub(r'\s*\)', ')', tree)
    return re.sub(r'\s+', ' ', tree).strip()


cases = []
for path in sorted(corpus.glob('*.txt')):
    text = path.read_text()
    headers = []
    for match in HEADER.finditer(text):
        if not headers or dividers(text[headers[-1].end():match.start()], headers[-1].group(2)):
            headers.append(match)
    for k, header in enumerate(headers):
        end = headers[k + 1].start() if k + 1 < len(headers) else len(text)
        body = text[header.end():end]
        divider = dividers(body, header.group(2))[-1]
        source = body[:divider.start()].removesuffix('\n')
        cases.append((path.name, header.group(3).strip(), source, body[divider.end():]))

files = []
for n, case in enumerate(cases):
    source_file = work / f'{n:03d}.adoc'
    source_file.write_text(case[2] + '\n')
    files.append(str(source_file))

result = subprocess.run([driver, *files], capture_output=True, text=True)
lines = result.stdout.splitlines()
if result.returncode != 0 or len(lines) != len(cases):
    print(result.stdout.strip() or result.stderr.strip() or 'driver failed')
    sys.exit(1)

failed = [case for case, line in zip(cases, lines)
          if normalise(line.split('\t', 1)[1]) != normalise(case[3])]
print(f'{len(cases)} cases, {len(cases) - len(failed)} match, {len(failed)} differ')
for case in failed:
    print(f'  {case[0]}: {case[1]}')
sys.exit(1 if failed else 0)
