#-------------------------------------------------
# UsbCanTools — 工程文件位于项目根目录，源码分目录存放
#-------------------------------------------------

QT       += core gui svg
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = UsbCanTools
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

# 头文件搜索路径：业务头文件 + 厂商库头文件（与 DLL 同目录 lib/）
INCLUDEPATH += $$PWD/include \
               $$PWD/include/app \
               $$PWD/include/can \
               $$PWD/include/device \
               $$PWD/include/ui \
               $$PWD/include/uds \
               $$PWD/include/protocol \
               $$PWD/lib

SOURCES += \
    src/main.cpp \
    src/ui/main_window.cpp \
    src/app/app_controller.cpp \
    src/device/device_management_widget.cpp \
    src/can/can_panel_widget.cpp \
    src/can/can_worker.cpp \
    src/protocol/isotp.cpp \
    src/uds/uds_diagnostic_simple_full.cpp \
    src/uds/uds_flash_worker.cpp \
    src/uds/uds_flash_dialog.cpp

HEADERS += \
    include/ui/main_window.h \
    include/app/app_controller.h \
    include/device/device_management_widget.h \
    include/can/can_panel_widget.h \
    include/can/can_worker_api.h \
    include/protocol/isotp.h \
    include/uds/uds_diagnostic_simple.h \
    include/uds/uds_flash_worker.h \
    include/uds/uds_flash_dialog.h \
    lib/ECanVci.h

RESOURCES += \
    resources/resources.qrc

# Windows 可执行文件图标（资源管理器/任务栏统一图标）
win32 {
    RC_ICONS = resources/icons/elecan_app_256.ico
}

# 编译输出目录（与 Qt Creator 默认 shadow build 的 debug/release 子目录一致）
CONFIG(debug, debug|release) {
    CAN_VENDOR_DLL_DEST = $$OUT_PWD/debug
} else {
    CAN_VENDOR_DLL_DEST = $$OUT_PWD/release
}
!isEmpty(DESTDIR) {
    CAN_VENDOR_DLL_DEST = $$DESTDIR
}

# Windows：链接完成后把 lib 目录下所有 .dll 复制到与可执行文件同目录（如 ...\debug\）。
# 注意：勿用 $$files($$PWD/lib/*.dll) 循环——那是在「运行 qmake 当时」枚举文件；
# 若当时 lib 里还没有 dll，则 Makefile 里不会生成任何复制命令，导致必须手抄到 debug。
win32 {
    QMAKE_POST_LINK += $$escape_expand(\\n\\t)cmd /c if not exist $$shell_path($$CAN_VENDOR_DLL_DEST) mkdir $$shell_path($$CAN_VENDOR_DLL_DEST)
    # 构建时按通配符复制，之后只要把 dll 放进工程 lib/ 再编译即可，无需手放到 build-.../debug
    QMAKE_POST_LINK += $$escape_expand(\\n\\t)cmd /c xcopy /Y /Q $$shell_path($$PWD/lib/*.dll) $$shell_path($$CAN_VENDOR_DLL_DEST)\\ 2>nul & ver>nul
}

# 非 Windows：将 lib 下动态库复制到可执行文件同目录（便于运行时查找）
unix:!macx {
    for (so, $$files($$PWD/lib/*.so*)) {
        QMAKE_POST_LINK += $$escape_expand(\\n\\t)$$QMAKE_COPY_FILE $$shell_path($$so) $$shell_path($$CAN_VENDOR_DLL_DEST)
    }
}

macx {
    for (dylib, $$files($$PWD/lib/*.dylib)) {
        QMAKE_POST_LINK += $$escape_expand(\\n\\t)$$QMAKE_COPY_FILE $$shell_path($$dylib) $$shell_path($$CAN_VENDOR_DLL_DEST)
    }
}
