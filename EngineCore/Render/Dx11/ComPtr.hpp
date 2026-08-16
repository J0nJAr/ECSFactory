#pragma once

#include <utility>

namespace EF::Dx11 {

    // COM 객체용 최소 RAII 포인터.
    //
    // 왜 직접 만드나: `Microsoft::WRL::ComPtr`는 MSVC 계열 헤더라 MinGW(msys2 ucrt64)에서
    // 기대대로 잡히지 않는 경우가 있다(docs/design/rendering-dx11.md §6). 우리가 필요한 건
    // "소유하고, 놓을 때 Release" 하나뿐이라 이 정도면 충분하다.
    //
    // 규칙:
    //  - 생성 API에는 `PutRef()`를 넘긴다. 기존 소유를 먼저 놓고 빈 슬롯을 준다.
    //  - 복사하면 AddRef, 파괴하면 Release. COM의 참조 카운트 규약을 그대로 따른다.
    template<typename T>
    class ComPtr {
    public:
        ComPtr() = default;

        ~ComPtr() {
            Reset();
        }

        ComPtr(const ComPtr& other) : m_ptr(other.m_ptr) {
            if (m_ptr) {
                m_ptr->AddRef();
            }
        }

        ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) {
            other.m_ptr = nullptr;
        }

        ComPtr& operator=(const ComPtr& other) {
            if (this != &other) {
                // 자기 대입이 아니어도 먼저 AddRef 한 뒤 Release 해야 안전하다.
                // 같은 객체를 가리키는 서로 다른 ComPtr끼리 대입할 때, 순서를 반대로 하면
                // 참조 카운트가 0이 되어 객체가 죽은 뒤 그 포인터를 다시 잡게 된다.
                T* incoming = other.m_ptr;
                if (incoming) {
                    incoming->AddRef();
                }
                Reset();
                m_ptr = incoming;
            }
            return *this;
        }

        ComPtr& operator=(ComPtr&& other) noexcept {
            if (this != &other) {
                Reset();
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        void Reset() {
            if (m_ptr) {
                m_ptr->Release();
                m_ptr = nullptr;
            }
        }

        [[nodiscard]] T* Get() const noexcept {
            return m_ptr;
        }

        T* operator->() const noexcept {
            return m_ptr;
        }

        explicit operator bool() const noexcept {
            return m_ptr != nullptr;
        }

        // 생성 API에 넘길 출력 슬롯. 기존 소유는 여기서 놓는다.
        // (D3D11CreateDeviceAndSwapChain 등이 이 자리에 새 객체를 써 넣는다.)
        T** PutRef() {
            Reset();
            return &m_ptr;
        }

        // void** 를 요구하는 API(GetBuffer 등)용.
        void** PutVoid() {
            Reset();
            return reinterpret_cast<void**>(&m_ptr);
        }

    private:
        T* m_ptr = nullptr;
    };

}
