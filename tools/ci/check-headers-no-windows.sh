#!/usr/bin/env bash
#
# 설계 규칙 검사: `EngineCore/` 아래의 **헤더**는 windows.h / DX 헤더를 include하면 안 된다.
#
# 왜 CI에서 보나:
#   이건 "빌드가 되는가"가 아니라 "설계 규칙이 지켜지는가"의 문제다. 헤더에서
#   windows.h를 끌어와도 빌드는 대개 통과한다 — 대신 그 오염이 그 헤더를 include하는
#   **모든 번역 단위**로 번지고, 증상은 한참 떨어진 곳에서 엉뚱한 얼굴로 나타난다.
#
#     - min/max가 매크로가 되어 std::min / std::max가 토큰 단위로 깨진다.
#     - Rectangle, CreateWindow, GetMessage 같은 흔한 이름이 매크로에 점령당한다.
#     - windows.h가 winsock2.h보다 먼저 들어가면 winsock1과 충돌한다.
#       (실제로 이걸로 사고가 났다 — GameClient/main.cpp가 Base/InputManager.hpp를
#        네트워크 헤더보다 먼저 include하자 경고가 났고, 한동안 include 순서를 손으로
#        맞춰서 버텼다. 순서에 의존하는 상태는 그 자체로 함정이다.)
#
#   무엇보다 `EngineCore`는 **dedicated server도 링크한다.** 서버 빌드에 창·GPU 헤더가
#   딸려 들어갈 이유가 없다.
#
# 해법은 이미 저장소 안에 세 번 나와 있다: Platform/Win32Window.hpp, Render/Dx11/Dx11Renderer.hpp,
# Base/InputManager.hpp — 전부 플랫폼 헤더를 자기 .cpp 안에 가둔다(PIMPL 또는 상태 전체를 .cpp로).
#
# 무엇을 보나:
#   소스 트리만 본다. 빌드 산출물이 필요 없으므로 빌드 전에도 돌릴 수 있다.
#
# 📌 알려진 예외: `EngineCore/Network/UDPConnection.hpp`가 헤더에서 <winsock2.h>를 끌어온다.
#   그래서 winsock 계열은 아직 금지 목록에 없다. 같은 종류의 문제이고 같은 방법으로 풀 수 있지만
#   이 검사와 별개의 작업이다 — 그때 FORBIDDEN에 winsock2를 추가하면 된다.

set -euo pipefail

REPO_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
SCAN_DIR="$REPO_ROOT/EngineCore"

FORBIDDEN='#[[:space:]]*include[[:space:]]*<(windows\.h|d3d11\.h|d3dcompiler\.h|dxgi[0-9_]*\.h|d3d12\.h)>'

if [[ ! -d "$SCAN_DIR" ]]; then
    # 대상이 없으면 "통과"가 아니라 "실패"다. 조용히 지나가는 검사는 없느니만 못하다.
    echo "::error::검사 대상 디렉터리가 없다: $SCAN_DIR"
    exit 1
fi

echo "검사 대상: $SCAN_DIR/**/*.hpp"

hits="$(grep -rnE --include='*.hpp' "$FORBIDDEN" "$SCAN_DIR" || true)"

if [[ -n "$hits" ]]; then
    echo "$hits"
    echo "::error::EngineCore 헤더가 windows.h / DX 헤더를 include한다."
    echo "구현을 .cpp로 옮기고 플랫폼 헤더를 거기에 가둘 것(Platform/Win32Window.hpp, Base/InputManager.hpp 참조)."
    exit 1
fi

echo "OK — EngineCore 헤더는 windows.h / DX 헤더를 노출하지 않는다."
