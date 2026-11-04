# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
One token counter for every NodeToCode benchmark (card 0360), so their numbers compare.

    from tokens import count_all
    count_all(text)  # {'chars_div4': 1234, 'o200k_base': 1100 or None, 'qwen': None, 'anthropic': 1180 or None}

Counters:
- chars_div4: len(text) / 4, rounded up: the plugin's own estimate (N2CTokenEstimationService), always available.
- o200k_base: OpenAI's tokenizer through tiktoken, when the tiktoken package and its o200k_base file are already on this
  machine (TIKTOKEN_CACHE_DIR or tiktoken's default cache). This module never downloads a tokenizer file.
- qwen: the Qwen tokenizer through Hugging Face tokenizers, from a local tokenizer.json named by N2C_QWEN_TOKENIZER
  (for example a Qwen2.5 model folder's tokenizer.json). Never downloaded here.
- anthropic: Anthropic's count_tokens endpoint (POST /v1/messages/count_tokens, which counts without generating), only
  when ANTHROPIC_API_KEY is set (Tests/mcp_probe/env_keys.py loads it from the checkout's .env into a child process's
  environment); the count includes the few tokens of the message wrapper, reported as is. N2C_COUNT_MODEL picks the
  model (default claude-haiku-5-5).

A counter that is not available returns None; callers report it as "not measured", never as zero.
Stdlib only, apart from the optional tiktoken and tokenizers packages.
"""
import json
import math
import os
import urllib.error
import urllib.request

ANTHROPIC_URL = 'https://api.anthropic.com/v1/messages/count_tokens'
ANTHROPIC_VERSION = '2023-06-01'
DEFAULT_COUNT_MODEL = 'claude-haiku-5-5'

_tiktoken_encoding = None
_qwen_tokenizer = None


def chars_div4(text):
    return int(math.ceil(len(text) / 4.0))


def o200k_base(text):
    """tiktoken's o200k_base count, or None when the package or its cached file is missing (nothing is downloaded)"""
    global _tiktoken_encoding
    if _tiktoken_encoding is None:
        try:
            import tiktoken
            import tiktoken.load
        except ImportError:
            return None
        original = tiktoken.load.read_file

        def no_download(blobpath):
            if blobpath.startswith(('http://', 'https://')):
                raise OSError('tokenizer file not cached; this benchmark does not download it')
            return original(blobpath)

        tiktoken.load.read_file = no_download
        try:
            _tiktoken_encoding = tiktoken.get_encoding('o200k_base')
        except Exception:
            _tiktoken_encoding = False
        finally:
            tiktoken.load.read_file = original
    return len(_tiktoken_encoding.encode(text, disallowed_special=())) if _tiktoken_encoding else None


def qwen(text):
    """The Qwen tokenizer's count from N2C_QWEN_TOKENIZER (a local tokenizer.json), or None"""
    global _qwen_tokenizer
    if _qwen_tokenizer is None:
        path = os.environ.get('N2C_QWEN_TOKENIZER', '')
        try:
            from tokenizers import Tokenizer
            _qwen_tokenizer = Tokenizer.from_file(path) if path and os.path.isfile(path) else False
        except Exception:
            _qwen_tokenizer = False
    return len(_qwen_tokenizer.encode(text).ids) if _qwen_tokenizer else None


def anthropic(text, model=None, timeout=60):
    """Anthropic count_tokens for one user message holding text, or None without a key (or on any error)"""
    key = os.environ.get('ANTHROPIC_API_KEY', '')
    if not key:
        return None
    body = json.dumps({'model': model or os.environ.get('N2C_COUNT_MODEL', DEFAULT_COUNT_MODEL),
                       'messages': [{'role': 'user', 'content': text}]}).encode('utf-8')
    request = urllib.request.Request(ANTHROPIC_URL, data=body, method='POST', headers={
        'content-type': 'application/json', 'x-api-key': key, 'anthropic-version': ANTHROPIC_VERSION})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return int(json.loads(response.read().decode('utf-8'))['input_tokens'])
    except (urllib.error.URLError, KeyError, ValueError, OSError):
        return None


COUNTERS = (('chars_div4', chars_div4), ('o200k_base', o200k_base), ('qwen', qwen), ('anthropic', anthropic))


def count_all(text):
    """Every counter's count of text: {name: int or None}"""
    return {name: counter(text) for name, counter in COUNTERS}


if __name__ == '__main__':
    import sys
    data = open(sys.argv[1], encoding='utf-8').read() if len(sys.argv) > 1 else sys.stdin.read()
    print(json.dumps(count_all(data), indent=2))
