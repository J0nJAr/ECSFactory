// 실제 스킨 검증 — 프리셋 전체 × 5화면 × 3폭 × 2테마를 렌더해서 깨진 곳을 찾는다.
// 저장소 루트에서 돌린다:  node blog/skin/tools/audit-skin.mjs [프리셋]
// 실험실이 아니라 `blog/skin/` 정본을 본다. 티스토리는 되돌리기가 없으므로
// 붙여넣기 전에 여기를 통과시킨다.
import fs from 'fs';
import path from 'path';
import url from 'url';

/* playwright-core 는 저장소 의존성이 아니다(엔진 빌드와 무관하므로 넣지 않았다).
   설치 위치가 사람마다 다를 수 있어 몇 군데를 훑고, 못 찾으면 무엇을 하라고 알려 준다.
   그냥 import 하면 영어 스택 트레이스만 나와서 원인을 알기 어렵다. */
const HERE = path.dirname(url.fileURLToPath(import.meta.url));
const CAND = ['playwright-core',
              path.join(HERE, '..', '..', '..', '.tmpbuild', 'node_modules',
                        'playwright-core', 'index.mjs'),
              path.join(HERE, '..', '..', '..', 'node_modules',
                        'playwright-core', 'index.mjs')];
let chromium = null;
for (const c of CAND) {
  try { ({ chromium } = await import(c.startsWith('playwright') ? c
                                     : url.pathToFileURL(c).href)); break; } catch (e) { /* 다음 후보 */ }
}
if (!chromium) {
  for (const m of [
    'playwright-core 를 찾지 못했다. 저장소 루트에서 설치한다:',
    '  npm i playwright-core',
    '브라우저 실행 파일도 필요하다 — 이 스크립트는 chrome-headless-shell 경로를',
    '직접 가리키므로 없으면 파일 안의 executablePath 줄을 고친다.',
  ]) console.error(m);
  process.exit(2);
}

const DIR = path.join(process.cwd(), '.tmpbuild', 'render');
// 짧은 글과 긴 글을 둘 다 본다. 목차는 긴 글에만 붙으므로, 짧은 글만 검사하면
// 목차가 있는 배치가 통째로 안 돌아간다.
const PAGES = [['home', '홈'], ['list', '목록'], ['post', '짧은 글'],
               ['post-long', '긴 글'], ['tag', '태그']];
const WIDTHS = [[1180, '데스크톱'], [768, '태블릿'], [390, '모바일']];
const only = process.argv[2];   // 선택: 특정 프리셋만

const browser = await chromium.launch({
  headless: true,
  executablePath: 'C:/Users/macham/AppData/Local/ms-playwright/chromium_headless_shell-1223/'
                + 'chrome-headless-shell-win64/chrome-headless-shell.exe',
});
const page = await browser.newPage({ viewport: { width: 1180, height: 1000 } });
const jsErrs = [];
page.on('pageerror', e => jsErrs.push(String(e).slice(0, 160)));

// 프리셋 목록을 skin.html 의 버튼에서 직접 읽는다. 손으로 적으면 어긋난다.
const skin = fs.readFileSync(path.join(process.cwd(), 'blog', 'skin', 'skin.html'), 'utf8');
const layouts = [...skin.matchAll(/data-layout="([a-z]+)"><i>(\d+)<\/i>([^<]+)</g)]
  .map(m => ({ id: m[1], no: m[2], name: m[3] }))
  .filter(l => !only || l.id === only);
console.log(`프리셋 ${layouts.length}종 × ${PAGES.length}화면 × ${WIDTHS.length}폭 `
          + `× 2테마 = ${layouts.length * PAGES.length * WIDTHS.length * 2}개 조합`);

