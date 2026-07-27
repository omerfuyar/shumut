#define SHUC_NO_RUN_LOG
#define SHU_IMPLEMENTATION
#include "../../shu/shu.h"
#include "../../shuild/shuild.h"

#ifdef _WIN32
#define LINUX_SUFFIX
#else
#define LINUX_SUFFIX ".ignore"
#endif

void ShuildExample(const char *name)
{
    SHU_ModuleBegin(name LINUX_SUFFIX, NULL);
    SHU_ModuleAddSourceFile(name);
    SHU_ModuleCompile(NULL, SHUModuleType_Executable);
}

int main(int argc, char **argv)
{
    SHU_CompilerTryConfigure("gcc");
    SHU_UtilAutomate(argc, argv);

    SHU_CompilerAddFlags(SHUM_FLAGS_OPTIMIZATION_HIGH);
    SHU_CompilerAddFlags("-Wno-unused-function -Wno-format-truncation");

    ShuildExample("1_single_task");
    ShuildExample("2_multiple_threads");
    ShuildExample("3_multiple_tasks");
    ShuildExample("4_destroying_tasks");
    ShuildExample("5_destroying_threads");
    ShuildExample("6_yield_delay");

    return 0;
}