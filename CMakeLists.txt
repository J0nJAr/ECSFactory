# 3.23 이상이 필요하다. target_sources 의 FILE_SET HEADERS 가 3.23에서 들어왔고
# EngineCore/CMakeLists.txt 가 그걸 쓴다. 3.20으로 적어두면 3.20~3.22 사용자가
# "왜 안 되는지 모르는" 에러를 만난다 — 선언한 최소치는 실제 최소치여야 한다.
cmake_minimum_required(VERSION 3.23)

# 버전 규칙 (docs/design/roadmap.md §2.6):
#   - 0.x  : 첫 결승선(공장 게임 최소 루프) 이전
#   - minor: 로드맵의 한 트랙이 끝날 때 올린다 (R 트랙 완료 → 0.2.0)
#   - 1.0.0: 첫 결승선 통과
# 마일스톤 단위 기록은 git 태그(milestone/<id>)가 담당한다. 이 버전은 큰 눈금이다.
project(ECSFactory VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MSVC)
    add_compile_options(/W4 /permissive-)

    if(POLICY CMP0141)
        cmake_policy(SET CMP0141 NEW)
        set(
            CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
            "$<IF:$<CONFIG:Debug,RelWithDebInfo>,EditAndContinue,ProgramDatabase>"
        )
    endif()
else()
    add_compile_options(-Wall -Wextra)
endif()

enable_testing()

add_subdirectory(EngineCore)
add_subdirectory(GameServer)
add_subdirectory(GameClient)
add_subdirectory(tests)
