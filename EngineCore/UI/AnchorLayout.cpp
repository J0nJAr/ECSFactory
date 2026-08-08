#include "UI/AnchorLayout.hpp"

namespace EF::UI {

    namespace {

        struct Span {
            float lo = 0.0f;
            float hi = 0.0f;
        };

        // 한 축(x 또는 y)의 [lo, hi]를 낸다.
        //
        // 축을 이렇게 떼어낸 것이 곧 "축마다 독립 판정"이다. StretchX는 X 호출만 stretch
        // 분기로 가고 Y 호출은 점 앵커 분기로 간다 — 두 축을 한 덩어리로 계산하면
        // 이 조합을 표현할 수 없다.
        [[nodiscard]] Span ResolveAxis(
            float parentLo,
            float parentSize,
            float anchorMin,
            float anchorMax,
            float offsetMin,
            float offsetMax,
            float size,
            float pivot) noexcept {
            const float anchorLoPx = parentLo + parentSize * anchorMin;
            const float anchorHiPx = parentLo + parentSize * anchorMax;

            // float 정확 비교를 일부러 쓴다.
            //
            // 이 판정 하나로 size와 offset의 **의미 자체**가 바뀌므로 딱 떨어져야 한다.
            // epsilon을 두면 폭이 1e-7인 stretch 앵커가 조용히 점 앵커로 넘어가면서
            // size가 "부모를 따라감"에서 "지정값 그대로"로 뒤집힌다 — 화면에서 원인을
            // 짚기 어려운 종류의 사고다. 프리셋은 0 / 0.5 / 1 정확값만 내고, 손으로 만든
            // AnchorRect도 같게 쓸 의도가 있을 때만 같은 값이 들어간다. 두 경우 모두
            // 정확 비교가 맞다.
            if (anchorMin == anchorMax) {
                // 점 앵커: size는 지정값 그대로, pivot이 size 안에서 기준점을 정한다.
                // pivot 지점이 (anchorLoPx + offsetMin)에 놓인다.
                const float lo = anchorLoPx + offsetMin - size * pivot;
                return Span{lo, lo + size};
            }

            // stretch: size는 부모를 따라가고 offset이 양쪽 여백이 된다.
            // 먼 쪽은 빼는 방향이라 offsetMax는 "안쪽으로 얼마나"다(부호가 뒤집히지 않는다).
            return Span{anchorLoPx + offsetMin, anchorHiPx - offsetMax};
        }

    }

    AnchorRect ToAnchorRect(Anchor preset) noexcept {
        // Y가 아래로 증가하는 화면 좌표계다 — Top이 0, Bottom이 1이다.
        switch (preset) {
            case Anchor::TopLeft:     return AnchorRect{ {0.0f, 0.0f}, {0.0f, 0.0f} };
            case Anchor::Top:         return AnchorRect{ {0.5f, 0.0f}, {0.5f, 0.0f} };
            case Anchor::TopRight:    return AnchorRect{ {1.0f, 0.0f}, {1.0f, 0.0f} };
            case Anchor::Left:        return AnchorRect{ {0.0f, 0.5f}, {0.0f, 0.5f} };
            case Anchor::Center:      return AnchorRect{ {0.5f, 0.5f}, {0.5f, 0.5f} };
            case Anchor::Right:       return AnchorRect{ {1.0f, 0.5f}, {1.0f, 0.5f} };
            case Anchor::BottomLeft:  return AnchorRect{ {0.0f, 1.0f}, {0.0f, 1.0f} };
            case Anchor::Bottom:      return AnchorRect{ {0.5f, 1.0f}, {0.5f, 1.0f} };
            case Anchor::BottomRight: return AnchorRect{ {1.0f, 1.0f}, {1.0f, 1.0f} };
            case Anchor::StretchX:    return AnchorRect{ {0.0f, 0.5f}, {1.0f, 0.5f} };
            case Anchor::StretchY:    return AnchorRect{ {0.5f, 0.0f}, {0.5f, 1.0f} };
            case Anchor::StretchAll:  return AnchorRect{ {0.0f, 0.0f}, {1.0f, 1.0f} };
        }

        // 열거 밖의 값이 캐스팅으로 들어온 경우에만 온다. Center로 떨어뜨린다 —
        // 화면 밖으로 사라지는 것보다 눈에 보이는 편이 디버깅에 낫다.
        return AnchorRect{ {0.5f, 0.5f}, {0.5f, 0.5f} };
    }

    Float2 DefaultPivot(Anchor preset) noexcept {
        const AnchorRect anchor = ToAnchorRect(preset);

        // 점 앵커 축은 min == max 라서 중점이 곧 앵커 지점이고(부모 구석에 딱 붙는다),
        // stretch 축은 중점이 0.5라서 "안 쓰이는 무해한 값"이 된다.
        // 두 경우가 한 식으로 떨어지므로 분기를 두지 않는다.
        return Float2{
            (anchor.min.x + anchor.max.x) * 0.5f,
            (anchor.min.y + anchor.max.y) * 0.5f
        };
    }

    Rect ResolveRect(const Rect& parent, const LayoutSpec& spec) noexcept {
        const Span x = ResolveAxis(
            parent.min.x, parent.Width(),
            spec.anchor.min.x, spec.anchor.max.x,
            spec.offsetMin.x, spec.offsetMax.x,
            spec.size.x, spec.pivot.x);

        const Span y = ResolveAxis(
            parent.min.y, parent.Height(),
            spec.anchor.min.y, spec.anchor.max.y,
            spec.offsetMin.y, spec.offsetMax.y,
            spec.size.y, spec.pivot.y);

        return Rect{ Float2{x.lo, y.lo}, Float2{x.hi, y.hi} };
    }

    Rect ResolveRect(
        const Rect& parent,
        Anchor preset,
        Float2 offset,
        Float2 size,
        Float2 pivot) noexcept {
        LayoutSpec spec;
        spec.anchor = ToAnchorRect(preset);
        spec.offsetMin = offset;
        spec.offsetMax = offset;  // stretch 축에서만 쓰인다 → 대칭 여백. 점 앵커 축에선 무시된다.
        spec.size = size;
        spec.pivot = pivot;

        return ResolveRect(parent, spec);
    }

    Rect ResolveRect(
        const Rect& parent,
        Anchor preset,
        Float2 offset,
        Float2 size) noexcept {
        return ResolveRect(parent, preset, offset, size, DefaultPivot(preset));
    }

}
