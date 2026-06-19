# 1. 设置构建本项目所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.22)

# 2. 定义项目名称为 PulseQt，版本号为 1.0.0，主要使用的编程语言是 C++
project(PulseQt VERSION 1.0.0 LANGUAGES CXX)

# 3. 设置 C++ 编译标准为 C++17
set(CMAKE_CXX_STANDARD 17)
# 4. 强制要求编译器必须支持 C++17 标准，如果不支持则报错
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4.6 MSVC 需显式开启 __cplusplus 宏（否则 Qt6 编译报错）
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

# 4.5 显式开启 Qt 元对象编译器（AUTOMOC）
#     扫描所有源文件中的 Q_OBJECT 宏，自动生成 moc_*.cpp
set(CMAKE_AUTOMOC ON)

# 5. 查找并加载 Qt6 库。REQUIRED 表示如果找不到则报错终止；
#    COMPONENTS 后面列出了项目具体依赖的 5 个 Qt 模块
#    本地开发：指定 MSVC 版本路径（避免误用 mingw_64 的 Qt）
#    CI 环境：由 install-qt-action 或环境变量提供，此路径不存在时会跳过
if(EXISTS "D:/Qt/6.9.0/msvc2022_64")
    set(CMAKE_PREFIX_PATH "D:/Qt/6.9.0/msvc2022_64" ${CMAKE_PREFIX_PATH})
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort Network Sql Test)

# 6. 定义一个名为 PulseQt 的可执行程序，包含 main.cpp 和 MainWindow.cpp 两个源文件
#    (注：qt_add_executable 是 Qt6 推荐的命令，能更好地处理跨平台特性)
qt_add_executable(PulseQt
    src/main.cpp
    src/ui/MainWindow.cpp
    include/MainWindow.h
    include/Logger.h
    src/utils/Logger.cpp
    include/IChannel.h
    include/SerialChannel.h
    src/communication/SerialChannel.cpp
    include/TcpChannel.h
    src/communication/TcpChannel.cpp
    include/ChannelManager.h
    src/communication/ChannelManager.cpp
    include/ChannelRegistry.h
    src/communication/ChannelRegistry.cpp
    include/ConnectionDialog.h
    src/ui/ConnectionDialog.cpp
    include/Frame.h
    include/ProtocolDecoder.h
    src/protocol/ProtocolDecoder.cpp
    include/DataPoint.h
    include/DataBuffer.h
    src/data/DataBuffer.cpp
    include/DatabaseManager.h
    src/data/DatabaseManager.cpp
    include/DataTableModel.h
    src/data/DataTableModel.cpp
    include/RealTimeChart.h
    src/ui/RealTimeChart.cpp
    include/ParseWorker.h
    src/worker/ParseWorker.cpp
    include/HistoryPlayer.h
    src/ui/HistoryPlayer.cpp
    include/ExportDialog.h
    src/ui/ExportDialog.cpp
)

# 7. 为 PulseQt 程序添加私有的头文件搜索路径（PRIVATE 表示这些头文件仅本项目内部使用）
target_include_directories(PulseQt PRIVATE include)

# 8. 将前面找到的 5 个 Qt6 模块库链接到 PulseQt 可执行程序上
target_link_libraries(PulseQt PRIVATE Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Network Qt6::Sql)

# ═══════════════════════════════════════════════════════════
# 测试
# ═══════════════════════════════════════════════════════════
enable_testing()

# 测试公共静态库（复用被测模块 .cpp，避免重复编译）
# 头文件也需列入源列表，让 AUTOMOC 生成信号实现
add_library(PulseQtTestLib STATIC
    src/protocol/ProtocolDecoder.cpp
    src/data/DataBuffer.cpp
    src/data/DatabaseManager.cpp
    src/communication/ChannelManager.cpp
    src/communication/TcpChannel.cpp
    src/communication/ChannelRegistry.cpp
    src/worker/TcpWorker.cpp
    src/worker/ParseWorker.cpp
    include/ProtocolDecoder.h
    include/Frame.h
    include/DataBuffer.h
    include/DataPoint.h
    include/DatabaseManager.h
    include/IChannel.h
    include/ChannelManager.h
    include/ChannelRegistry.h
    include/TcpChannel.h
    include/TcpWorker.h
    include/ParseWorker.h
)
target_include_directories(PulseQtTestLib PRIVATE include)
target_link_libraries(PulseQtTestLib PUBLIC Qt6::Core Qt6::Sql Qt6::Network)

