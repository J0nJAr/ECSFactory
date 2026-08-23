#include "Render/Camera.hpp"

namespace EF {

    Mat4 Camera::ViewProjection(float aspect) const noexcept {
        if (aspect <= 0.0f || viewHeightWorldUnits <= 0.0f) {
            return Mat4::Identity();
        }

        const float halfHeight = viewHeightWorldUnits * 0.5f;
        const float halfWidth = halfHeight * aspect;

        const float sx = 1.0f / halfWidth;
        const float sy = 1.0f / halfHeight;

        // 만들려는 변환 (w = 1 인 점에 대해):
        //   ndc.x = (world.x - position.x) * sx
        //   ndc.y = (world.z - position.y) * sy      ← 월드 Z가 화면 세로가 된다
        //   ndc.z = 0.5                              ← 깊이 버퍼가 없다. 자세한 건 아래
        //   ndc.w = 1
        //
        // 행 우선 저장 + mul(행벡터, 행렬)에서 결과의 j번째 성분은
        //   result[j] = x*m[0*4+j] + y*m[1*4+j] + z*m[2*4+j] + w*m[3*4+j]
        // 다. 즉 **열이 출력 성분**이고 마지막 행이 평행이동이다.
        //
        // 그래서 아래 표기에서 각 줄은 "입력 성분 하나가 출력에 어떻게 기여하는가"다.
        //   1행: world.x 가 ndc.x 에만 기여
        //   2행: world.y 는 아무 데도 기여하지 않는다 (2D 단계에서 높이축을 버린다)
        //   3행: world.z 가 ndc.y 에 기여
        //   4행: 평행이동 + 상수 깊이 + w
        //
        // ndc.z 를 0.5 상수로 두는 이유: 깊이 버퍼를 만들지 않았지만 래스터라이저의
        // DepthClipEnable 이 켜져 있어서 클립 공간 z 가 [0, w] 밖이면 잘린다.
        // 0.5 는 그 구간 한가운데라 안전하다. 깊이 정렬은 지금 제출 순서가 정한다.
        // (3D로 갈 때 이 자리에 world.y 또는 진짜 근/원평면이 들어온다.)
        Mat4 result;
        result.m[0]  = sx;   result.m[1]  = 0.0f; result.m[2]  = 0.0f; result.m[3]  = 0.0f;
        result.m[4]  = 0.0f; result.m[5]  = 0.0f; result.m[6]  = 0.0f; result.m[7]  = 0.0f;
        result.m[8]  = 0.0f; result.m[9]  = sy;   result.m[10] = 0.0f; result.m[11] = 0.0f;
        result.m[12] = -position.x * sx;
        result.m[13] = -position.y * sy;
        result.m[14] = 0.5f;
        result.m[15] = 1.0f;
        return result;
    }

    Float3 ProjectPoint(const Mat4& viewProjection, Float3 worldPosition) noexcept {
        const float* m = viewProjection.m;

        // mul(float4(pos, 1), M) 과 같은 계산. 위 주석의 인덱싱 규칙 그대로다.
        const float x = worldPosition.x * m[0] + worldPosition.y * m[4] + worldPosition.z * m[8]  + m[12];
        const float y = worldPosition.x * m[1] + worldPosition.y * m[5] + worldPosition.z * m[9]  + m[13];
        const float z = worldPosition.x * m[2] + worldPosition.y * m[6] + worldPosition.z * m[10] + m[14];
        const float w = worldPosition.x * m[3] + worldPosition.y * m[7] + worldPosition.z * m[11] + m[15];

        // 원근 나눗셈. 직교 투영에서는 w가 항상 1이라 효과가 없지만, 3D로 갈 때
        // 이 줄이 빠져 있으면 테스트가 조용히 틀린 값을 통과시킨다.
        if (w != 0.0f) {
            const float invW = 1.0f / w;
            return Float3{ x * invW, y * invW, z * invW };
        }
        return Float3{ x, y, z };
    }

}
