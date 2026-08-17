#!/usr/bin/env python3
"""블로그 글의 문체 습관을 재는 도구.

산문 1만 자당 빈도로 재고, **상한만 본다.** 적은 쪽은 문제가 아니다.
자세한 근거와 고치는 법은 docs/reference/writing-style.md 를 본다.

한도를 넘으면 "몇 개까지 남기면 되는지"를 함께 찍는다. "줄여라"만으로는 얼마나가 안 잡혀서,
너무 깎았다가 되살리는 왕복이 실제로 두 번 있었다.

쓰는 법:
    python tools/check-writing-style.py docs/blog/08_*.md
    python tools/check-writing-style.py ~/Documents/explanations/blog-0*.html
    python tools/check-writing-style.py --all-blog          # docs/blog/*.md 전부

종료 코드: 기준을 벗어난 항목이 있으면 1. CI 에 걸고 싶으면 이걸 쓴다.
"""
import glob
import io
import os
import re
import sys

# 산문 1만 자당 허용 상한.
#
# ⚠️ 발행본 실측치가 **아니다.** 발행본(machamy.tistory.com/28~/33)을 이 스크립트와 같은
# 방식으로 재면 em dash 8.3 / `A가 아니라 B` 6.8 이 나온다. 그 글들도 손질을 거쳤지만
# 구문이 꽤 남아 있어서, 그 밀도를 따라가면 덜어낼 이유가 없어진다.
# 그래서 상한은 **발행본보다 낮게** 잡는다. 저자가 정한 값이다.
#
# **하한은 두지 않는다.** 0이어도 통과다. 예전에 하한을 뒀더니 "줄여라 → 너무 깎았다 →
# 되살려라"를 두 바퀴 돌았다. 적은 쪽은 문제가 아니라는 것이 이 프로젝트의 결론이다.
#
# 근거와 고치는 법은 docs/reference/writing-style.md 를 본다.
LIMIT = {
    'em dash —':      2.0,
    'A가 아니라 B':    2.4,
    '~가 아니다':      0.4,
    '정확히':          0.5,
    '~인 셈이다':      0.7,
    '바로 그/이':      0.2,
    '~일 뿐이다':      0.5,
}

PATS = {
    'em dash —':     r'—',
    'A가 아니라 B':   r'(?:이|가|은|는|을|를)\s*아니라',
    '~가 아니다':     r'\S+(?:이|가)\s*아니다',
    '정확히':         r'정확히\s',
    '~인 셈이다':     r'셈이다',
    '바로 그/이':     r'바로\s*(?:그|이)',
    '~일 뿐이다':     r'뿐이다',
}




def prose_of(path):
    """자기가 쓴 산문만 남긴다.

    빼는 것:
      - 코드 블록과 인라인 코드 (그건 코드다)
      - mermaid 블록 (다이어그램 라벨의 부호는 문체가 아니다)
      - **인용문** (남의 말이거나 앞 글에서 그대로 옮긴 문장이다)
      - HTML 태그

    인용문을 빼는 이유가 있다. 한 번은 이 검사를 통과시키려고 07편이 06편에서 옮겨 온
    인용문의 문구를 고쳤는데, 그러면 **발행된 원문과 다른 말을 인용하게 된다.**
    인용은 원문 그대로여야 하므로 측정 대상에서 뺀다.
    """
    s = io.open(path, encoding='utf-8').read()
    if path.lower().endswith(('.html', '.htm')):
        s = re.sub(r'<pre.*?</pre>', ' ', s, flags=re.S)
        s = re.sub(r'<code.*?</code>', ' ', s, flags=re.S)
        s = re.sub(r'<blockquote.*?</blockquote>', ' ', s, flags=re.S)
        s = re.sub(r'<[^>]+>', ' ', s)
    else:  # 마크다운
        s = re.sub(r'^```.*?^```', ' ', s, flags=re.S | re.M)
        s = re.sub(r'`[^`]*`', ' ', s)
        s = re.sub(r'^\s*>.*$', ' ', s, flags=re.M)
    return s


def main(paths):
    bad = 0
    for path in paths:
        if not os.path.exists(path):
            print('%s  — 없음' % path)
            continue
        t = prose_of(path)
        n = len(t)
        if n < 500:
            print('%s  — 산문이 너무 짧아 건너뜀(%d자)' % (os.path.basename(path), n))
            continue

        notes = []
        for name, pat in PATS.items():
            c = len(re.findall(pat, t))
            rate = c / n * 10000
            limit = LIMIT[name]
            if rate > limit:
                # 몇 개를 덜어내야 하는지까지 알려준다. "줄여라"만으로는 얼마나가 안 잡힌다.
                keep = int(limit * n / 10000)
                notes.append('%s %d회(%.1f, 한도 %.1f → %d개까지)'
                             % (name, c, rate, limit, keep))
        bad += len(notes)
        print('%-34s 산문 %6d자  %s'
              % (os.path.basename(path), n, ' | '.join(notes) if notes else '기준 이내'))

    print('\n기준을 벗어난 항목: %d' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args or args[0] == '--all-blog':
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        args = sorted(glob.glob(os.path.join(root, 'docs', 'blog', '*.md')))
    sys.exit(main(args))
