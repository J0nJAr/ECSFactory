# -*- coding: utf-8 -*-
"""티스토리 스킨을 로컬에서 렌더한다.

왜 필요한가 — 티스토리는 스킨을 덮어쓰면 되돌릴 수단이 없다. 관리 화면에 판본 기록이
남지 않는다. 그래서 붙여넣기 전에 여기서 확인한다. 32종 프리셋을 실물에 올려 보고
고치는 방식은 되돌릴 수 없는 실수를 부른다.

무엇을 하는가 — `<s_xxx_rep>` 반복 블록을 N번 펼치고, 그 페이지에 없는 블록을 지우고,
`[##_치환자_##]` 를 표본 내용으로 바꾼다. 티스토리 엔진의 흉내일 뿐 정본이 아니다.
레이아웃 검증용이지 기능 검증용이 아니다.
"""
import io, re, sys, os

# 이 파일은 `blog/skin/tools/` 에 있다. 세 단계 위가 저장소 루트다.
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    '..', '..', '..'))
SKIN = os.path.join(ROOT, 'blog', 'skin', 'skin.html')
CSS  = os.path.join(ROOT, 'blog', 'skin', 'style.css')

# 실제 블로그 글 구성을 닮게 잡는다 — 백준 풀이가 제일 많고, ECS 시리즈와 유니티가 섞인다.
POSTS = [
    ('C++ ECS 게임 만들기 9. 결정론이 깨지는 자리',
     'tick 해시가 어긋나는 순간을 어떻게 잡아내는지, 그리고 재동기화가 무엇을 되돌리는지 적었다.',
     'ECS 엔진', '2026.08.16', '9'),
    ('C++ ECS 게임 만들기 8. 입력을 중계한다',
     '상태가 아니라 입력을 보내면 무엇이 달라지는가. lockstep 루프를 처음부터 따라간다.',
     'ECS 엔진', '2026.08.09', '4'),
    ('[BOJ 17272] 리그 오브 레전설 (Large)',
     '수열의 주기를 찾아 나머지로 접는 문제. 나이브하게 풀면 시간이 터진다.',
     '알고리즘', '2026.08.02', '0'),
    ('유니티 어드레서블 처음 붙일 때 걸린 것들',
     '라벨과 그룹을 잘못 나눠서 빌드가 두 배로 커졌던 이야기.',
     '유니티', '2026.07.28', '2'),
    ('[BOJ 1197] 최소 스패닝 트리',
     '크루스칼과 프림 중 무엇을 고를지는 간선 밀도가 정한다.',
     '알고리즘', '2026.07.21', '1'),
    ('요즘 공부 방식을 바꿨다',
     '만들면서 배우는 쪽으로 옮긴 뒤 무엇이 남았는지.',
     '일상', '2026.07.14', '3'),
]

