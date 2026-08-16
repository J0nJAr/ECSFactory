#include "EntityCommandBuffer.hpp"

namespace EF {

    // ⚠️ 아직 구현되지 않은 스텁이다. 호출해도 아무 일도 일어나지 않는다.
    //
    // 의도: 시스템이 순회 도중에 컴포넌트를 추가하면 그 자리에서 풀이 재할당되어
    // 순회 중인 반복자가 무효화된다. 그래서 명령을 모아 두었다가 안전한 지점에서
    // 한꺼번에 반영하려는 것이 이 타입의 목적이다.
    //
    // 구현하기 전에 정해야 할 것: 명령을 어디에 저장할지(타입 소거된 바이트 버퍼),
    // 언제 flush할지(시스템 경계 vs tick 끝), 그리고 flush 순서가 결정론적인지.
    // 마지막 항목이 특히 중요하다 — 적용 순서가 peer마다 다르면 그대로 디싱크다.
    void EntityCommandBuffer::AddComponent(EntityId entity, TypeHash componentType, void* componentData) {
        static_cast<void>(entity);
        static_cast<void>(componentType);
        static_cast<void>(componentData);
    }

}