const bad = [];
for (const [file, pname] of PAGES) {
  const url = 'file:///' + path.join(DIR, file + '.html').replace(/\\/g, '/');
  for (const [w, wname] of WIDTHS) {
    await page.setViewportSize({ width: w, height: 1000 });
    await page.goto(url, { waitUntil: 'load' });
    await page.waitForTimeout(260);
    for (const L of layouts) {
     // 라이트와 다크를 둘 다 본다. 프리셋이 리터럴 색을 깔면 한쪽에서만 깨지는데,
     // 이 스킨은 프리셋마다 팔레트를 갈아끼우므로 그 사고가 나기 쉬운 구조다.
     for (const theme of ['light', 'dark']) {
      await page.evaluate(([id, th]) => {
        document.documentElement.dataset.layout = id;
        document.documentElement.dataset.theme = th;
      }, [L.id, theme]);
      await page.waitForTimeout(170);
      const r = await page.evaluate(() => {
        const de = document.documentElement;
        const out = { over: [], clip: [], invisible: [], tiny: [] };
        // 1) 가로 넘침 — 화면 밖으로 나간 요소.
        //    스크롤 상자(예: `pre{overflow-x:auto}`) 안의 요소는 제외한다. 코드 블록은
        //    내용이 넓은 게 정상이고, 상자가 잘라 주므로 화면은 안 넘친다. 이걸 안 빼면
        //    코드가 있는 모든 프리셋이 거짓 양성으로 잡힌다.
        const lim = de.clientWidth;
        const clipped = el => {
          for (let n = el.parentElement; n && n !== document.body; n = n.parentElement)
            if (/auto|scroll|hidden/.test(getComputedStyle(n).overflowX)) return true;
          return false;
        };
        document.querySelectorAll('.mm *').forEach(el => {
          const b = el.getBoundingClientRect();
          if (b.width < 1 || b.height < 1) return;
          if ((b.right > lim + 2 || b.left < -2) && !clipped(el))
            out.over.push(el.tagName + '.' + (el.className || '-').toString().split(' ')[0]
                        + ' ' + Math.round(b.left) + '~' + Math.round(b.right));
        });
        // 2) 글자 잘림 — 내용이 상자보다 넓은데 넘침 처리가 없다
        document.querySelectorAll('.lst-t,.art-t,.lst-s,.art-body p,.art-body td,.art-body th,.cv-ti')
          .forEach(el => {
            const cs = getComputedStyle(el);
            if (el.scrollWidth > el.clientWidth + 4 && cs.overflowX === 'visible'
                && cs.textOverflow !== 'ellipsis')
              out.clip.push(el.className || el.tagName);
          });
        // 3) 대비 실종 — 글자색과 배경색이 같다(다크 모드 이식 사고의 전형)
        function rgb(s) { const m = s.match(/\d+/g); return m ? m.slice(0, 3).map(Number) : null; }
        function bgOf(el) {
          for (let n = el; n && n !== document; n = n.parentElement) {
            const c = getComputedStyle(n).backgroundColor;
            if (c && !/rgba\(0, 0, 0, 0\)|transparent/.test(c)) return c;
          }
          return getComputedStyle(document.body).backgroundColor;
        }
        document.querySelectorAll('.lst-t,.art-t,.art-body p,.lst-s,.hd-brand').forEach(el => {
          const f = rgb(getComputedStyle(el).color), b = rgb(bgOf(el));
          if (!f || !b) return;
          const d = Math.abs(f[0]-b[0]) + Math.abs(f[1]-b[1]) + Math.abs(f[2]-b[2]);
          if (d < 60) out.invisible.push((el.className || el.tagName) + ' 차이' + d);
        });
        // 4) 빈 칸 — 격자에 트랙을 잡아 놓고 아무도 안 들어간 열.
        //    사이드를 감춘 페이지에서 프리셋이 트랙 정의만 남겨 두면 이렇게 된다.
        //    넘치지도 잘리지도 않아 다른 검사에 안 걸리는데, 화면에는 빈 여백으로 남는다.
        document.querySelectorAll('.cols,.art,.lst-i').forEach(g => {
          const cs = getComputedStyle(g);
          if (cs.display !== 'grid') return;
          const tracks = cs.gridTemplateColumns.split(' ').filter(Boolean);
          if (tracks.length < 2) return;
          const used = new Set();
          [...g.children].forEach(c => {
            if (getComputedStyle(c).display === 'none') return;
            const n = parseInt(getComputedStyle(c).gridColumnStart, 10);
            if (n) used.add(n);
            else used.add(0);            // auto 배치는 판정하지 않는다
          });
          if (used.has(0)) return;
          for (let i = 1; i <= tracks.length; i++)
            if (!used.has(i) && parseFloat(tracks[i - 1]) > 4)
              out.tiny.push('빈열 ' + (g.className || g.tagName) + ' ' + i + '번 ' + tracks[i - 1]);
        });
        // 5) 찌그러짐 — 제목이 비정상적으로 좁다(격자 축 미지정 사고의 전형)
        document.querySelectorAll('.lst-t,.cv-ti').forEach(el => {
          const b = el.getBoundingClientRect();
          if (b.width > 0 && b.width < 60) out.tiny.push((el.className || el.tagName)
                                                       + ' ' + Math.round(b.width) + 'px');
        });
        return out;
      });
      const msg = [];
      if (r.over.length)      msg.push('넘침 ' + r.over.length + ' [' + r.over[0] + ']');
      if (r.clip.length)      msg.push('잘림 ' + r.clip.length + ' [' + r.clip[0] + ']');
      if (r.invisible.length) msg.push('대비없음 ' + r.invisible.length + ' [' + r.invisible[0] + ']');
      if (r.tiny.length)      msg.push('찌그러짐 ' + r.tiny.length + ' [' + r.tiny[0] + ']');
      if (msg.length) {
        bad.push(1);
        console.log(`${L.no} ${L.name} · ${pname} · ${wname} · ${theme === 'dark' ? '다크' : '라이트'}`
                  + `  ${msg.join(' / ')}`);
      }
     }
    }
  }
}
console.log(jsErrs.length ? `\nJS 오류: ${[...new Set(jsErrs)].join(' | ')}` : '\nJS 오류 없음');
console.log(bad.length ? `\n문제 ${bad.length}건` : '\n이상 신호 없음');
await browser.close();