BODY = """
<h2>문제부터</h2>
<p>서버와 클라이언트가 같은 입력을 받아 같은 tick 을 돌렸는데 결과가 갈렸다.
<b>같은 코드</b>가 같은 값을 넣고도 다른 상태로 끝난다면, 원인은 셋 중 하나다.</p>
<ul><li>부동소수점이 시뮬레이션 경로에 남아 있다</li>
<li>순회 순서가 프로세스마다 다르다</li>
<li>입력이 서로 다른 tick 에 들어갔다</li></ul>
<h2>순회 순서를 의심한 이유</h2>
<p>먼저 확인할 것은 <code>Fixed64</code> 밖에서 실수 연산이 섞였는지다.
그런데 <code>grep</code> 으로 훑어도 나오지 않았다. 남은 것은 순서였다.</p>
<blockquote>해시는 상태를 요약한 값이다. 상태가 같아도 순서가 다르면 다른 값이 나온다.</blockquote>
<pre><code>uint64_t World::ComputeHash() const {
    uint64_t h = 1469598103934665603ull;
    for (const auto&amp; pool : m_registry.Pools()) {   // 이 순서가 문제였다
        if (!pool || !pool-&gt;IsTrivial()) continue;
        for (uint8_t b : pool-&gt;DenseBytes()) { h ^= b; h *= 1099511628211ull; }
    }
    return h;
}</code></pre>
<h2>고친 방향</h2>
<ol><li>풀을 타입 인덱스가 아니라 타입 해시로 정렬해서 순회한다</li>
<li>정렬 비용은 tick 당 한 번이라 무시할 수 있다</li>
<li>테스트에 등록 순서를 일부러 뒤집은 케이스를 넣는다</li></ol>
<table><thead><tr><th>기준</th><th>바뀌기 전</th><th>바뀐 뒤</th></tr></thead>
<tbody><tr><td>순회 키</td><td>타입 인덱스</td><td>타입 해시</td></tr>
<tr><td>프로세스 간 안정성</td><td>없음</td><td>있음</td></tr>
<tr><td>tick 당 비용</td><td>0</td><td>정렬 1회</td></tr></tbody></table>
<h2>남은 일</h2>
<p>스폰과 디스폰이 아직 확정 입력 밖에서 일어난다. 그 경로를 닫기 전에는
해시가 맞아도 안심할 수 없다.</p>
"""

# 목차는 소제목 3개 이상 · 2,500자 이상일 때만 켜진다. 짧은 글(post)만 검사하면
# 목차가 붙는 경로가 통째로 안 돌아가므로, 그 문턱을 넘는 판을 따로 만든다.
_MORE = """
<h2>재전송을 언제 거는가</h2>
<p>ack 가 안 오면 다시 보내야 하는데, 너무 빨리 걸면 링크만 더 막히고 너무 늦게 걸면
멈춘 것처럼 보인다. 지금은 4 tick 을 기다린다. 50Hz 에서 80ms 다.</p>
<p>이 값을 RTT 에 맞춰 움직이게 하는 방법도 있었다. 고르지 않은 이유는 결정론 때문이다.
peer 마다 다른 시점에 재전송하면 로그를 맞춰 보기가 어려워진다. 고정값이 디버깅에 낫다.</p>
<h2>조각으로 나누는 기준</h2>
<p>데이터그램 페이로드는 1200바이트까지만 쓴다. 경로 MTU 를 재는 대신 안전한 값을 박았다.
스냅샷은 이 크기를 쉽게 넘으므로 1024바이트 조각으로 잘라 보내고 받는 쪽에서 잇는다.</p>
<p>조각 번호는 채널 안에서만 의미가 있다. 채널이 다르면 같은 번호가 다른 뜻이 된다.
이 점을 놓치면 서로 다른 메시지의 조각이 한 버퍼에 섞인다.</p>
<h2>확정 입력을 만드는 순서</h2>
<p>서버는 그 tick 에 도착한 입력을 모아 하나의 목록으로 확정한다. 이때 순서가 중요하다.
연결 목록을 그대로 돌면 프로세스마다 순서가 달라지므로 clientId 로 정렬한 뒤 처리한다.</p>
<p>도착하지 않은 입력은 직전 입력을 그대로 유지한다. 멈추게 하지 않는 이유는, 한 명이
느리면 전원이 같이 멈추기 때문이다. 다만 이 대체는 이동에는 맞고 한 번만 눌리는
행동에는 틀리다. 그런 비트가 생기면 대체 대상에서 빼야 한다.</p>
<h2>왜 스냅샷을 매 tick 보내지 않는가</h2>
<p>상태를 통째로 보내면 결정론이 필요 없다. 대신 대역폭이 엔티티 수에 비례해 늘고,
공장 게임처럼 물체가 수천 개 도는 장르에서는 금방 한계가 온다. 입력만 보내면
크기가 사람 수에만 비례한다. 그 대가로 모든 peer 가 같은 결과를 내야 한다.</p>
<p>그래서 스냅샷은 처음 들어올 때와 어긋났을 때만 쓴다.</p>
<h2>해시를 언제 비교하는가</h2>
<p>매 tick 해시를 주고받으면 확인은 빨라지지만 트래픽이 늘고, 드물게 보내면 어긋난 뒤
한참을 더 달린 다음에야 알아챈다. 지금은 매 tick 보내되 등급을 sequenced 로 둔다.
구식 해시는 도착해도 버리므로 손실이 나도 밀리지 않는다.</p>
<p>해시가 어긋났다고 곧바로 스냅샷을 요청하지는 않는다. 한 번 어긋난 것이
전송 지연 때문일 수 있어서다. 같은 tick 에 대해 두 번 어긋나야 재동기화로 넘어간다.</p>
<h2>재동기화가 되돌리는 것</h2>
<p>스냅샷은 컴포넌트 풀의 바이트를 그대로 싣는다. 그래서 trivially copyable 이 아닌
컴포넌트는 아예 대상이 아니다. 포인터가 든 컴포넌트를 스냅샷에 넣으려는 순간
프로세스가 다른 쪽에서는 의미 없는 주소가 된다.</p>
<p>받는 쪽은 레지스트리를 처음부터 다시 세운다. 이름 해시와 크기와 정렬이 하나라도
어긋나면 그 타입을 거부한다. 조용히 넘어가면 어긋난 채로 계속 달리기 때문이다.</p>
<h2>세션과 캐릭터를 나눈 이유</h2>
<p>clientId 는 접속할 때마다 새로 발급된다. 그래서 이것을 사람의 정체성으로 쓰면
재접속한 사람이 남이 된다. 세션 · 영속 플레이어 · 캐릭터를 나눠 두면 끊긴 사람의
캐릭터를 지우는 대신 얼려 둘 수 있다.</p>
<p>다만 얼리는 것도 상태 변화라서 모든 peer 가 같은 tick 에 적용해야 한다.
한쪽만 먼저 얼리면 그 순간부터 시뮬레이션이 갈라진다.</p>
<p>이 구분을 미루면 나중에 되돌리기가 어렵다. 캐릭터에 붙은 컴포넌트가 늘어난 뒤에
소유 관계를 바꾸면 스냅샷 형식과 확정 입력 형식이 동시에 바뀌기 때문이다.
지금처럼 컴포넌트가 몇 개 없을 때 정리하는 편이 싸다.</p>
<h2>다음에 할 것</h2>
<p>스폰과 디스폰을 확정 입력 경로 안으로 넣는 일이 남았다. 건설이 들어가는 장르라
이걸 정리하지 않으면 물체가 하나 생길 때마다 어긋날 여지가 생긴다.</p>
"""

