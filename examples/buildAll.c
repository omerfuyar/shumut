#define SHU_IMPLEMENTATION
#include "../../shu/shu.h"
#include "../../shuild/shuild.h"

#ifdef _WIN32
#define LINUX_SUFFIX
#else
#define LINUX_SUFFIX ".ignore"
#endif

int main(int argc, char **argv)
{
    SHU_CompilerTryConfigure("gcc");
    SHU_UtilAutomate(argc, argv);

    SHU_CompilerAddFlags(SHUM_FLAGS_OPTIMIZATION_HIGH);
    SHU_CompilerAddFlags("-Wno-unused-function -Wno-format-truncation");

    SHU_ModuleBegin("1_single_task" LINUX_SUFFIX, NULL);
    SHU_ModuleAddSourceFile("1_single_task.c");
    SHU_ModuleCompile(NULL, SHUModuleType_Executable);

    SHU_ModuleBegin("2_multiple_threads" LINUX_SUFFIX, NULL);
    SHU_ModuleAddSourceFile("2_multiple_threads.c");
    SHU_ModuleCompile(NULL, SHUModuleType_Executable);

    SHU_ModuleBegin("3_multiple_tasks" LINUX_SUFFIX, NULL);
    SHU_ModuleAddSourceFile("3_multiple_tasks.c");
    SHU_ModuleCompile(NULL, SHUModuleType_Executable);

    SHU_ModuleBegin("4_destroying_tasks" LINUX_SUFFIX, NULL);
    SHU_ModuleAddSourceFile("4_destroying_tasks.c");
    SHU_ModuleCompile(NULL, SHUModuleType_Executable);

    SHU_ModuleBegin("5_destroying_threads" LINUX_SUFFIX, NULL);
    SHU_ModuleAddSourceFile("5_destroying_threads.c");
    SHU_ModuleCompile(NULL, SHUModuleType_Executable);

    return 0;
}