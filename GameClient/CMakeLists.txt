add_executable(GameClient main.cpp)

# EngineRender는 EngineCore를 PUBLIC으로 링크하므로 둘 다 딸려온다.
# GameServer는 EngineRender를 링크하지 않는다 — 서버에 DX11이 들어가면 안 된다.
target_link_libraries(GameClient PRIVATE EngineRender)

# 생성된 Visual Studio 솔루션의 F5 기본 실행 모드.
set_target_properties(GameClient PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "--single"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:GameClient>"
)