def SVG(w, h, color):
    """따옴표 없는 데이터 URI. 속성 안에 그대로 넣어도 안 깨진다."""
    return ('data:image/svg+xml;utf8,'
            '%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 '
            'width=%22{w}%22 height=%22{h}%22%3E'
            '%3Crect width=%22{w}%22 height=%22{h}%22 fill=%22%23{c}%22/%3E'
            '%3C/svg%3E').format(w=w, h=h, c=color)


CATS = [('ECS 엔진', 9), ('알고리즘', 22), ('유니티', 6), ('일상', 4), ('간단 코드', 3)]


def block(s, tag, keep=True, times=1):
    """`<s_tag>...</s_tag>` 를 지우거나(keep=False), 껍데기를 벗기고 times 번 펼친다."""
    pat = re.compile(r'<s_%s>(.*?)</s_%s>' % (tag, tag), re.S)
    if not keep:
        return pat.sub('', s)
    return pat.sub(lambda m: m.group(1) * times, s)


def render(kind):
    s = io.open(SKIN, encoding='utf-8').read()
    css = io.open(CSS, encoding='utf-8').read()

    is_list = kind in ('home', 'list')
    is_post = kind.startswith('post')
    is_tag  = kind == 'tag'

    # 페이지 종류에 따라 켜고 끄는 블록
    s = block(s, 't3')
    s = block(s, 'cover_group', keep=(kind == 'home'))
    s = block(s, 'cover_rep', keep=True)
    s = block(s, 'cover_item', keep=True, times=3)
    s = block(s, 'cover_item_thumbnail'); s = block(s, 'cover_item_article_info')
    s = block(s, 'cover_url')
    s = block(s, 'list', keep=is_list)
    s = block(s, 'list_rep', keep=is_list, times=len(POSTS))
    s = block(s, 'list_empty', keep=False)
    s = block(s, 'list_rep_thumbnail'); s = block(s, 'rp_count')
    s = block(s, 'notice_rep', keep=False); s = block(s, 'index_article_rep', keep=False)
    s = block(s, 'article_rep', keep=is_post)
    s = block(s, 'permalink_article_rep', keep=is_post)
    s = block(s, 'article_rep_thumbnail', keep=False)   # 대표 이미지 미지정이 현재 실태다
    s = block(s, 'tag_label'); s = block(s, 'ad_div', keep=False)
    s = block(s, 'article_prev'); s = block(s, 'article_next')
    s = block(s, 'article_related'); s = block(s, 'article_related_rep', times=3)
    s = block(s, 'article_related_rep_thumbnail')
    s = block(s, 'rp', keep=False)
    s = block(s, 'article_protected', keep=False)
    s = block(s, 'page_rep', keep=False)
    s = block(s, 'tag', keep=is_tag); s = block(s, 'tag_rep', times=18)
    s = block(s, 'guest', keep=False)
    s = block(s, 'local', keep=False)
    s = block(s, 'local_spot_rep', keep=False); s = block(s, 'local_info_rep', keep=False)
    s = block(s, 'paging', keep=is_list); s = block(s, 'paging_rep', times=5)
    s = block(s, 'sidebar'); s = block(s, 'sidebar_element', times=1)
    s = re.sub(r'</?s_[a-z_0-9]+>', '', s)   # 남은 껍데기 제거

    # 반복 블록에 서로 다른 내용을 넣는다. 전부 같은 글이면 잘림·정렬 문제가 안 드러난다.
    def cycle(token, values):
        out, i = [], [0]
        def rep(m):
            v = values[i[0] % len(values)]; i[0] += 1
            return v
        return re.sub(re.escape(token), rep, s)

    for token, vals in [
        ('[##_list_rep_title_##]',    [p[0] for p in POSTS]),
        ('[##_list_rep_summary_##]',  [p[1] for p in POSTS]),
        # 이 치환자는 항목마다 두 번 나온다(링크 속성 + 표시 글자).
        # 값을 한 번씩만 주면 항목마다 카테고리가 한 칸씩 밀린다.
        ('[##_list_rep_category_##]', [c for p in POSTS for c in (p[2], p[2])]),
        ('[##_list_rep_regdate_##]',  [p[3] for p in POSTS]),
        ('[##_list_rep_date_##]',     [p[3] for p in POSTS]),
        ('[##_list_rep_rp_cnt_##]',   [p[4] for p in POSTS]),
        ('[##_cover_item_title_##]',  [p[0] for p in POSTS]),
        ('[##_cover_item_summary_##]',[p[1] for p in POSTS]),
        ('[##_cover_item_category_##]',[p[2] for p in POSTS]),
        ('[##_cover_item_simple_date_##]',[p[3] for p in POSTS]),
        ('[##_cover_item_comment_count_##]',[p[4] for p in POSTS]),
        ('[##_rctps_rep_title_##]',   [p[0] for p in POSTS]),
        ('[##_rctps_rep_date_##]',    [p[3] for p in POSTS]),
        ('[##_related_rep_title_##]', [p[0] for p in POSTS]),
        ('[##_related_rep_date_##]',  [p[3] for p in POSTS]),
        ('[##_paging_rep_link_num_##]', ['1','2','3','4','5']),
        ('[##_tag_name_##]',          ['C++','ECS','결정론','네트워크','유니티','알고리즘',
                                       'lockstep','고정소수점','UDP','스냅샷','BOJ','그래프',
                                       'DP','자료구조','렌더링','DX11','회고','공부']),
        ('[##_tag_class_##]',         ['cloud1','cloud2','cloud3','cloud4','cloud5']),
    ]:
        s = cycle(token, vals)

    fixed = {
        '[##_page_title_##]': 'machamy',
        '[##_title_##]': 'machamy',
        '[##_desc_##]': '만들면서 배우는 기록',
        '[##_blog_link_##]': '#',
        '[##_rss_url_##]': '#',
        '[##_blog_menu_##]': '<ul><li><a href="#">홈</a></li><li><a href="#">태그</a></li>'
                             '<li><a href="#">방명록</a></li></ul>',
        '[##_category_list_##]': '<ul>' + ''.join(
            '<li><a href="#">%s <span class="c_cnt">(%d)</span></a></li>' % c for c in CATS) + '</ul>',
        '[##_count_total_##]': '44',
        '[##_article_rep_title_##]': POSTS[0][0],
        '[##_article_rep_desc_##]': BODY + (_MORE if kind == 'post-long' else ''),
        '[##_article_rep_category_##]': POSTS[0][2],
        '[##_article_rep_date_##]': POSTS[0][3],
        '[##_article_rep_simple_date_##]': POSTS[0][3],
        '[##_article_prev_title_##]': POSTS[1][0],
        '[##_article_next_title_##]': POSTS[2][0],
        # 큰따옴표가 들어가면 `style="background-image:url('…')"` 속성을 깨고
        # 나머지 마크업이 글자로 새어 나온다. 전부 %22 로 인코딩한다.
        '[##_image_##]': SVG(80, 80, 'c9c2b6'),
        '[##_tag_label_rep_##]': '<a href="#">C++</a><a href="#">ECS</a><a href="#">결정론</a>',
        '[##_subscription_button_##]': '<button type="button">구독</button>',
        '[##_search_onclick_submit_##]': 'return false;',
        # 이 둘은 주소가 아니라 **앵커 요소 통째**로 치환된다. '#' 로 두면
        # 우물정자 두 개가 화면에 찍히고 페이지 이동 버튼 모양을 확인할 수 없다.
        '[##_prev_page_##]': '<a href="#" class="pg-t">이전</a>',
        '[##_next_page_##]': '<a href="#" class="pg-t">다음</a>',
        '[##_body_id_##]': 'tt-body-' + ('index' if is_list else
                                         'page' if is_post else 'tag'),
        '[##_var_footerCopyright_##]': '© machamy',
        '[##_list_rep_thumbnail_url_##]': SVG(240, 160, 'd6cfc4'),
    }
    for k, v in fixed.items():
        s = s.replace(k, v)
    # 남은 치환자는 지운다. '#' 같은 글자로 바꾸면 화면이 그 글자로 뒤덮여
    # 진짜 레이아웃 문제를 못 본다. 대소문자가 섞인 이름(`_var_footerCopyright_`)도 있다.
    s = re.sub(r'\[##_[A-Za-z_0-9]+_##\]', '', s)
    s = s.replace('<link rel="stylesheet" href="./style.css">',
                  '<style>\n' + css + '\n</style>')
    # 외부 폰트 CDN 은 오프라인 검증에서 지연만 만든다. 뺀다.
    s = re.sub(r'<link rel="(?:preconnect|stylesheet)" href="https://cdn\.jsdelivr[^>]*>', '', s)
    return s


if __name__ == '__main__':
    out = os.path.join(ROOT, '.tmpbuild', 'render')   # 산출물은 저장소에 넣지 않는다
    if not os.path.isdir(out):
        os.makedirs(out)
    for kind in ('home', 'list', 'post', 'post-long', 'tag'):
        p = os.path.join(out, kind + '.html')
        io.open(p, 'w', encoding='utf-8', newline='').write(render(kind))
        print(kind, os.path.getsize(p), '바이트')