# T022 — ProtocolDecoder + Frame 单元测试
qt_add_executable(TestFrame
    tests/TestFrame.cpp
)
target_include_directories(TestFrame PRIVATE include)
target_include_directories(TestFrame PRIVATE ${CMAKE_BINARY_DIR}/TestFrame_autogen/include_Debug)
target_link_libraries(TestFrame PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME Frame COMMAND TestFrame)

qt_add_executable(TestProtocolDecoder
    tests/TestProtocolDecoder.cpp
)
target_include_directories(TestProtocolDecoder PRIVATE include)
target_include_directories(TestProtocolDecoder PRIVATE ${CMAKE_BINARY_DIR}/TestProtocolDecoder_autogen/include_Debug)
target_link_libraries(TestProtocolDecoder PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME ProtocolDecoder COMMAND TestProtocolDecoder)

# T023 — DataBuffer 单元测试
qt_add_executable(TestDataBuffer
    tests/TestDataBuffer.cpp
)
target_include_directories(TestDataBuffer PRIVATE include)
target_include_directories(TestDataBuffer PRIVATE ${CMAKE_BINARY_DIR}/TestDataBuffer_autogen/include_Debug)
target_link_libraries(TestDataBuffer PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME DataBuffer COMMAND TestDataBuffer)

# T025 — 全链路集成测试
qt_add_executable(TestIntegration
    tests/TestIntegration.cpp
)
target_include_directories(TestIntegration PRIVATE include)
target_include_directories(TestIntegration PRIVATE ${CMAKE_BINARY_DIR}/TestIntegration_autogen/include_Debug)
target_link_libraries(TestIntegration PRIVATE PulseQtTestLib Qt6::Test Qt6::Network)
target_compile_definitions(TestIntegration PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME Integration COMMAND TestIntegration)
set_tests_properties(Integration PROPERTIES TIMEOUT 120)

# T024 — DatabaseManager 单元测试
qt_add_executable(TestDatabaseManager
    tests/TestDatabaseManager.cpp
)
target_include_directories(TestDatabaseManager PRIVATE include)
target_include_directories(TestDatabaseManager PRIVATE ${CMAKE_BINARY_DIR}/TestDatabaseManager_autogen/include_Debug)
target_link_libraries(TestDatabaseManager PRIVATE PulseQtTestLib Qt6::Test Qt6::Sql)
add_test(NAME DatabaseManager COMMAND TestDatabaseManager)

# T028 — ChannelManager 单元测试
qt_add_executable(TestChannelManager
    tests/TestChannelManager.cpp
)
target_include_directories(TestChannelManager PRIVATE include)
target_include_directories(TestChannelManager PRIVATE ${CMAKE_BINARY_DIR}/TestChannelManager_autogen/include_Debug)
target_link_libraries(TestChannelManager PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME ChannelManager COMMAND TestChannelManager)

# T029 — ChannelRegistry 单元测试
qt_add_executable(TestChannelRegistry
    tests/TestChannelRegistry.cpp
)
target_include_directories(TestChannelRegistry PRIVATE include)
target_include_directories(TestChannelRegistry PRIVATE ${CMAKE_BINARY_DIR}/TestChannelRegistry_autogen/include_Debug)
target_link_libraries(TestChannelRegistry PRIVATE PulseQtTestLib Qt6::Test)
add_test(NAME ChannelRegistry COMMAND TestChannelRegistry)

# 测试运行时需 Qt DLL（Windows 专用）
if(WIN32)
    set_tests_properties(Frame ProtocolDecoder DataBuffer DatabaseManager Integration ChannelManager ChannelRegistry PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:D:/Qt/6.9.0/msvc2022_64/bin")
endif()
