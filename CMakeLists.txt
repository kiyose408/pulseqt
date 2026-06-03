# 1. 设置构建本项目所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.20)

# 2. 定义项目名称为 PulseQt，版本号为 1.0.0，主要使用的编程语言是 C++
project(PulseQt VERSION 1.0.0 LANGUAGES CXX)

# 3. 设置 C++ 编译标准为 C++17
set(CMAKE_CXX_STANDARD 17)
# 4. 强制要求编译器必须支持 C++17 标准，如果不支持则报错
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4.5 显式开启 Qt 元对象编译器（AUTOMOC）
#     扫描所有源文件中的 Q_OBJECT 宏，自动生成 moc_*.cpp
set(CMAKE_AUTOMOC ON)

# 5. 查找并加载 Qt6 库。REQUIRED 表示如果找不到则报错终止；
#    COMPONENTS 后面列出了项目具体依赖的 5 个 Qt 模块
find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort Network Sql)

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
)

# 7. 为 PulseQt 程序添加私有的头文件搜索路径（PRIVATE 表示这些头文件仅本项目内部使用）
target_include_directories(PulseQt PRIVATE include)

# 8. 将前面找到的 5 个 Qt6 模块库链接到 PulseQt 可执行程序上
target_link_libraries(PulseQt PRIVATE Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Network Qt6::Sql)
