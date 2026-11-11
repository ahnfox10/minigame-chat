# 끝말잇기 뜻풀이 자료(data/korean_defs.txt)를 만든다. 평소에는 돌릴 필요가 없다 (결과 파일이 저장소에 들어 있다).
#
# 원본: 표준국어대사전 (국립국어원, CC BY-SA 2.0 KR) 을 옮겨 놓은 Hugging Face 자료 hac541309/stdict_kor
#       https://huggingface.co/datasets/hac541309/stdict_kor  (parquet 파일 1개, 약 72.5MB)
# 준비: pip install pyarrow, 위 parquet 파일 내려받기
# 실행: python tools/make_defs.py <parquet 파일> [pyarrow를 --target 으로 설치한 폴더]
#
# 하는 일: data/korean_nouns.txt 에 있는 단어만 골라 "단어<탭>뜻" 한 줄로 적는다.
#   - 뜻이 하나면 ONE_LEN 글자까지, 소리가 같은 말이 여럿이면 많이 쓰일 법한 것 3개를 "① … ② … ③ …" 로 적는다.
#   - 어느 것이 흔한 말인지는 사전에 적혀 있지 않다. 용례 · 속담 · 관용구 · 그림이 달린 항목을 앞에 둔다 (짐작이라 틀릴 수 있다).
#     끝말잇기 시작 단어처럼 꼭 맞아야 하는 것은 PREFER 에 "단어: 사전의 번호"로 적어 맨 앞에 오게 한다.
#   - 프로그램(defOf)이 이분 탐색으로 찾으므로 반드시 단어의 UTF-8 바이트 순서로 정렬한다.
import io, os, re, sys

ONE_LEN, MANY_LEN = 40, 25
PREFER = {'지구': 4, '사과': 5, '시계': 2, '의자': 3, '사자': 11, '배': 2, '하늘': 0, '바다': 0, '기차': 0}
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if len(sys.argv) > 2:
    sys.path.insert(0, sys.argv[2])
import pyarrow.parquet as pq

words = set(io.open(os.path.join(root, 'data', 'korean_nouns.txt'), encoding='utf-8').read().split())
col = pq.read_table(sys.argv[1]).column('text')
head_re = re.compile(r'^어휘: (.*?), 구성 단위: ')
def_re = re.compile(r', 뜻풀이: (.*?)(?:, 용례: |, 범주: |$)', re.S)
ex_re = re.compile(r', 용례: (.*?)(?:, 범주: |$)', re.S)
mark_re = re.compile(r'^(?:\s*(?:「\d+」|\[[ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩ]+\]|【[^】]*】|『[^』]*』|\(\([^)]*\)\)))+\s*')
num_re = re.compile(r'\((\d+)\)$')


def cut(s, n):
    return s if len(s) <= n else s[:n].rstrip() + '…'


found = {}   # 단어 → [(점수, 뜻), ...]
for i in range(len(col)):
    t = col[i].as_py()
    m = head_re.match(t)
    if not m or '「명사」' not in t:
        continue
    head = m.group(1)
    n = num_re.search(head)
    no = int(n.group(1)) if n else 0
    w = num_re.sub('', head).replace('-', '').replace('^', '').replace(' ', '')
    if w not in words:
        continue
    d = def_re.search(t)
    if not d:
        continue
    first = d.group(1).split(chr(10))[0]   # 뜻이 여러 개면 줄바꿈으로 나뉘어 있다. 첫 번째만 쓴다
    first = mark_re.sub('', first)
    first = re.sub(r'\s+', ' ', first.replace('\t', ' ')).strip()
    if not first or first.startswith('→') or '의 어근.' in first:
        continue
    e = ex_re.search(t)
    score = ((len(e.group(1)) if e else 0) * 4 + 600 * (', 속담: ' in t) + 600 * (', 관용구: ' in t)
             + 500 * (', 멀티미디어: ' in t) - no)
    if PREFER.get(w, -1) == no:
        score += 100000
    found.setdefault(w, []).append((score, first))

lines = []
many = 0
for w, lst in found.items():
    lst.sort(key=lambda x: -x[0])
    if len(lst) == 1:
        d = cut(lst[0][1], ONE_LEN)
    else:
        many += 1
        d = ' '.join('①②③'[k] + ' ' + cut(x[1], MANY_LEN) for k, x in enumerate(lst[:3]))
    lines.append((w.encode('utf-8'), d.encode('utf-8')))
lines.sort(key=lambda x: x[0])
out = os.path.join(root, 'data', 'korean_defs.txt')
with open(out, 'wb') as f:
    for w, d in lines:
        f.write(w + b'\t' + d + b'\n')
print('words in list:', len(words), ' with meaning:', len(lines), ' (several meanings:', many, ') file bytes:', os.path.getsize(out))
