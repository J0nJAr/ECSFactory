#pragma once

#include "Render/RenderTypes.hpp"

namespace EF {

    // 2D 탑다운 직교 카메라. 월드의 XZ 평면을 화면에 투영한다.
    //
    // 왜 XZ인가: 게임플레이의 이동이 XZ에서 일어나기 때문이다
    // (Gameplay/PlayerMovement.hpp의 kInputMoveForward가 +Z를 민다).
    // 월드 Y는 높이축이고, 2D 단계에서는 투영에서 통째로 버린다.
    //
    // 화면 대응:
    //   월드 +X → 화면 오른쪽
    //   월드 +Z → 화면 위쪽   (DX11 NDC는 y가 위로 증가한다)
    //   월드 +Y → 버려짐      (3D로 갈 때 이 자리에 진짜 투영이 들어온다)
    //
    // ⚠️ 이 타입은 리소스로 클라이언트 월드에만 붙는다(ClientPresentation).
    //    서버 월드에는 카메라가 없다 — 서버는 그리지 않는다.
    //    그리고 스냅샷 대상이 아니다. 표현 상태이지 시뮬레이션 상태가 아니기 때문이다.
    //    (docs/design/rendering-dx11.md §0 원칙 2·3)
    struct Camera {
        // 화면 중심이 바라보는 월드 지점. x는 월드 X, y는 월드 **Z**다.
        // (화면 평면 위의 좌표라 Float2를 쓴다. y가 월드 Y가 아닌 점에 주의.)
        Float2 position{ 0.0f, 0.0f };

        // 화면 세로에 담기는 월드 길이.
        //
        // 설계서(§2)에는 `zoom`으로 적혀 있었는데 이름을 바꿨다. "zoom이 크면 확대인가
        // 축소인가"가 매번 헷갈리고, 코드를 읽는 쪽이 그걸 추측하게 되기 때문이다.
        // "세로에 월드 몇 유닛이 들어가는가"는 방향이 하나뿐이라 틀릴 수 없다.
        // 값이 커지면 더 넓게 보인다(= 축소).
        //
        // 기본값 60의 근거: 이동이 tick당 1유닛이고 tick이 50Hz라 초당 50유닛 움직인다.
        // 세로 60유닛이면 화면을 가로지르는 데 1초 남짓이라 눈으로 따라갈 만하다.
        float viewHeightWorldUnits = 60.0f;

        // 이 카메라의 view-projection 행렬. aspect는 화면 가로/세로 비다.
        //
        // 결과는 행 우선(row-major) 저장이고, 셰이더에서 `row_major` 로 선언한 채
        // mul(float4(pos,1), M) 으로 곱하는 것을 전제한다.
        // (Render/Dx11/Dx11Renderer.cpp의 kShaderSource, RenderTypes.hpp의 Mat4 주석)
        //
        // aspect가 0 이하이거나 viewHeightWorldUnits가 0 이하면 항등 행렬을 돌려준다.
        // 창이 최소화되는 순간 크기가 0으로 들어오는 경로가 실제로 있어서,
        // 여기서 0으로 나누면 행렬이 NaN이 되고 그 프레임이 통째로 사라진다.
        [[nodiscard]] Mat4 ViewProjection(float aspect) const noexcept;
    };

    // 정점 셰이더가 하는 것과 같은 변환을 CPU에서 한다. 월드 좌표 → NDC.
    //
    // 이게 있는 이유는 **행렬 규약을 GPU 없이 테스트하기 위해서**다.
    // row-major 저장과 mul(행벡터, 행렬) 조합은 한쪽만 틀려도 화면이 조용히 이상해지는데,
    // 그걸 눈으로 확인하려면 사람이 창을 띄워야 한다. 같은 계산을 여기 한 번 더 적어 두면
    // 단위 테스트가 규약을 붙잡아 준다.
    //
    // ⚠️ 그래서 이 함수와 셰이더의 mul은 **같이 고쳐야 한다.** 한쪽만 바꾸면
    //    테스트는 통과하는데 화면은 틀린 상태가 된다.
    [[nodiscard]] Float3 ProjectPoint(const Mat4& viewProjection, Float3 worldPosition) noexcept;

}
