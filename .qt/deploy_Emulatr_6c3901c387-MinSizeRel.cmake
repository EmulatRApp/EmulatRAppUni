include("D:/EmulatR/EmulatRAppUniV4/Emulatr/.qt/QtDeploySupport-MinSizeRel.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Emulatr-plugins-MinSizeRel.cmake" OPTIONAL)
set(__QT_DEPLOY_I18N_CATALOGS "qtbase")

qt6_deploy_runtime_dependencies(
    EXECUTABLE "D:/EmulatR/EmulatRAppUniV4/Emulatr/MinSizeRel/Emulatr.exe"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
)
