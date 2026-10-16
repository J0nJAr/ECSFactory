#!/usr/bin/env bash
#
# 설계 규칙 검사: GameServer 바이너리는 DX11/DXGI에 의존하면 안 된다.
#
# 왜 CI에서 보나:
#   이건 "빌드가 되는가"가 아니라 "설계 규칙이 지켜지는가"의 문제다.
#   누군가 GameServer에 EngineRender를 링크해도 빌드는 멀쩡히 통과한다.
#   그래서 사람이 못 알아채고, dedicated server 빌드에 GPU 의존이 슬금슬금 들어온다.
#   (docs/design/rendering-dx11.md §0 원칙 2, docs/design/roadmap.md E2)
#
# 무엇을 보나:
#   빌드 산출물의 임포트 DLL 목록. 생성기(Ninja / MinGW Makefiles)에 무관하게
#   "실제로 배포되는 물건"을 검사한다.
#
# 📌 이 검사는 R2부터 실제로 유효하다.
#   그전까지는 Dx11Renderer가 스텁이라 DX11 함수를 하나도 호출하지 않았고,
#   그러면 링커가 쓰이지 않는 임포트 라이브러리를 떨어뜨린다. 즉 "규칙이 지켜져서 통과"와
#   "아직 물어뜯을 게 없어서 통과"가 구별되지 않았다.
#   지금은 Dx11Renderer가 D3D11CreateDeviceAndSwapChain / D3DCompile 을 실제로 부르므로,
#   GameServer 가 EngineRender 를 링크하면 임포트 테이블에 잡힌다.

set -euo pipefail

BUILD_DIR="${1:?사용법: $0 <빌드 디렉터리>   예: out/build/ci-debug}"
SERVER_EXE="$BUILD_DIR/bin/GameServer.exe"

FORBIDDEN='d3d11|dxgi|d3dcompiler|d3d12'

if [[ ! -f "$SERVER_EXE" ]]; then
    # 대상이 없으면 "통과"가 아니라 "실패"다.
    # 조용히 지나가는 검사는 없느니만 못하다 — 경로가 바뀌었다면 그 사실을 알아야 한다.
    echo "::error::GameServer 실행 파일을 찾을 수 없다: $SERVER_EXE"
    echo "빌드가 끝난 뒤에 실행해야 한다. 출력 경로가 바뀌었다면 이 스크립트를 고칠 것."
    exit 1
fi

if ! command -v objdump >/dev/null 2>&1; then
    echo "::error::objdump가 없다. 임포트 목록을 확인할 수 없다."
    exit 1
fi

echo "검사 대상: $SERVER_EXE"

imports="$(objdump -p "$SERVER_EXE" | grep -i 'DLL Name' || true)"
echo "--- 임포트 DLL ---"
echo "$imports"
echo "------------------"

if echo "$imports" | grep -qiE "$FORBIDDEN"; then
    echo "::error::GameServer가 렌더 계층 DLL에 의존한다."
    echo "dedicated server는 창도 GPU도 없다. EngineRender를 링크하는 대상은 GameClient(그리고 나중의 EngineEditor)뿐이어야 한다."
    exit 1
fi

echo "OK — GameServer는 DX11/DXGI에 의존하지 않는다."
