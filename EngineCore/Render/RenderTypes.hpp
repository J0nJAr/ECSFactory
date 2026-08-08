#pragma once

#include <cstdint>

namespace EF {

    // 렌더 계층 전용 표현 타입들.
    //
    // ⚠️ 여기 있는 것은 전부 float다. 시뮬레이션 타입(Fixed64 / Vector3)이 아니다.
    //    이 구분이 이 엔진의 결정론을 떠받친다 — Fixed64 → float 변환은 RenderSystem에서
    //    한 방향으로만 일어나고, 그 결과가 시뮬레이션으로 되돌아가면 안 된다.
    //    (docs/design/rendering-dx11.md §0 원칙 1)

    struct Float2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    // 렌더 위치는 처음부터 3D로 둔다.
    //
    // 지금 목표는 2D 탑다운(XZ 평면을 화면에 투영)이지만, 게임플레이의 Transform.position이
    // 이미 Vector3다. 여기서 Float2로 납작하게 만들면 나중에 3D로 갈 때 RenderSystem을
    // 통째로 다시 써야 한다. Y를 당장 안 쓰더라도 자리를 비워두는 편이 싸다.
    // (docs/design/roadmap.md §1 "2D→3D 확장을 위해 지금 지킬 것")
    struct Float3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // 0~1 RGBA. 감마 보정과 색공간은 지금 다루지 않는다 —
    // 컬러 쿼드만 그리는 단계에서는 차이가 눈에 띄지 않고, 필요해지면 그때 넣는다.
    struct Color {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        static constexpr Color Black()       { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
        static constexpr Color White()       { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
        static constexpr Color CornflowerBlue() { return { 0.39f, 0.58f, 0.93f, 1.0f }; }
    };

    // 화면 픽셀 사각형. UI 앵커 레이아웃의 계산 단위이기도 하다.
    //
    // min이 좌상단, max가 우하단이다 — 화면 좌표계는 Y가 아래로 증가한다.
    // 월드 좌표계(Y가 위)와 반대이므로 헷갈리지 말 것.
    struct Rect {
        Float2 min;
        Float2 max;

        [[nodiscard]] constexpr float Width() const noexcept {
            return max.x - min.x;
        }

        [[nodiscard]] constexpr float Height() const noexcept {
            return max.y - min.y;
        }

        // 경계는 [min, max) 로 취급한다.
        // 사각형 두 개를 딱 붙여 놓았을 때 경계 픽셀이 양쪽 모두에 잡히지 않게 하기 위함이다
        // (UI 히트테스트에서 위젯이 겹쳐 보이는 문제를 막는다).
        [[nodiscard]] constexpr bool Contains(Float2 point) const noexcept {
            return point.x >= min.x && point.x < max.x
                && point.y >= min.y && point.y < max.y;
        }
    };

    // 4x4 행렬. 카메라의 view-projection을 셰이더로 넘기는 용도로만 쓴다.
    //
    // 저장은 행 우선(row-major): m[0..3]이 첫 행이다.
    // HLSL 기본은 열 우선이라 셰이더에 넘길 때 전치하거나 `row_major`를 명시해야 하는데,
    // 어느 쪽으로 갈지는 실제로 셰이더를 붙이는 R3에서 확정한다.
    // (지금 정해봐야 검증할 방법이 없다.)
    struct Mat4 {
        float m[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        static constexpr Mat4 Identity() noexcept {
            return {};
        }
    };

}
