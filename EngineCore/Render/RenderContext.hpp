#pragma once

#include <vector>

#include "Render/RenderTypes.hpp"

namespace EF {

    // 이번 프레임에 그릴 것 하나.
    //
    // 좌표계는 **월드**다. 화면 좌표로 바꾸는 것은 카메라의 몫이고(Render/Camera.hpp),
    // 2D 탑다운 단계에서 쿼드는 XZ 평면(지면)에 눕는다. 그래서:
    struct DrawItem {
        Float3 position;          // 월드 좌표. 2D 단계에서 y(높이)는 투영에서 버려진다
        Float2 size;              // 지면 위 크기. x는 월드 X 방향, y는 월드 **Z** 방향 길이
        float rotation = 0.0f;    // 라디안. Y축(수직축) 기준 회전 = 탑다운에서 본 방향
        Color color;
    };

    // RenderSystem(ECS를 아는 쪽)과 IRenderer(GPU를 아는 쪽) 사이의 완충 지대.
    //
    // 왜 사이에 값 리스트를 하나 끼우나:
    //  - ECS 순회와 GPU 제출이 서로를 모르게 된다. 백엔드를 갈아끼워도 RenderSystem은 그대로다.
    //  - 정렬·배칭 같은 최적화를 "제출 직전"에 한 곳에서 할 수 있다.
    //    (지금은 안 하지만, 나중에 넣을 자리가 이미 있다는 게 중요하다.)
    class RenderContext {
    public:
        // capacity를 유지한 채 비운다 — 매 프레임 재할당을 피하기 위함.
        void Clear() noexcept {
            m_items.clear();
        }

        void Add(const DrawItem& item) {
            m_items.push_back(item);
        }

        [[nodiscard]] const std::vector<DrawItem>& Items() const noexcept {
            return m_items;
        }

    private:
        std::vector<DrawItem> m_items;
    };

}
