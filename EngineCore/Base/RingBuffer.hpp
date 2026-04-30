#pragma once

#include <cstddef>
#include <vector>
#include <algorithm>

namespace EF {
    
    template<typename T>
    class SimpleRingBuffer {
    private:
        std::vector<T> m_buffer;
        size_t m_head = 0; // 가장 오래된 데이터의 인덱스
        size_t m_tail = 0; // 다음에 데이터를 쓸 인덱스
        size_t m_count = 0; 
        size_t m_capacity;

    public:
        explicit SimpleRingBuffer(size_t capacity) : m_buffer(capacity), m_capacity(capacity) {
            EF_ASSERT(capacity > 0, "Capacity must be greater than 0");
        }
        
        void Push(const T* data, size_t size) {
            if (size == 0 || m_capacity == 0) return;

            // 데이터가 capacity보다 크면, 가장 최신 데이터만 잘라서 넣습니다.
            if (size > m_capacity) {
                data += (size - m_capacity);
                size = m_capacity;
            }

            // 1. 첫 번째 복사: 버퍼의 끝(capacity)까지 넣을 수 있는 만큼만 복사
            size_t firstPart = std::min(size, m_capacity - m_tail);
            std::copy(data, data + firstPart, m_buffer.begin() + m_tail);

            // 2. 두 번째 복사: 배열의 끝을 넘어갔다면(wrap-around), 다시 인덱스 0부터 복사
            if (firstPart < size) {
                size_t secondPart = size - firstPart;
                std::copy(data + firstPart, data + size, m_buffer.begin());
                m_tail = secondPart;
            } else {
                m_tail += firstPart;
                // 딱 끝에 도달했을 때 0으로 돌려줌 (Modulo 연산 제거)
                if (m_tail == m_capacity) m_tail = 0;
            }

            // 3. 사이즈 및 Head 조정 (오버플로우 처리)
            if (m_count + size > m_capacity) {
                m_count = m_capacity;
                m_head = m_tail; // 꽉 찼으므로 가장 오래된 데이터는 가장 최근에 쓴 곳 바로 다음(즉, tail)입니다.
            } else {
                m_count += size;
            }
        }

        // 요소 1개짜리 고속 Push (롤백 엔진의 Input 저장 시 자주 쓰임)
        void Push(const T& item) {
            m_buffer[m_tail] = item;
            
            m_tail++;
            if (m_tail == m_capacity) m_tail = 0;

            if (m_count < m_capacity) {
                m_count++;
            } else {
                m_head = m_tail; // 꽉 찬 상태로 밀어내기
            }
        }

        void Pop(size_t size = 1) {
            if (size > m_count) {
                size = m_count;
            }
            
            m_head += size;
            if (m_head >= m_capacity) {
                m_head -= m_capacity;
            }
            m_count -= size;
        }


        // index 0은 가장 오래된 데이터, Size() - 1 은 가장 최근 데이터
        const T& operator[](size_t index) const {
            EF_ASSERT(index < m_count, "Index out of bounds");

            size_t actualIndex = (m_head + index);
            if (actualIndex >= m_capacity) {
                actualIndex -= m_capacity;
            }
            return m_buffer[actualIndex];
        }

        void Clear() {
            m_head = 0;
            m_tail = 0;
            m_count = 0;
        }

        size_t Size() const { return m_count; }
        size_t Capacity() const { return m_capacity; }
    };
}