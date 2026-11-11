"""Compare the compiled tokenizer against Strata using the actual model pack."""
import json
import pathlib
import random
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'external/moe/tools'))
import strata_tokenizer as st

pack = pathlib.Path(sys.argv[1])
vocab = json.loads((pack / 'vocab.json').read_text(encoding='utf-8'))
tokens = [''] * len(vocab)
for token, index in vocab.items():
    tokens[index] = token
merges = (pack / 'merges.txt').read_text(encoding='utf-8').splitlines()
merges = [x for x in merges if x and not x.startswith('#version:')]
reference = st.Tokenizer(tokens, merges,
    json.loads((pack / 'token_type.json').read_text(encoding='utf-8')))
corpus = ['', '1234567890', '  \r\n\t ', '#include <windows.h>\n',
          'G:/CMakePJ/Test/main.cpp', '\u4f60\u597d\uff0c\u4e16\u754c',
          'e\u0301\u0301 combining', '\x00\x01\x7f',
          '<|im_start|>assistant\n<think>\n\n</think>\n\n',
          '<tool_response>\n\n</tool_response>',
          '<tool_call>\n<function=Bash>\n<parameter=command>\nmkdir -p /g/CMakePJ/Test\n</parameter>\n</function>\n</tool_call>']
rng = random.Random(42)
pieces = ['hello', "we're", '\u4e2d\u6587', '1234', '\r\n', '   ', '\t',
          '###', '(){', 'e\u0301', '\U0001f600', '<tool_call>', '\n\n']
corpus += [''.join(rng.choices(pieces, k=15)) for _ in range(200)]
result = subprocess.run([str(root / 'build/Release/moe-tokenizer-probe.exe'), str(pack)],
    input=''.join(json.dumps(s) + '\n' for s in corpus), text=True,
    encoding='utf-8', capture_output=True, check=True)
actual = [json.loads(line) for line in result.stdout.splitlines()]
assert len(actual) == len(corpus), result.stderr
for text, ids in zip(corpus, actual):
    expected = reference.encode(text, parse_special=True)
    assert ids == expected, (repr(text), ids, expected)
print(f'PASS: {len(corpus)} inputs exactly match Strata token IDs')
