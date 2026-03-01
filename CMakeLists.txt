cmake_minimum_required(VERSION 3.24)

project(VaultRDP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql DBus)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FREERDP3 REQUIRED freerdp3)
pkg_check_modules(FREERDP_CLIENT3 REQUIRED freerdp-client3)
pkg_check_modules(WINPR3 REQUIRED winpr3)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
pkg_check_modules(LIBARGON2 REQUIRED libargon2)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(OPENSSL REQUIRED openssl)

qt_standard_project_setup()

add_executable(vaultrdp
  src/main.cpp
  resources/resources.qrc
  src/core/AppLogging.cpp
  src/core/AppPaths.cpp
  src/core/DatabaseManager.cpp
  src/core/MigrationManager.cpp
  src/core/VaultManager.cpp
  src/core/Uuid.cpp
  src/core/security/PassphrasePolicy.cpp
  src/core/repository/ConnectionRepository.cpp
  src/core/repository/CredentialRepository.cpp
  src/core/repository/GatewayRepository.cpp
  src/core/repository/Repository.cpp
  src/core/repository/SecretRepository.cpp
  src/protocols/ISession.cpp
  src/protocols/RdpSession.cpp
  src/protocols/RdpSessionUtils.cpp
  src/system/ScreenLockMonitor.cpp
  src/ui/MainWindowInteraction.cpp
  src/ui/MainWindow.cpp
  src/ui/IconTheme.cpp
  src/ui/CredentialScope.cpp
  src/ui/RootScope.cpp
  src/ui/NewCredentialDialog.cpp
  src/ui/FolderTreeView.cpp
  src/ui/GatewayScope.cpp
  src/ui/NewConnectionDialog.cpp
  src/ui/NewGatewayDialog.cpp
  src/ui/CredentialPromptDialog.cpp
  src/ui/RdpRenderWidget.cpp
  src/ui/SessionRuntimeOptions.cpp
  src/ui/SessionTabContent.cpp
)

target_include_directories(vaultrdp
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREERDP3_INCLUDE_DIRS}
    ${FREERDP_CLIENT3_INCLUDE_DIRS}
    ${WINPR3_INCLUDE_DIRS}
    ${LIBSODIUM_INCLUDE_DIRS}
    ${LIBARGON2_INCLUDE_DIRS}
)

target_compile_options(vaultrdp PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(vaultrdp
  PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::DBus
    ${FREERDP3_LIBRARIES}
    ${FREERDP_CLIENT3_LIBRARIES}
    ${WINPR3_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${LIBARGON2_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

target_compile_definitions(vaultrdp
  PRIVATE
    QT_NO_KEYWORDS
)

set_target_properties(vaultrdp PROPERTIES
  BUILD_RPATH "${FREERDP3_LIBRARY_DIRS};${FREERDP_CLIENT3_LIBRARY_DIRS};${WINPR3_LIBRARY_DIRS}"
  INSTALL_RPATH "${FREERDP3_LIBRARY_DIRS};${FREERDP_CLIENT3_LIBRARY_DIRS};${WINPR3_LIBRARY_DIRS}"
)

enable_testing()

add_executable(vaultrdp_integration_tests
  tests/integration_tests.cpp
  src/core/AppLogging.cpp
  src/core/AppPaths.cpp
  src/core/DatabaseManager.cpp
  src/core/MigrationManager.cpp
  src/core/VaultManager.cpp
  src/core/Uuid.cpp
  src/core/security/PassphrasePolicy.cpp
  src/core/repository/ConnectionRepository.cpp
  src/core/repository/CredentialRepository.cpp
  src/core/repository/GatewayRepository.cpp
  src/core/repository/Repository.cpp
  src/core/repository/SecretRepository.cpp
  src/protocols/ISession.cpp
  src/protocols/RdpSession.cpp
  src/protocols/RdpSessionUtils.cpp
  src/ui/CredentialScope.cpp
  src/ui/RootScope.cpp
  src/ui/GatewayScope.cpp
  src/ui/CredentialPromptDialog.cpp
)

target_include_directories(vaultrdp_integration_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREERDP3_INCLUDE_DIRS}
    ${FREERDP_CLIENT3_INCLUDE_DIRS}
    ${WINPR3_INCLUDE_DIRS}
    ${LIBSODIUM_INCLUDE_DIRS}
    ${LIBARGON2_INCLUDE_DIRS}
)

target_compile_options(vaultrdp_integration_tests PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(vaultrdp_integration_tests
  PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Sql
    ${FREERDP3_LIBRARIES}
    ${FREERDP_CLIENT3_LIBRARIES}
    ${WINPR3_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${LIBARGON2_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

target_compile_definitions(vaultrdp_integration_tests
  PRIVATE
    QT_NO_KEYWORDS
)

set_target_properties(vaultrdp_integration_tests PROPERTIES
  BUILD_RPATH "${FREERDP3_LIBRARY_DIRS};${FREERDP_CLIENT3_LIBRARY_DIRS};${WINPR3_LIBRARY_DIRS}"
  INSTALL_RPATH "${FREERDP3_LIBRARY_DIRS};${FREERDP_CLIENT3_LIBRARY_DIRS};${WINPR3_LIBRARY_DIRS}"
)

add_test(NAME vaultrdp_integration COMMAND vaultrdp_integration_tests)
