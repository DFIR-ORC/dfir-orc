SET(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

include(vcpkg_common_functions)

include(${CURRENT_INSTALLED_DIR}/share/qt5modularscripts/qt_modular_library.cmake)

qt_modular_library(qtquickcontrols 23410fb82088591a8bed7e8e4127d13929a03adc0dfd18f7e2f906acdac21f7dcbb15cb2257272b893d937bbb54860992667c11aa0c6157d4a3b871616c4641c)