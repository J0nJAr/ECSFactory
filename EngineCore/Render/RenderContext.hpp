#pragma once

#include <vector>

#include "Render/RenderTypes.hpp"

namespace EF {

    // 이번 프레임에 그릴 것 하나.
    struct DrawItem {
        Float3 position;          // 월드 좌표 (2D 단계에서 y는 0)
        Float2 size;
        float rotation = 0.0f;    // 라디안, 화면 평면 기준 회전
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
